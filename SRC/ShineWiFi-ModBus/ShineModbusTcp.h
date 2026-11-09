#pragma once

#include "Config.h"

#if MODBUS_TCP_SUPPORTED == 1

#include <Arduino.h>
#include <ArduinoJson.h>
#ifdef ESP8266
#include <ESP8266WiFi.h>
#else
#include <WiFi.h>
#endif
#include "ShineWifi.h"
#include "Growatt.h"

#ifndef MODBUS_TCP_PORT
#define MODBUS_TCP_PORT 502
#endif

#ifndef MODBUS_TCP_ALLOW_WRITE
#define MODBUS_TCP_ALLOW_WRITE 0
#endif

#ifndef MODBUS_TCP_MAX_CLIENTS
#define MODBUS_TCP_MAX_CLIENTS 2
#endif

// close connections without traffic after this time [ms]
#ifndef MODBUS_TCP_IDLE_TIMEOUT
#define MODBUS_TCP_IDLE_TIMEOUT 60000
#endif

// Modbus TCP -> Modbus RTU gateway.
//
// Requests received on MODBUS_TCP_PORT are forwarded 1:1 to the inverter via
// the RS485/USB connection of the stick and the answer is sent back to the TCP
// client. Supported function codes: 0x03, 0x04 (read holding/input registers,
// up to 125 registers) and, if MODBUS_TCP_ALLOW_WRITE is 1, 0x06 and 0x10
// (write single/multiple holding registers, up to 64 registers).
//
// loop() must be called from the main loop. Requests are processed
// synchronously there, so they never collide with the regular polling of the
// inverter, MQTT commands or the web interface.
class ShineModbusTcp {
 public:
  ShineModbusTcp(Growatt& inverter);
  void begin();
  void loop();
  void CreateStatusJson(JsonDocument& doc);

 private:
  // MBAP header (7 bytes) + max PDU (253 bytes)
  static const uint16_t FRAME_MAX = 260;
  static const uint16_t MAX_READ_REGS = 125;

  struct Connection {
    WiFiClient client;
    uint8_t buf[FRAME_MAX];
    uint16_t len;
    unsigned long lastActivity;
  };

  Growatt& _inverter;
  WiFiServer _server;
  Connection _conn[MODBUS_TCP_MAX_CLIENTS];
  uint32_t _requests;
  uint32_t _errors;
  IPAddress _lastClient;
#ifdef MODBUS_TCP_ALLOWED_IP
  IPAddress _allowedIp;
#endif

  void acceptClients();
  void serviceClient(Connection& c);
  void closeConnection(Connection& c);
  uint16_t processPdu(const uint8_t* req, uint16_t reqLen, uint8_t* resp);
  uint16_t exceptionPdu(uint8_t function, uint8_t code, uint8_t* resp);
  static uint8_t mapResult(uint8_t res);
};

#endif
