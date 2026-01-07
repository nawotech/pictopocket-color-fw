#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
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

// Global state
DeviceState deviceState;
bool displayInitialized = false;
String globalDeviceKey = ""; // Device key loaded in setup(), used throughout

// Function declarations
bool connectWiFi();
bool quickReconnectWiFi(); // Helper to quickly reconnect WiFi after display update
void updateSlideshow();
bool displayCurrentImage();
void advanceToNextImage();
bool downloadAndStoreImages(const SlideshowManifestResponse &manifest);
void goToDeepSleep();

// Helper function to quickly reconnect WiFi after display update
// Uses saved IP/channel for fast reconnection
bool quickReconnectWiFi()
{
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true);

  // Try to use saved IP/channel for fast reconnection
  if (has_saved_ip && saved_ip != 0)
  {
    IPAddress ip(saved_ip);
    IPAddress gateway(saved_gateway);
    IPAddress subnet(saved_subnet);
    IPAddress dns1(saved_dns1);
    IPAddress dns2(saved_dns2);
    WiFi.config(ip, gateway, subnet, dns1, dns2);
  }

  if (has_saved_info && saved_channel > 0)
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
  }
  else
  {
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }

  // Wait for reconnection with short timeout
  unsigned long reconnectStart = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - reconnectStart) < 5000)
  {
    delay(10);
  }

  return (WiFi.status() == WL_CONNECTED);
}

