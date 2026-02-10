#pragma once

#include <Arduino.h>

// Forward Declaration
class HueGatewayClient;

/**
 * @brief Einzelnes Hue Licht mit KNX-Mapping
 * 
 * Repräsentiert ein Philips Hue Licht und verwaltet:
 * - Bidirektionale Synchronisation zwischen KNX und Hue
 * - Status-Caching für Performance
 * - KO-Mapping (Switch, Brightness, etc.)
 * 
 * KNX → Hue: processKnxUpdate()
 * Hue → KNX: updateFromHue()
 */

class HueGatewayLight
{
public:
    /**
     * @brief Konstruktor
     * @param lightId Hue Light Resource ID
     * @param name Gerätename
     * @param client Pointer auf HueGatewayClient
     */
    HueGatewayLight(const String& lightId, const String& name, HueGatewayClient* client);
    ~HueGatewayLight();
    
    /**
     * @brief Initialisierung
     * @param koSwitch KO-Nummer für Schalten (Ein/Aus)
     * @param koBrightness KO-Nummer für Helligkeit absolut (DPT 5.001, 0-100%)
     * @param koDimming KO-Nummer für Helligkeit relativ (DPT 3.007, 4-Bit)
     * @param koStatusSwitch KO-Nummer für Status-Rückmeldung Ein/Aus (DPT 1.001)
     * @param koStatusBrightness KO-Nummer für Status-Rückmeldung Helligkeit (DPT 5.001)
     */
    void begin(uint16_t koSwitch, uint16_t koBrightness, uint16_t koDimming, 
               uint16_t koStatusSwitch, uint16_t koStatusBrightness);
    
    /**
     * @brief Verarbeitet KNX-Update (Schalten)
     * @param value true = Ein, false = Aus
     */
    void processKnxSwitch(bool value);
    
    /**
     * @brief Verarbeitet KNX-Update (Helligkeit in %)
     * @param value Helligkeit 0-100% (DPT 5.001 - Absolut)
     */
    void processKnxBrightness(uint8_t value);
    
    /**
     * @brief Verarbeitet KNX-Update (Relatives Dimmen)
     * @param control DPT 3.007 Dimm-Steuerung (4-Bit: 3-Bit Schritte + 1-Bit Richtung)
     *               Bit 3: 0=dunkler, 1=heller
     *               Bit 0-2: Anzahl Schritte (0=Stop, 1-7=Schritte)
     */
    void processKnxDimming(uint8_t control);
    
    /**
     * @brief Aktualisiert lokalen Status von Hue Bridge
     * Wird vom Event Stream oder periodisch aufgerufen
     * @param on Schaltzustand
     * @param brightness Helligkeit 0-254 (Hue Range)
     */
    void updateFromHue(bool on, uint8_t brightness);
    
    /**
     * @brief Sendet aktuellen Status an KNX
     * Schreibt Werte in KOs
     */
    void sendStatusToKnx();
    
    /**
     * @brief Loop-Methode für kontinuierliche HCL-Updates
     * Prüft periodisch, ob HCL-Werte sich geändert haben und wendet sie an
     */
    void loop();
    
    /**
     * @brief Abrufen des Light IDs
     */
    String getLightId() const { return _lightId; }
    
    /**
     * @brief Abrufen des Namens
     */
    String getName() const { return _name; }
    
    /**
     * @brief Status-Abfrage
     */
    bool isOn() const { return _on; }
    uint8_t getBrightness() const { return _brightness; }
    bool isReachable() const { return _reachable; }
    
    /**
     * @brief HCL Master zuordnen (0 = kein HCL, 1-4 = Master Nr.)
     */
    void setHCLMaster(uint8_t masterNum) { _hclMasterNum = masterNum; }
    
    /**
     * @brief HCL Master Nummer abrufen
     */
    uint8_t getHCLMaster() const { return _hclMasterNum; }

private:
    String _lightId;
    String _name;
    HueGatewayClient* _client;
    
    // KO-Nummern
    uint16_t _koSwitch;
    uint16_t _koBrightness;
    uint16_t _koDimming;
    uint16_t _koStatusSwitch;
    uint16_t _koStatusBrightness;
    
    // Alte Status-KO (deprecated)
    uint16_t _koStatus;
    
    // Status-Cache
    bool _on;
    uint8_t _brightness;  // 0-254 (Hue API Range)
    bool _reachable;
    
    // HCL Configuration
    uint8_t _hclMasterNum;  // 0 = kein HCL, 1-4 = HCL Master Nummer
    bool _fadingActive;     // true wenn gerade ein Fade läuft
    uint16_t _currentKelvin; // Aktuelle Farbtemperatur in Kelvin (für HCL)
    unsigned long _lastHCLUpdate; // Zeitstempel des letzten HCL-Updates (millis())
    uint8_t _lastHCLBrightness; // Letzte angewendete HCL-Helligkeit (%)
    
    // Flags
    bool _initialized;
    unsigned long _lastUpdate;
    
    /**
     * @brief Sendet Update an Hue Bridge
     */
    void sendToHue();
    
    /**
     * @brief Sendet Update mit Farbtemperatur an Hue Bridge
     * @param kelvin Farbtemperatur in Kelvin (2000-6500)
     * @param fadeDuration Überblendzeit in Sekunden (0 = sofort)
     */
    void sendToHueWithColorTemp(uint16_t kelvin, uint8_t fadeDuration = 0);
    
    /**
     * @brief Konvertiert Kelvin zu mirek (Micro Reciprocal Kelvin)
     * @param kelvin Farbtemperatur in Kelvin (2000-6500)
     * @return mirek-Wert (153-500)
     */
    static uint16_t kelvinToMirek(uint16_t kelvin);
    
    /**
     * @brief Konvertiert KNX-Brightness (0-255) zu Hue (0-254)
     */
    uint8_t knxToHueBrightness(uint8_t knxValue);
    
    /**
     * @brief Konvertiert Hue-Brightness (0-254) zu KNX (0-255)
     */
    uint8_t hueToKnxBrightness(uint8_t hueValue);
};

