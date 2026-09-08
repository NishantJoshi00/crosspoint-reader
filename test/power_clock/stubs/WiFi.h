#pragma once
constexpr int WL_CONNECTED = 3;
struct TestWifi {
  int connectionStatus = 0;
  int status() const { return connectionStatus; }
};
extern TestWifi WiFi;