void setup()
{
  // Timing diagnostics
  unsigned long totalStartTime = millis();
  unsigned long stateLoadTime = 0;
  unsigned long keyLoadTime = 0;
  unsigned long wifiConnectTime = 0;
  unsigned long versionCheckTime = 0;
  unsigned long flashInitTime = 0;
  unsigned long slideshowUpdateTime = 0;
  unsigned long displayTime = 0;
  unsigned long ackTime = 0;
  unsigned long stateSaveTime = 0;

  // turn LED on after wake
  // pinMode(LED_PIN, OUTPUT);
  // digitalWrite(LED_PIN, HIGH);

  // Check wake reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool buttonWake = false;

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO)
  {
    // digitalWrite(LED_PIN, LOW);
    // buttonWake = false;
  }

  Serial.begin(115200);
  delay(100); // Allow Serial to initialize

  cycle_count++;

  // Load device state (loadState handles its own begin()/end())
  unsigned long stateLoadStart = millis();
  // // Serial.println("\n--- Loading device state ---");
  if (!NVSStorage::loadState(deviceState))
  {
    // First boot - initialize state
    // // Serial.println("First boot - initializing default state");
    deviceState.currentImageIndex = 0;
    deviceState.wakeCounter = 0;
    deviceState.slideshowVersion = 0;
    deviceState.imageCount = 0;
  }
  stateLoadTime = millis() - stateLoadStart;

  // Handle button wake - advance image immediately (before WiFi connection)
  // This allows manual image advance without waiting for network operations
  if (buttonWake)
  {
    if (deviceState.imageCount > 0)
    {
      int oldImageIndex = deviceState.currentImageIndex;
      advanceToNextImage();
      bool displaySuccess = displayCurrentImage();
      if (displaySuccess)
      {
        // Save state after successful display
        NVSStorage::saveState(deviceState);
      }
    }
    else
    {
      // // Serial.println("No images available to display");
    }
  }

  // TEMPORARY: Try NVS first, fallback to hardcoded key
  unsigned long keyLoadStart = millis();
  String deviceKey = "";
  bool usingHardcodedKey = false;

  // Try to load from NVS first
  bool keyExists = NVSStorage::hasDeviceKey();
  // // Serial.printf("NVS key exists check: %s\n", keyExists ? "YES" : "NO");

  if (keyExists)
  {
    deviceKey = NVSStorage::loadDeviceKey();
    // // Serial.printf("Key loaded from NVS, length: %d\n", deviceKey.length());
  }

  // Fallback to hardcoded key if NVS doesn't have one
  if (deviceKey.length() == 0)
  {
    // Serial.println("WARNING: No key in NVS, using hardcoded key (TEMPORARY)");
    // Serial.println("TODO: Fix NVS preservation during upload");
    deviceKey = String(HARDCODED_DEVICE_KEY);
    usingHardcodedKey = true;
  }

  // Verify key format (should be 64 hex characters)
  if (deviceKey.length() != 64)
  {
    // Serial.printf("ERROR: Device key length is %d, expected 64\n", deviceKey.length());
    // Serial.println("Going to sleep...");
    goToDeepSleep();
    return;
  }

  // Serial.printf("✓ Device key loaded successfully (length: %d)\n", deviceKey.length());
  // Serial.printf("Source: %s\n", usingHardcodedKey ? "HARDCODED (temporary)" : "NVS");
  // Serial.printf("Key preview (first 10 chars): ");
  for (int i = 0; i < 10 && i < deviceKey.length(); i++)
  {
    // Serial.print(deviceKey.charAt(i));
  }
  // Serial.println();

  // Store device key globally for use in other functions
  globalDeviceKey = deviceKey;
  keyLoadTime = millis() - keyLoadStart;

  // Get device ID from chip MAC address
  String deviceId = getDeviceId();

  // Connect to WiFi
  unsigned long wifiStart = millis();
  // Serial.println("\n--- Connecting to WiFi ---");
  if (!connectWiFi())
  {
    // Serial.println("ERROR: WiFi connection failed!");
    // WiFi connection failed - display current image if available and go to sleep
    if (deviceState.imageCount > 0)
    {
      // Serial.println("Displaying current image before sleep...");
      // Need flash storage for display
      if (FlashStorage::begin())
      {
        displayCurrentImage();
      }
    }
    // Serial.println("Going to sleep...");
    goToDeepSleep();
    return;
  }
  wifiConnectTime = millis() - wifiStart;
  // Serial.printf("✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // OPTIMIZATION: Check for new slideshow FIRST (before initializing flash storage)
  // This allows us to skip expensive operations if there's no new slideshow
  unsigned long versionCheckStart = millis();
  // Serial.println("\n--- Checking for new slideshow ---");
  // Serial.printf("Current slideshow version: %d\n", deviceState.slideshowVersion);
  SlideshowVersionResponse versionResponse;
  bool slideshowUpdated = false;
  bool newSlideshowDownloaded = false;
  bool needToDownload = false;

  if (APIClient::getSlideshowVersion(deviceId, globalDeviceKey, versionResponse))
  {
    // Serial.printf("Server slideshow version: %d, Status: %s\n",
    // versionResponse.slideshowVersion, versionResponse.status.c_str());

    // Download if server version is higher than our stored version
    if (versionResponse.slideshowVersion > deviceState.slideshowVersion)
    {
      // Serial.println("NEW slideshow available! Downloading...");
      needToDownload = true;
    }
    else if (versionResponse.status == "NEW" && versionResponse.slideshowVersion == deviceState.slideshowVersion)
    {
      // Status says NEW but versions match - might be a state sync issue, download anyway
      // Serial.println("Status is NEW but versions match - re-downloading to sync state...");
      needToDownload = true;
    }
    else
    {
      // Serial.println("No new slideshow available");
    }
  }
  else
  {
    // Serial.println("ERROR: Failed to check slideshow version");
  }
  versionCheckTime = millis() - versionCheckStart;

  // Track if we need to display (only when image changes)
  bool needToDisplay = false;

  // Only initialize flash storage if we need to download or display
  unsigned long flashInitStart = millis();
  if (needToDownload || deviceState.imageCount > 0)
  {
    // Serial.println("\n--- Initializing flash storage ---");
    if (!FlashStorage::begin())
    {
      // Serial.println("ERROR: Failed to initialize flash storage!");
      // Serial.println("Going to sleep...");
      goToDeepSleep();
      return;
    }
    // Serial.printf("✓ Flash storage initialized (Free: %d bytes, Used: %d bytes)\n",
    // FlashStorage::getFreeSpace(), FlashStorage::getUsedSpace());
  }
  flashInitTime = millis() - flashInitStart;

  // Download new slideshow if needed
  if (needToDownload)
  {
    unsigned long slideshowUpdateStart = millis();
    updateSlideshow();
    slideshowUpdateTime = millis() - slideshowUpdateStart;
    slideshowUpdated = true;
    newSlideshowDownloaded = true;
    needToDisplay = true; // New slideshow - must display
  }

  // Save state immediately after slideshow update to ensure slideshowVersion is persisted
  if (slideshowUpdated)
  {
    unsigned long stateSaveStart = millis();
    // Serial.println("\n--- Saving state after slideshow update ---");
    NVSStorage::end();
    if (NVSStorage::saveState(deviceState))
    {
      // Serial.println("✓ State saved after slideshow update");
    }
    else
    {
      // Serial.println("ERROR: Failed to save state after slideshow update");
    }
    stateSaveTime += millis() - stateSaveStart;
  }

  // Increment wake counter
  int oldWakeCounter = deviceState.wakeCounter;
  deviceState.wakeCounter++;
  bool stateChanged = false;

  // Advance to next image every 6 wakes (24 hours)
  bool imageAdvanced = false;
  if (deviceState.wakeCounter >= WAKES_PER_DAY)
  {
    // Serial.println("24 hours passed - advancing to next image");
    deviceState.wakeCounter = 0;
    stateChanged = true;
    if (deviceState.imageCount > 0)
    {
      // Need flash storage for display
      if (!FlashStorage::begin())
      {
        // Serial.println("ERROR: Failed to initialize flash storage for image advance!");
      }
      else
      {
        int oldImageIndex = deviceState.currentImageIndex;
        advanceToNextImage();
        imageAdvanced = true;
        needToDisplay = true; // Image changed - need to display
                              // Serial.printf("Image advanced from %d to %d (of %d total)\n",
                              // oldImageIndex, deviceState.currentImageIndex, deviceState.imageCount);
      }
    }
  }

  // Check if we have images in flash even if state says we don't
  // This can happen if images were downloaded but state save failed previously
  // Only check if we haven't already initialized flash storage for other reasons
  if (deviceState.imageCount == 0 && !needToDownload && !imageAdvanced)
  {
    // Try to initialize flash storage to check for images
    if (FlashStorage::begin())
    {
      // Serial.println("State shows no images, checking flash storage...");
      int imagesInFlash = 0;
      for (int i = 0; i < MAX_IMAGES; i++)
      {
        if (FlashStorage::hasImage(i))
        {
          imagesInFlash++;
        }
      }
      if (imagesInFlash > 0)
      {
        // Serial.printf("Found %d images in flash! Updating state to match...\n", imagesInFlash);
        deviceState.imageCount = imagesInFlash;
        deviceState.currentImageIndex = 0;
        needToDisplay = true; // Found images - need to display
        stateChanged = true;
      }
    }
  }

  // Only display if image changed (new slideshow or automatic advancement)
  if (needToDisplay)
  {
    // Serial.println("\n--- Displaying image ---");

    if (deviceState.imageCount > 0)
    {
      unsigned long displayStart = millis();
      // Serial.printf("Displaying image %d of %d\n", deviceState.currentImageIndex + 1, deviceState.imageCount);
      bool displaySuccess = displayCurrentImage();
      displayTime = millis() - displayStart;

      // Only acknowledge display if:
      // 1. Display was successful
      // 2. A new slideshow was downloaded (not just advancing through existing slideshow)
      // 3. WiFi is still connected
      if (displaySuccess && newSlideshowDownloaded && WiFi.status() == WL_CONNECTED)
      {
        unsigned long ackStart = millis();
        // Serial.printf("Acknowledging display of new slideshow version %d\n", deviceState.slideshowVersion);
        if (APIClient::ackDisplayed(deviceId, globalDeviceKey, deviceState.slideshowVersion))
        {
          // Serial.println("✓ Display acknowledged");
        }
        else
        {
          // Serial.println("ERROR: Failed to acknowledge display");
        }
        ackTime = millis() - ackStart;
      }
    }
    stateChanged = true; // Display happened, state may have changed
  }
  else
  {
    // Serial.println("\n--- No display needed (image unchanged) ---");
  }

  // OPTIMIZATION: Only save state if something actually changed
  // This avoids unnecessary NVS writes when there's no new slideshow
  if (stateChanged || slideshowUpdated || imageAdvanced)
  {
    unsigned long stateSaveStart = millis();
    // Serial.println("\n--- Saving state ---");
    NVSStorage::end();
    if (NVSStorage::saveState(deviceState))
    {
      // Serial.println("✓ State saved");
    }
    else
    {
      // Serial.println("ERROR: Failed to save state");
    }
    stateSaveTime += millis() - stateSaveStart;
  }
  else
  {
    // Serial.println("\n--- No state changes - skipping save ---");
  }

  // Print timing diagnostics
  unsigned long totalTime = millis() - totalStartTime;
  Serial.println("\n========================================");
  Serial.println("TIMING DIAGNOSTICS");
  Serial.println("========================================");
  Serial.printf("Total wake time:        %6lu ms\n", totalTime);
  Serial.printf("State load:              %6lu ms\n", stateLoadTime);
  Serial.printf("Device key load:         %6lu ms\n", keyLoadTime);
  Serial.printf("WiFi connection:         %6lu ms\n", wifiConnectTime);
  Serial.printf("Version check API:       %6lu ms\n", versionCheckTime);
  Serial.printf("Flash storage init:      %6lu ms\n", flashInitTime);
  Serial.printf("Slideshow update:         %6lu ms\n", slideshowUpdateTime);
  Serial.printf("Display:                 %6lu ms\n", displayTime);
  Serial.printf("ACK:                     %6lu ms\n", ackTime);
  Serial.printf("State save:              %6lu ms\n", stateSaveTime);
  Serial.println("========================================");

  // Go to deep sleep
  goToDeepSleep();
}

