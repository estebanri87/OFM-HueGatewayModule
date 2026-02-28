#include "HueGatewayClient.h"
#include <cstring>
#include <functional>
#include <math.h>
#include <esp_heap_caps.h>

namespace
{
static constexpr size_t kMaxEventLineChars = 4096U;
static constexpr size_t kMaxEventPayloadChars = 8192U;
#if defined(DEVICE_REG1_LAN_TP_BASE) || defined(DEVICE_DEV_REG1_LAN_TP_Base_V00_11)
static constexpr uint32_t kTlsMinInternalFreeBytes = 62000U;
static constexpr uint32_t kTlsMinInternalLargestBlockBytes = 32000U;
#else
static constexpr uint32_t kTlsMinInternalFreeBytes = 70000U;
static constexpr uint32_t kTlsMinInternalLargestBlockBytes = 50000U;
#endif
static constexpr unsigned long kLightLocationCacheMs = 300000UL;

float dimmingStepCodeToPercent(uint8_t stepCode)
{
    switch (stepCode)
    {
        case 1: return 100.0f;
        case 2: return 50.0f;
        case 3: return 25.0f;
        case 4: return 12.5f;
        case 5: return 6.25f;
        case 6: return 3.125f;
        case 7: return 1.5625f;
        default: return 0.0f;
    }
}

bool isHttpSuccessStatus(int statusCode)
{
    return statusCode >= 200 && statusCode < 300;
}

bool hasTlsInternalHeadroom()
{
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return freeInternal >= kTlsMinInternalFreeBytes && largestInternal >= kTlsMinInternalLargestBlockBytes;
}

void logHeapStats(const char* phase)
{
    const uint32_t freeHeap = ESP.getFreeHeap();
    const uint32_t minFreeHeap = ESP.getMinFreeHeap();
    const size_t free8Bit = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t largest8Bit = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const size_t largestInternal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    const uint32_t fragPercent = (free8Bit > 0 && largest8Bit <= free8Bit)
        ? static_cast<uint32_t>(((free8Bit - largest8Bit) * 100U) / free8Bit)
        : 0U;

    Serial.printf("[HueGatewayClient] HEAP %s free=%lu min=%lu free8=%u largest8=%u intFree=%u intLargest=%u frag=%lu%%\n",
                  phase,
                  static_cast<unsigned long>(freeHeap),
                  static_cast<unsigned long>(minFreeHeap),
                  static_cast<unsigned>(free8Bit),
                  static_cast<unsigned>(largest8Bit),
                  static_cast<unsigned>(freeInternal),
                  static_cast<unsigned>(largestInternal),
                  static_cast<unsigned long>(fragPercent));
}

bool extractResourceRef(JsonVariantConst refVar, String& outRid, String& outType)
{
    outRid = "";
    outType = "";

    if (refVar.is<const char*>())
    {
        const char* rid = refVar.as<const char*>();
        if (rid != nullptr && rid[0] != '\0')
        {
            outRid = String(rid);
            return true;
        }
        return false;
    }

    JsonObjectConst ref = refVar.as<JsonObjectConst>();
    if (ref.isNull())
    {
        return false;
    }

    const char* rid = ref["rid"] | "";
    if (rid[0] == '\0') rid = ref["id"] | "";
    if (rid[0] == '\0') rid = ref["resource_identifier"]["rid"] | "";
    if (rid[0] == '\0') rid = ref["service"]["rid"] | "";
    if (rid[0] == '\0') rid = ref["target"]["rid"] | "";

    const char* rtype = ref["rtype"] | "";
    if (rtype[0] == '\0') rtype = ref["type"] | "";
    if (rtype[0] == '\0') rtype = ref["resource_identifier"]["rtype"] | "";
    if (rtype[0] == '\0') rtype = ref["service"]["rtype"] | "";
    if (rtype[0] == '\0') rtype = ref["target"]["rtype"] | "";

    if (rid[0] == '\0')
    {
        return false;
    }

    outRid = String(rid);
    if (rtype[0] != '\0')
    {
        outType = String(rtype);
    }
    return true;
}

int parseHttpStatusCode(const String& statusLine)
{
    int firstSpace = statusLine.indexOf(' ');
    if (firstSpace < 0 || firstSpace + 3 >= static_cast<int>(statusLine.length()))
    {
        return -1;
    }

    String codeStr = statusLine.substring(firstSpace + 1, firstSpace + 4);
    return codeStr.toInt();
}
}

