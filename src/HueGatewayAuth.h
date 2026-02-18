#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

/**
 * @brief Hue Bridge Authentication
 * 
 * Manages Hue Bridge authentication using the button-press flow.
 */
class HueGatewayAuth
{
public:
    HueGatewayAuth();
    
    /**
    * @brief Performs authentication (non-blocking).
    * @param bridgeIP Bridge IP address
    * @return true if an app key is already available or was requested successfully
     */
    bool authenticate(const char* bridgeIP);
    
    /**
    * @brief Performs authentication with timeout (blocking, for console usage).
    * @param bridgeIP Bridge IP address
    * @param timeoutMs Timeout in milliseconds
    * @return true on success
     */
    bool authenticateBlocking(const char* bridgeIP, uint32_t timeoutMs = 30000);
    
    /**
    * @brief Checks whether a valid app key exists.
    * @return true if an app key is available
     */
    bool hasValidAppKey();
    
    /**
    * @brief Returns the stored app key.
    * @return app key or empty string
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

