#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <vector>

/**
 * @brief Hue API v2 Client
 * 
 * Communicates with the Hue Bridge using Hue API v2.
 * Supports:
 * - Fetching devices (lights, grouped lights, etc.)
 * - Controlling lights (on/off, brightness, color)
 * - Error handling and retry logic
 * 
 * API v2 documentation: https://developers.meethue.com/develop/hue-api-v2/
 */

struct HueGatewayLightState
{
    bool on;
    uint8_t brightness;  // 0-254 (Hue API Range)
    bool reachable;
    uint16_t colorTempKelvin;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    bool supportsColorTemp;
    bool supportsColor;
    String id;
    String name;
    String room;
    String roomRid;
    String roomIdV1;
    String zone;
    String zoneRid;
    String zoneIdV1;
};

struct HueGatewayEventLightUpdate
{
    String lightId;
    bool hasOn;
    bool on;
    bool hasBrightness;
    uint8_t brightness;
    bool hasColorTemp;
    uint16_t colorTempKelvin;
    bool hasColorRgb;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
};

struct HueGatewayTargetInfo
{
    String id;
    String name;
    String groupedLightId;
};

class HueGatewayClient
{
public:
    HueGatewayClient();
    ~HueGatewayClient();
    
    /**
    * @brief Initializes the client with bridge connection data.
    * @param bridgeIP Bridge IP address
    * @param appKey Application key (provided by HueGatewayAuth)
    * @return true on success
     */
    bool begin(const String& bridgeIP, const String& appKey);
    
    /**
    * @brief Fetches all lights from the bridge.
    * @param lights Array that receives light states
    * @param maxLights Maximum number of entries (array size)
    * @return number of discovered lights
     */
    int getLights(HueGatewayLightState* lights, int maxLights);
    int getGroupedLights(HueGatewayLightState* groupedLights, int maxLights);
    
    /**
    * @brief Switches a light on or off.
     * @param lightId Light Resource ID (z.B. "abc12345-...")
    * @param on true = on, false = off
    * @return true on success
     */
    bool setLightOnOff(const String& lightId, bool on);
    bool setGroupedLightOnOff(const String& groupedLightId, bool on);
    
    /**
    * @brief Sets the brightness of a light.
     * @param lightId Light Resource ID
    * @param brightness Brightness 0-254
    * @return true on success
     */
    bool setLightBrightness(const String& lightId, uint8_t brightness);
    bool setGroupedLightBrightness(const String& groupedLightId, uint8_t brightness);
    
    /**
    * @brief Sets on/off and brightness in one request.
     * @param lightId Light Resource ID
    * @param on true = on
    * @param brightness Brightness 0-254
    * @param fadeDurationSec Transition duration in seconds (0 = immediate)
    * @return true on success
     */
    bool setLightState(const String& lightId, bool on, uint8_t brightness, uint8_t fadeDurationSec = 0);
    bool setGroupedLightState(const String& groupedLightId, bool on, uint8_t brightness, uint8_t fadeDurationSec = 0);

    /**
    * @brief Applies relative dimming delta to a light.
    * @param lightId Light Resource ID
    * @param brighter true = up, false = down
    * @param steps KNX 3.007 step count (1-7)
    * @return true on success
     */
    bool setLightDimmingDelta(const String& lightId, bool brighter, uint8_t steps);
    bool setGroupedLightDimmingDelta(const String& groupedLightId, bool brighter, uint8_t steps);

    /**
    * @brief Stops an active relative dimming action on the light.
    * @param lightId Light Resource ID
    * @return true on success
     */
    bool stopLightDimming(const String& lightId);
    bool stopGroupedLightDimming(const String& groupedLightId);
    
    /**
    * @brief Sets on/off, brightness, and color temperature with fade duration.
     * @param lightId Light Resource ID
    * @param on true = on
    * @param brightness Brightness 0-254
    * @param mirek Color temperature in mirek (153-500, clamped)
    * @param fadeDurationSec Fade duration in seconds (0 = instant)
    * @return true on success
     */
    bool setLightStateWithColorTemp(const String& lightId, bool on, uint8_t brightness, 
                                    uint16_t mirek, uint8_t fadeDurationSec);
    bool setGroupedLightStateWithColorTemp(const String& groupedLightId, bool on, uint8_t brightness,
                                           uint16_t mirek, uint8_t fadeDurationSec);
    
