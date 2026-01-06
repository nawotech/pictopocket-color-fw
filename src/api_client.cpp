#include "api_client.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "wifi_config.h"
#include <mbedtls/sha256.h>
#include <Stream.h>

String APIClient::parseUrl(const String& url, String& host, String& path) {
  String urlStr = url;
  if (urlStr.startsWith("https://")) {
    urlStr = urlStr.substring(8);
  }
  
  int slashIndex = urlStr.indexOf('/');
  if (slashIndex > 0) {
    host = urlStr.substring(0, slashIndex);
    path = urlStr.substring(slashIndex);
  } else {
    host = urlStr;
    path = "/";
  }
  
  return host;
}

String APIClient::calculateSHA256(const uint8_t* data, size_t length) {
  uint8_t hash[32];
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);
  mbedtls_sha256_update(&ctx, data, length);
  mbedtls_sha256_finish(&ctx, hash);
  mbedtls_sha256_free(&ctx);
  
  String result = "";
  for (int i = 0; i < 32; i++) {
    if (hash[i] < 0x10) result += "0";
    result += String(hash[i], HEX);
  }
  return result;
}

bool APIClient::getSlideshowVersion(const String& deviceId, const String& deviceKey, SlideshowVersionResponse& response) {
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String host, path;
  parseUrl(String(GET_SLIDESHOW_VERSION_URL), host, path);
  path += "?device_id=" + deviceId + "&device_key=" + deviceKey;
  
  http.begin(client, host.c_str(), 443, path.c_str());
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  bool success = false;
  
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<200> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      response.slideshowVersion = doc["slideshowVersion"] | 0;
      response.status = doc["status"] | "NO_CHANGE";
      response.success = true;
      success = true;
    }
  }
  
  http.end();
  return success;
}

bool APIClient::getSlideshowManifest(const String& deviceId, const String& deviceKey, SlideshowManifestResponse& response) {
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String host, path;
  parseUrl(String(GET_SLIDESHOW_MANIFEST_URL), host, path);
  path += "?device_id=" + deviceId + "&device_key=" + deviceKey;
  
  http.begin(client, host.c_str(), 443, path.c_str());
  http.setTimeout(10000);
  
  int httpCode = http.GET();
  bool success = false;
  
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, payload);
    
    if (!error) {
      response.slideshowVersion = doc["slideshowVersion"] | 0;
      
      JsonArray imageIds = doc["imageIds"];
      JsonArray imageHashes = doc["imageHashes"];
      
      response.imageCount = 0;
      int maxCount = (imageIds.size() < 12) ? imageIds.size() : 12;
      
      for (int i = 0; i < maxCount; i++) {
        response.imageIds[i] = imageIds[i].as<String>();
        response.imageHashes[i] = imageHashes[i].as<String>();
        response.imageCount++;
      }
      
      response.success = true;
      success = true;
    }
  }
  
  http.end();
  return success;
}

bool APIClient::getSignedUrls(const String& deviceId, const String& deviceKey, const String* imageIds, int count, SignedUrlsResponse& response) {
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String host, path;
  parseUrl(String(GET_SIGNED_URLS_URL), host, path);
  
  // Build JSON request
  StaticJsonDocument<2048> doc;
  doc["device_id"] = deviceId;
  doc["device_key"] = deviceKey;
  JsonArray imageIdsArray = doc.createNestedArray("imageIds");
  for (int i = 0; i < count; i++) {
    imageIdsArray.add(imageIds[i]);
  }
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  http.begin(client, host.c_str(), 443, path.c_str());
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(30000);
  
  int httpCode = http.POST(requestBody);
  bool success = false;
  
  if (httpCode == 200) {
    String payload = http.getString();
    StaticJsonDocument<2048> responseDoc;
    DeserializationError error = deserializeJson(responseDoc, payload);
    
    if (!error) {
      response.count = 0;
      // The response is a JSON object mapping imageId -> signedUrl
      // We need to match the order of imageIds we sent
      for (int i = 0; i < count && i < 12; i++) {
        if (responseDoc.containsKey(imageIds[i])) {
          response.urls[i] = responseDoc[imageIds[i]].as<String>();
          response.count++;
        } else {
          // Missing URL for this imageId
          response.urls[i] = "";
        }
      }
      response.success = (response.count == count);
      success = true;
    }
  }
  
  http.end();
  return success;
}

bool APIClient::ackDisplayed(const String& deviceId, const String& deviceKey, int slideshowVersion) {
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String host, path;
  parseUrl(String(ACK_DISPLAYED_URL), host, path);
  
  StaticJsonDocument<200> doc;
  doc["device_id"] = deviceId;
  doc["device_key"] = deviceKey;
  doc["slideshow_version"] = slideshowVersion;
  
  String requestBody;
  serializeJson(doc, requestBody);
  
  http.begin(client, host.c_str(), 443, path.c_str());
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000);
  
  int httpCode = http.POST(requestBody);
  http.end();
  
  return httpCode == 200;
}

bool APIClient::downloadImage(const String& signedUrl, uint8_t* buffer, size_t bufferSize, size_t& bytesDownloaded) {
  WiFiClientSecure client;
  HTTPClient http;
  client.setInsecure();
  
  String host, path;
  parseUrl(signedUrl, host, path);
  
  http.begin(client, host.c_str(), 443, path.c_str());
  http.setTimeout(60000);  // 60 second timeout for image download
  
  int httpCode = http.GET();
  bytesDownloaded = 0;
  
  if (httpCode == 200) {
    int contentLength = http.getSize();
    if (contentLength > 0 && contentLength <= (int)bufferSize) {
      // Read binary data directly from HTTP client
      // Use getStream() which returns a Stream* that we can read from
      Stream* stream = http.getStreamPtr();
      if (stream) {
        bytesDownloaded = stream->readBytes((char*)buffer, contentLength);
      }
    }
  }
  
  http.end();
  return httpCode == 200 && bytesDownloaded > 0;
}

// Note: This function returns a stream, but the caller must keep the HTTPClient alive
// For proper streaming, we'll do it inline in the download function instead

