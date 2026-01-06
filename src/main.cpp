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

// Get unique device ID from ESP32 chip MAC address
String getDeviceId() {
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

// Button pin (if available - adjust based on your hardware)
#define BUTTON_PIN 9  // Change this to your button GPIO pin

// Global state
DeviceState deviceState;
bool displayInitialized = false;

// Function declarations
bool connectWiFi();
void updateSlideshow();
void displayCurrentImage();
void advanceToNextImage();
bool downloadAndStoreImages(const SlideshowManifestResponse& manifest);

void setup() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);
  
  // Initialize button pin (if available)
  #ifdef BUTTON_PIN
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  #endif
  
  cycle_count++;
  
  // Initialize NVS storage
  if (!NVSStorage::begin()) {
    // Serial.println("Failed to initialize NVS");
    // If NVS fails, we can't continue - go to sleep
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  
  // Load device state
  if (!NVSStorage::loadState(deviceState)) {
    // First boot - initialize state
    deviceState.currentImageIndex = 0;
    deviceState.wakeCounter = 0;
    deviceState.slideshowVersion = 0;
    deviceState.imageCount = 0;
  }
  
  // Load device key
  String deviceKey = NVSStorage::loadDeviceKey();
  if (deviceKey.length() == 0) {
    // Serial.println("No device key found! Device must be configured first.");
    // Go to sleep - device needs to be configured
    NVSStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  
  // Initialize flash storage
  if (!FlashStorage::begin()) {
    // Serial.println("Failed to initialize flash storage");
    NVSStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  
  // Check for button press (manual advance)
  #ifdef BUTTON_PIN
  if (digitalRead(BUTTON_PIN) == LOW) {
    // Button pressed - advance to next image
    if (deviceState.imageCount > 0) {
      advanceToNextImage();
      displayCurrentImage();
      // Save state
      NVSStorage::saveState(deviceState);
    }
    // Go back to sleep
    NVSStorage::end();
    FlashStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  #endif
  
  // Get device ID from chip MAC address
  String deviceId = getDeviceId();
  
  // Connect to WiFi
  if (!connectWiFi()) {
    // WiFi connection failed - display current image if available and go to sleep
    if (deviceState.imageCount > 0) {
      displayCurrentImage();
    }
    NVSStorage::end();
    FlashStorage::end();
    esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
    esp_deep_sleep_start();
    return;
  }
  
  // Check for new slideshow version
  SlideshowVersionResponse versionResponse;
  if (APIClient::getSlideshowVersion(deviceId, deviceKey, versionResponse)) {
    if (versionResponse.status == "NEW" && versionResponse.slideshowVersion > deviceState.slideshowVersion) {
      // New slideshow available - download it
      updateSlideshow();
    }
  }
  
  // Increment wake counter
  deviceState.wakeCounter++;
  
  // Advance to next image every 6 wakes (24 hours)
  if (deviceState.wakeCounter >= WAKES_PER_DAY) {
    deviceState.wakeCounter = 0;
    if (deviceState.imageCount > 0) {
      advanceToNextImage();
    }
  }
  
  // Display current image
  if (deviceState.imageCount > 0) {
    displayCurrentImage();
    
    // Acknowledge display if we just displayed a new slideshow
    if (deviceState.slideshowVersion > 0) {
      APIClient::ackDisplayed(deviceId, deviceKey, deviceState.slideshowVersion);
    }
  }
  
  // Save state
  NVSStorage::saveState(deviceState);
  
  // Cleanup
  NVSStorage::end();
  FlashStorage::end();
  
  // Go to deep sleep for 4 hours
  esp_sleep_enable_timer_wakeup(WAKE_INTERVAL_MICROSECONDS);
  esp_deep_sleep_start();
}

void loop() {
  // This should never be reached due to deep sleep
}

bool connectWiFi() {
  WiFi.mode(WIFI_STA);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(false);
  WiFi.persistent(true);
  
  bool connection_success = false;
  
  // Try to use saved IP configuration first
  if (has_saved_ip && saved_ip != 0) {
    IPAddress ip(saved_ip);
    IPAddress gateway(saved_gateway);
    IPAddress subnet(saved_subnet);
    IPAddress dns1(saved_dns1);
    IPAddress dns2(saved_dns2);
    
    if (WiFi.config(ip, gateway, subnet, dns1, dns2)) {
      if (has_saved_info && saved_channel > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
      } else {
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      }
      
      unsigned long start_time = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < 5000) {
        delay(10);
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        connection_success = true;
      }
    }
  }
  
  // Fallback: Try saved channel/BSSID method
  if (!connection_success) {
    if (has_saved_ip && saved_ip != 0) {
      IPAddress zero(0, 0, 0, 0);
      WiFi.config(zero, zero, zero, zero, zero);
    }
    
    if (has_saved_info && saved_channel > 0) {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD, saved_channel, saved_bssid);
    } else {
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    }
    
    unsigned long timeout = 30000;
    unsigned long start_time = millis();
    
    while (WiFi.status() != WL_CONNECTED && (millis() - start_time) < timeout) {
      delay(10);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      connection_success = true;
    }
  }
  
  if (connection_success) {
    // Save IP configuration
    IPAddress ip = WiFi.localIP();
    if (ip[0] != 0 || ip[1] != 0 || ip[2] != 0 || ip[3] != 0) {
      saved_ip = ip;
      saved_gateway = WiFi.gatewayIP();
      saved_subnet = WiFi.subnetMask();
      saved_dns1 = WiFi.dnsIP(0);
      saved_dns2 = WiFi.dnsIP(1);
      has_saved_ip = true;
    }
    
    // Save channel and BSSID
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
      saved_channel = ap_info.primary;
      memcpy(saved_bssid, ap_info.bssid, 6);
      has_saved_info = true;
    }
  } else {
    has_saved_info = false;
    has_saved_ip = false;
  }
  
  return connection_success;
}

