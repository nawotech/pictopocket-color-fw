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
void updateSlideshow();
bool displayCurrentImage();
void advanceToNextImage();
bool downloadAndStoreImages(const SlideshowManifestResponse &manifest);
void goToDeepSleep();

void setup()
{
  // turn LED on after wake
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Check wake reason
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  bool buttonWake = false;

  if (wakeup_reason == ESP_SLEEP_WAKEUP_GPIO)
  {
    digitalWrite(LED_PIN, LOW);
    buttonWake = true;
  }

  Serial.begin(115200);

  cycle_count++;

  // Load device state (loadState handles its own begin()/end())
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
      Serial.println("No images available to display");
    }
  }

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
    goToDeepSleep();
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
    goToDeepSleep();
    return;
  }
  Serial.printf("✓ Flash storage initialized (Free: %d bytes, Used: %d bytes)\n",
                FlashStorage::getFreeSpace(), FlashStorage::getUsedSpace());

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
    goToDeepSleep();
    return;
  }
  Serial.printf("✓ WiFi connected! IP: %s\n", WiFi.localIP().toString().c_str());

  // Track if we need to display (only when image changes)
  bool needToDisplay = false;
  bool newSlideshowDownloaded = false;
  int previousImageIndex = deviceState.currentImageIndex;

  // Check for new slideshow version
  Serial.println("\n--- Checking for new slideshow ---");
  Serial.printf("Current slideshow version: %d\n", deviceState.slideshowVersion);
  SlideshowVersionResponse versionResponse;
  bool slideshowUpdated = false;
  if (APIClient::getSlideshowVersion(deviceId, globalDeviceKey, versionResponse))
  {
    Serial.printf("Server slideshow version: %d, Status: %s\n",
                  versionResponse.slideshowVersion, versionResponse.status.c_str());

    // Download if server version is higher than our stored version
    // This handles the case where images were downloaded but state wasn't saved
    if (versionResponse.slideshowVersion > deviceState.slideshowVersion)
    {
      Serial.println("NEW slideshow available! Downloading...");
      // New slideshow available - download it
      updateSlideshow();
      slideshowUpdated = true;
      newSlideshowDownloaded = true;
      needToDisplay = true; // New slideshow - must display
    }
    else if (versionResponse.status == "NEW" && versionResponse.slideshowVersion == deviceState.slideshowVersion)
    {
      // Status says NEW but versions match - might be a state sync issue, download anyway
      Serial.println("Status is NEW but versions match - re-downloading to sync state...");
      updateSlideshow();
      slideshowUpdated = true;
      newSlideshowDownloaded = true;
      needToDisplay = true; // New slideshow - must display
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

  // Save state immediately after slideshow update to ensure slideshowVersion is persisted
  if (slideshowUpdated)
  {
    Serial.println("\n--- Saving state after slideshow update ---");
    Serial.printf("State to save: imageIndex=%d, wakeCounter=%d, slideshowVersion=%d, imageCount=%d\n",
                  deviceState.currentImageIndex, deviceState.wakeCounter,
                  deviceState.slideshowVersion, deviceState.imageCount);
    // saveState() handles begin()/end() internally, but ensure it's closed first
    NVSStorage::end();
    if (NVSStorage::saveState(deviceState))
    {
      Serial.println("✓ State saved after slideshow update");
      // Verify it was saved by loading it back
      DeviceState verifyState;
      if (NVSStorage::loadState(verifyState))
      {
        Serial.printf("✓ Verification: Loaded slideshowVersion=%d (expected %d)\n",
                      verifyState.slideshowVersion, deviceState.slideshowVersion);
        if (verifyState.slideshowVersion != deviceState.slideshowVersion)
        {
          Serial.println("ERROR: Slideshow version mismatch after save!");
        }
      }
    }
    else
    {
      Serial.println("ERROR: Failed to save state after slideshow update");
    }
  }

  // Increment wake counter
  int oldWakeCounter = deviceState.wakeCounter;
  deviceState.wakeCounter++;
  Serial.printf("\n--- Wake counter: %d -> %d/%d ---\n", oldWakeCounter, deviceState.wakeCounter, WAKES_PER_DAY);

  // Advance to next image every 6 wakes (24 hours)
  bool imageAdvanced = false;
  if (deviceState.wakeCounter >= WAKES_PER_DAY)
  {
    Serial.println("24 hours passed - advancing to next image");
    deviceState.wakeCounter = 0;
    if (deviceState.imageCount > 0)
    {
      int oldImageIndex = deviceState.currentImageIndex;
      advanceToNextImage();
      imageAdvanced = true;
      needToDisplay = true; // Image changed - need to display
      Serial.printf("Image advanced from %d to %d (of %d total)\n",
                    oldImageIndex, deviceState.currentImageIndex, deviceState.imageCount);
    }
  }

  // Check if we have images in flash even if state says we don't
  // This can happen if images were downloaded but state save failed previously
  if (deviceState.imageCount == 0)
  {
    Serial.println("State shows no images, checking flash storage...");
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
      Serial.printf("Found %d images in flash! Updating state to match...\n", imagesInFlash);
      deviceState.imageCount = imagesInFlash;
      deviceState.currentImageIndex = 0;
      needToDisplay = true; // Found images - need to display
      // Note: We don't have the imageIds/hashes in state, but we can still display
      // The next slideshow update will properly sync the state
    }
  }

  // Only display if image changed (new slideshow or automatic advancement)
  if (!needToDisplay)
  {
    Serial.println("\n--- No display needed (image unchanged) ---");
  }
  else
  {
    Serial.println("\n--- Displaying image ---");

    if (deviceState.imageCount > 0)
    {
      Serial.printf("Displaying image %d of %d\n", deviceState.currentImageIndex + 1, deviceState.imageCount);
      bool displaySuccess = displayCurrentImage();

      // Only acknowledge display if:
      // 1. Display was successful
      // 2. A new slideshow was downloaded (not just advancing through existing slideshow)
      if (displaySuccess && newSlideshowDownloaded)
      {
        Serial.printf("Acknowledging display of new slideshow version %d\n", deviceState.slideshowVersion);
        if (APIClient::ackDisplayed(deviceId, globalDeviceKey, deviceState.slideshowVersion))
        {
          Serial.println("✓ Display acknowledged");
        }
        else
        {
          Serial.println("ERROR: Failed to acknowledge display");
        }
      }
      else if (!displaySuccess)
      {
        Serial.println("ERROR: Display failed - not acknowledging");
      }
      else if (!newSlideshowDownloaded)
      {
        Serial.println("No ACK needed (advancing through existing slideshow)");
      }
    }
    else
    {
      Serial.println("No images to display");
    }
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

  // Go to deep sleep
  goToDeepSleep();
}

void loop()
{
  // This should never be reached due to deep sleep
}

bool connectWiFi()
{
  Serial.println("Initializing WiFi...");
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

  Serial.println();

  bool connection_success = false;
  if (WiFi.status() == WL_CONNECTED)
  {
    connection_success = true;
    Serial.println("\n✓ Connected to WiFi!");
    Serial.printf("Connection time: %lu ms\n", connection_time);
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.printf("RSSI: %d dBm\n", WiFi.RSSI());

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
  Serial.printf("Updating device state: slideshowVersion %d -> %d\n",
                deviceState.slideshowVersion, manifest.slideshowVersion);
  deviceState.slideshowVersion = manifest.slideshowVersion;
  deviceState.imageCount = manifest.imageCount;
  for (int i = 0; i < manifest.imageCount && i < 12; i++)
  {
    deviceState.imageIds[i] = manifest.imageIds[i];
    deviceState.imageHashes[i] = manifest.imageHashes[i];
  }
  deviceState.currentImageIndex = 0; // Reset to first image
  deviceState.wakeCounter = 0;       // Reset wake counter
  Serial.printf("✓ Device state updated: slideshowVersion=%d, imageCount=%d\n",
                deviceState.slideshowVersion, deviceState.imageCount);
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

bool displayCurrentImage()
{
  if (deviceState.imageCount == 0)
  {
    Serial.println("No images to display");
    return false;
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

  // Open image file from flash for streaming
  Serial.printf("Opening image %d from flash for streaming...\n", deviceState.currentImageIndex);
  File imageFile = FlashStorage::openImageFile(deviceState.currentImageIndex);

  if (!imageFile)
  {
    Serial.printf("ERROR: Failed to open image %d from flash\n", deviceState.currentImageIndex);
    return false;
  }

  Serial.printf("✓ Image file opened (%d bytes)\n", imageFile.size());
  Serial.println("Streaming image to display...");

  // Stream directly from file to display SPI
  bool displaySuccess = EPD_4IN0E_DisplayFromFile(imageFile, IMAGE_SIZE_BYTES);
  imageFile.close();

  if (displaySuccess)
  {
    Serial.println("✓ Image successfully sent to display");
  }
  else
  {
    Serial.println("ERROR: Failed to send image to display");
  }

  // Put display to sleep
  EPD_4IN0E_Sleep();
  Serial.println("Display put to sleep");

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
  Serial.printf("Image advanced: %d -> %d (of %d total)\n",
                oldIndex, deviceState.currentImageIndex, deviceState.imageCount);
}

void goToDeepSleep()
{
  // Cleanup storage
  NVSStorage::end();
  FlashStorage::end();

  // Turn off LED before sleep
#ifdef LED_PIN
  digitalWrite(LED_PIN, LOW);
#endif

  const uint64_t WAKEUP_LOW_PIN_BITMASK = 0b000100; // GPIO 2
  esp_deep_sleep_enable_gpio_wakeup(WAKEUP_LOW_PIN_BITMASK, ESP_GPIO_WAKEUP_GPIO_LOW);

  esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);

  esp_deep_sleep_start();
  // This should never be reached
}
