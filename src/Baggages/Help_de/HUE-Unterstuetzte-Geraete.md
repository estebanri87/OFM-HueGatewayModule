### Unterstützte Geräte

Das Modul arbeitet generisch auf Basis der Hue API v2 Ressourcentypen.
Es werden **alle Geräte** unterstützt, die an einer Hue Bridge (Zigbee) angelernt sind und dort die entsprechenden API-Ressourcen bereitstellen.
Eine Prüfung auf bestimmte Modell- oder Herstellernamen findet nicht statt – entscheidend ist ausschließlich der vom Bridge gemeldete Ressourcentyp.

### Gerätekategorien im Modul

- **Licht** (`light`): Alle Light-Ressourcen ohne Steckdosen-Archetype — Schalten, Dimmen, Farbtemperatur, RGB (je nach Fähigkeit)
- **Steckdose** (`light` + Archetype enthält *plug*, *socket* oder *outlet*): Archetype-Substring-Prüfung — Schalten Ein/Aus
- **Taster/Schalter** (`button`, `relative_rotary`): Service-Typ einer Geräte-Ressource — Tastenereignisse (Kurz-/Langdruck), Drehregler
- **Bewegungsmelder** (`motion`): Service-Typ einer Geräte-Ressource — Präsenz, optional Temperatur, Helligkeit, Batterie
- **Kontaktsensor** (`contact_sensor`): Service-Typ einer Geräte-Ressource — Kontaktstatus, optional Manipulation, Temperatur, Batterie
- **Raum/Zone** (`grouped_light`): Aggregierte Lichtressource eines Raumes oder einer Zone — Schalten, Dimmen, Farbtemperatur, RGB (je nach enthaltenen Leuchten)

Die Fähigkeiten einer Leuchte (dimmbar, Farbtemperatur, Farbe) werden automatisch anhand der von der Bridge gemeldeten JSON-Felder erkannt:

- **Ein/Aus**: Nur `on/off`-Feld vorhanden
- **Dimmbar**: `dimming`-Feld vorhanden
- **Farbtemperatur**: `color_temperature`-Feld vorhanden (Mirek 153–500)
- **Farbe (RGB)**: `color`-Feld vorhanden (CIE 1931 XY)

### Philips Hue Produkte (Signify)

#### Leuchten

- Hue White (E27, E14, GU10, A19, BR30, PAR38): Dimmbar — Ja
- Hue White Ambiance (E27, E14, GU10, A19, BR30, Lightstrip): Farbtemperatur — Ja
- Hue White and Color Ambiance (E27, E14, GU10, A19, BR30, Lightstrip Plus): Farbe (RGB) — Ja
- Hue Filament (ST64, G93, G125, A60, ST72, T75): Farbtemperatur — Nein
- Hue Lightguide (Ellipse, Triangle, Globe): Farbe (RGB) — Nein
- Hue Gradient Lightstrip (Lightstrip, Signe Tisch-/Stehleuchte, Tube): Farbe (RGB) — Ja
- Hue Play (Light Bar): Farbe (RGB) — Nein
- Hue Go (Portable): Farbe (RGB) — Nein
- Hue Iris, Bloom (Tischleuchten): Farbe (RGB) — Nein
- Hue Centura, Fugato, Perifo, Xamento (Einbau-/Decken-/Schienensysteme): Farbe (RGB) — Nein
- Hue Aurelle (Deckenleuchte Panel): Farbtemperatur — Ja
- Hue Being, Fair, Still, Cher, enrave (Decken-/Pendelleuchten): Farbtemperatur — Nein
- Hue Outdoor (Lily, Calla, Appear, Nyro, Impress, Econic u.a.): je nach Modell Farbtemperatur oder Farbe (RGB) — Nein

#### Smart Plugs

- Hue Smart Plug (EU): Archetype `plug` — Steckdose (Ein/Aus) — Nein

#### Sensoren

- Hue Motion Sensor (Indoor): `motion`, Zusatzdaten: Temperatur, Helligkeit, Batterie — Nein
- Hue Outdoor Sensor: `motion`, Zusatzdaten: Temperatur, Helligkeit, Batterie — Nein
- Hue Secure Contact Sensor: `contact_sensor`, Zusatzdaten: Temperatur, Manipulation, Batterie — Nein

#### Taster und Schalter

