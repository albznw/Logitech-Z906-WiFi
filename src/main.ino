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
#include <IRrecv.h>
#include <EEPROM.h>
#include <PubSubClient.h>


#include "Secret.h"
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

/**************************** General - Settings ******************************/
#define OTA_HOSTNAME                "Logitech-Z906" // Leave empty for esp8266-[ChipID]
#define WIFI_MANAGER_STATION_NAME   "Logitech-Z906" // Leave empty for auto generated name ESP + ChipID

#define ON_LED                D6    // The pin that is connected to the on-led on speaker system
#define IR_LED                D2    // The IR LED pin
#define RECV_LED              D1    // The ir reciever pin
#define MS_BETWEEN_SENDING_IR 10    // Amount of ms to leap between sending commands in a row
#include "LogitechIRCodes.h"

bool OTA_ON = false; // Turn on OTA

/****************************** MQTT - Settings *******************************/
// Connection things is found in Secret.h
#define MQTTClientId        "logitech_z906"
#define MQTTCategory        "speaker"

#define ClientRoot          MQTTCategory "/" MQTTClientId

// Some examples on how the routes should be
#define CommandTopic        ClientRoot "/cmnd/json"
#define StateTopic          ClientRoot "/state/json"
#define DebugTopic          ClientRoot "/debug"
#define WillTopic           ClientRoot "/will"
#define WillQoS             0
#define WillRetain          false
const char* willMessage = "clientId has disconnected...";

#define FirstMessage        "I communicate via JSON!"

WiFiClient wificlient;  // is needed for the mqtt client
PubSubClient mqttclient;

ESP8266WebServer server(80);
IRsend irsend(IR_LED);
IRrecv rec(RECV_LED);

/********************************* Variables **********************************/

byte soundLevel;

enum Input { AUX, Input1, Input2, Input3, Input4, Input5 };
const char* inputs[] { "AUX", "Input1", "Input2", "Input3", "Input4", "Input5" };
Input currentInput;

enum Effect { Surround, Music, Stereo };
const char* effects[] { "Surround", "Music", "Stereo" };
Effect currentEffectOnInput[5];

enum Mode : byte { Off, On };
const char* modes[] = { "Off", "On" };
Mode currentMode = Off;


void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  setupWifiManager();
  setupOTA();
  setupWebServer();
  setupIR();

  setupEEPROM();
  printSettings();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  checkIfStillOn();
  if(OTA_ON) ArduinoOTA.handle();
  server.handleClient();
  mqttclient.loop();
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
  Serial.println(getStringIndex("Input3", inputs, ARRAY_SIZE(inputs)));

  loadSettings();
  unsigned long start = micros();
  saveSettings();
  unsigned long finish = micros();
  Serial.printf("Done, took %u µs", finish - start);
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
    // Print message
    Serial.println("\nPOST \"\\\": ");
    server.send(200, "application/json", \
      handleJSONReq(server.arg("plain")));
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

// Be sure to setup WIFI before running this method!
void setupMQTT() {
  mqttclient = PubSubClient(Broker, Port, callback, wificlient);
  connectMQTT();
}

bool connectMQTT() {
  while (!mqttclient.connected()) {
    Serial.print("Connecting to MQTT server... ");

    //if connected, subscribe to the topic(s) we want to be notified about
    if (mqttclient.connect(MQTTClientId, MQTTUsername, MQTTPassword, WillTopic,\
        WillQoS, WillRetain, willMessage)) {
      Serial.println("MTQQ Connected!");
      mqttclient.subscribe(CommandTopic);
      publishMQTT(DebugTopic, FirstMessage);
      return true;
    }
  }
  Serial.println("Failed to connect to MQTT Server");
  return false;
}

bool publishMQTT(const char* topic, const char* payload){
  String printString = "";
  bool returnBool = false;
  if(mqttclient.publish(topic, payload)) {
    returnBool = true;
    printString = String("[publishMQTT] '" + String(payload) + "' was sent sucessfully to: ");
  } else{
    returnBool = false;
    printString = String("[publishMQTT] ERROR sending: '" + String(payload) + "' to: ");
  }
  printString += topic;
  Serial.println(printString);
  return returnBool;
}

