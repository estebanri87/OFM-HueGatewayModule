#include "HueAuth.h"
#include <ArduinoJson.h>
#include <Preferences.h>

extern Preferences prefs;

HueAuth::HueAuth()
    : _appKey("")
{
}

bool HueAuth::authenticate(const char* bridgeIP)
{
    Serial.printf("[HueAuth] Authenticating with bridge at %s\n", bridgeIP);
    
    // App-Key aus Speicher laden
    _appKey = loadAppKey();
    
    if (_appKey.length() > 0)
    {
        Serial.println("[HueAuth] Using stored App-Key");
        return true;
    }
    
    // Neuen App-Key anfordern (max 30 Sekunden warten)
    Serial.println("[HueAuth] Requesting new App-Key...");
    Serial.println("[HueAuth] *** PRESS BUTTON ON HUE BRIDGE NOW! ***");
    
    for (int i = 0; i < 30; i++)
    {
        if (requestAppKey(bridgeIP))
        {
            Serial.println("[HueAuth] Authentication successful!");
            return true;
        }
        
        Serial.printf("[HueAuth] Waiting for button press... (%d/30)\n", i + 1);
        
#ifdef INFO_LED_PIN
        // Schnelles Blinken während Wartezeit (wird von HueModule gesteuert)
        delay(100);
        digitalWrite(INFO_LED_PIN, !digitalRead(INFO_LED_PIN));
        delay(900);
#else
        delay(1000);
#endif
    }
    
    Serial.println("[HueAuth] Authentication timeout - button not pressed");
    return false;
}

bool HueAuth::hasValidAppKey()
{
    return _appKey.length() > 0;
}

String HueAuth::getAppKey()
{
    return _appKey;
}

bool HueAuth::requestAppKey(const char* ip)
{
    HTTPClient http;
    String url = String("http://") + ip + "/api";
    
    http.begin(url);
    http.addHeader("Content-Type", "application/json");
    http.setTimeout(5000);
    
    String body = "{\"devicetype\":\"openknx#huemodule\"}";
    int httpCode = http.POST(body);
    
    if (httpCode == 200)
    {
        String response = http.getString();
        Serial.printf("[HueAuth] Response: %s\n", response.c_str());
        
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
                        return false;
                    }
                    else
                    {
                        Serial.printf("[HueAuth] Error %d: %s\n", 
                                    errorType, 
                                    obj["error"]["description"].as<const char*>());
                        return false;
                    }
                }
                
                // Success: App-Key erhalten
                if (obj.containsKey("success"))
                {
                    String username = obj["success"]["username"].as<String>();
                    Serial.printf("[HueAuth] App-Key received: %s\n", username.c_str());
                    saveAppKey(username);
                    return true;
                }
            }
        }
    }
    else
    {
        Serial.printf("[HueAuth] HTTP Error: %d\n", httpCode);
    }
    
    http.end();
    return false;
}

void HueAuth::saveAppKey(const String& key)
{
    prefs.begin("hue", false);
    prefs.putString("app_key", key);
    prefs.end();
    _appKey = key;
    Serial.println("[HueAuth] App-Key saved to flash");
}

String HueAuth::loadAppKey()
{
    prefs.begin("hue", true); // read-only
    String key = prefs.getString("app_key", "");
    prefs.end();
    
    if (key.length() > 0)
    {
        Serial.println("[HueAuth] App-Key loaded from flash");
    }
    
    return key;
}