- Hue Dimmer Switch (V1/V2): 4 Tasten, `button` — Ja
- Hue Tap Dial Switch: 4 Tasten + 1 Drehregler, `button` + `relative_rotary` — Ja
- Hue Wall Switch Module: 2 Tasten, `button` — Nein
- Hue Tap Mini: 4 Tasten, `button` — Nein

### Friends of Hue (Zigbee Green Power)

Friends-of-Hue-Schalter werden vom Modul als **Taster/Schalter** mit `button`-Ressourcen erkannt.

- Busch-Jaeger Friends of Hue (1-fach, 2-fach): 1–4 Tasten — Nein
- Gira Friends of Hue (1-fach, 2-fach): 1–4 Tasten — Nein
- JUNG Friends of Hue (1-fach, 2-fach): 1–4 Tasten — Nein
- Niko Friends of Hue (1-fach, 2-fach): 1–4 Tasten — Nein
- Vimar Friends of Hue: 1–4 Tasten — Nein
- Feller Friends of Hue (Schweiz): 1–4 Tasten — Nein
- illumra EnOcean/Zigbee Green Power Schalter: 1–4 Tasten — Nein
- Senic / Nuimo Friends of Hue Smart Switch: 1–4 Tasten — Nein
- RunLessWire Friends of Hue Click: 1–4 Tasten — Nein

### Drittanbieter-Leuchten und -Steckdosen

Folgende Hersteller bieten Zigbee-Leuchtmittel und -Steckdosen an, die sich mit der Hue Bridge koppeln lassen.
Nach erfolgreicher Kopplung an der Bridge werden sie vom Modul wie native Hue-Leuchten bzw. -Steckdosen behandelt.

> **Hinweis:** Die Kompatibilität mit der Hue Bridge hängt vom jeweiligen Gerätemodell und der Firmware-Version ab.
> Offiziell von Signify unterstützte Drittanbieter sind mit (✓) markiert; bei übrigen Einträgen ist die Kopplung erfahrungsgemäß möglich, aber nicht von Signify garantiert.

#### Leuchten

- innr (E27, E14, GU10, LED-Strips, Deckenleuchten): ✓ offiziell, je nach Modell Dimmbar / CT / RGB — Nein
- IKEA TRÅDFRI / DIRIGERA (E27, E14, GU10, LED-Panels): erfahrungsgemäß (Touchlink), je nach Modell Dimmbar / CT / RGB — Nein
- OSRAM/LEDVANCE Smart+ (E27, E14, GU10, LED-Strips ältere ZLL-Modelle): teilweise, je nach Modell Dimmbar / CT / RGB — Nein
- Müller-Licht tint (E27, E14, GU10, LED-Panels): teilweise, je nach Modell Dimmbar / CT — Nein
- GLEDOPTO (Zigbee LED-Controller RGB, RGBW, CCT): erfahrungsgemäß, CT / RGB — Nein
- Sengled (Smart LED Bulbs E27, BR30): teilweise, Dimmbar / CT — Nein
- Paulmann (SmartHome Zigbee Leuchtmittel): teilweise, je nach Modell Dimmbar / CT — Nein

#### Steckdosen

- innr Smart Plug (SP 120, SP 220, SP 224): ✓ offiziell, Archetype `plug` — Nein
- OSRAM/LEDVANCE Smart+ Plug: teilweise, Archetype `plug` — Nein
- IKEA TRÅDFRI ASKVADER Steckdose: erfahrungsgemäß (Touchlink), Archetype `plug` — Nein

### Hinweise zur Gerätekompatibilität

- **Entscheidend ist die Kopplung an der Hue Bridge**: Sobald ein Gerät dort angelegt ist, stellt die Bridge es über die API v2 bereit und das Gateway-Modul kann es nutzen.
- **Keine Modellprüfung im Code**: Das Modul prüft weder `model_id`, `manufacturer_name` noch `product_name`. Es arbeitet ausschließlich mit den API-Ressourcentypen (`light`, `button`, `motion`, `contact_sensor`, `relative_rotary`, `grouped_light`).
- **Steckdosen-Erkennung**: Smart Plugs werden an der Bridge als `light`-Ressource geführt. Das Modul unterscheidet sie anhand des Archetype-Feldes (Substring `plug`, `socket` oder `outlet`).
- **Zukünftige Geräte**: Neue Hue- oder Zigbee-Produkte, die sich an der Bridge anlernen lassen und Standard-Ressourcentypen verwenden, werden automatisch unterstützt – ohne Firmware-Update des Moduls.

