#ifndef DEBUG_H_
#define DEBUG_H_

#include <Arduino.h>

// Define debug and log port
#define DEBUG_PORT

// Define wanted debug levels
#define LOG_ENABLED
#define ERRORS_ENABLED
// #define DEBUG_ENABLED

#ifdef DEBUG_PORT
  #define DebugInit(baud) Serial.begin(baud)
  #define LogInit(baud)   Serial.begin(baud)
#else
  #define DebugInit(baud)
  #define LogInit(baud)
#endif

/******************** Debug levels ********************/
#ifdef LOG_ENABLED
  #define Log(...)        Serial.printf( __VA_ARGS__ )
  #define LogFunc(...) do { Serial.print("["); Serial.print(__PRETTY_FUNCTION__); Serial.print("] "); Serial.printf(__VA_ARGS__); } while (0)
  #define Logln(...)      Serial.println( __VA_ARGS__ )
#else
  #define Log(...)
  #define LogFunc(...)
  #define Logln(...)
#endif

#ifdef ERRORS_ENABLED
  #define Err(...)      Serial.printf( __VA_ARGS__ )
  #define ErrFunc(...) do { Serial.print("["); Serial.print(__PRETTY_FUNCTION__); Serial.print("] "); Serial.printf(__VA_ARGS__); } while (0)
  #define Errln(...)     Serial.println( __VA_ARGS__ )
#else
  #define Err(...)
  #define ErrFunc(...)
  #define Errln(...)
#endif

#ifdef DEBUG_ENABLED
  #define Debugf(...)      Serial.printf( __VA_ARGS__ )
#else
  #define Debugf(...)
#endif

void chipInformation() {
  Serial.println();
  Serial.print( F("Heap: ") );  Serial.println(system_get_free_heap_size());
  Serial.print( F("Boot Vers: ") );  Serial.println(system_get_boot_version());
  Serial.print( F("CPU: ") );  Serial.println(system_get_cpu_freq());
  Serial.print( F("SDK: ") );  Serial.println(system_get_sdk_version());
  Serial.print( F("Chip ID: ") );  Serial.println(system_get_chip_id());
  Serial.print( F("Flash ID: ") );  Serial.println(spi_flash_get_id());
  Serial.print( F("Flash Size: ") );  Serial.println(ESP.getFlashChipRealSize());
  Serial.print( F("Vcc: ") );  Serial.println(ESP.getVcc());
  Serial.println();
}

#endif // DEBUG_H_