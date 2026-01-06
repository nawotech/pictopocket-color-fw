/*****************************************************************************
 * | File      	:   flash_storage.h
 * | Function    :   Flash storage for images (SPIFFS)
 ******************************************************************************/
#ifndef _FLASH_STORAGE_H_
#define _FLASH_STORAGE_H_

#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"

class FlashStorage {
public:
  static bool begin();
  static void end();
  
  // Image storage operations
  static bool saveImage(int index, const uint8_t* imageData, size_t imageSize);
  static bool saveImageFromStream(int index, Stream* stream, size_t expectedSize);
  static bool loadImage(int index, uint8_t* imageData, size_t imageSize);
  static File openImageFile(int index);
  static bool hasImage(int index);
  static bool deleteImage(int index);
  static bool clearAllImages();
  
  // Get image file path
  static String getImagePath(int index);
  
  // Storage info
  static size_t getFreeSpace();
  static size_t getUsedSpace();
  static size_t getTotalSpace();
  
private:
  static bool initialized;
};

#endif

