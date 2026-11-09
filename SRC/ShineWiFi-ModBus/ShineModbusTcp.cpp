#include "ShineModbusTcp.h"

#if MODBUS_TCP_SUPPORTED == 1

#include <TLog.h>

// Modbus exception codes
#define MB_EX_ILLEGAL_FUNCTION 0x01
#define MB_EX_ILLEGAL_DATA_ADDRESS 0x02
#define MB_EX_ILLEGAL_DATA_VALUE 0x03
#define MB_EX_SLAVE_DEVICE_FAILURE 0x04
#define MB_EX_SLAVE_DEVICE_BUSY 0x06
#define MB_EX_GATEWAY_PATH_UNAVAILABLE 0x0A
#define MB_EX_GATEWAY_TARGET_NO_RESPONSE 0x0B
// ModbusMaster result codes >= 0xE0 are communication errors (timeout, CRC)
#define MB_COMMUNICATION_ERROR 0xE0

ShineModbusTcp::ShineModbusTcp(Growatt& inverter)
    : _inverter(inverter), _server(MODBUS_TCP_PORT), _requests(0), _errors(0) {
  for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
    _conn[i].len = 0;
    _conn[i].lastActivity = 0;
  }
}

void ShineModbusTcp::begin() {
#ifdef MODBUS_TCP_ALLOWED_IP
  _allowedIp.fromString(MODBUS_TCP_ALLOWED_IP);
#endif
  _server.begin();
  _server.setNoDelay(true);
  Log.print(F("Modbus TCP server listening on port "));
  Log.println(MODBUS_TCP_PORT);
}

void ShineModbusTcp::closeConnection(Connection& c) {
  c.client.stop();
  c.len = 0;
}

void ShineModbusTcp::acceptClients() {
#ifdef ESP8266
  WiFiClient newClient = _server.accept();
#else
  WiFiClient newClient = _server.available();
#endif
  if (!newClient) {
    return;
  }

#ifdef MODBUS_TCP_ALLOWED_IP
  if (newClient.remoteIP() != _allowedIp) {
    Log.print(F("Modbus TCP: rejected client "));
    Log.println(newClient.remoteIP().toString());
    newClient.stop();
    return;
  }
#endif

  // use a free slot, otherwise replace the connection idle for the longest
  // time (clients that reconnect without closing must not lock us out)
  Connection* slot = NULL;
  for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
    if (!_conn[i].client.connected()) {
      slot = &_conn[i];
      break;
    }
    if (slot == NULL || _conn[i].lastActivity < slot->lastActivity) {
      slot = &_conn[i];
    }
  }
  if (slot->client.connected()) {
    Log.println(F("Modbus TCP: all slots used, dropping oldest client"));
  }
  closeConnection(*slot);

  slot->client = newClient;
  slot->client.setNoDelay(true);
  slot->len = 0;
  slot->lastActivity = millis();
  _lastClient = newClient.remoteIP();
  Log.print(F("Modbus TCP: client connected "));
  Log.println(_lastClient.toString());
}

void ShineModbusTcp::loop() {
  acceptClients();
  for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
    serviceClient(_conn[i]);
  }
}

void ShineModbusTcp::serviceClient(Connection& c) {
  if (!c.client) {
    return;
  }
  if (!c.client.connected() && !c.client.available()) {
    closeConnection(c);
    return;
  }

  unsigned long now = millis();
  while (c.client.available() && c.len < FRAME_MAX) {
    int n = c.client.read(c.buf + c.len, FRAME_MAX - c.len);
    if (n <= 0) {
      break;
    }
    c.len += n;
    c.lastActivity = now;
  }

  if (c.len == 0) {
    if (now - c.lastActivity > MODBUS_TCP_IDLE_TIMEOUT) {
      Log.println(F("Modbus TCP: closing idle connection"));
      closeConnection(c);
    }
    return;
  }

  // need at least the MBAP header + function code
  if (c.len < 8) {
    return;
  }

  uint16_t protocolId = (c.buf[2] << 8) | c.buf[3];
  uint16_t length = (c.buf[4] << 8) | c.buf[5];  // unit id + pdu
  if (protocolId != 0 || length < 2 || length > FRAME_MAX - 6) {
    Log.println(F("Modbus TCP: invalid frame, closing connection"));
    _errors++;
    closeConnection(c);
    return;
  }
  uint16_t frameLen = 6 + length;
  if (c.len < frameLen) {
    return;  // wait for the rest of the frame
  }

  // process exactly one request per loop() call to keep the main loop
  // responsive. Pipelined requests stay in the buffer.
  uint8_t resp[FRAME_MAX];
  memcpy(resp, c.buf, 7);  // transaction id, protocol id, unit id
  uint16_t pduLen = processPdu(c.buf + 7, length - 1, resp + 7);
  resp[4] = (pduLen + 1) >> 8;
  resp[5] = (pduLen + 1) & 0xFF;
  _requests++;
  if (resp[7] & 0x80) {
    _errors++;
  }
  c.client.write(resp, 7 + pduLen);

  memmove(c.buf, c.buf + frameLen, c.len - frameLen);
  c.len -= frameLen;
  c.lastActivity = millis();
}

uint16_t ShineModbusTcp::exceptionPdu(uint8_t function, uint8_t code,
                                      uint8_t* resp) {
  resp[0] = function | 0x80;
  resp[1] = code;
  return 2;
}

