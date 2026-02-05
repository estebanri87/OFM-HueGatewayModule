# Copilot instructions for OFM-HueBridgeModule

## Big picture
- This is an OpenKNX module that bridges Philips Hue to KNX: Hue Bridge <-> ESP32 <-> KNX bus.
- The OpenKNX module entry point is HueBridgeModule, which owns discovery, auth, HueBridgeClient, and HueBridgeLight instances. See [src/HueBridgeModule.h](src/HueBridgeModule.h) and [src/HueBridgeModule.cpp](src/HueBridgeModule.cpp).
- Hue API v2 access is encapsulated in HueBridgeClient. All HTTP/JSON calls go through this class. See [src/HueBridgeClient.h](src/HueBridgeClient.h) and [src/HueBridgeClient.cpp](src/HueBridgeClient.cpp).
- Device mapping is per-channel: HueBridgeLight maps KNX KOs to Hue light state and writes status KOs. See [src/Devices/HueBridgeLight.h](src/Devices/HueBridgeLight.h) and [src/Devices/HueBridgeLight.cpp](src/Devices/HueBridgeLight.cpp).

## ETS and KNX integration
- ETS configuration is defined in [src/HueBridgeModule.share.xml](src/HueBridgeModule.share.xml) and [src/HueBridgeModule.templ.xml](src/HueBridgeModule.templ.xml). Parameter IDs and KO layout must stay consistent with `knxprod.h` used in code.
- Code reads ETS parameters via `ParamHUE_*` macros from `knxprod.h` and computes KO indices for channels in HueBridgeModule. When adding or changing parameters or KOs, update XML + regenerate `knxprod.h` with OpenKNXproducer, then update mapping logic in [src/HueBridgeModule.cpp](src/HueBridgeModule.cpp).
- KO layout used in code: 5 KOs per light (switch, brightness, dimming, status switch, status brightness). Color-related KOs are reserved in XML but not handled in code yet.

## Network and discovery flow
- Bridge discovery uses Preferences storage and mDNS `_hue._tcp` lookup. See [src/HueBridgeDiscovery.cpp](src/HueBridgeDiscovery.cpp).
- Authentication uses Hue button-press flow and stores the app key in Preferences. See [src/HueBridgeAuth.cpp](src/HueBridgeAuth.cpp).
- HueBridgeClient builds HTTPS URLs by default. If you change transport, keep Hue API v2 endpoint paths consistent. See [src/HueBridgeClient.cpp](src/HueBridgeClient.cpp).

## Web UI and diagnostics
- The built-in web server is started in HueBridgeModule and serves `/`, `/hue/scan`, and `/hue/status`. See [src/HueBridgeModule.cpp](src/HueBridgeModule.cpp).
- There is also a console command handler for `hue scan` and `hue status` in HueBridgeModule.

## Development workflows
- PlatformIO build: `pio run` with default env `esp32dev`. See [platformio.ini](platformio.ini).
- Unit tests (Unity): `pio test -e test`. See [platformio.ini](platformio.ini).
- The `src/main.cpp` is a standalone test harness that manages WiFi credentials for local testing only. In real OpenKNX firmware, WiFi is provided by another module. See [src/main.cpp](src/main.cpp).

## Project-specific conventions
- Keep the global module instance `openknxHueBridgeModule` in [src/HueBridgeModuleInstance.cpp](src/HueBridgeModuleInstance.cpp).
- Prefer keeping KNX value conversions in HueBridgeLight (e.g., DPT 5.001 percent to Hue 0-254) rather than in HueBridgeClient.
- When adding new device types or KO behaviors, mirror the pattern of HueBridgeLight: KNX input methods, Hue update, and KNX status feedback.


