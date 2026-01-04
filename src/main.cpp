#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "wifi_config.h"

void setup()
{
  Serial.begin(115200);
  delay(1000);

  WiFi.mode(WIFI_STA); // Optional
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("\nConnecting");

  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(100);
  }

  Serial.println("\nConnected to the WiFi network");
  Serial.print("Local ESP32 IP: ");
  Serial.println(WiFi.localIP());
}

void loop() {}