void loop()
{
  // This should never be reached due to deep sleep
}

bool connectWiFi()
{
  // Serial.println("Initializing WiFi...");
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
    // Serial.println("Trying saved IP configuration...");
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
        // Serial.printf("Connecting with saved IP, channel %d and BSSID...\n", saved_channel);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
      }
      else
      {
        // Serial.println("Connecting with saved IP...");
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }

      // Wait for connection with shorter timeout for static IP
      unsigned long start_time = millis();
      unsigned long static_timeout = 5000; // 5 second timeout for static IP

      while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < static_timeout)
      {
        delay(1); // Reduced delay - check more frequently
      }

      if (WiFi.status() == WL_CONNECTED)
      {
        connection_success = true;
        // Serial.printf("Connected in %lu ms (saved IP method)\n", millis() - start_time);
      }
      else
      {
        // Serial.println("Saved IP method failed, trying fallback...");
      }
    }
  }

  // Fallback: Try saved channel/BSSID method if static IP failed or not available
  if (!connection_success)
  {
    // Serial.println("Trying fallback connection method...");
    // Reset to DHCP if static IP was attempted
    if (has_saved_ip && saved_ip != 0)
    {
      IPAddress zero(0, 0, 0, 0);
      WiFi.config(zero, zero, zero, zero, zero); // Reset to DHCP
    }

    if (has_saved_info && saved_channel > 0)
    {
      // Serial.printf("Connecting with saved channel %d and BSSID...\n", saved_channel);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
    }
    else
    {
      // Serial.println("First connection - scanning for network...");
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }

    // Wait for connection with timeout
    unsigned long timeout = 30000; // 30 second timeout
    unsigned long start_time = millis();

    while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < timeout)
    {
      delay(1); // Reduced delay - check more frequently
    }

    if (WiFi.status() == WL_CONNECTED)
    {
      connection_success = true;
      // Serial.printf("Connected in %lu ms (fallback method)\n", millis() - start_time);
    }
  }

  unsigned long connect_end = millis();
  unsigned long connection_time = connect_end - connect_start;

  if (WiFi.status() == WL_CONNECTED)
  {
    // Serial.println("\n✓ Connected to WiFi!");
    // Serial.printf("Total connection time: %lu ms\n", connection_time);
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
      //       saved_channel,
      //       saved_bssid[0], saved_bssid[1], saved_bssid[2],
      //       saved_bssid[3], saved_bssid[4], saved_bssid[5]);
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

  return connection_success;
}

