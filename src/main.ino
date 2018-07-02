/*
  _                _ _            _       ________   ___   __   
 | |    ___   __ _(_) |_ ___  ___| |__   |__  / _ \ / _ \ / /_  
 | |   / _ \ / _` | | __/ _ \/ __| '_ \    / / (_) | | | | '_ \ 
 | |__| (_) | (_| | | ||  __/ (__| | | |  / /_\__, | |_| | (_) |
 |_____\___/ \__, |_|\__\___|\___|_| |_| /____| /_/ \___/ \___/ 
 __        __|___/__ _      _    ____ ___                       
 \ \      / (_)  ___(_)    / \  |  _ \_ _|                      
  \ \ /\ / /| | |_  | |   / _ \ | |_) | |                       
   \ V  V / | |  _| | |  / ___ \|  __/| |                       
    \_/\_/  |_|_|   |_| /_/   \_\_|  |___|                      
                                                                
 A Wifi API for the Logitech Z906 Speakers
 by Albin Winkelmann

*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <ESP8266WebServer.h>
#include <ArduinoOTA.h>
#include <WiFiManager.h>
#include <ArduinoJson.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <EEPROM.h>

#include "Secret.h"

#define OTA_HOSTNAME                ""       // Leave empty for esp8266-[ChipID]
#define WIFI_MANAGER_STATION_NAME   ""       // Leave empty for auto generated name ESP + ChipID

#define ON_LED D6                   // The pin that is connected to the on-led on speaker system
#define IR_LED D2                   // The IR LED pin
#define MS_BETWEEN_SENDING_IR 10    // Amount of ms to leap between sending commands in a row
#include "LogitechIRCodes.h"

ESP8266WebServer server(80);
IRsend irsend(IR_LED);

byte soundLevel;

enum Input { AUX, Input1, Input2, Input3, Input4, Input5 };
String inputs[] { "AUX", "Input1", "Input2", "Input3", "Input4", "Input5" };
Input currentInput;

enum Effect { Surround, Music, Stereo };
String effects[] { "Surround", "Music", "Stereo" };
Effect currentEffectOnInput[5];

enum Mode {
  Off,
  On
};
Mode currentMode = Off;

String modes[] = { "Off", "On" };

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  testShit();

  setupWifiManager();
  setupOTA();
  setupWebServer();
  setupIR();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
}

void loop() {
  checkIfStillOn();
  ArduinoOTA.handle();
  server.handleClient();
}

void testShit() {
  String json_string = "{\"sensor\":\"gps\",\"time\":1351824120,\"data\":[48.75608,2.302038]}";
  StaticJsonBuffer<200> reqBuffer;
  JsonObject& json = reqBuffer.parseObject(json_string);

  String one = json["sensor"];
  String two = "lol";
  if(!json["yes"]) {
    two = "yes";
  }  else if(json["yes"] == "") {
    two = "lel";
  }

  Serial.println("1: " + one);
  Serial.println("2: " + two);
  Serial.println(getStringIndex("Input3", inputs));
}

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
    StaticJsonBuffer<200> reqBuffer;
    JsonObject& reqjson = reqBuffer.parseObject(server.arg("plain"));

    // Print message
    Serial.println("\nPOST \"\\\": ");
    reqjson.prettyPrintTo(Serial);
    Serial.printf("\n");

    const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(4);
    DynamicJsonBuffer respBuffer(bufferSize);
    JsonObject& json = respBuffer.createObject();

    String response = "";
    String method = reqjson["method"];
    if(method == "turnOn") {
      Serial.println("Calling turnOn");
      turnOn();
    } else if(method == "turnOff") {
      Serial.println("Calling turnOff");
      turnOff();
    }
    // Getters 
    else if(method == "getSettings") {
      Serial.println("Calling getSettings");
      getSettings(json);
    } else if(method == "getMode") {
      Serial.println("Calling getMode");
      Serial.println(modes[currentMode]);
      json["mode"] = modes[currentMode];
    } else if(method == "getSoundLevel") {
      Serial.println("Calling getSoundLevel");
      json["soundlevel"] = soundLevel;
    } else if(method == "getInput") {
      Serial.println("Calling getInput");
      json["input"] = inputs[currentInput];
    } else if(method == "getEffect") {
      Serial.println("Calling getEffect");
      json["effect"] = effects[currentEffect()];
    }
    // Setters
    else if(method == "setSettings") {
      if(json["input"])       changeInput((Input)getStringIndex(json["input"], inputs));
      if(json["effect"])      changeEffect((Effect)getStringIndex(json["effect"], effects));
      if(json["soundlevel"])  changeSoundLevel(json["soundlevel"]);
    }
    else {
      String error = "Method: \"" + String(method) + "\" does not exist";
      Serial.println(error);
      server.send(400, "text/plain", error);
    }

    json.prettyPrintTo(response);
    Serial.println("Response:\n" + response);
    server.send(200, "application/json", response);
  });

  // Start webserver
  server.begin();
  Serial.println("[Webserver] Done.");
}

void setupIR() {
  Serial.println("[IRSend] Begin");
  irsend.begin();
}

void setupEEPROM() {
  EEPROM.begin(512);
  loadSettings();
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

void getSettings(JsonObject &json) {
  JsonObject& settings = json.createNestedObject("settings");
  settings["mode"] = currentMode;
  settings["soundlevel"] = soundLevel;
  settings["input"] = currentInput;
}

void loadSettings() {
  soundLevel = EEPROM.read(1);
  currentInput = (Input)EEPROM.read(2);

  for(uint8_t i= 0; i < 5; i++) {
    currentEffectOnInput[0] = (Effect)EEPROM.read(3 + i);
  }
}

void saveSettings() {
  EEPROM.write(1, soundLevel);
  EEPROM.write(2, currentInput);
  for(uint8_t i= 0; i < 5; i++) {
    EEPROM.write(3 + i, currentEffectOnInput[0]);
  }
  EEPROM.commit();
}

void turnOn() {
  currentMode = On;
  irsend.sendNEC(POWER_IR);
  saveSettings();
}

void turnOff() {
  currentMode = Off;
  irsend.sendNEC(POWER_IR);
  saveSettings();
}

void changeInput(Input input) {
  Serial.printf("Changing input to: %s", inputs[input]);
  switch(input) {
    case AUX:
      irsend.sendNEC(AUX_IR);
      break;
    case Input1:
      irsend.sendNEC(INPUT1_IR);
      break;
    default:
      Serial.print("No such input \"");
      Serial.print(String(input));
      Serial.println("\"");
  }
  currentInput = input;
  saveSettings();
}

void changeEffect(Effect effect) {
  Serial.printf("Changing effect to: %s", effects[effect]);
  int8_t diff = effect - currentEffectOnInput[currentInput];
  int8_t ir_send_times = (diff >= 0) ? diff : (abs(diff) + 1) % 3;

  for(uint8_t i = 0; i < ir_send_times; i++) {
    irsend.sendNEC(EFFECT_IR);
    delay(MS_BETWEEN_SENDING_IR);
  }

  currentEffectOnInput[currentInput] = effect;
  saveSettings();
}

void changeSoundLevel(byte level) {
  Serial.printf("Setting sound level to: %u\n", level);


}

void plus() {
  irsend.sendNEC(PLUS_IR);
}

void minus() {
  irsend.sendNEC(MINUS_IR);
}

Effect currentEffect() {
  return currentEffectOnInput[currentInput];
}

// check and see if speaker system is still on
void checkIfStillOn() {
  currentMode = digitalRead(ON_LED) ? Off : currentMode;
}

uint8_t getStringIndex(String s, String array[]) {
  for(uint8_t i = 0; i < sizeof(array); i++) {
    if(s == array[i]) return i;
  }
} 