HueGatewayClient::HueGatewayClient()
    : _initialized(false)
    , _eventStreamConnected(false)
    , _eventHandshakePending(false)
    , _eventHandshakeStartMs(0)
    , _eventLastDataMs(0)
    , _eventParseErrorStreak(0)
    , _eventDropCount(0)
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
    _eventLineBuffer.reserve(512);
    _eventDataBuffer.reserve(2048);
    _eventLineBuffer = "";
    _eventDataBuffer = "";
    _eventStreamConnected = false;
    _eventParseErrorStreak = 0;
    _eventLastDataMs = millis();
    
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

    static DynamicJsonDocument doc(65536);
    static DynamicJsonDocument lightFilterDoc(768);
    lightFilterDoc.clear();
    JsonObject lightFilterRoot = lightFilterDoc.to<JsonObject>();
    JsonObject lightFilterData = lightFilterRoot["data"][0].to<JsonObject>();
    lightFilterData["id"] = true;
    lightFilterData["metadata"]["name"] = true;
    lightFilterData["on"]["on"] = true;
    lightFilterData["owner"]["rid"] = true;
    lightFilterData["dimming"]["brightness"] = true;
    lightFilterData["color_temperature"]["mirek"] = true;
    lightFilterData["color"]["xy"]["x"] = true;
    lightFilterData["color"]["xy"]["y"] = true;
    doc.clear();
    int statusCode = httpGet("/clip/v2/resource/light", doc, &lightFilterDoc);

    if (!isHttpSuccessStatus(statusCode))
    {
        Serial.print("[HueGatewayClient] ERROR: GET lights failed - HTTP ");
        Serial.println(statusCode);
        return 0;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    std::vector<LightLocation> locations;

    static std::vector<LightLocation> sCachedLocations;
    static unsigned long sLocationCacheValidUntilMs = 0UL;

    const unsigned long now = millis();
    const bool cacheValid = !sCachedLocations.empty()
        && sLocationCacheValidUntilMs != 0UL
        && now <= sLocationCacheValidUntilMs;

    if (cacheValid)
    {
        locations = sCachedLocations;
    }
    else
    {
        std::vector<DeviceLightLink> deviceLightLinks;
        deviceLightLinks.reserve(static_cast<size_t>(data.size()));

        // Build a direct device->light map from light payload first.
        // This is robust even if /device does not expose light refs on some bridge firmwares.
        for (JsonObjectConst light : data)
        {
            const char* lightRid = light["id"] | "";
            const char* ownerRid = light["owner"]["rid"] | "";
            if (lightRid[0] == '\0' || ownerRid[0] == '\0')
            {
                continue;
            }

            DeviceLightLink link;
            link.deviceId = String(ownerRid);
            link.lightId = String(lightRid);
            deviceLightLinks.push_back(link);
        }

        static DynamicJsonDocument deviceDoc(32768);
        static DynamicJsonDocument deviceFilterDoc(512);
        deviceFilterDoc.clear();
        JsonObject deviceFilterRoot = deviceFilterDoc.to<JsonObject>();
        JsonObject deviceFilterData = deviceFilterRoot["data"][0].to<JsonObject>();
        deviceFilterData["id"] = true;
        deviceFilterData["services"][0]["rid"] = true;
        deviceFilterData["services"][0]["rtype"] = true;
        deviceFilterData["children"][0]["rid"] = true;
        deviceFilterData["children"][0]["rtype"] = true;
        deviceFilterData["grouped_services"][0]["rid"] = true;
        deviceFilterData["grouped_services"][0]["rtype"] = true;
        deviceDoc.clear();
        int deviceStatus = httpGet("/clip/v2/resource/device", deviceDoc, &deviceFilterDoc);
        if (isHttpSuccessStatus(deviceStatus))
        {
            appendDeviceLightLinksFromDoc(deviceLightLinks, deviceDoc);
        }

        static DynamicJsonDocument roomDoc(32768);
        static DynamicJsonDocument locationFilterDoc(640);
        locationFilterDoc.clear();
        JsonObject locationFilterRoot = locationFilterDoc.to<JsonObject>();
        JsonObject locationFilterData = locationFilterRoot["data"][0].to<JsonObject>();
        locationFilterData["id"] = true;
        locationFilterData["id_v1"] = true;
        locationFilterData["metadata"]["name"] = true;
        locationFilterData["name"] = true;
        locationFilterData["children"][0]["rid"] = true;
        locationFilterData["children"][0]["rtype"] = true;
        locationFilterData["services"][0]["rid"] = true;
        locationFilterData["services"][0]["rtype"] = true;
        locationFilterData["grouped_services"][0]["rid"] = true;
        locationFilterData["grouped_services"][0]["rtype"] = true;
        roomDoc.clear();
        int roomStatus = httpGet("/clip/v2/resource/room", roomDoc, &locationFilterDoc);
        if (isHttpSuccessStatus(roomStatus))
        {
            appendLocationsFromDoc(locations, roomDoc, true, deviceLightLinks);
        }

        static DynamicJsonDocument zoneDoc(32768);
        zoneDoc.clear();
        int zoneStatus = httpGet("/clip/v2/resource/zone", zoneDoc, &locationFilterDoc);
        if (isHttpSuccessStatus(zoneStatus))
        {
            appendLocationsFromDoc(locations, zoneDoc, false, deviceLightLinks);
        }

        const size_t roomItems = roomDoc["data"].is<JsonArrayConst>() ? roomDoc["data"].as<JsonArrayConst>().size() : 0;
        const size_t zoneItems = zoneDoc["data"].is<JsonArrayConst>() ? zoneDoc["data"].as<JsonArrayConst>().size() : 0;
        if (locations.empty() && (roomItems > 0 || zoneItems > 0))
        {
            Serial.printf("[HueGatewayClient] WARNING: No room/zone mapping created (roomItems=%u zoneItems=%u links=%u)\n",
                        static_cast<unsigned>(roomItems),
                        static_cast<unsigned>(zoneItems),
                        static_cast<unsigned>(deviceLightLinks.size()));
        }

        if (!locations.empty())
        {
            sCachedLocations = locations;
            sLocationCacheValidUntilMs = now + kLightLocationCacheMs;
        }
        else if (!sCachedLocations.empty())
        {
            locations = sCachedLocations;
        }
    }

    int count = 0;

    for (JsonObjectConst light : data)
    {
        if (count >= maxLights)
            break;

        lights[count].id = light["id"].as<String>();
        lights[count].name = light["metadata"]["name"].as<String>();
        lights[count].on = light["on"]["on"].as<bool>();
        String ownerRid = light["owner"]["rid"].as<String>();

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
        lights[count].roomRid = "-";
        lights[count].roomIdV1 = "-";
        lights[count].zone = "-";
        lights[count].zoneRid = "-";
        lights[count].zoneIdV1 = "-";

        for (const auto& location : locations)
        {
            if (location.id.equalsIgnoreCase(lights[count].id)
                || (ownerRid.length() > 0 && location.id.equalsIgnoreCase(ownerRid)))
            {
                if (location.room.length() > 0)
                {
                    lights[count].room = location.room;
                }
                if (location.roomRid.length() > 0)
                {
                    lights[count].roomRid = location.roomRid;
                }
                if (location.roomIdV1.length() > 0)
                {
                    lights[count].roomIdV1 = location.roomIdV1;
                }
                if (location.zone.length() > 0)
                {
                    lights[count].zone = location.zone;
                }
                if (location.zoneRid.length() > 0)
                {
                    lights[count].zoneRid = location.zoneRid;
                }
                if (location.zoneIdV1.length() > 0)
                {
                    lights[count].zoneIdV1 = location.zoneIdV1;
                }
                break;
            }
        }

        count++;
    }

    Serial.print("[HueGatewayClient] Found lights: ");
    Serial.println(count);
    return count;
}

