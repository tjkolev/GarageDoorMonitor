#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <sensitive.h>
#include <main.h>

void setupWiFi() {
  log("Setting up Wifi.");

  WiFi.disconnect();
  delay(5 * 1000);

  WiFi.persistent(false);
  WiFi.mode(WIFI_OFF);
  WiFi.mode(WIFI_STA);
  WiFi.config(0U, 0U, 0U); // use DHCP
  WiFi.setHostname("iotGarageDoor");
  WiFi.begin(WIFI_NETWORK, WIFI_PASSWORD);

  delay(15 * 1000);
  for(int n = 0; n < 8; n++) {
    if(WiFi.status() == WL_CONNECTED) {
        break;
    }
    delay(5 * 1000);
  }

  IPAddress ip = WiFi.localIP();
  log("WiFi setup done. %d.%d.%d.%d %s", ip[0], ip[1], ip[2], ip[3], WiFi.macAddress().c_str());
}

bool wifiConnected() {
  return (WiFi.status() == WL_CONNECTED);
}

bool ensureWiFi() {
  if(!wifiConnected()) {
    setupWiFi();
  }
  return wifiConnected();
}