uint8_t ShineModbusTcp::mapResult(uint8_t res) {
  // slave exceptions are passed through, communication errors are reported
  // as "gateway target device failed to respond"
  if (res >= MB_EX_ILLEGAL_FUNCTION && res <= MB_EX_SLAVE_DEVICE_FAILURE) {
    return res;
  }
  return MB_EX_GATEWAY_TARGET_NO_RESPONSE;
}

uint16_t ShineModbusTcp::processPdu(const uint8_t* req, uint16_t reqLen,
                                    uint8_t* resp) {
  uint8_t function = req[0];
  const uint8_t maxRegs = Growatt::MODBUS_RAW_MAX_REGS;

  if (function != 0x03 && function != 0x04 && function != 0x06 &&
      function != 0x10) {
    return exceptionPdu(function, MB_EX_ILLEGAL_FUNCTION, resp);
  }
#if MODBUS_TCP_ALLOW_WRITE != 1
  if (function == 0x06 || function == 0x10) {
    return exceptionPdu(function, MB_EX_ILLEGAL_FUNCTION, resp);
  }
#endif
  if (reqLen < 5) {
    return exceptionPdu(function, MB_EX_ILLEGAL_DATA_VALUE, resp);
  }
  if (_inverter.GetWiFiStickType() == Undef_stick) {
    return exceptionPdu(function, MB_EX_GATEWAY_PATH_UNAVAILABLE, resp);
  }

  uint16_t adr = (req[1] << 8) | req[2];
  uint16_t regs[maxRegs];
  uint8_t res;

  switch (function) {
    case 0x03:
    case 0x04: {
      uint16_t qty = (req[3] << 8) | req[4];
      if (qty == 0 || qty > MAX_READ_REGS) {
        return exceptionPdu(function, MB_EX_ILLEGAL_DATA_VALUE, resp);
      }
      if ((uint32_t)adr + qty > 0x10000) {
        return exceptionPdu(function, MB_EX_ILLEGAL_DATA_ADDRESS, resp);
      }
      resp[0] = function;
      resp[1] = qty * 2;
      // the serial modbus buffer holds 64 registers, split larger requests
      uint16_t done = 0;
      while (done < qty) {
        uint16_t chunk = qty - done;
        if (chunk > maxRegs) chunk = maxRegs;
        res = _inverter.ModbusRawRequest(function, adr + done, chunk, regs);
        if (res == MB_EX_SLAVE_DEVICE_BUSY || res >= MB_COMMUNICATION_ERROR) {
          // the inverter sometimes answers busy or not at all, if requests
          // follow each other quickly. Retry once after a short pause.
          delay(10);
          res = _inverter.ModbusRawRequest(function, adr + done, chunk, regs);
        }
        if (res != 0) {
          return exceptionPdu(function, mapResult(res), resp);
        }
        for (uint16_t i = 0; i < chunk; i++) {
          resp[2 + (done + i) * 2] = regs[i] >> 8;
          resp[3 + (done + i) * 2] = regs[i] & 0xFF;
        }
        done += chunk;
        yield();
      }
      return 2 + qty * 2;
    }

    case 0x06: {
      regs[0] = (req[3] << 8) | req[4];
      res = _inverter.ModbusRawRequest(function, adr, 1, regs);
      if (res != 0) {
        return exceptionPdu(function, mapResult(res), resp);
      }
      Log.print(F("Modbus TCP: wrote holding register "));
      Log.println(adr);
      memcpy(resp, req, 5);  // echo request
      return 5;
    }

    case 0x10: {
      uint16_t qty = (req[3] << 8) | req[4];
      if (reqLen < 6) {
        return exceptionPdu(function, MB_EX_ILLEGAL_DATA_VALUE, resp);
      }
      uint8_t byteCount = req[5];
      if (qty == 0 || qty > maxRegs || byteCount != qty * 2 ||
          reqLen < 6 + byteCount) {
        return exceptionPdu(function, MB_EX_ILLEGAL_DATA_VALUE, resp);
      }
      if ((uint32_t)adr + qty > 0x10000) {
        return exceptionPdu(function, MB_EX_ILLEGAL_DATA_ADDRESS, resp);
      }
      for (uint16_t i = 0; i < qty; i++) {
        regs[i] = (req[6 + i * 2] << 8) | req[7 + i * 2];
      }
      res = _inverter.ModbusRawRequest(function, adr, qty, regs);
      if (res != 0) {
        return exceptionPdu(function, mapResult(res), resp);
      }
      Log.print(F("Modbus TCP: wrote holding registers from "));
      Log.println(adr);
      memcpy(resp, req, 5);  // function, address, quantity
      return 5;
    }
  }
  return exceptionPdu(function, MB_EX_ILLEGAL_FUNCTION, resp);
}

void ShineModbusTcp::CreateStatusJson(JsonDocument& doc) {
  uint8_t clients = 0;
  for (uint8_t i = 0; i < MODBUS_TCP_MAX_CLIENTS; i++) {
    if (_conn[i].client.connected()) clients++;
  }
  doc["port"] = MODBUS_TCP_PORT;
  doc["write_enabled"] = (MODBUS_TCP_ALLOW_WRITE == 1);
  doc["max_clients"] = MODBUS_TCP_MAX_CLIENTS;
  doc["clients"] = clients;
  doc["requests"] = _requests;
  doc["errors"] = _errors;
  doc["last_client"] = _lastClient.toString();
#ifdef MODBUS_TCP_ALLOWED_IP
  doc["allowed_ip"] = MODBUS_TCP_ALLOWED_IP;
#endif
}

#endif