int HueGatewayClient::getGroupedLights(HueGatewayLightState* groupedLights, int maxLights)
{
    if (!_initialized || groupedLights == nullptr || maxLights <= 0)
    {
        return 0;
    }

    static DynamicJsonDocument doc(32768);
    static DynamicJsonDocument groupedLightFilterDoc(640);
    groupedLightFilterDoc.clear();
    JsonObject groupedFilterRoot = groupedLightFilterDoc.to<JsonObject>();
    JsonObject groupedFilterData = groupedFilterRoot["data"][0].to<JsonObject>();
    groupedFilterData["id"] = true;
    groupedFilterData["metadata"]["name"] = true;
    groupedFilterData["on"]["on"] = true;
    groupedFilterData["dimming"]["brightness"] = true;
    groupedFilterData["color_temperature"]["mirek"] = true;
    groupedFilterData["color"]["xy"]["x"] = true;
    groupedFilterData["color"]["xy"]["y"] = true;
    doc.clear();
    int statusCode = httpGet("/clip/v2/resource/grouped_light", doc, &groupedLightFilterDoc);
    if (!isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] ERROR: GET grouped_light failed - HTTP %d\n", statusCode);
        return 0;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    int count = 0;
    for (JsonObjectConst item : data)
    {
        if (count >= maxLights)
        {
            break;
        }

        const char* id = item["id"] | "";
        if (id[0] == '\0')
        {
            continue;
        }

        groupedLights[count].id = String(id);
        groupedLights[count].name = item["metadata"]["name"].as<String>();
        groupedLights[count].on = item["on"]["on"].as<bool>();
        groupedLights[count].reachable = true;

        float brightnessPct = item["dimming"]["brightness"].as<float>();
        if (brightnessPct < 0.0f) brightnessPct = 0.0f;
        if (brightnessPct > 100.0f) brightnessPct = 100.0f;
        groupedLights[count].brightness = static_cast<uint8_t>(brightnessPct * 2.54f);

        groupedLights[count].supportsColorTemp = !item["color_temperature"].isNull();
        groupedLights[count].supportsColor = !item["color"].isNull();

        groupedLights[count].colorTempKelvin = 0;
        JsonVariantConst mirekVar = item["color_temperature"]["mirek"];
        if (!mirekVar.isNull())
        {
            groupedLights[count].colorTempKelvin = mirekToKelvin(mirekVar.as<uint16_t>());
        }

        groupedLights[count].red = 255;
        groupedLights[count].green = 255;
        groupedLights[count].blue = 255;
        JsonVariantConst xVar = item["color"]["xy"]["x"];
        JsonVariantConst yVar = item["color"]["xy"]["y"];
        if (!xVar.isNull() && !yVar.isNull())
        {
            xyToRgb(xVar.as<float>(), yVar.as<float>(), groupedLights[count].red, groupedLights[count].green, groupedLights[count].blue);
        }

        groupedLights[count].room = "-";
        groupedLights[count].roomRid = "-";
        groupedLights[count].roomIdV1 = "-";
        groupedLights[count].zone = "-";
        groupedLights[count].zoneRid = "-";
        groupedLights[count].zoneIdV1 = "-";

        count++;
    }

    Serial.printf("[HueGatewayClient] Found grouped_light targets: %d\n", count);
    return count;
}

void HueGatewayClient::appendDeviceLightLinksFromDoc(std::vector<DeviceLightLink>& links, const JsonDocument& doc)
{
    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    if (data.isNull())
    {
        data = doc.as<JsonArrayConst>();
    }
    if (data.isNull())
    {
        return;
    }

    for (JsonObjectConst item : data)
    {
        const char* deviceRid = item["id"] | "";
        if (deviceRid[0] == '\0')
        {
            continue;
        }

        auto collectFromRefs = [&](JsonArrayConst refs) {
            for (JsonVariantConst refVar : refs)
            {
                String rid;
                String rtype;
                if (!extractResourceRef(refVar, rid, rtype))
                {
                    continue;
                }

                if (!(rtype.equalsIgnoreCase("light") || rtype.indexOf("light") >= 0))
                {
                    continue;
                }

                DeviceLightLink link;
                link.deviceId = String(deviceRid);
                link.lightId = rid;
                links.push_back(link);
            }
        };

        collectFromRefs(item["services"].as<JsonArrayConst>());
        collectFromRefs(item["children"].as<JsonArrayConst>());
        collectFromRefs(item["grouped_services"].as<JsonArrayConst>());
    }
}

