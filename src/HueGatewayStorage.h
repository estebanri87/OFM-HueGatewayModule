#pragma once

#include <Arduino.h>

namespace HueGatewayStorage
{
String loadAppKey();
String loadClientKey();
String loadBridgeIp();

void saveAppKey(const String& key);
void saveClientKey(const String& key);
void saveBridgeIp(const String& ip);

void clearAuthKeys();
}
