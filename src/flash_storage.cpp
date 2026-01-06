#include "flash_storage.h"
#include <LittleFS.h>

bool FlashStorage::initialized = false;

bool FlashStorage::begin() {
  if (initialized) return true;
  
  // Initialize LittleFS with the partition label "storage"
  // begin(formatOnFail, basePath, maxOpenFiles, partitionLabel)
  // partitionLabel defaults to "spiffs" but we need "storage"
  if (!LittleFS.begin(true, "/littlefs", 10, STORAGE_PARTITION_LABEL)) {  // true = format if mount fails
    return false;
  }
  
  initialized = true;
  return true;
}

void FlashStorage::end() {
  if (initialized) {
    LittleFS.end();
    initialized = false;
  }
}

String FlashStorage::getImagePath(int index) {
  return "/image_" + String(index) + ".bin";
}

bool FlashStorage::saveImage(int index, const uint8_t* imageData, size_t imageSize) {
  if (!begin()) return false;
  if (imageSize != IMAGE_SIZE_BYTES) return false;
  
  String path = getImagePath(index);
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  
  size_t written = file.write(imageData, imageSize);
  file.close();
  
  return written == imageSize;
}

bool FlashStorage::saveImageFromStream(int index, Stream* stream, size_t expectedSize) {
  if (!begin()) return false;
  if (expectedSize != IMAGE_SIZE_BYTES) return false;
  if (!stream) return false;
  
  String path = getImagePath(index);
  File file = LittleFS.open(path, "w");
  if (!file) {
    return false;
  }
  
  // Stream data in chunks to avoid large buffer allocation
  const size_t chunkSize = 4096;  // 4KB chunks
  uint8_t* chunkBuffer = (uint8_t*)malloc(chunkSize);
  if (!chunkBuffer) {
    file.close();
    return false;
  }
  
  size_t totalWritten = 0;
  size_t bytesToRead = expectedSize;
  unsigned long timeout = millis() + 60000;  // 60 second timeout
  
  // Read until we have all the data or timeout
  while (bytesToRead > 0 && (millis() < timeout)) {
    // Wait a bit if no data is available yet (data might be arriving)
    if (stream->available() == 0) {
      delay(10);
      continue;
    }
    
    size_t toRead = (bytesToRead < chunkSize) ? bytesToRead : chunkSize;
    // Don't read more than what's available
    if (toRead > (size_t)stream->available()) {
      toRead = stream->available();
    }
    
    if (toRead == 0) {
      delay(10);
      continue;
    }
    
    size_t bytesRead = stream->readBytes((char*)chunkBuffer, toRead);
    
    if (bytesRead == 0) {
      delay(10);
      continue;
    }
    
    size_t bytesWritten = file.write(chunkBuffer, bytesRead);
    if (bytesWritten != bytesRead) {
      free(chunkBuffer);
      file.close();
      return false;
    }
    
    totalWritten += bytesWritten;
    bytesToRead -= bytesWritten;
  }
  
  free(chunkBuffer);
  file.close();
  
  if (totalWritten != expectedSize) {
    // Clean up partial file
    LittleFS.remove(path);
    return false;
  }
  
  return true;
}

bool FlashStorage::loadImage(int index, uint8_t* imageData, size_t imageSize) {
  if (!begin()) return false;
  if (imageSize != IMAGE_SIZE_BYTES) return false;
  
  String path = getImagePath(index);
  if (!LittleFS.exists(path)) {
    return false;
  }
  
  File file = LittleFS.open(path, "r");
  if (!file) {
    return false;
  }
  
  if (file.size() != imageSize) {
    file.close();
    return false;
  }
  
  size_t read = file.read(imageData, imageSize);
  file.close();
  
  return read == imageSize;
}

File FlashStorage::openImageFile(int index) {
  if (!begin()) {
    return File();
  }
  
  String path = getImagePath(index);
  if (!LittleFS.exists(path)) {
    return File();
  }
  
  File file = LittleFS.open(path, "r");
  if (!file) {
    return File();
  }
  
  if (file.size() != IMAGE_SIZE_BYTES) {
    file.close();
    return File();
  }
  
  return file;
}

bool FlashStorage::hasImage(int index) {
  if (!begin()) return false;
  String path = getImagePath(index);
  return LittleFS.exists(path);
}

bool FlashStorage::deleteImage(int index) {
  if (!begin()) return false;
  String path = getImagePath(index);
  return LittleFS.remove(path);
}

bool FlashStorage::clearAllImages() {
  if (!begin()) return false;
  
  // Delete all image files
  for (int i = 0; i < MAX_IMAGES; i++) {
    deleteImage(i);
  }
  
  return true;
}

size_t FlashStorage::getFreeSpace() {
  if (!begin()) return 0;
  // LittleFS doesn't have info() method, calculate by iterating files
  // For simplicity, return a fixed value based on partition size
  // Partition is 0x170000 = 1,507,328 bytes
  size_t total = 0x170000;
  size_t used = 0;
  
  // Calculate used space by summing file sizes
  File root = LittleFS.open("/");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      used += file.size();
      file = root.openNextFile();
    }
    root.close();
  }
  
  return total - used;
}

size_t FlashStorage::getUsedSpace() {
  if (!begin()) return 0;
  size_t used = 0;
  
  // Calculate used space by summing file sizes
  File root = LittleFS.open("/");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      used += file.size();
      file = root.openNextFile();
    }
    root.close();
  }
  
  return used;
}

size_t FlashStorage::getTotalSpace() {
  if (!begin()) return 0;
  // Partition size: 0x170000 = 1,507,328 bytes
  return 0x170000;
}