void HueGatewayClient::appendLocationsFromDoc(std::vector<LightLocation>& locations,
                                              const JsonDocument& doc,
                                              bool isRoom,
                                              const std::vector<DeviceLightLink>& deviceLightLinks)
{
    auto mapDeviceToLights = [&](const String& deviceRid,
                                 const String& locationName,
                                 const String& locationRid,
                                 const String& locationIdV1,
                                 bool roomLocation) {
        bool resolved = false;
        for (const auto& link : deviceLightLinks)
        {
            if (!link.deviceId.equalsIgnoreCase(deviceRid))
            {
                continue;
            }

            resolved = true;
            if (roomLocation)
            {
                upsertLocation(locations, link.lightId, locationName, locationRid, locationIdV1, "", "", "");
            }
            else
            {
                upsertLocation(locations, link.lightId, "", "", "", locationName, locationRid, locationIdV1);
            }
        }

        if (!resolved)
        {
            if (roomLocation)
            {
                upsertLocation(locations, deviceRid, locationName, locationRid, locationIdV1, "", "", "");
            }
            else
            {
                upsertLocation(locations, deviceRid, "", "", "", locationName, locationRid, locationIdV1);
            }
        }
    };

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    if (data.isNull())
    {
        data = doc.as<JsonArrayConst>();
    }
    if (data.isNull())
    {
        return;
    }

    for (JsonObjectConst item : data)
    {
        String locationName = item["metadata"]["name"].as<String>();
        if (locationName.length() == 0)
        {
            locationName = item["name"].as<String>();
        }
        if (locationName.length() == 0)
        {
            continue;
        }

        String locationRid = item["id"].as<String>();
        String locationIdV1 = item["id_v1"].as<String>();

        auto handleRefs = [&](JsonArrayConst refs) {
            for (JsonVariantConst refVar : refs)
            {
                String rid;
                String rawType;
                if (!extractResourceRef(refVar, rid, rawType))
                {
                    continue;
                }

                String rtype = rawType;
                rtype.toLowerCase();

                if (rtype == "light")
                {
                    if (isRoom)
                    {
                        upsertLocation(locations, rid, locationName, locationRid, locationIdV1, "", "", "");
                    }
                    else
                    {
                        upsertLocation(locations, rid, "", "", "", locationName, locationRid, locationIdV1);
                    }
                    continue;
                }

                if (rtype == "device")
                {
                    mapDeviceToLights(rid, locationName, locationRid, locationIdV1, isRoom);
                    continue;
                }

                bool matchedAsLight = false;
                for (const auto& link : deviceLightLinks)
                {
                    if (link.lightId.equalsIgnoreCase(rid))
                    {
                        if (isRoom)
                        {
                            upsertLocation(locations, link.lightId, locationName, locationRid, locationIdV1, "", "", "");
                        }
                        else
                        {
                            upsertLocation(locations, link.lightId, "", "", "", locationName, locationRid, locationIdV1);
                        }
                        matchedAsLight = true;
                        break;
                    }
                }

                if (!matchedAsLight)
                {
                    mapDeviceToLights(rid, locationName, locationRid, locationIdV1, isRoom);
                }
            }
        };

        handleRefs(item["children"].as<JsonArrayConst>());
        handleRefs(item["services"].as<JsonArrayConst>());
        handleRefs(item["grouped_services"].as<JsonArrayConst>());
    }

}

void HueGatewayClient::upsertLocation(std::vector<LightLocation>& locations,
                                      const String& id,
                                      const String& room,
                                      const String& roomRid,
                                      const String& roomIdV1,
                                      const String& zone,
                                      const String& zoneRid,
                                      const String& zoneIdV1)
{
    for (auto& loc : locations)
    {
        if (loc.id == id)
        {
            if (room.length() > 0)
            {
                loc.room = room;
            }
            if (roomRid.length() > 0)
            {
                loc.roomRid = roomRid;
            }
            if (roomIdV1.length() > 0)
            {
                loc.roomIdV1 = roomIdV1;
            }
            if (zone.length() > 0)
            {
                loc.zone = zone;
            }
            if (zoneRid.length() > 0)
            {
                loc.zoneRid = zoneRid;
            }
            if (zoneIdV1.length() > 0)
            {
                loc.zoneIdV1 = zoneIdV1;
            }
            return;
        }
    }

    LightLocation loc;
    loc.id = id;
    loc.room = room;
    loc.roomRid = roomRid;
    loc.roomIdV1 = roomIdV1;
    loc.zone = zone;
    loc.zoneRid = zoneRid;
    loc.zoneIdV1 = zoneIdV1;
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

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] Light %s -> %s\n", lightId.c_str(), on ? "ON" : "OFF");
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::setGroupedLightOnOff(const String& groupedLightId, bool on)
{
    if (!_initialized)
        return false;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(256);
    doc["on"]["on"] = on;

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> %s\n", groupedLightId.c_str(), on ? "ON" : "OFF");
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light on/off failed - HTTP %d\n", statusCode);
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
    
    if (isHttpSuccessStatus(statusCode))
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

bool HueGatewayClient::setGroupedLightBrightness(const String& groupedLightId, uint8_t brightness)
{
    if (!_initialized)
        return false;

    float brightnessPct = (brightness / 254.0f) * 100.0f;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(256);
    doc["dimming"]["brightness"] = brightnessPct;

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> Brightness: %d\n", groupedLightId.c_str(), brightness);
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light brightness failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::setLightState(const String& lightId, bool on, uint8_t brightness, uint8_t fadeDurationSec)
{
    if (!_initialized)
        return false;
    
    float brightnessPct = (brightness / 254.0f) * 100.0f;
    
    String endpoint = "/clip/v2/resource/light/" + lightId;
    
    DynamicJsonDocument doc(512);
    doc["on"]["on"] = on;
    doc["dimming"]["brightness"] = brightnessPct;
    if (fadeDurationSec > 0)
    {
        doc["dynamics"]["duration"] = static_cast<uint32_t>(fadeDurationSec) * 1000UL;
    }
    
    String payload;
    serializeJson(doc, payload);
    
    int statusCode = httpPut(endpoint, payload);
    
    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] Light %s -> On:%d Bri:%d fade:%us\n", 
                      lightId.c_str(), on, brightness, static_cast<unsigned>(fadeDurationSec));
        return true;
    }
    else
    {
        Serial.printf("[HueGatewayClient] ERROR: PUT state failed - HTTP %d\n", statusCode);
        return false;
    }
}

