#include "HueBridgeClient.h"
#include <cstring>

HueBridgeClient::HueBridgeClient()
    : _initialized(false)
{
}

HueBridgeClient::~HueBridgeClient()
{
    _http.end();
}

bool HueBridgeClient::begin(const String& bridgeIP, const String& appKey)
{
    if (bridgeIP.isEmpty() || appKey.isEmpty())
    {
        Serial.println("[HueBridgeClient] ERROR: Invalid Bridge IP or App Key");
        return false;
    }
    
    _bridgeIP = bridgeIP;
    _appKey = appKey;
    _initialized = true;
    _secureClient.setInsecure();
    
    Serial.printf("[HueBridgeClient] Initialized - Bridge: %s\n", _bridgeIP.c_str());
    return true;
}

int HueBridgeClient::getLights(HueBridgeLightState* lights, int maxLights)
{
    if (!_initialized)
    {
        Serial.println("[HueBridgeClient] ERROR: Not initialized");
        return 0;
    }
    
    // Build location index (room/zone -> light id)
    std::vector<LightLocation> locations;
    DynamicJsonDocument roomsDoc(8192);
    if (httpGet("/clip/v2/resource/room", roomsDoc) == 200)
    {
        appendLocationsFromDoc(locations, roomsDoc, true);
    }
    DynamicJsonDocument zonesDoc(8192);
    if (httpGet("/clip/v2/resource/zone", zonesDoc) == 200)
    {
        appendLocationsFromDoc(locations, zonesDoc, false);
    }

    // API v2: GET /clip/v2/resource/light
    DynamicJsonDocument doc(8192);
    int statusCode = httpGet("/clip/v2/resource/light", doc);
    
    if (statusCode != 200)
    {
        Serial.printf("[HueBridgeClient] ERROR: GET lights failed - HTTP %d\n", statusCode);
        return 0;
    }
    
    // Parse Response
    JsonArray data = doc["data"].as<JsonArray>();
    int count = 0;
    
    for (JsonObject light : data)
    {
        if (count >= maxLights)
            break;
        
        lights[count].id = light["id"].as<String>();
        lights[count].name = light["metadata"]["name"].as<String>();
        lights[count].on = light["on"]["on"].as<bool>();
        
        // Brightness: API gibt 0.0-100.0, wir brauchen 0-254
        float brightnessPct = light["dimming"]["brightness"].as<float>();
        lights[count].brightness = (uint8_t)(brightnessPct * 2.54f);
        
        // Status (erreichbar wenn owner vorhanden)
        lights[count].reachable = light["owner"].isNull() == false;

        lights[count].room = "";
        lights[count].zone = "";
        for (const auto& loc : locations)
        {
            if (loc.id == lights[count].id)
            {
                lights[count].room = loc.room;
                lights[count].zone = loc.zone;
                break;
            }
        }
        
        Serial.printf("[HueBridgeClient] Light %d: %s (%s) - On:%d Bri:%d Room:%s Zone:%s\n",
                  count, lights[count].name.c_str(), lights[count].id.c_str(),
                  lights[count].on, lights[count].brightness,
                  lights[count].room.c_str(), lights[count].zone.c_str());
        
        count++;
    }
    
    Serial.printf("[HueBridgeClient] Found %d lights\n", count);
    return count;
}

void HueBridgeClient::appendLocationsFromDoc(std::vector<LightLocation>& locations, const JsonDocument& doc, bool isRoom)
{
    if (!doc.is<JsonObject>())
    {
        return;
    }

    JsonArray data = doc["data"].as<JsonArray>();
    for (JsonObject item : data)
    {
        const char* name = item["metadata"]["name"] | "";
        if (name[0] == '\0')
        {
            continue;
        }

        JsonArray children = item["children"].as<JsonArray>();
        for (JsonObject child : children)
        {
            const char* rtype = child["rtype"] | "";
            if (strcmp(rtype, "light") != 0)
            {
                continue;
            }

            const char* rid = child["rid"] | "";
            if (rid[0] == '\0')
            {
                continue;
            }

            if (isRoom)
            {
                upsertLocation(locations, String(rid), String(name), "");
            }
            else
            {
                upsertLocation(locations, String(rid), "", String(name));
            }
        }
    }
}

void HueBridgeClient::upsertLocation(std::vector<LightLocation>& locations, const String& id, const String& room, const String& zone)
{
    for (auto& loc : locations)
    {
        if (loc.id == id)
        {
            if (room.length() > 0)
            {
                loc.room = room;
            }
            if (zone.length() > 0)
            {
                loc.zone = zone;
            }
            return;
        }
    }

    LightLocation loc;
    loc.id = id;
    loc.room = room;
    loc.zone = zone;
    locations.push_back(loc);
}

