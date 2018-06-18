#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>

const char* ssid = "..........";
const char* password = "..........";

#define OTA_HOSTNAME                ""       // Leave empty for esp8266-[ChipID]
#define WIFI_MANAGER_STATION_NAME   ""       // Leave empty for auto generated name ESP + ChipID

ESP8266WebServer server(80);

void setupOTA() {
  Serial.println("[OTA] Initializing...");
  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  if (OTA_HOSTNAME != "") {
    ArduinoOTA.setHostname(OTA_HOSTNAME);
  }

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("[OTA] Done.");
}

void setupWifiManager() {
  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  //reset saved settings
  //wifiManager.resetSettings();
  
  //set custom ip for portal
  //wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //fetches ssid and pass from eeprom and tries to connect
  //if it does not connect it starts an access point with the specified name
  //and goes into a blocking loop awaiting configuration
  if (WIFI_MANAGER_STATION_NAME == "") {
    //use this for auto generated name ESP + ChipID
    wifiManager.autoConnect();
  } else {
    wifiManager.autoConnect(WIFI_MANAGER_STATION_NAME);
  }
}

void setupWebServer() {
  Serial.println("[Webserver] Initializing...");
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/plain", "It works!");
  });

  server.on("/", HTTP_POST, [](){
    StaticJsonBuffer<200> newBuffer;
    JsonObject& newjson = newBuffer.parseObject(server.arg("plain"));

    // Print message
    Serial.println("\nPOST \"\\\": ");
    newjson.prettyPrintTo(Serial);
    Serial.printf("\n\n");

    String method = newjson["method"];
    if(method == "test") {
        
    } else {
        Serial.print("Method: \"");
        Serial.print(method);
        Serial.println("\" does not exist");
    }

    server.send(200, "application/json");
  });

  // Start webserver
  server.begin();
  Serial.println("[Webserver] Done.");
}

String getChipStatsJSON() {
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(4);
  DynamicJsonBuffer jsonBuffer(bufferSize);

  JsonObject& root = jsonBuffer.createObject();
  JsonObject& chip = root.createNestedObject("chip");

  chip["id"] = String(ESP.getFlashChipId(), HEX);
  chip["mode"] = String(ESP.getFlashChipMode());
  chip["size"] = String(String(ESP.getFlashChipRealSize()) + " bytes");
  chip["speed"] = String(String(ESP.getFlashChipSpeed()) + " Hz");

  String resp;
  root.prettyPrintTo(resp);
  return resp;
}

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  setupWifiManager();
  setupOTA();
  setupWebServer();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
}