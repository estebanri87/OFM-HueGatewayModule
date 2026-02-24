#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/**
 * @brief Hue Bridge Discovery via mDNS
 * 
 * Automatically discovers Philips Hue Bridges in the local network.
 */
class HueGatewayDiscovery
{
public:
    HueGatewayDiscovery();
    
    /**
    * @brief Searches for a Hue Bridge in the network.
    * @param ipAddress Receives the discovered bridge IP address
    * @return true if a bridge was found
     */
    bool findBridge(String& ipAddress);
    
    /**
    * @brief Sets a manual bridge IP address.
    * @param ip Bridge IP address
    * @return true if the IP is valid
     */
    bool setManualIP(const char* ip, bool requireReachable = true);
    
private:
    bool discoverMDNS(String& ip);
    bool discoverNupnp(String& ip);
    bool isBridgeReachable(const String& ip);
    void saveIP(const String& ip);
    String loadIP();
};

