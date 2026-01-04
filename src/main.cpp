#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "wifi_config.h"

// Store WiFi info in RTC memory to speed up reconnection after deep sleep
RTC_DATA_ATTR uint8_t saved_channel = 0;
RTC_DATA_ATTR uint8_t saved_bssid[6] = {0};
RTC_DATA_ATTR bool has_saved_info = false;
RTC_DATA_ATTR int cycle_count = 0;

// Store IP configuration for faster reconnection
RTC_DATA_ATTR uint32_t saved_ip = 0;
RTC_DATA_ATTR uint32_t saved_gateway = 0;
RTC_DATA_ATTR uint32_t saved_subnet = 0;
RTC_DATA_ATTR uint32_t saved_dns1 = 0;
RTC_DATA_ATTR uint32_t saved_dns2 = 0;
RTC_DATA_ATTR bool has_saved_ip = false;

// Stub function for handling new image
void handleNewImage()
{
  // Serial.println("→ New image detected! (stub function)");
  // TODO: Implement image download and display logic here
  digitalWrite(LED_PIN, LOW);
}

// Check for new image from server
bool checkForNewImage()
{
  WiFiClientSecure client;
  HTTPClient http;

  // For HTTPS, we need to skip certificate validation (or add proper cert)
  // This is fine for testing, but consider adding proper certificate validation for production
  client.setInsecure();

  // Parse the base URL (remove https:// if present)
  String baseUrl = String(CHECK_IMAGE_URL);
  if (baseUrl.startsWith("https://"))
  {
    baseUrl = baseUrl.substring(8); // Remove "https://"
  }

  // Extract host (everything before the first /)
  int slashIndex = baseUrl.indexOf('/');
  String host = slashIndex > 0 ? baseUrl.substring(0, slashIndex) : baseUrl;
  String path = slashIndex > 0 ? baseUrl.substring(slashIndex) : "/";

  // Add query parameter to path
  path += "?frameId=" + String(FRAME_ID);

  // Serial.print("Checking for new image: https://");
  //  Serial.print(host);
  // Serial.println(path);

  // Use begin with host, port, and path separately for better parsing
  http.begin(client, host.c_str(), 443, path.c_str());
  http.setTimeout(10000); // 10 second timeout

  unsigned long request_start = millis();
  int httpResponseCode = http.GET();
  unsigned long request_time = millis() - request_start;

  // Serial.printf("HTTP Response: %d (took %lu ms)\n", httpResponseCode, request_time);

  bool hasNewImage = false;

  if (httpResponseCode == 200)
  {
    // Serial.println("✓ New image available!");
    hasNewImage = true;
  }
  else if (httpResponseCode == 204)
  {
    // Serial.println("✓ No new image (204 No Content)");
    hasNewImage = false;
  }
  else
  {
    // Serial.printf("✗ Unexpected response code: %d\n", httpResponseCode);
    if (httpResponseCode < 0)
    {
      // Serial.printf("Error code: %d\n", httpResponseCode);
    }
    hasNewImage = false;
  }

  http.end();
  return hasNewImage;
}

void setup()
{
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  // Record wakeup time
  unsigned long wakeup_time = millis();

  // Serial.begin(115200);
  //  Wait for serial to be ready (only on first boot, not after deep sleep)
  //  if (cycle_count == 0)
  //{
  //   delay(2000);
  //
  //}

  cycle_count++;
  // Serial.println("\n========================================");
  // Serial.printf("Cycle #%d - Waking from deep sleep\n", cycle_count);
  // Serial.println("========================================");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);

  // Disable power saving during connection for faster connection
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Configure WiFi for faster connection
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true); // Store credentials in flash

  unsigned long connect_start = millis();
  bool connection_success = false;

  // Try to use saved IP configuration first (fastest method)
  if (has_saved_ip && saved_ip != 0)
  {
    IPAddress ip;
    ip = saved_ip;
    IPAddress gateway;
    gateway = saved_gateway;
    IPAddress subnet;
    subnet = saved_subnet;
    IPAddress dns1;
    dns1 = saved_dns1;
    IPAddress dns2;
    dns2 = saved_dns2;

    // Configure static IP
    if (WiFi.config(ip, gateway, subnet, dns1, dns2))
    {
      // Try connection with saved channel/BSSID if available
      if (has_saved_info && saved_channel > 0)
      {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
      }
      else
      {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }

      // Wait for connection with shorter timeout for static IP
      unsigned long start_time = millis();
      unsigned long static_timeout = 5000; // 5 second timeout for static IP

      while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < static_timeout)
      {
        delay(10);
      }

      if (WiFi.status() == WL_CONNECTED)
      {
        connection_success = true;
      }
    }
  }

  // Fallback: Try saved channel/BSSID method if static IP failed or not available
  if (!connection_success)
  {
    // Reset to DHCP if static IP was attempted
    if (has_saved_ip && saved_ip != 0)
    {
      IPAddress zero(0, 0, 0, 0);
      WiFi.config(zero, zero, zero, zero, zero); // Reset to DHCP
    }

    if (has_saved_info && saved_channel > 0)
    {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
    }
    else
    {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    // Wait for connection with timeout
    unsigned long timeout = 30000; // 30 second timeout
    unsigned long start_time = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < timeout)
    {
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      connection_success = true;
    }
  }

  unsigned long connect_end = millis();
  unsigned long connection_time = connect_end - connect_start;
  unsigned long total_wakeup_time = connect_end - wakeup_time;

  // Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    // Serial.println("\n✓ Connected to WiFi!");
    // Serial.printf("Connection time: %lu ms\n", connection_time);
    // Serial.printf("Total wakeup time: %lu ms\n", total_wakeup_time);
    // Serial.print("IP Address: ");
    // Serial.println(WiFi.localIP());
    // Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    // Save IP configuration for next wake cycle
    IPAddress ip = WiFi.localIP();
    IPAddress gateway = WiFi.gatewayIP();
    IPAddress subnet = WiFi.subnetMask();
    IPAddress dns1 = WiFi.dnsIP(0);
    IPAddress dns2 = WiFi.dnsIP(1);

    // Check if IP is valid (not 0.0.0.0)
    if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0)
    {
      saved_ip = ip;
      saved_gateway = gateway;
      saved_subnet = subnet;
      saved_dns1 = dns1;
      saved_dns2 = dns2;
      has_saved_ip = true;
    }

    // Save channel and BSSID for next wake cycle
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
      saved_channel = ap_info.primary;
      memcpy(saved_bssid, ap_info.bssid, 6);
      has_saved_info = true;
      // Serial.printf("Saved channel: %d, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
      //               saved_channel,
      //               saved_bssid[0], saved_bssid[1], saved_bssid[2],
      //               saved_bssid[3], saved_bssid[4], saved_bssid[5]);
    }

    // Check for new image
    // Serial.println("\n--- Checking for new image ---");
    if (checkForNewImage())
    {
      handleNewImage();
    }
  }
  else
  {
    // Serial.println("\n✗ Connection failed!");
    // Serial.printf("Timeout after: %lu ms\n", connection_time);
    // Clear saved info if connection failed (will retry with DHCP next time)
    has_saved_info = false;
    has_saved_ip = false;
  }

  // Serial.println("\nGoing to deep sleep for 10 seconds...");
  // Serial.flush();

  digitalWrite(LED_PIN, HIGH);

  // Deep sleep for 10 seconds
  esp_sleep_enable_timer_wakeup(10 * 1000000ULL); // 10 seconds in microseconds
  esp_deep_sleep_start();
}

void loop()
{
  // This should never be reached due to deep sleep
}