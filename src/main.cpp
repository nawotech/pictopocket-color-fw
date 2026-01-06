#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include "wifi_config.h"
#include "config.h"
#include "nvs_storage.h"
#include "flash_storage.h"
#include "api_client.h"
#include "EPD_4in0e.h"
#include "DEV_Config.h"

// TEMPORARY: Hardcoded device key for testing
// TODO: Remove this and use NVS storage once upload/NVS preservation is fixed
// Replace with your actual 64-character hex device key
#define HARDCODED_DEVICE_KEY "9ecc9ddc6e0329b045f97928d0bf406fddcc2df90f1cba83eab9616aa8447350"

// Get unique device ID from ESP32 chip MAC address
String getDeviceId()
{
  uint8_t mac[6];
  // WiFi.macAddress() works even if WiFi is not connected
  // Just need to set mode first
  WiFi.mode(WIFI_STA);
  WiFi.macAddress(mac);
  char deviceId[32];
  snprintf(deviceId, sizeof(deviceId), "%02X%02X%02X%02X%02X%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(deviceId);
}

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

// Button pin (if available - defined in platformio.ini build_flags)
#ifndef BUTTON_PIN
// Button not configured - button functionality will be disabled
#endif

// Global state
DeviceState deviceState;
bool displayInitialized = false;
String globalDeviceKey = ""; // Device key loaded in setup(), used throughout

// Function declarations
bool connectWiFi();
void updateSlideshow();
void displayCurrentImage();
void advanceToNextImage();
bool downloadAndStoreImages(const SlideshowManifestResponse &manifest);

void setup()
{
  // CRITICAL: Initialize Serial FIRST before anything else
  // For ESP32-C3 with USB CDC, this must be done early
  Serial.begin(115200);

  // Wait for USB CDC to be ready (ESP32-C3 with USB CDC)
  // This is critical - without it, Serial output may not appear
  // Note: Serial may return false immediately after boot, so we wait up to 3 seconds
  unsigned long startWait = millis();
  while (!Serial && (millis() - startWait < 3000))
  {
    delay(10);
  }

  // Additional delay to ensure USB CDC is fully initialized
  delay(500);

  // Force flush to ensure output buffer is clear
  Serial.flush();
  delay(100);

  // Print test pattern immediately to verify Serial is working
  Serial.println("\n\n");
  Serial.println("========================================");
  Serial.println("E-Ink Photo Frame - Wake Cycle");
  Serial.println("========================================");
  Serial.println("If you see this, Serial is working!");
  Serial.flush();
  delay(200);

// With -DARDUINO_USB_CDC_ON_BOOT=1, Serial uses USB CDC, not hardware UART
// Hardware UART pins (GPIO20/RX, GPIO21/TX) are free for GPIO use
// No need to explicitly disable UART - it's not used when USB CDC is enabled

// LED pin (defined in platformio.ini build_flags)
#ifdef LED_PIN
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  Serial.println("LED pin initialized");
#endif

// Initialize button pin (if available - defined in platformio.ini build_flags)
#ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.println("Button pin initialized");
#endif

  cycle_count++;
  Serial.printf("Wake cycle #%d\n", cycle_count);
  Serial.flush();

  // Test pattern to verify Serial is working
  Serial.println("TEST: Serial output working!");
  Serial.flush();
  delay(100);

  // Initialize NVS storage
  Serial.println("\n--- Initializing NVS storage ---");
  if (!NVSStorage::begin())
  {
    Serial.println("ERROR: Failed to initialize NVS!");
    Serial.println("Going to sleep...");
    // If NVS fails, we can't continue - go to sleep
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  Serial.println("✓ NVS initialized");

  // Load device state
  Serial.println("\n--- Loading device state ---");
  if (!NVSStorage::loadState(deviceState))
  {
    // First boot - initialize state
    Serial.println("First boot - initializing default state");
    deviceState.currentImageIndex = 0;
    deviceState.wakeCounter = 0;
    deviceState.slideshowVersion = 0;
    deviceState.imageCount = 0;
  }
  else
  {
    Serial.printf("Loaded state: imageIndex=%d, wakeCounter=%d, slideshowVersion=%d, imageCount=%d\n",
                  deviceState.currentImageIndex, deviceState.wakeCounter,
                  deviceState.slideshowVersion, deviceState.imageCount);
  }

  // Load device key
  Serial.println("\n--- Loading device key ---");

  // TEMPORARY: Try NVS first, fallback to hardcoded key
  String deviceKey = "";
  bool usingHardcodedKey = false;

  // Try to load from NVS first
  bool keyExists = NVSStorage::hasDeviceKey();
  Serial.printf("NVS key exists check: %s\n", keyExists ? "YES" : "NO");

  if (keyExists)
  {
    deviceKey = NVSStorage::loadDeviceKey();
    Serial.printf("Key loaded from NVS, length: %d\n", deviceKey.length());
  }

  // Fallback to hardcoded key if NVS doesn't have one
  if (deviceKey.length() == 0)
  {
    Serial.println("WARNING: No key in NVS, using hardcoded key (TEMPORARY)");
    Serial.println("TODO: Fix NVS preservation during upload");
    deviceKey = String(HARDCODED_DEVICE_KEY);
    usingHardcodedKey = true;
  }

  // Verify key format (should be 64 hex characters)
  if (deviceKey.length() != 64)
  {
    Serial.printf("ERROR: Device key length is %d, expected 64\n", deviceKey.length());
    Serial.println("Going to sleep...");
    NVSStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }

  Serial.printf("✓ Device key loaded successfully (length: %d)\n", deviceKey.length());
  Serial.printf("Source: %s\n", usingHardcodedKey ? "HARDCODED (temporary)" : "NVS");
  Serial.printf("Key preview (first 10 chars): ");
  for (int i = 0; i < 10 && i < deviceKey.length(); i++)
  {
    Serial.print(deviceKey.charAt(i));
  }
  Serial.println();

  // Store device key globally for use in other functions
  globalDeviceKey = deviceKey;

  // Initialize flash storage
  Serial.println("\n--- Initializing flash storage ---");
  if (!FlashStorage::begin())
  {
    Serial.println("ERROR: Failed to initialize flash storage!");
    Serial.println("Going to sleep...");
    NVSStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  Serial.printf("✓ Flash storage initialized (Free: %d bytes, Used: %d bytes)\n",
                FlashStorage::getFreeSpace(), FlashStorage::getUsedSpace());

// Check for button press (manual advance)
#ifdef BUTTON_PIN
  Serial.println("\n--- Checking button ---");
  if (digitalRead(BUTTON_PIN) == LOW)
  {
    Serial.println("Button pressed - manual image advance");
    // Button pressed - advance to next image
    if (deviceState.imageCount > 0)
    {
      advanceToNextImage();
      Serial.printf("Advanced to image index %d\n", deviceState.currentImageIndex);
      displayCurrentImage();
      // Save state
      NVSStorage::saveState(deviceState);
    }
    else
    {
      Serial.println("No images available to display");
    }
    // Go back to sleep
    Serial.println("Going to sleep...");
    NVSStorage::end();
    FlashStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  Serial.println("No button press");
#endif

  // Get device ID from chip MAC address
  String deviceId = getDeviceId();
  Serial.printf("\n--- Device ID: %s ---\n", deviceId.c_str());

  // Connect to WiFi
  Serial.println("\n--- Connecting to WiFi ---");
  if (!connectWiFi())
  {
    Serial.println("ERROR: WiFi connection failed!");
    // WiFi connection failed - display current image if available and go to sleep
    if (deviceState.imageCount > 0)
    {
      Serial.println("Displaying current image before sleep...");
      displayCurrentImage();
    }
    Serial.println("Going to sleep...");
    NVSStorage::end();
    FlashStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  Serial.printf("✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Check for new slideshow version
  Serial.println("\n--- Checking for new slideshow ---");
  Serial.printf("Current slideshow version: %d\n", deviceState.slideshowVersion);
  SlideshowVersionResponse versionResponse;
  if (APIClient::getSlideshowVersion(deviceId, globalDeviceKey, versionResponse))
  {
    Serial.printf("Server slideshow version: %d, Status: %s\n",
                  versionResponse.slideshowVersion, versionResponse.status.c_str());
    if (versionResponse.status == "NEW" && versionResponse.slideshowVersion > deviceState.slideshowVersion)
    {
      Serial.println("NEW slideshow available! Downloading...");
      // New slideshow available - download it
      updateSlideshow();
      Serial.println("✓ Slideshow update complete");
    }
    else
    {
      Serial.println("No new slideshow available");
    }
  }
  else
  {
    Serial.println("ERROR: Failed to check slideshow version");
  }

  // Increment wake counter
  deviceState.wakeCounter++;
  Serial.printf("\n--- Wake counter: %d/%d ---\n", deviceState.wakeCounter, WAKES_PER_DAY);

  // Advance to next image every 6 wakes (24 hours)
  if (deviceState.wakeCounter >= WAKES_PER_DAY)
  {
    Serial.println("24 hours passed - advancing to next image");
    deviceState.wakeCounter = 0;
    if (deviceState.imageCount > 0)
    {
      advanceToNextImage();
      Serial.printf("Now showing image index %d\n", deviceState.currentImageIndex);
    }
  }

  // Display current image
  Serial.println("\n--- Displaying image ---");
  if (deviceState.imageCount > 0)
  {
    Serial.printf("Displaying image %d of %d\n", deviceState.currentImageIndex + 1, deviceState.imageCount);
    displayCurrentImage();
    Serial.println("✓ Image displayed");

    // Acknowledge display if we just displayed a new slideshow
    if (deviceState.slideshowVersion > 0)
    {
      Serial.printf("Acknowledging display of slideshow version %d\n", deviceState.slideshowVersion);
      if (APIClient::ackDisplayed(deviceId, globalDeviceKey, deviceState.slideshowVersion))
      {
        Serial.println("✓ Display acknowledged");
      }
      else
      {
        Serial.println("ERROR: Failed to acknowledge display");
      }
    }
  }
  else
  {
    Serial.println("No images to display");
  }

  // Save state
  Serial.println("\n--- Saving state ---");
  Serial.printf("State to save: imageIndex=%d, wakeCounter=%d, slideshowVersion=%d, imageCount=%d\n",
                deviceState.currentImageIndex, deviceState.wakeCounter,
                deviceState.slideshowVersion, deviceState.imageCount);

  // Ensure NVS is closed (it might be open from earlier operations)
  NVSStorage::end();

  // Try to save state
  if (NVSStorage::saveState(deviceState))
  {
    Serial.println("✓ State saved");
  }
  else
  {
    Serial.println("ERROR: Failed to save state");
    Serial.println("Possible causes:");
    Serial.println("  - NVS partition full");
    Serial.println("  - NVS corrupted");
    Serial.println("  - Write operation failed");
    Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());

    // Try to check if NVS can be opened at all
    if (!NVSStorage::begin())
    {
      Serial.println("ERROR: Cannot open NVS - partition may be corrupted");
    }
    else
    {
      Serial.println("NVS can be opened, but write failed");
      NVSStorage::end();
    }
  }

  // Cleanup
  NVSStorage::end();
  FlashStorage::end();

  // Go to deep sleep for 4 hours
  Serial.println("\n--- Going to deep sleep ---");
  Serial.printf("Sleep duration: %d hours\n", WAKE_INTERVAL_HOURS);
  Serial.println("========================================\n");
  Serial.println("NOTE: After deep sleep, USB CDC may not work.");
  Serial.println("You may need to manually reset the device to see Serial output again.");
  Serial.flush(); // Ensure all output is sent before sleep
  delay(500);     // Extra delay to ensure all output is sent
  esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
  esp_deep_sleep_start();
}

void loop()
{
  // This should never be reached due to deep sleep
}

bool connectWiFi()
{
  Serial.println("Initializing WiFi...");
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true);

  bool connection_success = false;

  // Try to use saved IP configuration first
  if (has_saved_ip && saved_ip != 0)
  {
    Serial.println("Trying saved IP configuration...");
    IPAddress ip(saved_ip);
    IPAddress gateway(saved_gateway);
    IPAddress subnet(saved_subnet);
    IPAddress dns1(saved_dns1);
    IPAddress dns2(saved_dns2);

    if (WiFi.config(ip, gateway, subnet, dns1, dns2))
    {
      if (has_saved_info && saved_channel > 0)
      {
        Serial.printf("Connecting with saved channel %d and BSSID...\n", saved_channel);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
      }
      else
      {
        Serial.println("Connecting with saved IP...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }

      unsigned long start_time = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < 5000)
      {
        delay(10);
      }

      if (WiFi.status() == WL_CONNECTED)
      {
        connection_success = true;
        Serial.printf("Connected in %lu ms (saved IP method)\n", millis() - start_time);
      }
      else
      {
        Serial.println("Saved IP method failed, trying fallback...");
      }
    }
  }

  // Fallback: Try saved channel/BSSID method
  if (!connection_success)
  {
    Serial.println("Trying fallback connection method...");
    if (has_saved_ip && saved_ip != 0)
    {
      IPAddress zero(0, 0, 0, 0);
      WiFi.config(zero, zero, zero, zero, zero);
    }

    if (has_saved_info && saved_channel > 0)
    {
      Serial.printf("Connecting with saved channel %d...\n", saved_channel);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
    }
    else
    {
      Serial.println("Connecting with default method...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    unsigned long timeout = 30000;
    unsigned long start_time = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < timeout)
    {
      delay(10);
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      connection_success = true;
      Serial.printf("Connected in %lu ms (fallback method)\n", millis() - start_time);
    }
    else
    {
      Serial.printf("Connection failed after %lu ms\n", millis() - start_time);
    }
  }

  if (connection_success)
  {
    // Save IP configuration
    IPAddress ip = WiFi.localIP();
    if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0)
    {
      saved_ip = ip;
      saved_gateway = WiFi.gatewayIP();
      saved_subnet = WiFi.subnetMask();
      saved_dns1 = WiFi.dnsIP(0);
      saved_dns2 = WiFi.dnsIP(1);
      has_saved_ip = true;
    }

    // Save channel and BSSID
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK)
    {
      saved_channel = ap_info.primary;
      memcpy(saved_bssid, ap_info.bssid, 6);
      has_saved_info = true;
    }
  }
  else
  {
    has_saved_info = false;
    has_saved_ip = false;
  }

  return connection_success;
}

void updateSlideshow()
{
  Serial.println("\n--- Updating slideshow ---");
  // Use global device key (loaded in setup with fallback to hardcoded)
  String deviceKey = globalDeviceKey;
  String deviceId = getDeviceId();

  // Get slideshow manifest
  Serial.println("Fetching slideshow manifest...");
  SlideshowManifestResponse manifest;
  if (!APIClient::getSlideshowManifest(deviceId, deviceKey, manifest))
  {
    Serial.println("ERROR: Failed to get slideshow manifest");
    return;
  }
  Serial.printf("✓ Manifest received: %d images\n", manifest.imageCount);

  // Download and store images
  Serial.println("Downloading images...");
  if (!downloadAndStoreImages(manifest))
  {
    Serial.println("ERROR: Failed to download/store images");
    Serial.println("Slideshow update incomplete - not updating device state");
    return;
  }
  Serial.println("✓ All images downloaded and stored");
  Serial.println("✓ Slideshow update complete");

  // Update device state
  deviceState.slideshowVersion = manifest.slideshowVersion;
  deviceState.imageCount = manifest.imageCount;
  for (int i = 0; i < manifest.imageCount && i < 12; i++)
  {
    deviceState.imageIds[i] = manifest.imageIds[i];
    deviceState.imageHashes[i] = manifest.imageHashes[i];
  }
  deviceState.currentImageIndex = 0; // Reset to first image
  deviceState.wakeCounter = 0;       // Reset wake counter
  Serial.println("✓ Device state updated");
}

bool downloadAndStoreImages(const SlideshowManifestResponse &manifest)
{
  // Use global device key (loaded in setup with fallback to hardcoded)
  String deviceKey = globalDeviceKey;
  String deviceId = getDeviceId();

  // Get signed URLs
  Serial.println("Requesting signed URLs...");
  SignedUrlsResponse urlsResponse;
  if (!APIClient::getSignedUrls(deviceId, deviceKey, manifest.imageIds, manifest.imageCount, urlsResponse))
  {
    Serial.println("ERROR: Failed to get signed URLs");
    return false;
  }
  Serial.printf("✓ Received %d signed URLs\n", urlsResponse.count);

  // Clear old images
  Serial.println("Clearing old images from flash...");
  FlashStorage::clearAllImages();

  // Download each image directly to flash (streaming, no large buffer needed)
  bool allSuccess = true;
  for (int i = 0; i < manifest.imageCount && i < 12; i++)
  {
    Serial.printf("Downloading image %d/%d (ID: %s)...\n", i + 1, manifest.imageCount, manifest.imageIds[i].c_str());

    if (urlsResponse.urls[i].length() == 0)
    {
      // Missing URL for this image
      Serial.printf("ERROR: Missing URL for image %d\n", i);
      allSuccess = false;
      continue;
    }

    // Stream directly to flash to avoid large RAM allocation
    WiFiClientSecure client;
    HTTPClient http;
    client.setInsecure();

    String host, path;
    APIClient::parseUrl(urlsResponse.urls[i], host, path);

    http.begin(client, host.c_str(), 443, path.c_str());
    http.setTimeout(60000); // 60 second timeout for image download

    unsigned long downloadStart = millis();
    int httpCode = http.GET();

    if (httpCode == 200)
    {
      size_t contentLength = http.getSize();
      if (contentLength == IMAGE_SIZE_BYTES)
      {
        Stream *stream = http.getStreamPtr();
        if (stream)
        {
          Serial.printf("Streaming %d bytes directly to flash...\n", contentLength);
          Serial.printf("Stream available: %d bytes\n", stream->available());

          if (FlashStorage::saveImageFromStream(i, stream, contentLength))
          {
            unsigned long downloadTime = millis() - downloadStart;
            Serial.printf("✓ Image %d downloaded and saved in %lu ms\n", i, downloadTime);
          }
          else
          {
            Serial.printf("ERROR: Failed to save image %d to flash\n", i);
            Serial.printf("Stream available after failure: %d bytes\n", stream->available());
            allSuccess = false;
          }
        }
        else
        {
          Serial.printf("ERROR: Failed to get stream for image %d\n", i);
          allSuccess = false;
        }
      }
      else
      {
        Serial.printf("ERROR: Image %d size mismatch (expected %d, got %d)\n", i, IMAGE_SIZE_BYTES, contentLength);
        allSuccess = false;
      }
    }
    else
    {
      Serial.printf("ERROR: HTTP %d for image %d\n", httpCode, i);
      allSuccess = false;
    }

    http.end();
  }

  if (allSuccess)
  {
    Serial.printf("✓ Successfully downloaded and stored all %d images\n", manifest.imageCount);
  }
  else
  {
    Serial.println("ERROR: Some images failed to download or store");
  }

  return allSuccess;
}

void displayCurrentImage()
{
  if (deviceState.imageCount == 0)
  {
    Serial.println("No images to display");
    return;
  }

  // Initialize display if not already done
  if (!displayInitialized)
  {
    Serial.println("Initializing display...");
    DEV_Module_Init();
    EPD_4IN0E_Init();
    displayInitialized = true;
    Serial.println("✓ Display initialized");
  }

  // Load image from flash
  Serial.printf("Loading image %d from flash...\n", deviceState.currentImageIndex);

  // Check available heap before allocation
  size_t freeHeap = ESP.getFreeHeap();
  Serial.printf("Free heap before allocation: %d bytes, Need: %d bytes\n", freeHeap, IMAGE_SIZE_BYTES);

  if (freeHeap < IMAGE_SIZE_BYTES + 10000)
  {
    Serial.printf("ERROR: Insufficient heap! Need %d bytes, have %d bytes\n", IMAGE_SIZE_BYTES, freeHeap);
    return;
  }

  uint8_t *imageBuffer = (uint8_t *)malloc(IMAGE_SIZE_BYTES);
  if (!imageBuffer)
  {
    Serial.printf("ERROR: Failed to allocate image buffer (%d bytes)\n", IMAGE_SIZE_BYTES);
    Serial.printf("Free heap after failed allocation: %d bytes\n", ESP.getFreeHeap());
    Serial.println("This might be due to heap fragmentation");
    return;
  }
  Serial.printf("✓ Image buffer allocated (%d bytes)\n", IMAGE_SIZE_BYTES);

  if (FlashStorage::loadImage(deviceState.currentImageIndex, imageBuffer, IMAGE_SIZE_BYTES))
  {
    Serial.println("Image loaded, sending to display...");
    // Display image
    EPD_4IN0E_Display(imageBuffer);
    Serial.println("✓ Image sent to display");
  }
  else
  {
    Serial.printf("ERROR: Failed to load image %d from flash\n", deviceState.currentImageIndex);
  }

  free(imageBuffer);

  // Put display to sleep
  EPD_4IN0E_Sleep();
  Serial.println("Display put to sleep");
}

void advanceToNextImage()
{
  if (deviceState.imageCount == 0)
  {
    return;
  }

  deviceState.currentImageIndex++;
  if (deviceState.currentImageIndex >= deviceState.imageCount)
  {
    deviceState.currentImageIndex = 0; // Wrap around
  }
}
