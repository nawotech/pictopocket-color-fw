#include "nvs_storage.h"

Preferences NVSStorage::preferences;

bool NVSStorage::begin() {
  return preferences.begin("frame", false);
}

void NVSStorage::end() {
  preferences.end();
}

bool NVSStorage::saveDeviceKey(const String& key) {
  if (!begin()) return false;
  bool result = preferences.putString("deviceKey", key);
  end();
  return result;
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
  if (!begin()) return false;
  
  preferences.putInt("currentImageIndex", state.currentImageIndex);
  preferences.putInt("wakeCounter", state.wakeCounter);
  preferences.putInt("slideshowVersion", state.slideshowVersion);
  preferences.putInt("imageCount", state.imageCount);
  
  // Save image IDs and hashes
  for (int i = 0; i < state.imageCount && i < 12; i++) {
    String idKey = "imageId" + String(i);
    String hashKey = "imageHash" + String(i);
    preferences.putString(idKey.c_str(), state.imageIds[i]);
    preferences.putString(hashKey.c_str(), state.imageHashes[i]);
  }
  
  end();
  return true;
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

