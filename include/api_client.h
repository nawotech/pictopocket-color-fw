/*****************************************************************************
 * | File      	:   api_client.h
 * | Function    :   HTTP client for Firebase Cloud Functions API
 ******************************************************************************/
#ifndef _API_CLIENT_H_
#define _API_CLIENT_H_

#include <Arduino.h>
#include <ArduinoJson.h>

struct SlideshowVersionResponse {
  int slideshowVersion;
  String status;  // "NEW" or "NO_CHANGE"
  bool success;
};

struct SlideshowManifestResponse {
  int slideshowVersion;
  String imageIds[12];
  String imageHashes[12];
  int imageCount;
  bool success;
};

struct SignedUrlsResponse {
  String urls[12];  // Array of signed URLs in same order as imageIds
  int count;
  bool success;
};

class APIClient {
public:
  static bool getSlideshowVersion(const String& deviceId, const String& deviceKey, SlideshowVersionResponse& response);
  static bool getSlideshowManifest(const String& deviceId, const String& deviceKey, SlideshowManifestResponse& response);
  static bool getSignedUrls(const String& deviceId, const String& deviceKey, const String* imageIds, int count, SignedUrlsResponse& response);
  static bool ackDisplayed(const String& deviceId, const String& deviceKey, int slideshowVersion);
  static bool downloadImage(const String& signedUrl, uint8_t* buffer, size_t bufferSize, size_t& bytesDownloaded);
  static String parseUrl(const String& url, String& host, String& path);
  
private:
  static String calculateSHA256(const uint8_t* data, size_t length);
};

#endif

