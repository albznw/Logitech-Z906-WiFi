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
#include <IRutils.h>
#include <EEPROM.h>
#include <PubSubClient.h>
#include <TaskScheduler.h>

#include "Secret.h"
#define ARRAY_SIZE(A) (sizeof(A) / sizeof((A)[0]))

/**************************** General - Settings ******************************/
#define OTA_HOSTNAME                "Logitech-Z906" // Leave empty for esp8266-[ChipID]
#define WIFI_MANAGER_STATION_NAME   "Logitech-Z906" // Leave empty for auto generated name ESP + ChipID

#define STATUS_LED            D7    // Status led pin
#define ON_LED                D0    // The pin that is connected to the on-led on speaker system
#define IR_LED                D2    // The IR LED pin
#define RECV_IR               D1    // The ir reciever pin
#define MS_BETWEEN_SENDING_IR 20    // Amount of ms to leap between sending commands in a row
#include "LogitechIRCodes.h"

bool OTA_ON = true; // Turn on OTA

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
const char* willMessage = MQTTClientId " has disconnected...";

#define FirstMessage        "I communicate via JSON!"
#define MQTT_MAX_PACKET_SIZE 192 //Remember to set this i platformio.ini

WiFiClient wificlient;  // is needed for the mqtt client
PubSubClient mqttclient;

ESP8266WebServer server(80);

#define CAPTURE_BUFFER_SIZE 64
#define MIN_UNKNOWN_SIZE    12
IRrecv irrecv(RECV_IR, CAPTURE_BUFFER_SIZE);
IRsend irsend(IR_LED);
decode_results results;  // Somewhere to store the results

/********************************* Variables **********************************/

int8_t soundLevel[4]; // [Volume, Bass, Rear, Center]

bool mute;
bool isOn = false;

enum Input : byte { AUX, Input1, Input2, Input3, Input4, Input5 };
const char* inputs[] { "AUX", "Input 1", "Input 2", "Input 3", "Input 4", "Input 5" };
Input currentInput;

enum Effect : byte { Surround, Music, Stereo };
const char* effects[] { "Surround", "Music", "Stereo" };
Effect currentEffectOnInput[5];

// In mode On we'll change the soundlevel (defult)
enum Mode : byte { Off, On, BassLevel, RearLevel, CenterLevel};
const char* modes[] = { "Off", "On", "Bass level", "Rear level", "Center level" };
Mode currentMode = Off;
Mode lastMode = On;

#define LEVEL_TIMEOUT 5000
unsigned long levelTimeout;

/* EEPROM Addresses */
#define SOUND_LEVEL_ADDR        1
#define BASS_LEVEL_ADDR         2
#define REAR_LEVEL_ADDR         3
#define CENTER_LEVEL_ADDR       4
#define CURRENT_INPUT_ADDR      5
#define EFFECT_ON_AUX           6
#define EFFECT_ON_INPUT1        7
#define EFFECT_ON_INPUT2        8
#define EFFECT_ON_INPUT3        9
#define EFFECT_ON_INPUT4        10
#define EFFECT_ON_INPUT5        11
#define MUTE_ADDR               12

/*********************************** Tasks ************************************/
Scheduler taskManager;
Task tCheckIfStillOn(TASK_SECOND, TASK_FOREVER, &checkIfStillOn, &taskManager);
Task tWifiStatus(TASK_SECOND, TASK_FOREVER, &checkWifiStatusCallback, &taskManager);
Task tBlink(200, 3, &blinkStatusLedCallback, &taskManager, false, NULL, &blinkStatusLedDisabledCallback);
Task tSendStatesMQTT(TASK_MINUTE, TASK_FOREVER, &sendStatesMQTT, &taskManager);

#define DEBUG false
// conditional debugging
#if DEBUG 

  #define beginDebug()      do { Serial.begin (115200); } while (0)
  #define Trace(x)          Serial.print      (x)
  #define Trace2(x,y)       Serial.print      (x,y)
  #define Traceln(x)        Serial.println    (x)
  #define Traceln2(x,y)     Serial.println    (x,y)
  #define Tracef(...)       Serial.printf     (__VA_ARGS__)
  #define Tracefunc()       do { Trace (F("In function: ")); Serial.println(__PRETTY_FUNCTION__); } while (0)

#else
  #define beginDebug()      ((void) 0)
  #define Trace(x)          ((void) 0)
  #define Trace2(x,y)       ((void) 0)
  #define Traceln(x)        ((void) 0)
  #define Traceln2(x,y)     ((void) 0)
  #define Tracef(...)         ((void) 0)
  #define Tracefunc()       ((void) 0)
