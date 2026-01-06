/*****************************************************************************
 * | File      	:   nvs_storage.h
 * | Function    :   NVS (Non-Volatile Storage) management for device state
 ******************************************************************************/
#ifndef _NVS_STORAGE_H_
#define _NVS_STORAGE_H_

#include <Arduino.h>
#include <Preferences.h>

// Device state stored in NVS
struct DeviceState {
  String deviceKey;           // Raw device key (32 bytes hex = 64 chars)
  int currentImageIndex;      // Current image index (0-based)
  int wakeCounter;            // Wake counter (0-5, resets at 6)
  int slideshowVersion;       // Last known slideshow version
  int imageCount;             // Number of images in current slideshow
  String imageIds[12];        // Image UUIDs (max 12)
  String imageHashes[12];     // Image hashes (max 12)
};

class NVSStorage {
public:
  static bool begin();
  static void end();
  
  // Device key management
  static bool saveDeviceKey(const String& key);
  static String loadDeviceKey();
  static bool hasDeviceKey();
  
  // State management
  static bool saveState(const DeviceState& state);
  static bool loadState(DeviceState& state);
  static bool clearState();
  
  // Individual state fields
  static bool saveInt(const char* key, int value);
  static int loadInt(const char* key, int defaultValue);
  static bool saveString(const char* key, const String& value);
  static String loadString(const char* key, const String& defaultValue);
  static bool saveStringArray(const char* key, const String* values, int count);
  static int loadStringArray(const char* key, String* values, int maxCount);
  
private:
  static Preferences preferences;
};

#endif

