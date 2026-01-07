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
  
  // OPTIMIZATION: Use larger chunk size for faster writes (8KB instead of 4KB)
  // This reduces the number of write operations
  const size_t chunkSize = 8192;  // 8KB chunks
  uint8_t* chunkBuffer = (uint8_t*)malloc(chunkSize);
  if (!chunkBuffer) {
    file.close();
    return false;
  }
  
  size_t totalWritten = 0;
  size_t bytesToRead = expectedSize;
  unsigned long timeout = millis() + 60000;  // 60 second timeout
  unsigned long lastDataTime = millis();
  
  // OPTIMIZATION: Read in larger chunks when available to reduce write operations
  // Read until we have all the data or timeout
  while (bytesToRead > 0 && (millis() < timeout)) {
    size_t available = stream->available();
    
    // OPTIMIZATION: Only wait if no data available
    // Use minimal delay to avoid blocking when data is actively streaming
    if (available == 0) {
      // Very short delay - just yield to WiFi stack
      delayMicroseconds(50);
      continue;
    }
    
    lastDataTime = millis(); // Update last data time
    
    // OPTIMIZATION: Read as much as possible in one go (up to chunkSize)
    // This reduces the number of write operations to flash
    size_t toRead = (bytesToRead < chunkSize) ? bytesToRead : chunkSize;
    // Read all available data if it's less than our target chunk size
    if (available < toRead) {
      toRead = available;
    }
    
    // OPTIMIZATION: Use readBytes which is more efficient than read()
    // It will read up to toRead bytes, but may return less if not all available
    size_t bytesRead = stream->readBytes((char*)chunkBuffer, toRead);
    
    if (bytesRead == 0) {
      // No data read - wait a tiny bit and try again
      delayMicroseconds(50);
      continue;
    }
    
    // Write to flash - this is the potentially slow operation
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