bool HueGatewayClient::setGroupedLightState(const String& groupedLightId, bool on, uint8_t brightness, uint8_t fadeDurationSec)
{
    if (!_initialized)
        return false;

    float brightnessPct = (brightness / 254.0f) * 100.0f;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(512);
    doc["on"]["on"] = on;
    doc["dimming"]["brightness"] = brightnessPct;
    if (fadeDurationSec > 0)
    {
        doc["dynamics"]["duration"] = static_cast<uint32_t>(fadeDurationSec) * 1000UL;
    }

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> On:%d Bri:%d fade:%us\n",
                      groupedLightId.c_str(), on, brightness, static_cast<unsigned>(fadeDurationSec));
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light state failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::setLightDimmingDelta(const String& lightId, bool brighter, uint8_t steps)
{
    if (!_initialized)
        return false;

    if (steps == 0)
    {
        return stopLightDimming(lightId);
    }

    if (steps > 7)
    {
        steps = 7;
    }

    // KNX DPT 3.007 step code mapping (Control Dimming):
    // 1=100%, 2=50%, 3=25%, 4=12.5%, 5=6.25%, 6=3.125%, 7=1.5625%
    float brightnessDeltaPct = dimmingStepCodeToPercent(steps);
    if (brightnessDeltaPct < 0.1f)
    {
        brightnessDeltaPct = 0.1f;
    }
    if (brightnessDeltaPct > 100.0f)
    {
        brightnessDeltaPct = 100.0f;
    }

    String endpoint = "/clip/v2/resource/light/" + lightId;

    DynamicJsonDocument doc(256);
    doc["dimming_delta"]["action"] = brighter ? "up" : "down";
    doc["dimming_delta"]["brightness_delta"] = brightnessDeltaPct;

    // Ensure "dim up" from OFF can activate the light similar to KNX behavior.
    if (brighter)
    {
        doc["on"]["on"] = true;
    }

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] Light %s -> Dimming delta: %s %u step(s) (%.1f%%)\n",
                      lightId.c_str(),
                      brighter ? "up" : "down",
                      static_cast<unsigned>(steps),
                      brightnessDeltaPct);
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT dimming_delta failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::setGroupedLightDimmingDelta(const String& groupedLightId, bool brighter, uint8_t steps)
{
    if (!_initialized)
        return false;

    if (steps == 0)
    {
        return stopGroupedLightDimming(groupedLightId);
    }

    if (steps > 7)
    {
        steps = 7;
    }

    float brightnessDeltaPct = dimmingStepCodeToPercent(steps);
    if (brightnessDeltaPct < 0.1f)
    {
        brightnessDeltaPct = 0.1f;
    }
    if (brightnessDeltaPct > 100.0f)
    {
        brightnessDeltaPct = 100.0f;
    }

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(256);
    doc["dimming_delta"]["action"] = brighter ? "up" : "down";
    doc["dimming_delta"]["brightness_delta"] = brightnessDeltaPct;

    if (brighter)
    {
        doc["on"]["on"] = true;
    }

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> Dimming delta: %s %u step(s) (%.1f%%)\n",
                      groupedLightId.c_str(), brighter ? "up" : "down", static_cast<unsigned>(steps), brightnessDeltaPct);
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light dimming delta failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::stopLightDimming(const String& lightId)
{
    if (!_initialized)
        return false;

    String endpoint = "/clip/v2/resource/light/" + lightId;

    DynamicJsonDocument doc(160);
    doc["dimming_delta"]["action"] = "stop";

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] Light %s -> Dimming stop\n", lightId.c_str());
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT dimming stop failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::stopGroupedLightDimming(const String& groupedLightId)
{
    if (!_initialized)
        return false;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(128);
    doc["dimming_delta"]["action"] = "stop";

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> Dimming STOP\n", groupedLightId.c_str());
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light dimming stop failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::extractGroupedLightRid(JsonObjectConst item, String& groupedLightRid)
{
    JsonArrayConst services = item["services"].as<JsonArrayConst>();
    for (JsonObjectConst service : services)
    {
        const char* rtype = service["rtype"] | "";
        if (strcmp(rtype, "grouped_light") != 0)
        {
            continue;
        }

        const char* rid = service["rid"] | "";
        if (rid[0] == '\0')
        {
            continue;
        }

        groupedLightRid = String(rid);
        return true;
    }

    return false;
}

bool HueGatewayClient::resolveGroupedLightForTarget(const String& endpoint, const String& targetRid, String& groupedLightRid, String& targetName)
{
    groupedLightRid = "";
    targetName = "";

    if (!_initialized || targetRid.length() == 0)
    {
        return false;
    }

    static DynamicJsonDocument doc(32768);
    static DynamicJsonDocument targetFilterDoc(512);
    targetFilterDoc.clear();
    JsonObject targetFilterRoot = targetFilterDoc.to<JsonObject>();
    JsonObject targetFilterData = targetFilterRoot["data"][0].to<JsonObject>();
    targetFilterData["id"] = true;
    targetFilterData["metadata"]["name"] = true;
    targetFilterData["services"][0]["rid"] = true;
    targetFilterData["services"][0]["rtype"] = true;
    targetFilterData["children"][0]["rid"] = true;
    targetFilterData["children"][0]["rtype"] = true;
    targetFilterData["grouped_services"][0]["rid"] = true;
    targetFilterData["grouped_services"][0]["rtype"] = true;
    doc.clear();

    int statusCode = httpGet(endpoint, doc, &targetFilterDoc);
    if (!isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] ERROR: GET %s failed - HTTP %d\n", endpoint.c_str(), statusCode);
        return false;
    }

    String lookup = targetRid;
    lookup.trim();
    String lookupLower = lookup;
    lookupLower.toLowerCase();

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    for (JsonObjectConst item : data)
    {
        String itemRid = item["id"].as<String>();
        String itemName = item["metadata"]["name"].as<String>();
        String itemNameLower = itemName;
        itemNameLower.toLowerCase();

        bool matchesId = itemRid.equalsIgnoreCase(lookup);
        bool matchesName = itemNameLower == lookupLower;
        if (!matchesId && !matchesName)
        {
            continue;
        }

        targetName = itemName;
        return extractGroupedLightRid(item, groupedLightRid);
    }

    return false;
}

