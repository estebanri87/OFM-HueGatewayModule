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
     * @brief Authentifizierung durchführen (non-blocking)
     * @param bridgeIP IP-Adresse der Bridge
     * @return true wenn App-Key vorhanden oder erfolgreich angefordert
     */
    bool authenticate(const char* bridgeIP);
    
    /**
     * @brief Authentifizierung mit Wartefenster (blocking, fuer Konsole)
     * @param bridgeIP IP-Adresse der Bridge
     * @param timeoutMs Wartezeit in Millisekunden
     * @return true wenn erfolgreich
     */
    bool authenticateBlocking(const char* bridgeIP, uint32_t timeoutMs = 30000);
    
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
    String getClientKey();

    // Loads stored app key (and client key if available) from preferences into memory.
    bool loadStoredAppKey();
    bool loadStoredClientKey();

    // Attempts a single app-key request (non-blocking helper).
    bool requestAppKeyOnce(const char* ip);

    // Clears the stored app key (forces re-auth on next attempt).
    void clearAppKey();
    
private:
    String _appKey;
    String _clientKey;

    void saveAppKey(const String& key);
    void saveClientKey(const String& key);
    String loadAppKey();
    String loadClientKey();
};