bool publishMQTT(const char* topic, String payload){
  return publishMQTT(topic, payload.c_str());
}

String payloadToString(byte* payload, unsigned int length) {
  char message_buff[length];
  int i = 0;
  for (i = 0; i < length; i++) {
      message_buff[i] = payload[i];
    }
  message_buff[i] = '\0';
  return String(message_buff);
}

void callback(char* topic, byte* payload, unsigned int length) {

  //convert topic to string to make it easier to work with
  String topicStr = topic;
  String payloadStr = payloadToString(payload, length);

  Serial.println("[MQTT][callback] Callback update.");
  Serial.println(String("[MQTT][callback] Topic: " + topicStr));

  if(topicStr.equals(CommandTopic)) {
    publishMQTT(StateTopic, handleJSONReq(payloadStr));
  } else {
    publishMQTT(DebugTopic, String("[MQTT][callback] No such MQTT topic \""\
     + topicStr +"\""));
  }
}

String handleJSONReq(String req) {
  StaticJsonBuffer<200> reqBuffer;
  JsonObject& reqjson = reqBuffer.parseObject(req);

  Serial.print("[handleJSON] Payload: ");
  reqjson.prettyPrintTo(Serial);
  Serial.printf("\n");

  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(4);
  DynamicJsonBuffer respBuffer(bufferSize);
  JsonObject& json = respBuffer.createObject();

  String response = "";
  String method = reqjson["method"];
  if(method == "turnOn") {
    Serial.println("[handleJSON] Calling turnOn");
    turnOn();
  } else if(method == "turnOff") {
    Serial.println("[handleJSON] Calling turnOff");
    turnOff();
  }
  // Getters 
  else if(method == "getSettings") {
    Serial.println("[handleJSON] Calling getSettings");
    getSettings(json);
  } else if(method == "getMode") {
    Serial.println("[handleJSON] Calling getMode");
    Serial.println(modes[currentMode]);
    json["mode"] = modes[currentMode];
  } else if(method == "getSoundLevel") {
    Serial.println("[handleJSON] Calling getSoundLevel");
    json["soundlevel"] = soundLevel;
  } else if(method == "getInput") {
    Serial.println("[handleJSON] Calling getInput");
    json["input"] = inputs[currentInput];
  } else if(method == "getEffect") {
    Serial.println("[handleJSON] Calling getEffect");
    json["effect"] = effects[currentEffect()];
  }
  // Setters
  else if(method == "setSettings") {
    bool somethingChanged = false;

    const char* input = reqjson["input"];
    if(input) {
      Serial.println("[setSettings] Input setting detected");
      changeInput((Input)getStringIndex(input, inputs, ARRAY_SIZE(inputs)));
      somethingChanged = true;
    }

    const char* effect = reqjson["effect"];
    if(effect) {
      Serial.println("[setSettings] Effect setting detected");
      changeEffect((Effect)getStringIndex(effect, effects, ARRAY_SIZE(effects)));
      somethingChanged = true;
    }

    int soundlevel = reqjson["soundlevel"];
    if(soundlevel) {
      Serial.println("[setSettings] Soundlevel setting detected");
      changeSoundLevel(soundlevel);
      somethingChanged = true;
    }
    if(somethingChanged) {
      getSettings(json);
    } else {
      json["message"] = "You didn't specify input, effect or soundlevel";
    }
  } else if(method = "reset") {
    soundLevel = 0;
    currentInput = Input1;
    for(uint8_t i= 0; i < 5; i++) {
      currentEffectOnInput[i] = Surround;
    }
    json["message"] = "Settings resetted";
    getSettings(json);
  }
  else {
    String error = "Method: \"" + String(method) + "\" does not exist";
    Serial.println(error);
    server.send(400, "text/plain", error);
  }

  json.prettyPrintTo(response);
  Serial.println("[handleJSON] Response:\n" + response);
  return response;
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
  settings["mode"] = modes[currentMode];
  settings["soundlevel"] = soundLevel;
  settings["input"] = inputs[currentInput];
  settings["effect"] = effects[currentEffect()];
}

