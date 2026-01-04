#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "wifi_config.h"

// Store WiFi info in RTC memory to speed up reconnection after deep sleep
RTC_DATA_ATTR uint8_t saved_channel = 0;
RTC_DATA_ATTR uint8_t saved_bssid[6] = {0};
RTC_DATA_ATTR bool has_saved_info = false;
RTC_DATA_ATTR int cycle_count = 0;

void setup()
{
  // Record wakeup time
  unsigned long wakeup_time = millis();

  Serial.begin(115200);
  // Wait for serial to be ready (only on first boot, not after deep sleep)
  if (cycle_count == 0)
  {
    delay(2000);
  }

  cycle_count++;
  Serial.println("\n========================================");
  Serial.printf("Cycle #%d - Waking from deep sleep\n", cycle_count);
  Serial.println("========================================");

  // Initialize WiFi
  WiFi.mode(WIFI_STA);

  // Disable power saving during connection for faster connection
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Configure WiFi for faster connection
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true); // Store credentials in flash

  unsigned long connect_start = millis();

  // Try to use saved channel and BSSID if available (faster reconnection)
  if (has_saved_info && saved_channel > 0)
  {
    Serial.println("Using saved WiFi channel and BSSID for faster connection...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
  }
  else
  {
    Serial.println("First connection - scanning for network...");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  Serial.print("Connecting");

  // Wait for connection with timeout
  unsigned long timeout = 30000; // 30 second timeout
  unsigned long start_time = millis();

  while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < timeout)
  {
    Serial.print(".");
    delay(100);
  }

  unsigned long connect_end = millis();
  unsigned long connection_time = connect_end - connect_start;
  unsigned long total_wakeup_time = connect_end - wakeup_time;

  Serial.println();

  if (WiFi.status() == WL_CONNECTED)
  {
    Serial.println("\n✓ Connected to WiFi!");
    Serial.printf("Connection time: %lu ms\n", connection_time);
    Serial.printf("Total wakeup time: %lu ms\n", total_wakeup_time);
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

    // Save channel and BSSID for next wake cycle
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
      saved_channel = ap_info.primary;
      memcpy(saved_bssid, ap_info.bssid, 6);
      has_saved_info = true;
      Serial.printf("Saved channel: %d, BSSID: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    saved_channel,
                    saved_bssid[0], saved_bssid[1], saved_bssid[2],
                    saved_bssid[3], saved_bssid[4], saved_bssid[5]);
    }
  }
  else
  {
    Serial.println("\n✗ Connection failed!");
    Serial.printf("Timeout after: %lu ms\n", connection_time);
    // Clear saved info if connection failed
    has_saved_info = false;
  }

  Serial.println("\nGoing to deep sleep for 10 seconds...");
  Serial.flush();

  // Deep sleep for 10 seconds
  esp_sleep_enable_timer_wakeup(10 * 1000000ULL); // 10 seconds in microseconds
  esp_deep_sleep_start();
}

void loop()
{
  // This should never be reached due to deep sleep
}