void updateSlideshow()
{
  // Serial.println("\n--- Updating slideshow ---");
  // Use global device key (loaded in setup with fallback to hardcoded)
  String deviceKey = globalDeviceKey;
  String deviceId = getDeviceId();

  // Get slideshow manifest
  unsigned long manifestStart = millis();
  // Serial.println("Fetching slideshow manifest...");
  SlideshowManifestResponse manifest;
  if (!APIClient::getSlideshowManifest(deviceId, deviceKey, manifest))
  {
    // Serial.println("ERROR: Failed to get slideshow manifest");
    return;
  }
  unsigned long manifestTime = millis() - manifestStart;
  // Serial.printf("✓ Manifest received: %d images\n", manifest.imageCount);

  // Download and store images
  unsigned long downloadStart = millis();
  // Serial.println("Downloading images...");
  if (!downloadAndStoreImages(manifest))
  {
    // Serial.println("ERROR: Failed to download/store images");
    // Serial.println("Slideshow update incomplete - not updating device state");
    return;
  }
  unsigned long downloadTime = millis() - downloadStart;
  // Serial.println("✓ All images downloaded and stored");
  // Serial.println("✓ Slideshow update complete");
  Serial.printf("  Manifest API: %lu ms, Downloads: %lu ms\n", manifestTime, downloadTime);

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
}

