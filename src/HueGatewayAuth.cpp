#include "HueGatewayAuth.h"
#include "HueGatewayStorage.h"
#include <ArduinoJson.h>
#include "OpenKNX.h"

HueGatewayAuth::HueGatewayAuth()
    : _appKey("")
{
}

bool HueGatewayAuth::authenticate(const char* bridgeIP)
{
    logInfo("HueGatewayAuth", "Authenticating with bridge at %s", bridgeIP);
    
    // Load App-Key from storage.
    _appKey = loadAppKey();
    _clientKey = loadClientKey();
    
    if (_appKey.length() > 0)
    {
        logInfo("HueGatewayAuth", "Using stored App-Key");
        return true;
    }

    // Non-blocking: single attempt.
    logInfo("HueGatewayAuth", "Requesting new App-Key (single attempt)...");
    return requestAppKeyOnce(bridgeIP);
}

bool HueGatewayAuth::authenticateBlocking(const char* bridgeIP, uint32_t timeoutMs)
{
    logInfo("HueGatewayAuth", "Authenticating with bridge at %s (blocking)", bridgeIP);

    if (authenticate(bridgeIP))
    {
        logInfo("HueGatewayAuth", "Authentication successful!");
        return true;
    }

    logInfo("HueGatewayAuth", "*** PRESS BUTTON ON HUE BRIDGE NOW! ***");

    const uint32_t start = millis();
    uint32_t nextTryMs = start;
    uint32_t attempt = 0;
    while ((millis() - start) < timeoutMs)
    {
        uint32_t now = millis();
        if (now >= nextTryMs)
        {
            if (requestAppKeyOnce(bridgeIP))
            {
                logInfo("HueGatewayAuth", "Authentication successful!");
                return true;
            }

            attempt++;
            nextTryMs = now + 1000UL;
            logDebug("HueGatewayAuth", "Waiting for button press... (%lu)", static_cast<unsigned long>(attempt));
#ifdef INFO_LED_PIN
            digitalWrite(INFO_LED_PIN, !digitalRead(INFO_LED_PIN));
#endif
        }

        delay(25);
        yield();
    }

    logError("HueGatewayAuth", "Authentication timeout - button not pressed");
    return false;
}

bool HueGatewayAuth::hasValidAppKey()
{
    return _appKey.length() > 0;
}

String HueGatewayAuth::getAppKey()
{
    return _appKey;
}

String HueGatewayAuth::getClientKey()
{
    return _clientKey;
}

bool HueGatewayAuth::loadStoredAppKey()
{
    if (_appKey.length() > 0)
    {
        return true;
    }

    _appKey = loadAppKey();
    _clientKey = loadClientKey();
    logDebug("HueGatewayAuth", "Stored App-Key available: %s (length=%u)",
             _appKey.length() > 0 ? "yes" : "no",
             static_cast<unsigned>(_appKey.length()));
    return _appKey.length() > 0;
}

bool HueGatewayAuth::loadStoredClientKey()
{
    _clientKey = loadClientKey();
    return _clientKey.length() > 0;
}

bool HueGatewayAuth::requestAppKeyOnce(const char* ip)
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String("https://") + ip + "/api";
    bool success = false;

    logDebug("HueGatewayAuth", "POST %s", url.c_str());
    
    if (!http.begin(client, url))
    {
        logError("HueGatewayAuth", "ERROR: HTTP begin failed (TLS/connection)");
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    String body = "{\"devicetype\":\"openknx#huegatewaymodule\",\"generateclientkey\":true}";
    int httpCode = http.POST(body);
    
    if (httpCode == 200)
    {
        String response = http.getString();
        logDebug("HueGatewayAuth", "Response: %s", response.c_str());
        
        StaticJsonDocument<1024> doc;
        DeserializationError error = deserializeJson(doc, response);
        
        if (!error && doc.is<JsonArray>())
        {
            JsonArray arr = doc.as<JsonArray>();
            if (arr.size() > 0)
            {
                JsonObject obj = arr[0];
                
                // Error 101: Button not pressed
                if (obj.containsKey("error"))
                {
                    int errorType = obj["error"]["type"];
                    if (errorType == 101)
                    {
                        // Expected during pairing: bridge button has not been pressed yet.
                        logDebug("HueGatewayAuth", "Bridge reports button not pressed yet (error 101)");
                        success = false;
                    }
                    else
                    {
                        logError("HueGatewayAuth", "Error %d: %s",
                                 errorType,
                                 obj["error"]["description"].as<const char*>());
                        success = false;
                    }
                }
                
                // Success: store the newly issued app key/client key.
                if (obj.containsKey("success"))
                {
                    String username = obj["success"]["username"].as<String>();
                    logInfo("HueGatewayAuth", "App-Key received: %s...%s",
                            username.substring(0, 4).c_str(),
                            username.substring(username.length() > 4 ? username.length() - 4 : 0).c_str());
                    saveAppKey(username);
                    if (obj["success"].containsKey("clientkey"))
                    {
                        String clientKey = obj["success"]["clientkey"].as<String>();
                        saveClientKey(clientKey);
                        logInfo("HueGatewayAuth", "Client-Key saved to flash");
                    }
                    success = true;
                }
            }
        }
    }
    else
    {
        String response = http.getString();
        logError("HueGatewayAuth", "HTTP Error: %d, response: %s", httpCode, response.c_str());
    }
    
    http.end();
    return success;
}

void HueGatewayAuth::clearAppKey()
{
    HueGatewayStorage::clearAuthKeys();
    _appKey = "";
    _clientKey = "";
    logInfo("HueGatewayAuth", "App-Key cleared");
}

void HueGatewayAuth::saveAppKey(const String& key)
{
    if (key == _appKey)
        return;
    HueGatewayStorage::saveAppKey(key);
    _appKey = key;
    logInfo("HueGatewayAuth", "App-Key saved to flash");
}

void HueGatewayAuth::saveClientKey(const String& key)
{
    if (key == _clientKey)
        return;
    HueGatewayStorage::saveClientKey(key);
    _clientKey = key;
}

String HueGatewayAuth::loadAppKey()
{
    String key = HueGatewayStorage::loadAppKey();
    
    if (key.length() > 0)
    {
        logInfo("HueGatewayAuth", "App-Key loaded from flash");
    }
    
    return key;
}

String HueGatewayAuth::loadClientKey()
{
    return HueGatewayStorage::loadClientKey();
}