void loadSettings() {
  soundLevel = EEPROM.read(1);
  if(soundLevel > 128 || soundLevel < 0) {
    soundLevel = 0;
  }

  currentInput = (Input)EEPROM.read(2);
  currentInput = currentInput < 6 ? currentInput : AUX;

  for(uint8_t i= 0; i < 5; i++) {
    currentEffectOnInput[i] = (Effect)EEPROM.read(3 + i);
    currentEffectOnInput[i] = currentEffectOnInput[i] < 3 ? currentEffectOnInput[i] : Surround;
  }
}

void printSettings() {
  Serial.printf("\nInput: %s\n", inputs[currentInput]);
  Serial.printf("Soundlevel: %d\n", soundLevel);
  Serial.printf("Effect: %s\n\n", effects[currentEffect()]);
}

void saveSettings() {
  EEPROM.write(1, soundLevel);
  EEPROM.write(2, currentInput);
  for(uint8_t i= 0; i < 5; i++) {
    EEPROM.write(3 + i, currentEffectOnInput[i]);
  }
  EEPROM.commit();
}

void turnOn() {
  if(currentMode != On) {
    irsend.sendNEC(POWER_IR, 32);
    currentMode = On;
    saveSettings();
  }
}

void turnOff() {
  if(currentMode != Off) {
    irsend.sendNEC(POWER_IR, 32);
    currentMode = Off;
    saveSettings();
  }
}

void changeInput(Input input) {
  Serial.printf("[changeInput] Changing input to: %s", inputs[input]);
  switch(input) {
    case AUX:
      irsend.sendNEC(AUX_IR, 32);
      break;
    case Input1:
      irsend.sendNEC(INPUT1_IR, 32);
      break;
    case Input2:
      irsend.sendNEC(INPUT2_IR, 32);
      break;
    case Input3:
      irsend.sendNEC(INPUT3_IR, 32);
      break;
    case Input4:
      irsend.sendNEC(INPUT4_IR, 32);
      break;
    case Input5:
      irsend.sendNEC(INPUT5_IR, 32);
      break;
    default:
      Serial.println("\tNo such input!");
  }
  Serial.printf("\n");
  currentInput = input;
  saveSettings();
}

void changeEffect(Effect effect) {
  Serial.printf("[changeEffect] Changing effect from: %s to: %s\n", effects[currentEffect()], effects[effect]);
  int8_t diff = effect - currentEffectOnInput[currentInput];
  int8_t ir_send_times = (diff >= 0) ? diff : (abs(diff) + 1) % 3;
  Serial.printf("[changeEffect] Diff: %d\t\tBlasting ir %d times\n", diff, ir_send_times);

  for(uint8_t i = 0; i < ir_send_times; i++) {
    irsend.sendNEC(EFFECT_IR);
    delay(MS_BETWEEN_SENDING_IR);
  }

  currentEffectOnInput[currentInput] = effect;
  saveSettings();
}

void changeSoundLevel(int8_t level) {
  int8_t diff = level - soundLevel;
  Serial.printf("[changeSoundLevel] Setting sound level %d -> %d\tDiff: %d\n", soundLevel, level, diff);
  if(diff > 1) {
    irsend.sendNEC(PLUS_IR, 32, diff);
  } else if(diff < 0) {
    irsend.sendNEC(MINUS_IR, 32, -diff);
  }
  soundLevel = level;
  saveSettings();
}

Effect currentEffect() {
  return currentEffectOnInput[currentInput];
}

// check and see if speaker system is still on
void checkIfStillOn() {
  currentMode = digitalRead(ON_LED) ? Off : currentMode;
}

uint8_t getStringIndex(String s, const char* array[], uint8_t len) {
  Serial.printf("[getStringIndex] Length of array: %d\n", len);
  for(uint8_t i = 0; i < len; i++) {
    if(s == array[i]) return i;
  }
} 