void updateSlideshow() {
  String deviceKey = NVSStorage::loadDeviceKey();
  String deviceId = getDeviceId();
  
  // Get slideshow manifest
  SlideshowManifestResponse manifest;
  if (!APIClient::getSlideshowManifest(deviceId, deviceKey, manifest)) {
    return;
  }
  
  // Download and store images
  if (!downloadAndStoreImages(manifest)) {
    return;
  }
  
  // Update device state
  deviceState.slideshowVersion = manifest.slideshowVersion;
  deviceState.imageCount = manifest.imageCount;
  for (int i = 0; i < manifest.imageCount && i < 12; i++) {
    deviceState.imageIds[i] = manifest.imageIds[i];
    deviceState.imageHashes[i] = manifest.imageHashes[i];
  }
  deviceState.currentImageIndex = 0;  // Reset to first image
  deviceState.wakeCounter = 0;  // Reset wake counter
}

bool downloadAndStoreImages(const SlideshowManifestResponse& manifest) {
  String deviceKey = NVSStorage::loadDeviceKey();
  String deviceId = getDeviceId();
  
  // Get signed URLs
  SignedUrlsResponse urlsResponse;
  if (!APIClient::getSignedUrls(deviceId, deviceKey, manifest.imageIds, manifest.imageCount, urlsResponse)) {
    return false;
  }
  
  // Download each image
  uint8_t* imageBuffer = (uint8_t*)malloc(IMAGE_SIZE_BYTES);
  if (!imageBuffer) {
    return false;
  }
  
  // Clear old images
  FlashStorage::clearAllImages();
  
  bool allSuccess = true;
  for (int i = 0; i < manifest.imageCount && i < 12; i++) {
    if (urlsResponse.urls[i].length() == 0) {
      // Missing URL for this image
      allSuccess = false;
      continue;
    }
    
    size_t bytesDownloaded = 0;
    if (APIClient::downloadImage(urlsResponse.urls[i], imageBuffer, IMAGE_SIZE_BYTES, bytesDownloaded)) {
      if (bytesDownloaded == IMAGE_SIZE_BYTES) {
        // Verify hash (optional but recommended)
        // For now, just store it
        if (!FlashStorage::saveImage(i, imageBuffer, IMAGE_SIZE_BYTES)) {
          allSuccess = false;
        }
      } else {
        allSuccess = false;
      }
    } else {
      allSuccess = false;
    }
  }
  
  free(imageBuffer);
  return allSuccess;
}

void displayCurrentImage() {
  if (deviceState.imageCount == 0) {
    return;
  }
  
  // Initialize display if not already done
  if (!displayInitialized) {
    DEV_Module_Init();
    EPD_4IN0E_Init();
    displayInitialized = true;
  }
  
  // Load image from flash
  uint8_t* imageBuffer = (uint8_t*)malloc(IMAGE_SIZE_BYTES);
  if (!imageBuffer) {
    return;
  }
  
  if (FlashStorage::loadImage(deviceState.currentImageIndex, imageBuffer, IMAGE_SIZE_BYTES)) {
    // Display image
    EPD_4IN0E_Display(imageBuffer);
  }
  
  free(imageBuffer);
  
  // Put display to sleep
  EPD_4IN0E_Sleep();
}

void advanceToNextImage() {
  if (deviceState.imageCount == 0) {
    return;
  }
  
  deviceState.currentImageIndex++;
  if (deviceState.currentImageIndex >= deviceState.imageCount) {
    deviceState.currentImageIndex = 0;  // Wrap around
  }
}