bool HueBridgeClient::setLightOnOff(const String& lightId, bool on)
{
    if (!_initialized)
        return false;
    
    // API v2: PUT /clip/v2/resource/light/{id}
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    // JSON Payload
    DynamicJsonDocument doc(256);
    doc["on"]["on"] = on;
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueBridgeClient] Light %s -> %s\n", lightId.c_str(), on ? "ON" : "OFF");
        return true;
    }
    else
    {
        Serial.printf("[HueBridgeClient] ERROR: PUT failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueBridgeClient::setLightBrightness(const String& lightId, uint8_t brightness)
{
    if (!_initialized)
        return false;
    
    // Brightness: 0-254 -> 0.0-100.0%
    float brightnessPct = (brightness / 254.0f) * 100.0f;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    DynamicJsonDocument doc(256);
    doc["dimming"]["brightness"] = brightnessPct;
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueBridgeClient] Light %s -> Brightness: %d\n", lightId.c_str(), brightness);
        return true;
    }
    else
    {
        Serial.printf("[HueBridgeClient] ERROR: PUT brightness failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueBridgeClient::setLightState(const String& lightId, bool on, uint8_t brightness)
{
    if (!_initialized)
        return false;
    
    float brightnessPct = (brightness / 254.0f) * 100.0f;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    DynamicJsonDocument doc(512);
    doc["on"]["on"] = on;
    doc["dimming"]["brightness"] = brightnessPct;
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueBridgeClient] Light %s -> On:%d Bri:%d\n", 
                      lightId.c_str(), on, brightness);
        return true;
    }
    else
    {
        Serial.printf("[HueBridgeClient] ERROR: PUT state failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueBridgeClient::setLightColorTemperature(const String& lightId, uint16_t mirek)
{
    if (!_initialized)
        return false;
    
    // Clamp mirek to valid range (153-500)
    // 153 = cold/6536K, 500 = warm/2000K
    uint16_t clampedMirek = mirek;
    if (clampedMirek < 153) clampedMirek = 153;
    if (clampedMirek > 500) clampedMirek = 500;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    // API v2 structure: {"color_temperature": {"mirek": value}}
    DynamicJsonDocument doc(256);
    doc["color_temperature"]["mirek"] = clampedMirek;
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueBridgeClient] Light %s -> ColorTemp: %d mirek (%d K)\n", 
                      lightId.c_str(), clampedMirek, mirekToKelvin(clampedMirek));
        return true;
    }
    else
    {
        Serial.printf("[HueBridgeClient] ERROR: PUT color_temperature failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueBridgeClient::setLightColor(const String& lightId, float x, float y)
{
    if (!_initialized)
        return false;
    
    // Clamp x and y to valid range (0.0-1.0)
    // CIE 1931 color space coordinates
    float clampedX = x;
    float clampedY = y;
    if (clampedX < 0.0f) clampedX = 0.0f;
    if (clampedX > 1.0f) clampedX = 1.0f;
    if (clampedY < 0.0f) clampedY = 0.0f;
    if (clampedY > 1.0f) clampedY = 1.0f;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    // API v2 structure: {"color": {"xy": {"x": x, "y": y}}}
    DynamicJsonDocument doc(256);
    doc["color"]["xy"]["x"] = clampedX;
    doc["color"]["xy"]["y"] = clampedY;
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueBridgeClient] Light %s -> Color XY: (%.3f, %.3f)\n", 
                      lightId.c_str(), clampedX, clampedY);
        return true;
    }
    else
    {
        Serial.printf("[HueBridgeClient] ERROR: PUT color failed - HTTP %d\n", statusCode);
        return false;
    }
}

uint16_t HueBridgeClient::kelvinToMirek(uint16_t kelvin)
{
    // Mirek = 1,000,000 / Kelvin
    // Clamp Kelvin to valid range first (2000-6536)
    if (kelvin < 2000) kelvin = 2000;
    if (kelvin > 6536) kelvin = 6536;
    
    uint16_t mirek = 1000000 / kelvin;
    
    // Ensure result is in valid mirek range (153-500)
    if (mirek < 153) mirek = 153;
    if (mirek > 500) mirek = 500;
    
    return mirek;
}

uint16_t HueBridgeClient::mirekToKelvin(uint16_t mirek)
{
    // Kelvin = 1,000,000 / Mirek
    // Clamp mirek to valid range first (153-500)
    if (mirek < 153) mirek = 153;
    if (mirek > 500) mirek = 500;
    
    uint16_t kelvin = 1000000 / mirek;
    
    return kelvin;
}

// ===== Private Methods =====

int HueBridgeClient::httpGet(const String& endpoint, JsonDocument& doc)
{
    String url = buildUrl(endpoint);
    
    _http.begin(_secureClient, url);
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.GET();
    
    if (statusCode == 200)
    {
        String response = _http.getString();
        DeserializationError error = deserializeJson(doc, response);
        
        if (error)
        {
            Serial.printf("[HueBridgeClient] JSON parse error: %s\n", error.c_str());
            statusCode = -1;
        }
    }
    
    _http.end();
    return statusCode;
}

int HueBridgeClient::httpPut(const String& endpoint, const String& payload)
{
    String url = buildUrl(endpoint);
    
    _http.begin(_secureClient, url);
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.PUT(payload);
    
    if (statusCode != 200)
    {
        String response = _http.getString();
        Serial.printf("[HueBridgeClient] Response: %s\n", response.c_str());
    }
    
    _http.end();
    return statusCode;
}

String HueBridgeClient::buildUrl(const String& endpoint)
{
    // Hue API access should use HTTPS (HTTP deprecated by Signify).
    return "https://" + _bridgeIP + endpoint;
}

