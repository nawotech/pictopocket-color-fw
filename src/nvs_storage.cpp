#include "nvs_storage.h"

Preferences NVSStorage::preferences;

bool NVSStorage::begin() {
  return preferences.begin("frame", false);
}

void NVSStorage::end() {
  preferences.end();
}

bool NVSStorage::saveDeviceKey(const String& key) {
  // Ensure preferences is closed first
  preferences.end();
  
  // Open preferences for writing (readonly=false allows writing)
  if (!preferences.begin("frame", false)) {
    return false;
  }
  
  // Save the key - putString returns the number of bytes written, or 0 on failure
  // For a 64-char hex string, we expect at least 64 bytes written
  size_t written = preferences.putString("deviceKey", key);
  
  // Force commit to ensure data is written
  preferences.end();
  
  // Reopen to verify it was written
  if (!preferences.begin("frame", true)) {  // readonly=true for verification
    return false;
  }
  String verify = preferences.getString("deviceKey", "");
  preferences.end();
  
  // Check if write was successful
  bool success = (written >= key.length()) && (verify == key);
  return success;
}

String NVSStorage::loadDeviceKey() {
  if (!begin()) return "";
  String key = preferences.getString("deviceKey", "");
  end();
  return key;
}

bool NVSStorage::hasDeviceKey() {
  if (!begin()) return false;
  bool has = preferences.isKey("deviceKey");
  end();
  return has;
}

bool NVSStorage::saveState(const DeviceState& state) {
  if (!begin()) {
    return false;
  }
  
  // Save basic state
  bool success = true;
  success = success && preferences.putInt("currentImageIndex", state.currentImageIndex);
  success = success && preferences.putInt("wakeCounter", state.wakeCounter);
  success = success && preferences.putInt("slideshowVersion", state.slideshowVersion);
  success = success && preferences.putInt("imageCount", state.imageCount);
  
  // Save image IDs and hashes (only for images that exist)
  for (int i = 0; i < state.imageCount && i < 12; i++) {
    String idKey = "imageId" + String(i);
    String hashKey = "imageHash" + String(i);
    success = success && preferences.putString(idKey.c_str(), state.imageIds[i]);
    success = success && preferences.putString(hashKey.c_str(), state.imageHashes[i]);
  }
  
  // Clear any old image entries beyond current count
  for (int i = state.imageCount; i < 12; i++) {
    String idKey = "imageId" + String(i);
    String hashKey = "imageHash" + String(i);
    preferences.remove(idKey.c_str());
    preferences.remove(hashKey.c_str());
  }
  
  end();
  return success;
}

bool NVSStorage::loadState(DeviceState& state) {
  if (!begin()) return false;
  
  state.currentImageIndex = preferences.getInt("currentImageIndex", 0);
  state.wakeCounter = preferences.getInt("wakeCounter", 0);
  state.slideshowVersion = preferences.getInt("slideshowVersion", 0);
  state.imageCount = preferences.getInt("imageCount", 0);
  
  // Load image IDs and hashes
  for (int i = 0; i < state.imageCount && i < 12; i++) {
    String idKey = "imageId" + String(i);
    String hashKey = "imageHash" + String(i);
    state.imageIds[i] = preferences.getString(idKey.c_str(), "");
    state.imageHashes[i] = preferences.getString(hashKey.c_str(), "");
  }
  
  end();
  return true;
}

bool NVSStorage::clearState() {
  if (!begin()) return false;
  preferences.clear();
  end();
  return true;
}

bool NVSStorage::saveInt(const char* key, int value) {
  if (!begin()) return false;
  bool result = preferences.putInt(key, value);
  end();
  return result;
}

int NVSStorage::loadInt(const char* key, int defaultValue) {
  if (!begin()) return defaultValue;
  int value = preferences.getInt(key, defaultValue);
  end();
  return value;
}

bool NVSStorage::saveString(const char* key, const String& value) {
  if (!begin()) return false;
  bool result = preferences.putString(key, value);
  end();
  return result;
}

String NVSStorage::loadString(const char* key, const String& defaultValue) {
  if (!begin()) return defaultValue;
  String value = preferences.getString(key, defaultValue);
  end();
  return value;
}

bool NVSStorage::saveStringArray(const char* key, const String* values, int count) {
  if (!begin()) return false;
  preferences.putInt(key, count);
  for (int i = 0; i < count; i++) {
    String itemKey = String(key) + "_" + String(i);
    preferences.putString(itemKey.c_str(), values[i]);
  }
  end();
  return true;
}

int NVSStorage::loadStringArray(const char* key, String* values, int maxCount) {
  if (!begin()) return 0;
  int count = preferences.getInt(key, 0);
  if (count > maxCount) count = maxCount;
  for (int i = 0; i < count; i++) {
    String itemKey = String(key) + "_" + String(i);
    values[i] = preferences.getString(itemKey.c_str(), "");
  }
  end();
  return count;
}

