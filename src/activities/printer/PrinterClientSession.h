#pragma once

#include <Arduino.h>

#include <utility>

#include "network/ipp/HttpIppConnection.h"
#include "util/TaskWatchdog.h"

// A retained HTTP connection with its own read-ahead buffer. Idle connections
// never enter a blocking read; ready connections process one request per turn.
// Client and Input are templates so the firmware transport is host-testable.
template <typename Client, typename Input>
class PrinterClientSession {
 public:
  class Transport final : public IppTransport {
    Client& client;
    Input& input;
    uint32_t startedAt = 0;
    uint32_t lastPoll = 0;
    bool stopped = false;

   public:
    bool backPressed = false;
    bool clearPressed = false;
    bool userActivity = false;
    bool printStarted = false;
    bool timedOut = false;
    bool deadlineExceeded = false;

    Transport(Client& client, Input& input) : client(client), input(input) {}

    void beginRequest() {
      startedAt = millis();
      lastPoll = startedAt;
      stopped = backPressed = clearPressed = userActivity = printStarted = timedOut = deadlineExceeded = false;
    }

    bool poll() {
      if (stopped) return false;
      resetTaskWatchdogIfSubscribed();
      const uint32_t now = millis();
      // A trickling sender must not defeat the battery timeout indefinitely.
      if (now - startedAt >= (printStarted ? 120000u : 10000u)) {
        deadlineExceeded = timedOut = stopped = true;
        return false;
      }
      if (now - lastPoll >= 10) {
        lastPoll = now;
        delay(1);
        input.update();
        userActivity = userActivity || input.wasAnyPressed();
        backPressed = backPressed || input.wasPressed(Input::Button::Back);
        clearPressed = clearPressed || input.wasPressed(Input::Button::Left);
        stopped = backPressed || clearPressed;
      }
      return !stopped;
    }

    int read(uint8_t* buf, size_t maxLen) override {
      const uint32_t waitStarted = millis();
      while (poll()) {
        // A peer may close after sending its final bytes. Consume those bytes
        // before interpreting the disconnected state as EOF.
        const int available = client.available();
        if (available > 0) return client.read(buf, maxLen);
        if (!client.connected()) return 0;
        if (millis() - waitStarted >= (printStarted ? 15000u : 2500u)) {
          timedOut = stopped = true;
          return -1;
        }
        delay(2);
      }
      return -1;
    }

    bool write(const uint8_t* buf, size_t len) override {
      while (len > 0 && poll()) {
        const size_t n = client.write(buf, len);
        if (n == 0) return false;
        buf += n;
        len -= n;
      }
      return len == 0;
    }
  };

  Client client;
  Transport transport;
  IppByteReader reader;
  uint32_t lastActive = millis();

  PrinterClientSession(Client client, Input& input)
      : client(std::move(client)), transport(this->client, input), reader(transport) {}

  bool ready() { return reader.hasBufferedData() || client.available() > 0; }
  bool expired(uint32_t now) { return !client.connected() || now - lastActive >= 15000; }

  bool serveOne(HttpIppConnection& connection, uint32_t (*upTime)()) {
    transport.beginRequest();
    const bool keepAlive = connection.serveOne(transport, reader, upTime);
    lastActive = millis();
    return keepAlive;
  }
};