bool HueGatewayClient::resolveGroupedLightForRoom(const String& roomRid, String& groupedLightRid, String& roomName)
{
    return resolveGroupedLightForTarget("/clip/v2/resource/room", roomRid, groupedLightRid, roomName);
}

bool HueGatewayClient::resolveGroupedLightForZone(const String& zoneRid, String& groupedLightRid, String& zoneName)
{
    return resolveGroupedLightForTarget("/clip/v2/resource/zone", zoneRid, groupedLightRid, zoneName);
}

int HueGatewayClient::getTargetsForEndpoint(const String& endpoint, HueGatewayTargetInfo* targets, int maxTargets)
{
    if (!_initialized || targets == nullptr || maxTargets <= 0)
    {
        return 0;
    }

    static DynamicJsonDocument doc(32768);
    static DynamicJsonDocument targetFilterDoc(512);
    targetFilterDoc.clear();
    JsonObject targetFilterRoot = targetFilterDoc.to<JsonObject>();
    JsonObject targetFilterData = targetFilterRoot["data"][0].to<JsonObject>();
    targetFilterData["id"] = true;
    targetFilterData["metadata"]["name"] = true;
    targetFilterData["services"][0]["rid"] = true;
    targetFilterData["services"][0]["rtype"] = true;
    targetFilterData["children"][0]["rid"] = true;
    targetFilterData["children"][0]["rtype"] = true;
    targetFilterData["grouped_services"][0]["rid"] = true;
    targetFilterData["grouped_services"][0]["rtype"] = true;
    doc.clear();

    int statusCode = httpGet(endpoint, doc, &targetFilterDoc);
    if (!isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] ERROR: GET %s failed - HTTP %d\n", endpoint.c_str(), statusCode);
        return 0;
    }

    JsonArrayConst data = doc["data"].as<JsonArrayConst>();
    int count = 0;

    for (JsonObjectConst item : data)
    {
        if (count >= maxTargets)
        {
            break;
        }

        String groupedRid;
        if (!extractGroupedLightRid(item, groupedRid) || groupedRid.length() == 0)
        {
            continue;
        }

        targets[count].id = item["id"].as<String>();
        targets[count].name = item["metadata"]["name"].as<String>();
        targets[count].groupedLightId = groupedRid;
        count++;
    }

    return count;
}

int HueGatewayClient::getRoomTargets(HueGatewayTargetInfo* targets, int maxTargets)
{
    return getTargetsForEndpoint("/clip/v2/resource/room", targets, maxTargets);
}