#endif // DEBUG

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");
  
  if(DEBUG)
    printChipStatus();

  pinMode(ON_LED, INPUT);
  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, HIGH);

  tWifiStatus.enable();
  taskManager.execute();

  setupWifiManager();
  setupOTA();
  setupEEPROM();
  printSettings();
  setupWebServer();
  setupMQTT();
  setupIR();

  tCheckIfStillOn.enable();
  tSendStatesMQTT.enable();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  digitalWrite(STATUS_LED, LOW);
}

void loop() {
  taskManager.execute();

  if(OTA_ON) {
    noInterrupts();
    ArduinoOTA.handle();
    interrupts();
  }

  server.handleClient();
  mqttclient.loop();
  handleIR();

  // We need to go back to On-mode after a while
  if(isOn && currentMode >= BassLevel) {
    if(currentMode != lastMode) {
      levelTimeout = millis() + LEVEL_TIMEOUT;
    } else if(millis() > levelTimeout) {
      currentMode = On;
      saveSettings();
      const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(7);
      DynamicJsonBuffer respBuffer(bufferSize);
      JsonObject& json = respBuffer.createObject();
      String resp = "";
      getSettings(json);
      json.printTo(resp);
      Traceln("[Loop] Ending level mode..");
      publishMQTT(StateTopic, resp);
    }
  }
  lastMode = currentMode;
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
  Traceln("[OTA] Initializing...");
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
    digitalWrite(STATUS_LED, HIGH);
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    if(progress % 5 == 0)
      digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
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
  // WiFiManager
  // Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;
  // reset saved settings
  // wifiManager.resetSettings();
  
  // set custom ip for portal
  // wifiManager.setAPConfig(IPAddress(10,0,1,1), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  // fetches ssid and pass from eeprom and tries to connect
  // if it does not connect it starts an access point with the specified name
  // and goes into a blocking loop awaiting configuration
  if (WIFI_MANAGER_STATION_NAME == "") {
    // use this for auto generated name ESP + ChipID
    wifiManager.autoConnect();
  } else {
    wifiManager.autoConnect(WIFI_MANAGER_STATION_NAME);
  }
}

void setupWebServer() {
  Traceln("[Webserver] Initializing...");
  server.on("/", HTTP_GET, [](){
    server.send(200, "text/plain", "It works!");
  });

  server.on("/", HTTP_POST, [](){
    // Print message
    Traceln("\nPOST \"\\\": ");
    server.send(200, "application/json", \
      handleJSONReq(server.arg("plain")));
  });

  // Start webserver
  server.begin();
  Traceln("[Webserver] Done.");
}

void setupIR() {
  Traceln("[IRSend] Begin");
  irsend.begin();
  irrecv.setUnknownThreshold(MIN_UNKNOWN_SIZE);
  irrecv.enableIRIn();
}

void setupEEPROM() {
  EEPROM.begin(512);
  mute = false; // Reset mute in memory
  EEPROM.write(MUTE_ADDR, mute);
  EEPROM.commit();
  loadSettings();
}

/** Be sure to setup WIFI before running this method! */
void setupMQTT() {
  mqttclient = PubSubClient(Broker, Port, mqttCallback, wificlient);
  connectMQTT();
}

/** Connects to the MQTT broker and subscribes to the topic */
bool connectMQTT() {
  while (!mqttclient.connected()) {
    Trace("[MQTT] Connecting to MQTT server... ");

    //if connected, subscribe to the topic(s) we want to be notified about
    if (mqttclient.connect(MQTTClientId, MQTTUsername, MQTTPassword, WillTopic,\
        WillQoS, WillRetain, willMessage)) {
      Traceln("MTQQ Connected!");
      if (mqttclient.subscribe(CommandTopic))
        Tracef("[MQTT] Sucessfully subscribed to %s\n", CommandTopic);
      publishMQTT(DebugTopic, FirstMessage);
      return true;
    }
  }
  Traceln("Failed to connect to MQTT Server");
  return false;
}

