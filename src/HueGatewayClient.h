#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <vector>

/**
 * @brief Hue API v2 Client
 * 
 * Kommuniziert mit der Hue Bridge über die Hue API v2.
 * Unterstützt:
 * - Abrufen von Geräten (Lights, Grouped Lights, etc.)
 * - Steuern von Lichtern (On/Off, Brightness, Color)
 * - Fehlerbehandlung und Retry-Logik
 * 
 * API v2 Dokumentation: https://developers.meethue.com/develop/hue-api-v2/
 */

struct HueGatewayLightState
{
    bool on;
    uint8_t brightness;  // 0-254 (Hue API Range)
    bool reachable;
    String id;
    String name;
    String room;
    String zone;
};

class HueGatewayClient
{
public:
    HueGatewayClient();
    ~HueGatewayClient();
    
    /**
     * @brief Initialisiert den Client mit Bridge-Daten
     * @param bridgeIP IP-Adresse der Bridge
     * @param appKey Application Key (von HueGatewayAuth)
     * @return true bei Erfolg
     */
    bool begin(const String& bridgeIP, const String& appKey);
    
    /**
     * @brief Ruft alle Lichter von der Bridge ab
     * @param lights Array für Light States (wird gefüllt)
     * @param maxLights Maximale Anzahl (Array-Größe)
     * @return Anzahl der gefundenen Lichter
     */
    int getLights(HueGatewayLightState* lights, int maxLights);
    
    /**
     * @brief Schaltet ein Licht ein/aus
     * @param lightId Light Resource ID (z.B. "abc12345-...")
     * @param on true = ein, false = aus
     * @return true bei Erfolg
     */
    bool setLightOnOff(const String& lightId, bool on);
    
    /**
     * @brief Setzt die Helligkeit eines Lichts
     * @param lightId Light Resource ID
     * @param brightness Helligkeit 0-254
     * @return true bei Erfolg
     */
    bool setLightBrightness(const String& lightId, uint8_t brightness);
    
    /**
     * @brief Setzt On/Off und Helligkeit gleichzeitig
     * @param lightId Light Resource ID
     * @param on true = ein
     * @param brightness Helligkeit 0-254
     * @return true bei Erfolg
     */
    bool setLightState(const String& lightId, bool on, uint8_t brightness);
    
    /**
     * @brief Setzt die Farbtemperatur eines Lichts
     * @param lightId Light Resource ID
     * @param mirek Farbtemperatur in Mirek (153-500, wird geclampt)
     *              153 = kalt/6536K, 500 = warm/2000K
     * @return true bei Erfolg
     */
    bool setLightColorTemperature(const String& lightId, uint16_t mirek);
    
    /**
     * @brief Setzt die XY-Farbe eines Lichts (CIE 1931 Farbraum)
     * @param lightId Light Resource ID
     * @param x X-Koordinate (0.0-1.0, wird geclampt)
     * @param y Y-Koordinate (0.0-1.0, wird geclampt)
     * @return true bei Erfolg
     */
    bool setLightColor(const String& lightId, float x, float y);
    
    /**
     * @brief Konvertiert Kelvin zu Mirek
     * @param kelvin Farbtemperatur in Kelvin (2000-6536)
     * @return Mirek-Wert (153-500)
     */
    static uint16_t kelvinToMirek(uint16_t kelvin);
    
    /**
     * @brief Konvertiert Mirek zu Kelvin
     * @param mirek Farbtemperatur in Mirek (153-500)
     * @return Kelvin-Wert (2000-6536)
     */
    static uint16_t mirekToKelvin(uint16_t mirek);
    
    /**
     * @brief Prüft ob Client initialisiert ist
     */
    bool isInitialized() const { return _initialized; }
    
    /**
     * @brief Gibt die Bridge IP zurück
     */
    String getBridgeIP() const { return _bridgeIP; }

private:
    bool _initialized;
    String _bridgeIP;
    String _appKey;
    HTTPClient _http;
    WiFiClientSecure _secureClient;

    struct LightLocation
    {
        String id;
        String room;
        String zone;
    };
    
    /**
     * @brief HTTP GET Request
     * @param endpoint API Endpoint (z.B. "/clip/v2/resource/light")
     * @param doc JsonDocument für Response
     * @return HTTP Status Code
     */
    int httpGet(const String& endpoint, JsonDocument& doc);
    
    /**
     * @brief HTTP PUT Request
     * @param endpoint API Endpoint
     * @param payload JSON Payload
     * @return HTTP Status Code
     */
    int httpPut(const String& endpoint, const String& payload);
    
    /**
     * @brief Erstellt volle URL
     */
    String buildUrl(const String& endpoint);

    void appendLocationsFromDoc(std::vector<LightLocation>& locations, const JsonDocument& doc, bool isRoom);
    void upsertLocation(std::vector<LightLocation>& locations, const String& id, const String& room, const String& zone);
};

