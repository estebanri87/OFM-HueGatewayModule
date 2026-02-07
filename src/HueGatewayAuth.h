#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/**
 * @brief Hue Bridge Authentication
 * 
 * Verwaltet die Authentifizierung mit der Hue Bridge (Button-Press Flow)
 */
class HueGatewayAuth
{
public:
    HueGatewayAuth();
    
    /**
     * @brief Authentifizierung durchführen
     * @param bridgeIP IP-Adresse der Bridge
     * @return true wenn erfolgreich
     */
    bool authenticate(const char* bridgeIP);
    
    /**
     * @brief Prüft ob gültiger App-Key vorhanden
     * @return true wenn App-Key vorhanden
     */
    bool hasValidAppKey();
    
    /**
     * @brief Gibt gespeicherten App-Key zurück
     * @return App-Key oder leerer String
     */
    String getAppKey();

    // Loads stored app key from preferences into memory.
    bool loadStoredAppKey();

    // Attempts a single app-key request (non-blocking helper).
    bool requestAppKeyOnce(const char* ip);

    // Clears the stored app key (forces re-auth on next attempt).
    void clearAppKey();
    
private:
    String _appKey;

    void saveAppKey(const String& key);
    String loadAppKey();
};

