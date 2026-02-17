#include "HueGatewayAuth.h"
#include <ArduinoJson.h>
#include <Preferences.h>

extern Preferences prefs;

HueGatewayAuth::HueGatewayAuth()
    : _appKey("")
{
}

bool HueGatewayAuth::authenticate(const char* bridgeIP)
{
    Serial.printf("[HueGatewayAuth] Authenticating with bridge at %s\n", bridgeIP);
    
    // App-Key aus Speicher laden
    _appKey = loadAppKey();
    _clientKey = loadClientKey();
    
    if (_appKey.length() > 0)
    {
        Serial.println("[HueGatewayAuth] Using stored App-Key");
        return true;
    }

    // Non-blocking: einmaliger Versuch
    Serial.println("[HueGatewayAuth] Requesting new App-Key (single attempt)...");
    return requestAppKeyOnce(bridgeIP);
}

bool HueGatewayAuth::authenticateBlocking(const char* bridgeIP, uint32_t timeoutMs)
{
    Serial.printf("[HueGatewayAuth] Authenticating with bridge at %s (blocking)\n", bridgeIP);

    if (authenticate(bridgeIP))
    {
        Serial.println("[HueGatewayAuth] Authentication successful!");
        return true;
    }

    Serial.println("[HueGatewayAuth] *** PRESS BUTTON ON HUE BRIDGE NOW! ***");

    const uint32_t start = millis();
    uint32_t attempt = 0;
    while ((millis() - start) < timeoutMs)
    {
        if (requestAppKeyOnce(bridgeIP))
        {
            Serial.println("[HueGatewayAuth] Authentication successful!");
            return true;
        }

        attempt++;
        Serial.printf("[HueGatewayAuth] Waiting for button press... (%lu)\n", static_cast<unsigned long>(attempt));
#ifdef INFO_LED_PIN
        delay(100);
        digitalWrite(INFO_LED_PIN, !digitalRead(INFO_LED_PIN));
        delay(900);
#else
        delay(1000);
#endif
    }

    Serial.println("[HueGatewayAuth] Authentication timeout - button not pressed");
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
    _appKey = loadAppKey();
    _clientKey = loadClientKey();
    Serial.printf("[HueGatewayAuth] Stored App-Key available: %s (length=%u)\n",
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

    Serial.printf("[HueGatewayAuth] POST %s\n", url.c_str());
    
    if (!http.begin(client, url))
    {
        Serial.println("[HueGatewayAuth] ERROR: HTTP begin failed (TLS/connection)");
        return false;
    }
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    String body = "{\"devicetype\":\"openknx#huegatewaymodule\",\"generateclientkey\":true}";
    int httpCode = http.POST(body);
    
    if (httpCode == 200)
    {
        String response = http.getString();
        Serial.printf("[HueGatewayAuth] Response: %s\n", response.c_str());
        
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
                        // Normal - Button noch nicht gedrückt
                        Serial.println("[HueGatewayAuth] Bridge reports button not pressed yet (error 101)");
                        success = false;
                    }
                    else
                    {
                        Serial.printf("[HueGatewayAuth] Error %d: %s\n", 
                                    errorType, 
                                    obj["error"]["description"].as<const char*>());
                        success = false;
                    }
                }
                
                // Success: App-Key erhalten
                if (obj.containsKey("success"))
                {
                    String username = obj["success"]["username"].as<String>();
                    Serial.printf("[HueGatewayAuth] App-Key received: %s\n", username.c_str());
                    saveAppKey(username);
                    if (obj["success"].containsKey("clientkey"))
                    {
                        String clientKey = obj["success"]["clientkey"].as<String>();
                        saveClientKey(clientKey);
                        Serial.println("[HueGatewayAuth] Client-Key saved to flash");
                    }
                    success = true;
                }
            }
        }
    }
    else
    {
        String response = http.getString();
        Serial.printf("[HueGatewayAuth] HTTP Error: %d, response: %s\n", httpCode, response.c_str());
    }
    
    http.end();
    return success;
}

void HueGatewayAuth::clearAppKey()
{
    prefs.begin("hue", false);
    prefs.remove("app_key");
    prefs.remove("client_key");
    prefs.end();
    _appKey = "";
    _clientKey = "";
    Serial.println("[HueGatewayAuth] App-Key cleared");
}

void HueGatewayAuth::saveAppKey(const String& key)
{
    prefs.begin("hue", false);
    prefs.putString("app_key", key);
    prefs.end();
    _appKey = key;
    Serial.println("[HueGatewayAuth] App-Key saved to flash");
}

void HueGatewayAuth::saveClientKey(const String& key)
{
    prefs.begin("hue", false);
    prefs.putString("client_key", key);
    prefs.end();
    _clientKey = key;
}

String HueGatewayAuth::loadAppKey()
{
    prefs.begin("hue", true); // read-only
    String key = prefs.getString("app_key", "");
    prefs.end();
    
    if (key.length() > 0)
    {
        Serial.println("[HueGatewayAuth] App-Key loaded from flash");
    }
    
    return key;
}

String HueGatewayAuth::loadClientKey()
{
    prefs.begin("hue", true); // read-only
    String key = prefs.getString("client_key", "");
    prefs.end();
    return key;
}