bool downloadAndStoreImages(const SlideshowManifestResponse &manifest)
{
  // Use global device key (loaded in setup with fallback to hardcoded)
  String deviceKey = globalDeviceKey;
  String deviceId = getDeviceId();

  // Get signed URLs
  unsigned long urlsStart = millis();
  SignedUrlsResponse urlsResponse;
  if (!APIClient::getSignedUrls(deviceId, deviceKey, manifest.imageIds, manifest.imageCount, urlsResponse))
  {
    return false;
  }
  unsigned long urlsTime = millis() - urlsStart;
  Serial.printf("  Signed URLs API: %lu ms\n", urlsTime);

  // OPTIMIZATION: Skip deletion - we're overwriting images 0 to (imageCount-1) anyway
  // LittleFS will overwrite existing files, so no need to delete first
  // Only delete if we're reducing the number of images (fewer than before)
  // For now, skip deletion to save time - overwrite is faster than delete+write

  // OPTIMIZATION: Reuse WiFiClientSecure connection for all downloads
  // This avoids TLS handshake overhead for each image
  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(30000); // Reduced timeout - fail faster if connection is slow

  // Download each image directly to flash (streaming, no large buffer needed)
  bool allSuccess = true;
  HTTPClient http; // Reuse HTTPClient object to avoid reallocation overhead
  unsigned long totalDownloadTime = 0;
  unsigned long totalFlashWriteTime = 0;

  for (int i = 0; i < manifest.imageCount && i < 12; i++)
  {
    if (urlsResponse.urls[i].length() == 0)
    {
      // Missing URL for this image
      allSuccess = false;
      continue;
    }

    unsigned long imageStart = millis();
    String host, path;
    APIClient::parseUrl(urlsResponse.urls[i], host, path);

    // OPTIMIZATION: Reuse HTTPClient - don't call end() until we're done
    // This keeps the underlying connection alive if possible
    http.begin(client, host.c_str(), 443, path.c_str());
    http.setTimeout(30000); // Reduced timeout - 30 seconds should be plenty
    http.setReuse(true);    // Reuse connection if possible

    unsigned long httpStart = millis();
    int httpCode = http.GET();
    unsigned long httpTime = millis() - httpStart;

    if (httpCode == 200)
    {
      size_t contentLength = http.getSize();
      if (contentLength == IMAGE_SIZE_BYTES)
      {
        Stream *stream = http.getStreamPtr();
        if (stream)
        {
          unsigned long flashStart = millis();
          if (FlashStorage::saveImageFromStream(i, stream, contentLength))
          {
            unsigned long flashTime = millis() - flashStart;
            totalFlashWriteTime += flashTime;
            unsigned long imageTime = millis() - imageStart;
            totalDownloadTime += imageTime;
            Serial.printf("  Image %d: HTTP=%lu ms, Flash=%lu ms, Total=%lu ms\n",
                          i, httpTime, flashTime, imageTime);
          }
          else
          {
            allSuccess = false;
          }
        }
        else
        {
          allSuccess = false;
        }
      }
      else
      {
        allSuccess = false;
      }
    }
    else
    {
      allSuccess = false;
    }

    // OPTIMIZATION: Don't call http.end() between downloads to keep connection alive
    // Only disconnect the stream, not the entire connection
    // The connection will be reused for the next http.begin() call
    http.end();
  }

  Serial.printf("  Total download time: %lu ms (Flash writes: %lu ms)\n",
                totalDownloadTime, totalFlashWriteTime);

  return allSuccess;
}