bool publishMQTT(const char* topic, const char* payload){
  String printString = "";
  bool returnBool = false;
  if(mqttclient.publish(topic, payload)) {
    returnBool = true;
    printString = String("[publishMQTT] '" + String(payload) + "' was sent sucessfully to: ");
  } else {
    returnBool = false;
    printString = String("[publishMQTT] ERROR sending: '" + String(payload) + "' to: ");
  }
  printString += topic;
  Traceln(printString);
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

void mqttCallback(char* topic, byte* payload, unsigned int length) {

  //convert topic to string to make it easier to work with
  String topicStr = topic;
  String payloadStr = payloadToString(payload, length);

  Traceln("[MQTT][callback] Callback update.");
  Traceln(String("[MQTT][callback] Topic: " + topicStr));

  if(topicStr.equals(CommandTopic))
    publishMQTT(StateTopic, handleJSONReq(payloadStr));
}

void checkWifiStatusCallback() {
  if(WiFi.getMode() != 1) {
    Serial.println("[checkWifiStatusCallback] Not connected to WiFi...");
    blinkStatusLed(TASK_FOREVER, 500);
    tWifiStatus.setCallback(&WiFiDisconnectedCallback);
  }
}

void WiFiDisconnectedCallback() {
  if(WiFi.getMode() == 1) {
    Serial.println("[WiFiDisconnectedCallback] Connected to WiFi!");
    digitalWrite(STATUS_LED, LOW);
    tBlink.disable();
    tWifiStatus.setCallback(&checkWifiStatusCallback);
  }
}

/** For handling requests, both the MQTT and REST requests are parsed here
 * Returns: response string (with settings formatted as json)
 */  
String handleJSONReq(String req) {
  StaticJsonBuffer<200> reqBuffer;
  JsonObject& reqjson = reqBuffer.parseObject(req);

  Trace("[handleJSON] Payload: ");
  reqjson.prettyPrintTo(Serial);
  Tracef("\n");

  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(7);
  DynamicJsonBuffer respBuffer(bufferSize);
  JsonObject& json = respBuffer.createObject();

  String response = "";
  String method = reqjson["method"];
  if(method == "turnOn") {
    Traceln("[handleJSON] Calling turnOn");
    turnOn();
  } else if(method == "turnOff") {
    Traceln("[handleJSON] Calling turnOff");
    turnOff();
  }
  // Getters 
  else if(method == "getSettings") {
    Traceln("[handleJSON] Calling getSettings");
    getSettings(json);
  } else if(method == "getMode") {
    Traceln("[handleJSON] Calling getMode");
    Traceln(modes[currentMode]);
    json["mode"] = modes[currentMode];
  } else if(method == "getSoundLevel") {
    Traceln("[handleJSON] Calling getSoundLevel");
    json["soundlevel"] = soundLevel;
  } else if(method == "getInput") {
    Traceln("[handleJSON] Calling getInput");
    json["input"] = inputs[currentInput];
  } else if(method == "getEffect") {
    Traceln("[handleJSON] Calling getEffect");
    json["effect"] = effects[currentEffect()];
  }
  // Setters
  else if(method == "setSettings") {
    // if(currentMode != On) {
    //    /* Turn on the speakers if they're not yet on 
    //   (THIS COULD CAUSE PROBLEMS IF YOU'RE STUPID AS ME AND FORGETS ABOUT THIS)*/
    //   turnOn();
    //   // If the speakers was Off or in Level mode we have to wait until
    //   // it's in for sure is in On mode
    //   delay(2000); // change dis later to more appropriate value
    // }

    bool somethingChanged = false;

    const char* input = reqjson["input"];
    if(input) {
      Traceln("[setSettings] Input setting detected");
      changeInput((Input)getStringIndex(input, inputs, ARRAY_SIZE(inputs)));
      somethingChanged = true;
    }

    const char* effect = reqjson["effect"];
    if(effect) {
      Traceln("[setSettings] Effect setting detected");
      changeEffect((Effect)getStringIndex(effect, effects, ARRAY_SIZE(effects)));
      somethingChanged = true;
    }

    const char* modeStr = reqjson["mode"];
    if(modeStr) {
      Traceln("[setSettings] Mode setting detected");
      changeMode((Mode)getStringIndex(modeStr, modes, ARRAY_SIZE(modes)));
      somethingChanged = true;
    }

    int soundlevel = reqjson["soundlevel"];
    if(soundlevel) {
      Traceln("[setSettings] Soundlevel setting detected");
      changeSoundLevel(soundlevel);
      somethingChanged = true;
    }
    if(somethingChanged) {
      getSettings(json);
    } else {
      json["message"] = "You didn't specify input, effect or soundlevel";
    }
  } 
  
  // reset
  else if(method = "reset") {
    blinkStatusLed(2, 300);
    soundLevel[0] = 10;
    soundLevel[1] = 25;
    soundLevel[2] = 25;
    soundLevel[3] = 25;
    currentInput = Input1;
    for(uint8_t i= 0; i < 5; i++) {
      currentEffectOnInput[i] = Surround;
    }
    saveSettings();
    json["message"] = "Settings resetted";
    getSettings(json);
  }

  // just else 
  else {
    String error = "Method: \"" + String(method) + "\" does not exist";
    Traceln(error);
    publishMQTT(DebugTopic, error);
  }

  json.printTo(response);
  Traceln("[handleJSON] Response:\n" + response);
  return response;
}

int lastState = 0;
void handleIR() {
  if (irrecv.decode(&results)) {
    const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(7);
    DynamicJsonBuffer respBuffer(bufferSize);
    JsonObject& json = respBuffer.createObject();
    String resp = "";

    int state = results.value;
    bool runStateMachine = true;

    if(state == 0xFFFFFFFF &&
      (lastState == 0x11E728E || lastState == 0xABB1A8D2)) {
      Traceln("[handleIR] Repeat.");
      state = lastState;
    }

    switch (state) {
      case 0xFFFFFFFF:
        Traceln("[handleIR] Not repeatable.");
        break;
      case 0x63C98B53:
        Traceln("[handleIR] Power button pressed.");
        currentMode = currentMode == On ? Off : On;
        break;
      case 0xEFA4E63F:
        Traceln("[handleIR] Input button pressed.");
        setNextInput();
        break;
      case 0x92CA878C:
        Traceln("[handleIR] Mute button pressed.");
        mute = !mute;
        break;
      case 0x58B863E3:
        Traceln("[handleIR] Level button pressed.");
        nextLevelOnCurrentEffect();
        break;
      case 0x11E728E:
        Traceln("[handleIR] Minus button pressed.");
        soundLevel[currentLevel()] -= soundLevel[currentLevel()] < 2 ? 0 : 1;
        break;
      case 0xABB1A8D2:
        Traceln("[handleIR] Plus button pressed.");
        soundLevel[currentLevel()] += soundLevel[currentLevel()] >= 100 ? 0 : 2;
        break;
      case 0x48C7229F:
        Traceln("[handleIR] Effect button pressed.");
        setNextEffect();
        break;
      default:
        Tracef("No such ir code case: %X\n", results.value);
        irrecv.resume();
        return;
    }
    // Things that has to be done in all standard states
    if(state != 0xFFFFFFFF) {
      runStateMachine = false;
      lastState = state;
    }
    saveSettings();
    getSettings(json);
    json.printTo(resp);
    publishMQTT(StateTopic, resp);
    irrecv.resume();
  }
}

/** Returns a json formatted string with chip status */
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

/** Prints chip status to serial */
void printChipStatus() {
  uint32_t realSize = ESP.getFlashChipRealSize();
  uint32_t ideSize = ESP.getFlashChipSize();
  FlashMode_t ideMode = ESP.getFlashChipMode();

  Serial.printf("Flash real   id: %08X\n", ESP.getFlashChipId());
  Serial.printf("Flash real size: %u bytes\n\n", realSize);
  Serial.printf("Flash ide  size: %u bytes\n", ideSize);
  Serial.printf("Flash ide speed: %u Hz\n", ESP.getFlashChipSpeed());
  Serial.printf("Flash ide  mode: %s\n\n", (ideMode == FM_QIO ? "QIO" : ideMode == FM_QOUT ? "QOUT" : ideMode == FM_DIO ? "DIO" : ideMode == FM_DOUT ? "DOUT" : "UNKNOWN"));
  Serial.printf("Sketch size:\t\t%u bytes\n", ESP.getSketchSize());
  Serial.printf("Free sketch space:\t%u bytes\n", ESP.getFreeSketchSpace());

  if (ideSize != realSize) {
    Serial.println("Flash Chip configuration wrong!\n");
  } else {
    Serial.println("Flash Chip configuration ok.\n");
  }
}

void getSettings(JsonObject &json) {
  JsonObject& settings = json.createNestedObject("settings");
  settings["mode"] = modes[currentMode];
  if(mute)
    settings["soundlevel"] = "mute";
  else {
    settings["soundlevel"] = soundLevel[0];
    settings["basslevel"] = soundLevel[1];
    settings["rearlevel"] = soundLevel[2];
    settings["centerlevel"] = soundLevel[3];
  }
  settings["input"] = inputs[currentInput];
  settings["effect"] = effects[currentEffect()];
}

void loadSettings() {
  for(int8_t i = 0; i < 4; i++) {
    soundLevel[i] = EEPROM.read(SOUND_LEVEL_ADDR + i);
    if(soundLevel[i] > 128 || soundLevel[i] < 0) {
      soundLevel[i] = 0;
    }
  }

  currentInput = (Input)EEPROM.read(CURRENT_INPUT_ADDR);
  currentInput = currentInput < 6 ? currentInput : AUX;

  for(uint8_t i= 0; i < 6; i++) {
    currentEffectOnInput[i] = (Effect)EEPROM.read(EFFECT_ON_AUX + i);
    currentEffectOnInput[i] = currentEffectOnInput[i] < 3 ? currentEffectOnInput[i] : Surround;
  }

  mute = (bool)EEPROM.read(MUTE_ADDR);
}

void printSettings() {
  Serial.printf("\nInput: %s\n", inputs[currentInput]);
  Serial.printf("Soundlevel: %d\t", soundLevel[0]);
  Serial.printf("Bass: %d\t", soundLevel[1]);
  Serial.printf("Rear: %d\n", soundLevel[2]);
  Serial.printf("Center: %d\n", soundLevel[3]);
  Serial.printf("Effect: %s\n\n", effects[currentEffect()]);
}

void saveSettings() {
  for(uint8_t i = 0; i < 4; i++) {
    EEPROM.write(SOUND_LEVEL_ADDR + i, soundLevel[i]);
  }
  EEPROM.write(CURRENT_INPUT_ADDR, currentInput);
  for(uint8_t i = 0; i < 6; i++) {
    EEPROM.write(EFFECT_ON_AUX + i, currentEffectOnInput[i]);
  }
  EEPROM.write(MUTE_ADDR, mute);
  if(EEPROM.commit())
    Traceln("Successfully saved to EEPROM");
  else
    Traceln("Failed to save to EEPROM");
}

void turnOn() {
  if(currentMode == Off) {
    sendIR(POWER_IR);
    currentMode = On;
    saveSettings();
  }
  checkIfStillOn();
}

void turnOff() {
  if(currentMode != Off) {
    sendIR(POWER_IR);
    currentMode = Off;
    saveSettings();
  }
  checkIfStillOn();
}

void togglePower() {
  sendIR(POWER_IR);
  currentMode = currentMode == On ? Off : On;
  saveSettings();
}

void toggleInput() {
  sendIR(INPUT_IR);
  setNextInput();
  saveSettings();
}

void toggleMute() {
  sendIR(MUTE_IR);
  mute = !mute;
  saveSettings();
}

/** Sends ir code and saves settings to change to wanted input */
void changeInput(Input input) {
  Tracef("[changeInput] Changing input to: %s", inputs[input]);
  switch(input) {
    case AUX:
      sendIR(AUX_IR);
      break;
    case Input1:
      sendIR(INPUT1_IR);
      break;
    case Input2:
      sendIR(INPUT2_IR);
      break;
    case Input3:
      sendIR(INPUT3_IR);
      break;
    case Input4:
      sendIR(INPUT4_IR);
      break;
    case Input5:
      sendIR(INPUT5_IR);
      break;
    default:
      Traceln("\tNo such input!");
  }
  Tracef("\n");
  currentInput = input;
  saveSettings();
}

/** Sends ir code and saves settings to change to wanted effect */
void changeEffect(Effect effect) {
  Tracef("[changeEffect] Changing effect from: %s to: %s\n", effects[currentEffect()], effects[effect]);
  int8_t diff = effect - currentEffectOnInput[currentInput];
  int8_t ir_send_times = (diff >= 0) ? diff : (abs(diff) + 1) % 3;
  Tracef("[changeEffect] Diff: %d\t\tBlasting ir %d times\n", diff, ir_send_times);

  for(uint8_t i = 0; i < ir_send_times; i++) {
    sendIR(EFFECT_IR);
    delay(MS_BETWEEN_SENDING_IR);
  }

  currentEffectOnInput[currentInput] = effect;
  saveSettings();
}

/** Sends ir code and saves settings to change to wanted sound level */
void changeSoundLevel(int8_t level) {
  if(level < 0)
    level = 0;
  int8_t diff = level - soundLevel[currentLevel()];
  Tracef("[changeSoundLevel] Setting sound level %d -> %d\tDiff: %d\n", soundLevel[currentLevel()], level, diff);
  if(diff > 1) {
    sendIR(PLUS_IR, diff);
  } else if(diff < 0) {
    sendIR(MINUS_IR, -diff);
  }
  soundLevel[currentLevel()] = level;
  saveSettings();
}

/** Sends ir code and saves settings to change to wanted mode (Level) */
void changeMode(Mode mode) {
  Tracef("[changeMode] Changing mode from: %s to: %s\n", modes[currentMode], modes[mode]);
  int8_t diff = (mode - 1) - (currentMode - 1);
  int8_t ir_send_times = (diff >= 0) ? diff : (abs(diff) + 1) % 4;
  Tracef("[changeMode] Diff: %d\t\tBlasting ir %d times\n", diff, ir_send_times);

  for(uint8_t i = 0; i < ir_send_times; i++) {
    sendIR(LEVEL_IR);
    delay(MS_BETWEEN_SENDING_IR);
  }

  currentMode = mode;
  saveSettings();
}

/** Returns the soundlevel that the receiver is currently on */
uint8_t currentLevel() {
  return currentMode - 1;
}

/** 
 * Returns the current effect for the current input
 * (Each input has it's on effect independent of the other inputs)
 */
Effect currentEffect() {
  return currentEffectOnInput[currentInput];
}

void setCurrentEffect(Effect effect) {
  currentEffectOnInput[currentInput] = effect;
}

/** Checks if speaker system is still on */
void checkIfStillOn() {
  bool lastBool = isOn;
  isOn = digitalRead(ON_LED);
  if(isOn) {
    currentMode = currentMode == Off ? On : currentMode;
  } else {
    currentMode = Off;
  }

  if(lastBool != isOn) {
    sendStatesMQTT();
  }
}

/** Returns the strings index in const char[] array*/ 
uint8_t getStringIndex(String s, const char* array[], uint8_t len) {
  Tracef("[getStringIndex] Length of array: %d\n", len);
  for(uint8_t i = 0; i < len; i++) {
    if(s == array[i]) return i;
  }
}

/** Sets the current input to the next input, but does not save or send 
 *  anything */
void setNextInput() {
  currentInput = currentInput >= 5 ? (Input)0 : (Input)(currentInput + 1);
}

/** Sets the current input to the next input, but does not save or send anything
 *  anything */
void setNextEffect() {
  Effect effect = currentEffect() >= 2 ? (Effect)0 : (Effect)(currentEffect() + 1);
  setCurrentEffect(effect);
}

void nextLevelOnCurrentEffect() {
  int limit = 4;
  switch(currentEffect()) {
    case Surround:
      limit = 4;
      break;
    case Music:
      limit = 3;
      break;
    case Stereo:
      limit = 2;
      break;
  }
  currentMode = currentMode >= limit ? (Mode)1 : (Mode)(currentMode + 1);
}

void sendIR(uint64_t data, uint16_t repeat) {
  irrecv.disableIRIn();
  irsend.sendNEC(data, 32, repeat);
  irrecv.enableIRIn();
  irrecv.resume();
}

void sendIR(uint64_t data) {
  irrecv.disableIRIn();
  irsend.sendNEC(data, 32);
  irrecv.enableIRIn();
  irrecv.resume();
}

void blinkStatusLed(int8_t times, unsigned long interval) {
  blinkStatusLed(times, interval, NULL, NULL);
}

void blinkStatusLed(int8_t times, unsigned long interval, TaskOnEnable onEnable, TaskOnDisable onDisable) {
  Serial.println("[blinkStatusLed] Called");
  tBlink.setIterations(times);
  tBlink.setInterval(interval);
  tBlink.setOnEnable(onEnable);
  tBlink.setOnDisable(onDisable);
  taskManager.addTask(tBlink);
  tBlink.enable();
}

void blinkStatusLedCallback() {
  digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
}

void blinkStatusLedDisabledCallback() {
  digitalWrite(STATUS_LED, LOW);
  taskManager.deleteTask(tBlink);
}

void sendStatesMQTT() {
  const size_t bufferSize = JSON_OBJECT_SIZE(1) + JSON_OBJECT_SIZE(7);
  DynamicJsonBuffer respBuffer(bufferSize);
  JsonObject& json = respBuffer.createObject();
  getSettings(json);
  String payload = "";
  json.printTo(payload);
  publishMQTT(StateTopic, payload);
}