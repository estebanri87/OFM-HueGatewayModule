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
    
    if (_appKey.length() > 0)
    {
        Serial.println("[HueGatewayAuth] Using stored App-Key");
        return true;
    }
    
    // Neuen App-Key anfordern (max 30 Sekunden warten)
    Serial.println("[HueGatewayAuth] Requesting new App-Key...");
    Serial.println("[HueGatewayAuth] *** PRESS BUTTON ON HUE BRIDGE NOW! ***");
    
    for (int i = 0; i < 30; i++)
    {
        if (requestAppKeyOnce(bridgeIP))
        {
            Serial.println("[HueGatewayAuth] Authentication successful!");
            return true;
        }
        
        Serial.printf("[HueGatewayAuth] Waiting for button press... (%d/30)\n", i + 1);
        
#ifdef INFO_LED_PIN
        // Schnelles Blinken während Wartezeit (wird von HueGatewayModule gesteuert)
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

bool HueGatewayAuth::loadStoredAppKey()
{
    _appKey = loadAppKey();
    return _appKey.length() > 0;
}

bool HueGatewayAuth::requestAppKeyOnce(const char* ip)
{
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    String url = String("https://") + ip + "/api";
    bool success = false;
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    String body = "{\"devicetype\":\"openknx#huegatewaymodule\"}";
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
                    success = true;
                }
            }
        }
    }
    else
    {
        Serial.printf("[HueGatewayAuth] HTTP Error: %d\n", httpCode);
    }
    
    http.end();
    return success;
}

void HueGatewayAuth::clearAppKey()
{
    prefs.begin("hue", false);
    prefs.remove("app_key");
    prefs.end();
    _appKey = "";
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