bool displayCurrentImage()
{
  if (deviceState.imageCount == 0)
  {
    // Serial.println("No images to display");
    return false;
  }

  // Initialize display if not already done
  if (!displayInitialized)
  {
    // Serial.println("Initializing display...");
    DEV_Module_Init();
    EPD_4IN0E_Init();
    displayInitialized = true;
    // Serial.println("✓ Display initialized");
  }

  // Open image file from flash for streaming
  // Serial.printf("Opening image %d from flash for streaming...\n", deviceState.currentImageIndex);
  File imageFile = FlashStorage::openImageFile(deviceState.currentImageIndex);

  if (!imageFile)
  {
    // Serial.printf("ERROR: Failed to open image %d from flash\n", deviceState.currentImageIndex);
    return false;
  }

  // OPTIMIZATION: Disconnect WiFi before display update to save power
  // The display update takes ~37 seconds, and ESP is idle during this time
  // Disconnecting WiFi saves significant power during the display update
  bool wifiWasConnected = (WiFi.status() == WL_CONNECTED);
  if (wifiWasConnected)
  {
    WiFi.disconnect(true); // Disconnect and disable WiFi to save power
    // Serial.println("WiFi disconnected for display update (power saving)");
  }

  // Serial.printf("✓ Image file opened (%d bytes)\n", imageFile.size());
  // Serial.println("Streaming image to display...");

  // Stream directly from file to display SPI
  // This will call EPD_4IN0E_TurnOnDisplay() which starts the display update
  // and waits for BUSY pin (takes ~37 seconds)
  // Note: The display library polls BUSY pin, but WiFi is off so less power consumed
  bool displaySuccess = EPD_4IN0E_DisplayFromFile(imageFile, IMAGE_SIZE_BYTES);
  imageFile.close();

  if (displaySuccess)
  {
    // Serial.println("✓ Image successfully sent to display");
  }
  else
  {
    // Serial.println("ERROR: Failed to send image to display");
  }

  // Put display to sleep
  EPD_4IN0E_Sleep();
  // Serial.println("Display put to sleep");

  // Reconnect WiFi if it was connected before (needed for ACK)
  if (wifiWasConnected)
  {
    // Serial.println("Reconnecting WiFi after display update...");
    if (quickReconnectWiFi())
    {
      // Serial.println("✓ WiFi reconnected");
    }
    else
    {
      // Serial.println("WARNING: WiFi reconnection failed (ACK may fail)");
    }
  }

  return displaySuccess;
}

void advanceToNextImage()
{
  if (deviceState.imageCount == 0)
  {
    return;
  }

  int oldIndex = deviceState.currentImageIndex;
  deviceState.currentImageIndex++;
  if (deviceState.currentImageIndex >= deviceState.imageCount)
  {
    deviceState.currentImageIndex = 0; // Wrap around
  }
  // Serial.printf("Image advanced: %d -> %d (of %d total)\n",
  //           oldIndex, deviceState.currentImageIndex, deviceState.imageCount);
}

void goToDeepSleep()
{
  // Cleanup storage
  NVSStorage::end();
  FlashStorage::end();

  // Turn off LED before sleep
  // #ifdef LED_PIN
  //   digitalWrite(LED_PIN, LOW);
  // #endif

  const uint64_t WAKEUP_LOW_PIN_BITMASK = 0b000100; // GPIO 2
  esp_deep_sleep_enable_gpio_wakeup(WAKEUP_LOW_PIN_BITMASK, ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);

  esp_deep_sleep_start();
  // This should never be reached
}
