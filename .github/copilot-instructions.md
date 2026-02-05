# OFM-HueModule AI Coding Instructions

## Project Overview
OpenKNX Funktionsmodul (OFM) that bridges Philips Hue devices (lights, LED strips) to KNX bus systems via ESP32. **Status: Early development (v0.1.0)** - many TODOs remain from initial scaffolding.

**Data Flow**: `Hue Bridge ↔ ESP32/OFM-HueModule ↔ KNX Bus`

## Architecture & Conventions

### Module Structure (OpenKNX Pattern)
- **Global instance pattern**: `HueModule hueModule;` declared in `.cpp`, externed in `.h` (see [HueModule.cpp](../src/HueModule.cpp#L4))
- **Lifecycle methods**: `setup()` called once at boot, `loop()` continuously, `processInputKo()` for KNX communication objects
- **Module metadata**: `name()`, `version()`, `enabled()` methods required for OpenKNX integration
- **Standalone mode**: [main.cpp](../src/main.cpp) provides test harness (WiFi setup + module execution) - NOT used in production firmware

### Component Organization
Core components in `src/`:
- **HueModule**: Main orchestrator - owns lifecycle and KNX integration
- **HueDiscovery**: mDNS-based bridge discovery (`_hue._tcp.local` service)
- **HueAuth**: Button-press authentication flow for Hue API v2 app-key generation
- **Devices/**: Future device abstraction layer (currently empty) - will contain `HueDeviceBase`, `HueLight`, etc.

### API & Protocol Specifics
- **Hue API v2** (not v1): Use `/clip/v2/resource/light` endpoints, not `/api/<username>/lights`
- **Authentication**: POST to `/api` with `devicetype:"openknx#huemodule"` requires physical button press on bridge
- **Real-time updates**: Server-Sent Events (SSE) on `/eventstream/clip/v2` (not polling) - TODO in [HueModule.cpp](../src/HueModule.cpp#L48)
- **App-key storage**: Must persist to ESP32 flash (currently unimplemented in [HueAuth.cpp](../src/HueAuth.cpp#L73-L80))

## Development Workflows

### Build & Flash
```bash
pio run                    # Build for esp32dev (default env)
pio run --target upload    # Flash to device
pio device monitor         # Serial output at 115200 baud
```
**Note**: Task available via VS Code: `PlatformIO Build` (run with Ctrl+Shift+B)

### Configuration Requirements
Update [main.cpp](../src/main.cpp#L6-L7) with WiFi credentials for standalone testing:
```cpp
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";
```

### Dependency Management
- PlatformIO handles ArduinoJson (^6.21.0) via [platformio.ini](../platformio.ini#L10)
- Future ETS integration uses XML files: `Hue.share.xml` (general settings) and `Hue.templ.xml` (per-channel config)

## Active Development Focus

### Current Phase: Proof of Concept (Week 1 of 3)
**Goal**: Single light switchable via KNX object by end of sprint

**High-priority TODOs** (marked in code):
1. Implement bridge discovery in [HueDiscovery.cpp](../src/HueDiscovery.cpp) - mDNS query for `_hue._tcp.local`
2. Complete authentication flow in [HueAuth.cpp](../src/HueAuth.cpp#L27) - handle button-press response
3. App-key persistence ([HueAuth.cpp](../src/HueAuth.cpp#L73-L80)) - use ESP32 Preferences/EEPROM
4. Wire up `setupBridge()` and `setupDevices()` in [HueModule.cpp](../src/HueModule.cpp#L82-L95)
5. Create `HueLight` class in `src/Devices/` for basic on/off control

### Known Limitations
- No ETS parameter reading yet ([HueModule.cpp](../src/HueModule.cpp#L55)) - `enabled()` hardcoded to `true`
- Connection health checks unimplemented ([HueModule.cpp](../src/HueModule.cpp#L98))
- Event stream handling absent (critical for bidirectional status)

## Design Decisions & Context

### Why Separate Module?
Distinct from OFM-SmartHomeBridge (which handles KNX→SmartHome direction). Hue requires client-side integration with different patterns - see [OFM-Hue-Konzept.md](../docs/OFM-Hue-Konzept.md#21-empfehlung-separates-ofm-hue-modul).

### Module ID Convention
Module Type 9 (`ModuleType="9"` in future ETS XML) - KO offset 1000, supports 50 channels max per [docs/OFM-Hue-Konzept.md](../docs/OFM-Hue-Konzept.md#41-allgemeine-einstellungen).

### Offline Handling Strategy
Cache last known state when bridge unreachable. KNX commands queued for retry. Status KOs reflect cached state until reconnection.

## Documentation References
- **Quickstart**: [docs/OFM-Hue-Quickstart.md](../docs/OFM-Hue-Quickstart.md)
- **Full concept**: [docs/OFM-Hue-Konzept.md](../docs/OFM-Hue-Konzept.md) (406 lines, German - includes API flows, ETS config)
- **Roadmap**: [docs/OFM-HueModule-Roadmap.md](../docs/OFM-HueModule-Roadmap.md) - 3-week sprint plan