int HueGatewayClient::getZoneTargets(HueGatewayTargetInfo* targets, int maxTargets)
{
    return getTargetsForEndpoint("/clip/v2/resource/zone", targets, maxTargets);
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
    
    // Fade duration in milliseconds (Hue API v2 dynamics.duration).
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
    
    if (isHttpSuccessStatus(statusCode))
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

bool HueGatewayClient::setGroupedLightStateWithColorTemp(const String& groupedLightId,
                                                          bool on,
                                                          uint8_t brightness,
                                                          uint16_t mirek,
                                                          uint8_t fadeDurationSec)
{
    if (!_initialized)
        return false;

    uint16_t clampedMirek = mirek;
    if (clampedMirek < 153) clampedMirek = 153;
    if (clampedMirek > 500) clampedMirek = 500;

    float brightnessPct = (brightness / 254.0f) * 100.0f;
    uint32_t fadeDurationMs = fadeDurationSec * 1000UL;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(512);
    doc["on"]["on"] = on;
    doc["dimming"]["brightness"] = brightnessPct;
    doc["color_temperature"]["mirek"] = clampedMirek;
    if (fadeDurationSec > 0)
    {
        doc["dynamics"]["duration"] = fadeDurationMs;
    }

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> On:%d Bri:%d CT:%d fade:%ds\n",
                      groupedLightId.c_str(), on, brightness, clampedMirek, fadeDurationSec);
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light state+CT failed - HTTP %d\n", statusCode);
    return false;
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
    
    if (isHttpSuccessStatus(statusCode))
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

bool HueGatewayClient::setGroupedLightColorTemperature(const String& groupedLightId, uint16_t mirek)
{
    if (!_initialized)
        return false;

    uint16_t clampedMirek = mirek;
    if (clampedMirek < 153) clampedMirek = 153;
    if (clampedMirek > 500) clampedMirek = 500;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(256);
    doc["color_temperature"]["mirek"] = clampedMirek;

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> ColorTemp: %d mirek (%d K)\n",
                      groupedLightId.c_str(), clampedMirek, mirekToKelvin(clampedMirek));
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light color_temperature failed - HTTP %d\n", statusCode);
    return false;
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
    
    if (isHttpSuccessStatus(statusCode))
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

bool HueGatewayClient::setGroupedLightColor(const String& groupedLightId, float x, float y)
{
    if (!_initialized)
        return false;

    float clampedX = x;
    float clampedY = y;
    if (clampedX < 0.0f) clampedX = 0.0f;
    if (clampedX > 1.0f) clampedX = 1.0f;
    if (clampedY < 0.0f) clampedY = 0.0f;
    if (clampedY > 1.0f) clampedY = 1.0f;

    String endpoint = "/clip/v2/resource/grouped_light/" + groupedLightId;

    DynamicJsonDocument doc(256);
    doc["color"]["xy"]["x"] = clampedX;
    doc["color"]["xy"]["y"] = clampedY;

    String payload;
    serializeJson(doc, payload);

    int statusCode = httpPut(endpoint, payload);

    if (isHttpSuccessStatus(statusCode))
    {
        Serial.printf("[HueGatewayClient] GroupedLight %s -> Color XY: (%.3f, %.3f)\n",
                      groupedLightId.c_str(), clampedX, clampedY);
        return true;
    }

    Serial.printf("[HueGatewayClient] ERROR: PUT grouped_light color failed - HTTP %d\n", statusCode);
    return false;
}

bool HueGatewayClient::pingBridgeApiV2()
{
    if (!_initialized)
    {
        return false;
    }

    DynamicJsonDocument doc(1024);
    int statusCode = httpGet("/clip/v2/resource/bridge", doc);
    if (isHttpSuccessStatus(statusCode))
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

    if (!hasTlsInternalHeadroom())
    {
        Serial.printf("[HueGatewayClient] EventStream deferred: insufficient internal TLS headroom (need free>=%lu, largest>=%lu)\n",
                      static_cast<unsigned long>(kTlsMinInternalFreeBytes),
                      static_cast<unsigned long>(kTlsMinInternalLargestBlockBytes));
        logHeapStats("event-connect-deferred");
        return false;
    }

    stopEventStream();

    _eventClient.setTimeout(100);
    Serial.printf("[HueGatewayClient] EventStream connecting to %s:443\n", _bridgeIP.c_str());
    logHeapStats("before-event-connect");
    if (!_eventClient.connect(_bridgeIP.c_str(), 443))
    {
        Serial.println("[HueGatewayClient] EventStream connect failed");
        logHeapStats("event-connect-failed");
        return false;
    }

    _eventClient.printf("GET /eventstream/clip/v2 HTTP/1.1\r\n");
    _eventClient.printf("Host: %s\r\n", _bridgeIP.c_str());
    _eventClient.printf("hue-application-key: %s\r\n", _appKey.c_str());
    _eventClient.print("Accept: text/event-stream\r\n");
    _eventClient.print("Connection: keep-alive\r\n\r\n");

    _eventHandshakePending = true;
    _eventHandshakeStartMs = millis();
    _eventLastDataMs = _eventHandshakeStartMs;
    _eventParseErrorStreak = 0;
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
    _eventParseErrorStreak = 0;
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
        int statusCode = parseHttpStatusCode(statusLine);
        if (!isHttpSuccessStatus(statusCode))
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
        _eventLastDataMs = millis();
        char ch = static_cast<char>(_eventClient.read());
        if (ch == '\r')
        {
            continue;
        }

        if (ch != '\n')
        {
            _eventLineBuffer += ch;
            if (_eventLineBuffer.length() > kMaxEventLineChars)
            {
                Serial.println("[HueGatewayClient] EventStream line exceeded limit, dropping line");
                _eventDropCount++;
                _eventParseErrorStreak = min<uint8_t>(static_cast<uint8_t>(_eventParseErrorStreak + 1), static_cast<uint8_t>(10));
                _eventLineBuffer = "";
                if (_eventParseErrorStreak >= 3)
                {
                    Serial.println("[HueGatewayClient] EventStream parser unstable, forcing reconnect");
                    stopEventStream();
                    return updateCount;
                }
            }
            continue;
        }

        if (_eventLineBuffer.length() == 0)
        {
            if (_eventDataBuffer.length() > 0)
            {
                int parsedCount = parseEventPayload(_eventDataBuffer, updates + updateCount, maxUpdates - updateCount);
                if (parsedCount < 0)
                {
                    _eventDropCount++;
                    _eventParseErrorStreak = min<uint8_t>(static_cast<uint8_t>(_eventParseErrorStreak + 1), static_cast<uint8_t>(10));
                    if (_eventParseErrorStreak >= 3)
                    {
                        Serial.println("[HueGatewayClient] EventStream parse failures repeated, forcing reconnect");
                        stopEventStream();
                        return updateCount;
                    }
                }
                else
                {
                    updateCount += parsedCount;
                    _eventParseErrorStreak = 0;
                }
                _eventDataBuffer = "";
            }
        }
        else if (_eventLineBuffer.startsWith("data:"))
        {
            String dataPart = _eventLineBuffer.substring(5);
            dataPart.trim();
            _eventDataBuffer += dataPart;
            if (_eventDataBuffer.length() > kMaxEventPayloadChars)
            {
                Serial.println("[HueGatewayClient] Event payload exceeded limit, dropping payload");
                _eventDropCount++;
                _eventParseErrorStreak = min<uint8_t>(static_cast<uint8_t>(_eventParseErrorStreak + 1), static_cast<uint8_t>(10));
                _eventDataBuffer = "";
                if (_eventParseErrorStreak >= 3)
                {
                    Serial.println("[HueGatewayClient] EventStream payload drops repeated, forcing reconnect");
                    stopEventStream();
                    return updateCount;
                }
            }
        }

        _eventLineBuffer = "";
    }

    if (!_eventClient.connected())
    {
        Serial.println("[HueGatewayClient] EventStream disconnected");
        stopEventStream();
    }
    else if (_eventStreamConnected && _eventLastDataMs != 0 && (millis() - _eventLastDataMs) > 180000UL)
    {
        Serial.println("[HueGatewayClient] EventStream stale for >180s, reconnecting");
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

    if (payload.length() > kMaxEventPayloadChars)
    {
        Serial.printf("[HueGatewayClient] Event payload too large: %u bytes\n", static_cast<unsigned>(payload.length()));
        return -1;
    }

    static DynamicJsonDocument doc(6144);
    doc.clear();
    DeserializationError error = deserializeJson(doc, payload);
    if (error)
    {
        Serial.printf("[HueGatewayClient] Event payload JSON parse error: %s\n", error.c_str());
        return -1;
    }

    JsonArrayConst events = doc.as<JsonArrayConst>();
    int updateCount = 0;

    for (JsonObjectConst eventObj : events)
    {
        JsonArrayConst data = eventObj["data"].as<JsonArrayConst>();
        for (JsonObjectConst item : data)
        {
            const char* type = item["type"] | "";
            if (strcmp(type, "light") != 0 && strcmp(type, "grouped_light") != 0)
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

int HueGatewayClient::httpGet(const String& endpoint, JsonDocument& doc, JsonDocument* filterDoc)
{
    String url = buildUrl(endpoint);
    Serial.print("[HueGatewayClient] HTTP GET ");
    Serial.println(url);
    _http.setTimeout(2000);
    const bool eventWasActive = (_eventHandshakePending || _eventStreamConnected) && _eventClient.connected();

    if (eventWasActive)
    {
        Serial.println("[HueGatewayClient] Pausing EventStream for HTTPS GET");
        _http.end();
        stopEventStream();
        _secureClient.stop();
        delay(25);
    }
    
    _http.useHTTP10(true);
    _http.begin(_secureClient, url);
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.GET();

    if (statusCode < 0)
    {
        logHeapStats("http-get-failed");
    }

    if (isHttpSuccessStatus(statusCode))
    {
        doc.clear();
        Stream& responseStream = _http.getStream();
        const int contentLength = _http.getSize();

        DeserializationError error;
        if (contentLength >= 0)
        {
            if (filterDoc != nullptr)
            {
                error = deserializeJson(doc,
                                        responseStream,
                                        DeserializationOption::Filter(filterDoc->as<JsonVariantConst>()));
            }
            else
            {
                error = deserializeJson(doc, responseStream);
            }
        }
        else
        {
            String response = _http.getString();
            if (filterDoc != nullptr)
            {
                error = deserializeJson(doc,
                                        response,
                                        DeserializationOption::Filter(filterDoc->as<JsonVariantConst>()));
            }
            else
            {
                error = deserializeJson(doc, response);
            }
        }

        if (error)
        {
            Serial.print("[HueGatewayClient] JSON parse error: ");
            Serial.println(error.c_str());
            Serial.print("[HueGatewayClient] JSON payload length: ");
            if (contentLength >= 0)
            {
                Serial.println(contentLength);
            }
            else
            {
                Serial.println("unknown");
            }
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

    if (eventWasActive)
    {
        if (!hasTlsInternalHeadroom())
        {
            Serial.println("[HueGatewayClient] EventStream restart deferred after HTTP GET (internal TLS headroom)");
        }
        else if (!startEventStream())
        {
            Serial.println("[HueGatewayClient] EventStream restart after HTTP GET failed");
        }
    }

    return statusCode;
}

int HueGatewayClient::httpPut(const String& endpoint, const String& payload)
{
    String url = buildUrl(endpoint);
    Serial.printf("[HueGatewayClient] HTTP PUT %s payload=%s\n", url.c_str(), payload.c_str());
    _http.setTimeout(2000);
    const bool eventWasActive = (_eventHandshakePending || _eventStreamConnected) && _eventClient.connected();
    bool eventPausedForPut = false;

    if (eventWasActive && !hasTlsInternalHeadroom())
    {
        Serial.println("[HueGatewayClient] Pausing EventStream for HTTPS PUT (internal TLS headroom low)");
        _http.end();
        stopEventStream();
        _secureClient.stop();
        delay(25);
        eventPausedForPut = true;
    }
    
    _http.begin(_secureClient, url);
    _http.addHeader("Content-Type", "application/json");
    _http.addHeader("hue-application-key", _appKey);
    
    int statusCode = _http.PUT(payload);

    if (statusCode < 0)
    {
        logHeapStats("http-put-failed");
    }

    if (!isHttpSuccessStatus(statusCode))
    {
        String response = _http.getString();
        Serial.printf("[HueGatewayClient] HTTP PUT failed (%d): %s\n", statusCode, response.c_str());
    }
    
    _http.end();

    if (eventPausedForPut)
    {
        if (!hasTlsInternalHeadroom())
        {
            Serial.println("[HueGatewayClient] EventStream restart deferred after HTTP PUT (internal TLS headroom)");
        }
        else if (!startEventStream())
        {
            Serial.println("[HueGatewayClient] EventStream restart after HTTP PUT failed");
        }
    }

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

