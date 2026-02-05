#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>

/**
 * @brief Hue Bridge Authentication
 * 
 * Verwaltet die Authentifizierung mit der Hue Bridge (Button-Press Flow)
 */
class HueBridgeAuth
{
public:
    HueBridgeAuth();
    
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
    
private:
    String _appKey;
    
    bool requestAppKey(const char* ip);
    void saveAppKey(const String& key);
    String loadAppKey();
};

