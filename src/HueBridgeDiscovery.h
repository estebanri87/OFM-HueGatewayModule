#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>

/**
 * @brief Hue Bridge Discovery via mDNS
 * 
 * Findet automatisch Philips Hue Bridges im lokalen Netzwerk
 */
class HueBridgeDiscovery
{
public:
    HueBridgeDiscovery();
    
    /**
     * @brief Sucht nach Hue Bridge im Netzwerk
     * @param ipAddress Gefundene IP-Adresse wird hier gespeichert
     * @return true wenn Bridge gefunden
     */
    bool findBridge(String& ipAddress);
    
    /**
     * @brief Setzt manuelle IP-Adresse
     * @param ip IP-Adresse der Bridge
     * @return true wenn gültige IP
     */
    bool setManualIP(const char* ip);
    
private:
    bool discoverMDNS(String& ip);
    void saveIP(const String& ip);
    String loadIP();
};

