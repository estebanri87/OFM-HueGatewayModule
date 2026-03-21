#pragma once

#include <Arduino.h>

// Forward declaration — avoid pulling OpenKNX headers into device headers
class GroupObject;

/**
 * @brief Abstract base class for all Hue device channel abstractions.
 *
 * Each configured channel in the module is backed by one HueGatewayDevice
 * subclass instance.  The module operates on the base pointer for
 * lifecycle (loop, sendStatusToKnx) and uses deviceType() to decide
 * which concrete cast is safe for type-specific operations.
 *
 * Device type values:
 *   0 = Licht          (HueGatewayLight)
 *   1 = Bewegungsmelder (HueGatewaySensor)
 *   2 = Taster/Schalter (HueGatewayButton)
 *   3 = Kontaktsensor  (HueGatewayContact)
 *   4 = Steckdose      (HueGatewayPlug)
 */
class HueGatewayDevice
{
public:
    virtual ~HueGatewayDevice() = default;

    /** Returns the device type discriminator (0-4). */
    virtual uint8_t deviceType() const = 0;

    /**
     * @brief Periodic loop — called every module loop cycle.
     * Drives continuous tasks such as HCL interpolation (lights)
     * or polling fallback timers.
     */
    virtual void loop() {}

    /**
     * @brief Pushes all cached state as KNX status telegrams.
     * Called after polling or event updates.
     */
    virtual void sendStatusToKnx() {}

    /**
     * @brief Called by the module for KNX input KOs directed at this channel.
     * @param koType KO slot index within the channel block (0-10).
     * @param ko     The GroupObject that was written.
     * @return true if the input was handled (and possibly forwarded to Hue).
     */
    virtual bool processKoInput(uint8_t koType, GroupObject& ko) { return false; };

    // ----------------------------------------------------------------
    // Diagnostics helpers (default implementations return "unknown" state)
    // ----------------------------------------------------------------

    /** Timestamp of the most recent successful Hue API write. */
    virtual unsigned long getLastHueWriteSuccessMs() const { return 0; }

    /** Human-readable device or channel name. */
    virtual String getName() const { return String(""); }

    /** Primary Hue resource-ID for this channel. */
    virtual String getResourceId() const { return String(""); }

    /** True when this channel targets a grouped resource (room/zone). */
    virtual bool isGroupedTarget() const { return false; }

    // ----------------------------------------------------------------
    // HCL support — no-ops for all non-light device types
    // ----------------------------------------------------------------
    virtual void setHCLMaster(uint8_t) {}
    virtual uint8_t getHCLMaster() const { return 0; }
    virtual void setHCLChannelLock(bool) {}
    virtual bool isHCLChannelLocked() const { return false; }

    // ----------------------------------------------------------------
    // Configuration setters — no-ops for non-light devices
    // ----------------------------------------------------------------
    virtual void setGroupedTarget(bool) {}
    virtual void setMinBrightness(uint8_t) {}
    virtual void setSwitchTransitionDurations(uint8_t, uint8_t) {}
    virtual uint8_t getSwitchOnTransitionSec() const { return 0; }
    virtual uint8_t getSwitchOffTransitionSec() const { return 0; }

    // ----------------------------------------------------------------
    // Light state accessors — only valid for deviceType()==0
    // ----------------------------------------------------------------
    virtual bool isOn() const { return false; }
    virtual uint8_t getBrightness() const { return 0; }
    virtual bool isReachable() const { return false; }
};