    /**
    * @brief Sets the color temperature of a light.
     * @param lightId Light Resource ID
    * @param mirek Color temperature in mirek (153-500, clamped)
    *              153 = cold/6536K, 500 = warm/2000K
    * @return true on success
     */
    bool setLightColorTemperature(const String& lightId, uint16_t mirek);
    bool setGroupedLightColorTemperature(const String& groupedLightId, uint16_t mirek);
    
    /**
    * @brief Sets the XY color of a light (CIE 1931 color space).
     * @param lightId Light Resource ID
    * @param x X coordinate (0.0-1.0, clamped)
    * @param y Y coordinate (0.0-1.0, clamped)
    * @return true on success
     */
    bool setLightColor(const String& lightId, float x, float y);
    bool setGroupedLightColor(const String& groupedLightId, float x, float y);

    bool pingBridgeApiV2();

    bool startEventStream();
    void stopEventStream();
    bool isEventStreamConnected() { return _eventStreamConnected && _eventClient.connected(); }
    int pollEventStream(HueGatewayEventLightUpdate* updates, int maxUpdates);
    
    /**
    * @brief Converts Kelvin to mirek.
    * @param kelvin Color temperature in Kelvin (2000-6536)
    * @return mirek value (153-500)
     */
    static uint16_t kelvinToMirek(uint16_t kelvin);
    
    /**
    * @brief Converts mirek to Kelvin.
    * @param mirek Color temperature in mirek (153-500)
    * @return Kelvin value (2000-6536)
     */
    static uint16_t mirekToKelvin(uint16_t mirek);
    
    /**
    * @brief Returns whether the client has been initialized.
     */
    bool isInitialized() const { return _initialized; }
    
    /**
    * @brief Returns the configured bridge IP.
     */
    String getBridgeIP() const { return _bridgeIP; }

    bool resolveGroupedLightForRoom(const String& roomRid, String& groupedLightRid, String& roomName);
    bool resolveGroupedLightForZone(const String& zoneRid, String& groupedLightRid, String& zoneName);
    int getRoomTargets(HueGatewayTargetInfo* targets, int maxTargets);
    int getZoneTargets(HueGatewayTargetInfo* targets, int maxTargets);

private:
    bool _initialized;
    String _bridgeIP;
    String _appKey;
    HTTPClient _http;
    WiFiClientSecure _secureClient;
    WiFiClientSecure _eventClient;
    bool _eventStreamConnected;
    bool _eventHandshakePending;
    unsigned long _eventHandshakeStartMs;
    unsigned long _eventLastDataMs;
    uint8_t _eventParseErrorStreak;
    uint32_t _eventDropCount;
    String _eventLineBuffer;
    String _eventDataBuffer;

    struct LightLocation
    {
        String id;
        String room;
        String roomRid;
        String roomIdV1;
        String zone;
        String zoneRid;
        String zoneIdV1;
    };

    struct DeviceLightLink
    {
        String deviceId;
        String lightId;
    };
    
    /**
    * @brief Executes an HTTP GET request.
     * @param endpoint API Endpoint (z.B. "/clip/v2/resource/light")
    * @param doc JsonDocument for the response payload
     * @return HTTP Status Code
     */
    int httpGet(const String& endpoint, JsonDocument& doc, JsonDocument* filterDoc = nullptr);
    
    /**
    * @brief Executes an HTTP PUT request.
     * @param endpoint API Endpoint
     * @param payload JSON Payload
     * @return HTTP Status Code
     */
    int httpPut(const String& endpoint, const String& payload);
    
    /**
    * @brief Builds the full request URL.
     */
    String buildUrl(const String& endpoint);
    static void xyToRgb(float x, float y, uint8_t& red, uint8_t& green, uint8_t& blue);
    int parseEventPayload(const String& payload, HueGatewayEventLightUpdate* updates, int maxUpdates);

    void appendDeviceLightLinksFromDoc(std::vector<DeviceLightLink>& links, const JsonDocument& doc);
    void appendLocationsFromDoc(std::vector<LightLocation>& locations,
                                const JsonDocument& doc,
                                bool isRoom,
                                const std::vector<DeviceLightLink>& deviceLightLinks);
    void upsertLocation(std::vector<LightLocation>& locations,
                        const String& id,
                        const String& room,
                        const String& roomRid,
                        const String& roomIdV1,
                        const String& zone,
                        const String& zoneRid,
                        const String& zoneIdV1);
    bool resolveGroupedLightForTarget(const String& endpoint, const String& targetRid, String& groupedLightRid, String& targetName);
    static bool extractGroupedLightRid(JsonObjectConst item, String& groupedLightRid);
    int getTargetsForEndpoint(const String& endpoint, HueGatewayTargetInfo* targets, int maxTargets);
};

