#include "HueGatewayClient.h"
#include <cstring>
#include <math.h>

HueGatewayClient::HueGatewayClient()
    : _initialized(false)
    , _eventStreamConnected(false)
    , _eventHandshakePending(false)
    , _eventHandshakeStartMs(0)
{
}

HueGatewayClient::~HueGatewayClient()
{
    stopEventStream();
    _http.end();
}

bool HueGatewayClient::begin(const String& bridgeIP, const String& appKey)
{
    if (bridgeIP.isEmpty() || appKey.isEmpty())
    {
        Serial.println("[HueGatewayClient] ERROR: Invalid Bridge IP or App Key");
        return false;
    }
    
    _bridgeIP = bridgeIP;
    _appKey = appKey;
    _initialized = true;
    _secureClient.setInsecure();
    _eventClient.setInsecure();
    _secureClient.setTimeout(2000);
    _eventClient.setTimeout(100);
    _eventLineBuffer = "";
    _eventDataBuffer = "";
    _eventStreamConnected = false;
    
    Serial.printf("[HueGatewayClient] Initialized - Bridge: %s\n", _bridgeIP.c_str());
    return true;
}

int HueGatewayClient::getLights(HueGatewayLightState* lights, int maxLights)
{
    if (!_initialized)
    {
        Serial.println("[HueGatewayClient] ERROR: Not initialized");
        return 0;
    }

    DynamicJsonDocument doc(16384);
    int statusCode = httpGet("/clip/v2/resource/light", doc);

    if (statusCode != 200)
    {
        Serial.print("[HueGatewayClient] ERROR: GET lights failed - HTTP ");
        Serial.println(statusCode);
        return 0;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    int count = 0;

    for (JsonObjectConst light : data)
    {
        if (count >= maxLights)
            break;

        lights[count].id = light["id"].as<String>();
        lights[count].name = light["metadata"]["name"].as<String>();
        lights[count].on = light["on"]["on"].as<bool>();

        float brightnessPct = light["dimming"]["brightness"].as<float>();
        lights[count].brightness = static_cast<uint8_t>(brightnessPct * 2.54f);

        lights[count].reachable = light["owner"].isNull() == false;
        lights[count].supportsColorTemp = !light["color_temperature"].isNull();
        lights[count].supportsColor = !light["color"].isNull();

        lights[count].colorTempKelvin = 0;
        lights[count].red = 255;
        lights[count].green = 255;
        lights[count].blue = 255;

        JsonVariantConst mirekVar = light["color_temperature"]["mirek"];
        if (!mirekVar.isNull())
        {
            uint16_t mirek = mirekVar.as<uint16_t>();
            lights[count].colorTempKelvin = mirekToKelvin(mirek);
        }

        JsonVariantConst xVar = light["color"]["xy"]["x"];
        JsonVariantConst yVar = light["color"]["xy"]["y"];
        if (!xVar.isNull() && !yVar.isNull())
        {
            xyToRgb(xVar.as<float>(), yVar.as<float>(), lights[count].red, lights[count].green, lights[count].blue);
        }

        lights[count].room = "-";
        lights[count].zone = "-";
        count++;
    }

    Serial.print("[HueGatewayClient] Found lights: ");
    Serial.println(count);
    return count;
}

void HueGatewayClient::appendLocationsFromDoc(std::vector<LightLocation>& locations, const JsonDocument& doc, bool isRoom)
{
    if (!doc.is<JsonObject>())
    {
        return;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    for (JsonObjectConst item : data)
    {
        const char* name = item["metadata"]["name"] | "";
        if (name[0] == '\0')
        {
            continue;
        }

        JsonArrayConst children = item["children"].as<JsonArrayConst>();
        for (JsonObjectConst child : children)
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

void HueGatewayClient::upsertLocation(std::vector<LightLocation>& locations, const String& id, const String& room, const String& zone)
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

bool HueGatewayClient::setLightOnOff(const String& lightId, bool on)
{
    if (!_initialized)
        return false;

    String endpoint = "/clip/v2/resource/light/" + lightId;

    DynamicJsonDocument doc(256);
    doc["on"]["on"] = on;

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (statusCode == 200)
    {
        Serial.printf("[HueGatewayClient] Light %s -> %s\n", lightId.c_str(), on ? "ON" : "OFF");
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::setLightBrightness(const String& lightId, uint8_t brightness)
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
        Serial.printf("[HueGatewayClient] Light %s -> Brightness: %d\n", lightId.c_str(), brightness);
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT brightness failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::setLightState(const String& lightId, bool on, uint8_t brightness)
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
        Serial.printf("[HueGatewayClient] Light %s -> On:%d Bri:%d\n", 
                      lightId.c_str(), on, brightness);
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT state failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::setLightStateWithColorTemp(const String& lightId, bool on, uint8_t brightness, 
                                                   uint16_t mirek, uint8_t fadeDurationSec)
{
    if (!_initialized)
        return false;
    
    // Clamp mirek to valid range (153-500)
    // 153 = cold/6536K, 500 = warm/2000K
    uint16_t clampedMirek = mirek;
    if (clampedMirek < 153) clampedMirek = 153;
    if (clampedMirek > 500) clampedMirek = 500;
    
    // Brightness 0-254 -> 0-100%
    float brightnessPct = (brightness / 254.0f) * 100.0f;
    
    // Fade-Dauer in Millisekunden (Hue API v2 dynamics.duration)
    uint32_t fadeDurationMs = fadeDurationSec * 1000;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    // API v2 structure with on, dimming, color_temperature, and dynamics
    DynamicJsonDocument doc(512);
    doc["on"]["on"] = on;
    doc["dimming"]["brightness"] = brightnessPct;
    doc["color_temperature"]["mirek"] = clampedMirek;
    
    if (fadeDurationSec > 0) {
        doc["dynamics"]["duration"] = fadeDurationMs;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (statusCode == 200)
    {
        Serial.printf("[HueGatewayClient] Light %s -> On:%d Bri:%d CT:%d fade:%ds\n", 
                      lightId.c_str(), on, brightness, clampedMirek, fadeDurationSec);
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT state+CT failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::setLightColorTemperature(const String& lightId, uint16_t mirek)
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
        Serial.printf("[HueGatewayClient] Light %s -> ColorTemp: %d mirek (%d K)\n", 
                      lightId.c_str(), clampedMirek, mirekToKelvin(clampedMirek));
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT color_temperature failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::setLightColor(const String& lightId, float x, float y)
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
        Serial.printf("[HueGatewayClient] Light %s -> Color XY: (%.3f, %.3f)\n", 
                      lightId.c_str(), clampedX, clampedY);
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT color failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::pingBridgeApiV2()
{
    if (!_initialized)
    {
        return false;
    }

    DynamicJsonDocument doc(1024);
    int statusCode = httpGet("/clip/v2/resource/bridge", doc);
    if (statusCode == 200)
    {
        return true;
    }

    Serial.printf("[HueGatewayClient] Bridge API v2 ping failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::startEventStream()
{
    if (!_initialized)
    {
        return false;
    }

    if (_eventStreamConnected && _eventClient.connected())
    {
        return true;
    }

    if (_eventHandshakePending && _eventClient.connected())
    {
        return true;
    }

    stopEventStream();

    _eventClient.setTimeout(100);
    Serial.printf("[HueGatewayClient] EventStream connecting to %s:443\n", _bridgeIP.c_str());
    if (!_eventClient.connect(_bridgeIP.c_str(), 443))
    {
        Serial.println("[HueGatewayClient] EventStream connect failed");
        return false;
    }

    _eventClient.printf("GET /eventstream/clip/v2 HTTP/1.1\r\n");
    _eventClient.printf("Host: %s\r\n", _bridgeIP.c_str());
    _eventClient.printf("hue-application-key: %s\r\n", _appKey.c_str());
    _eventClient.print("Accept: text/event-stream\r\n");
    _eventClient.print("Connection: keep-alive\r\n\r\n");

    _eventHandshakePending = true;
    _eventHandshakeStartMs = millis();
    _eventStreamConnected = false;
    _eventLineBuffer = "";
    _eventDataBuffer = "";

    return true;
}

void HueGatewayClient::stopEventStream()
{
    _eventStreamConnected = false;
    _eventHandshakePending = false;
    _eventHandshakeStartMs = 0;
    _eventLineBuffer = "";
    _eventDataBuffer = "";
    if (_eventClient.connected())
    {
        Serial.println("[HueGatewayClient] EventStream stopping");
        _eventClient.stop();
    }
}

int HueGatewayClient::pollEventStream(HueGatewayEventLightUpdate* updates, int maxUpdates)
{
    if (_eventHandshakePending)
    {
        if (!_eventClient.connected())
        {
            Serial.println("[HueGatewayClient] EventStream handshake failed: disconnected");
            stopEventStream();
            return 0;
        }

        if ((millis() - _eventHandshakeStartMs) > 3000UL && !_eventClient.available())
        {
            Serial.println("[HueGatewayClient] EventStream header timeout");
            stopEventStream();
            return 0;
        }

        if (!_eventClient.available())
        {
            return 0;
        }

        String statusLine = _eventClient.readStringUntil('\n');
        statusLine.trim();
        Serial.printf("[HueGatewayClient] EventStream status: %s\n", statusLine.c_str());
        if (statusLine.indexOf("200") < 0)
        {
            Serial.printf("[HueGatewayClient] EventStream HTTP error: %s\n", statusLine.c_str());
            stopEventStream();
            return 0;
        }

        while (_eventClient.connected())
        {
            if (!_eventClient.available())
            {
                return 0;
            }

            String headerLine = _eventClient.readStringUntil('\n');
            headerLine.trim();
            if (headerLine.length() == 0)
            {
                break;
            }
        }

        _eventHandshakePending = false;
        _eventHandshakeStartMs = 0;
        _eventLineBuffer = "";
        _eventDataBuffer = "";
        _eventStreamConnected = true;
        Serial.println("[HueGatewayClient] EventStream connected");
    }

    if (!_eventStreamConnected || !_eventClient.connected() || updates == nullptr || maxUpdates <= 0)
    {
        return 0;
    }

    int updateCount = 0;
    while (_eventClient.available() && updateCount < maxUpdates)
    {
        char ch = static_cast<char>(_eventClient.read());
        if (ch == '\r')
        {
            continue;
        }

        if (ch != '\n')
        {
            _eventLineBuffer += ch;
            if (_eventLineBuffer.length() > 4096)
            {
                _eventLineBuffer = "";
            }
            continue;
        }

        if (_eventLineBuffer.length() == 0)
        {
            if (_eventDataBuffer.length() > 0)
            {
                updateCount += parseEventPayload(_eventDataBuffer, updates + updateCount, maxUpdates - updateCount);
                _eventDataBuffer = "";
            }
        }
        else if (_eventLineBuffer.startsWith("data:"))
        {
            String dataPart = _eventLineBuffer.substring(5);
            dataPart.trim();
            _eventDataBuffer += dataPart;
        }

        _eventLineBuffer = "";
    }

    if (!_eventClient.connected())
    {
        Serial.println("[HueGatewayClient] EventStream disconnected");
        stopEventStream();
    }

    if (updateCount > 0)
    {
        Serial.printf("[HueGatewayClient] EventStream parsed %d update(s)\n", updateCount);
    }

    return updateCount;
}

int HueGatewayClient::parseEventPayload(const String& payload, HueGatewayEventLightUpdate* updates, int maxUpdates)
{
    if (payload.length() == 0 || updates == nullptr || maxUpdates <= 0)
    {
        return 0;
    }

    DynamicJsonDocument doc(8192);
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
        Serial.printf("[HueGatewayClient] Event payload JSON parse error: %s\n", error.c_str());
        return 0;
    }

    JsonArrayConst events = doc.as<JsonArrayConst>();
    int updateCount = 0;

    for (JsonObjectConst eventObj : events)
    {
        JsonArrayConst data = eventObj["data"].as<JsonArrayConst>();
        for (JsonObjectConst item : data)
        {
            const char* type = item["type"] | "";
            if (strcmp(type, "light") != 0)
            {
                continue;
            }

            const char* id = item["id"] | "";
            if (id[0] == '\0' || updateCount >= maxUpdates)
            {
                continue;
            }

            HueGatewayEventLightUpdate& update = updates[updateCount];
            update.lightId = String(id);
            update.hasOn = false;
            update.on = false;
            update.hasBrightness = false;
            update.brightness = 0;
            update.hasColorTemp = false;
            update.colorTempKelvin = 0;
            update.hasColorRgb = false;
            update.red = 255;
            update.green = 255;
            update.blue = 255;

            JsonVariantConst onVar = item["on"]["on"];
            if (!onVar.isNull())
            {
                update.hasOn = true;
                update.on = onVar.as<bool>();
            }

            JsonVariantConst brightnessVar = item["dimming"]["brightness"];
            if (!brightnessVar.isNull())
            {
                float brightnessPct = brightnessVar.as<float>();
                if (brightnessPct < 0.0f) brightnessPct = 0.0f;
                if (brightnessPct > 100.0f) brightnessPct = 100.0f;
                update.hasBrightness = true;
                update.brightness = static_cast<uint8_t>(brightnessPct * 2.54f);
            }

            JsonVariantConst mirekVar = item["color_temperature"]["mirek"];
            if (!mirekVar.isNull())
            {
                update.hasColorTemp = true;
                update.colorTempKelvin = mirekToKelvin(mirekVar.as<uint16_t>());
            }

            JsonVariantConst xVar = item["color"]["xy"]["x"];
            JsonVariantConst yVar = item["color"]["xy"]["y"];
            if (!xVar.isNull() && !yVar.isNull())
            {
                update.hasColorRgb = true;
                xyToRgb(xVar.as<float>(), yVar.as<float>(), update.red, update.green, update.blue);
            }

            if (update.hasOn || update.hasBrightness || update.hasColorTemp || update.hasColorRgb)
            {
                updateCount++;
            }
        }
    }

    return updateCount;
}

uint16_t HueGatewayClient::kelvinToMirek(uint16_t kelvin)
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

uint16_t HueGatewayClient::mirekToKelvin(uint16_t mirek)
{
    // Kelvin = 1,000,000 / Mirek
    // Clamp mirek to valid range first (153-500)
    if (mirek < 153) mirek = 153;
    if (mirek > 500) mirek = 500;
    
    uint16_t kelvin = 1000000 / mirek;
    
    return kelvin;
}

// ===== Private Methods =====

int HueGatewayClient::httpGet(const String& endpoint, JsonDocument& doc)
{
    String url = buildUrl(endpoint);
    Serial.print("[HueGatewayClient] HTTP GET ");
    Serial.println(url);
    _http.setTimeout(2000);
    _http.setTimeout(2000);
    
    _http.begin(_secureClient, url);
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.GET();
    
    if (statusCode == 200)
    {
        String response = _http.getString();
        DeserializationError error = deserializeJson(doc, response);
        
        if (error)
        {
            Serial.print("[HueGatewayClient] JSON parse error: ");
            Serial.println(error.c_str());
            Serial.print("[HueGatewayClient] JSON payload length: ");
            Serial.println(response.length());
            statusCode = -1;
        }
    }
    else
    {
        Serial.print("[HueGatewayClient] HTTP GET failed (");
        Serial.print(statusCode);
        Serial.println(")");
    }
    
    _http.end();
    return statusCode;
}

int HueGatewayClient::httpPut(const String& endpoint, const String& payload)
{
    String url = buildUrl(endpoint);
    Serial.printf("[HueGatewayClient] HTTP PUT %s payload=%s\n", url.c_str(), payload.c_str());
    _http.setTimeout(2000);
    _http.setTimeout(2000);
    
    _http.begin(_secureClient, url);
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.PUT(payload);
    
    if (statusCode != 200)
    {
        String response = _http.getString();
        Serial.printf("[HueGatewayClient] HTTP PUT failed (%d): %s\n", statusCode, response.c_str());
    }
    
    _http.end();
    return statusCode;
}

String HueGatewayClient::buildUrl(const String& endpoint)
{
    // Hue API access should use HTTPS (HTTP deprecated by Signify).
    return "https://" + _bridgeIP + endpoint;
}

void HueGatewayClient::xyToRgb(float x, float y, uint8_t& red, uint8_t& green, uint8_t& blue)
{
    if (y <= 0.00001f)
    {
        red = 255;
        green = 255;
        blue = 255;
        return;
    }

    float z = 1.0f - x - y;
    float Y = 1.0f;
    float X = (Y / y) * x;
    float Z = (Y / y) * z;

    float r = X * 1.656492f - Y * 0.354851f - Z * 0.255038f;
    float g = -X * 0.707196f + Y * 1.655397f + Z * 0.036152f;
    float b = X * 0.051713f - Y * 0.121364f + Z * 1.011530f;

    if (r < 0.0f) r = 0.0f;
    if (g < 0.0f) g = 0.0f;
    if (b < 0.0f) b = 0.0f;

    float maxValue = r;
    if (g > maxValue) maxValue = g;
    if (b > maxValue) maxValue = b;
    if (maxValue > 1.0f)
    {
        r /= maxValue;
        g /= maxValue;
        b /= maxValue;
    }

    auto gamma = [](float c) -> float {
        return (c <= 0.0031308f) ? (12.92f * c) : ((1.0f + 0.055f) * powf(c, 1.0f / 2.4f) - 0.055f);
    };

    r = gamma(r);
    g = gamma(g);
    b = gamma(b);

    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;

    red = static_cast<uint8_t>(r * 255.0f);
    green = static_cast<uint8_t>(g * 255.0f);
    blue = static_cast<uint8_t>(b * 255.0f);
}

