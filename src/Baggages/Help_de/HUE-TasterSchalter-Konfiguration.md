### Taster/Schalter – Konfiguration

Bei Gerätetyp **Taster/Schalter** werden Tastenereignisse (Kurz-/Langdruck) vom Hue-System empfangen und als KNX-Telegramme auf den Bus gesendet.

#### Anzahl Tasten

Legt fest, wie viele Tasten des Hue-Geräts konfiguriert werden (1–4). Entsprechend viele Taste-N-Sektionen werden eingeblendet.

#### Native Hue Aktion

Steuert, ob das Hue-Gerät zusätzlich seine eigene Hue-Nativaktion ausführt, wenn eine Taste gedrückt wird:

- **Beibehalten**: Das Gerät führt seine native Hue-Aktion UND das KNX-Telegramm aus (Parallelausführung).
- **Deaktivieren**: Das Gerät führt nur das KNX-Telegramm aus – die native Hue-Aktion wird über die API unterdrückt.

Empfehlung: **Deaktivieren**, wenn die Hue-Leuchten vollständig über KNX gesteuert werden sollen, um Doppelreaktionen zu vermeiden.

#### Beispielkonfigurationen

#### 2-Tasten-Dimmer

- Taste 1: Gewerk `Licht`
- Kurzdruck: Schalten
- Langdruck: Dimmen
- Taste 2: Gewerk `Licht`
- Kurzdruck: Schalten
- Langdruck: Dimmen

Geeignet für kompakte Wandtaster mit Auf/Ab-Logik.

#### 4-Tasten-Szenentaster

- Taste 1-4: Gewerk `Licht`
- Kurzdruck: Szene abrufen
- Optional Langdruck: Zusatzfunktion oder deaktiviert

Geeignet für Raumsteuerungen mit fester Szenenzuordnung wie `Arbeiten`, `Entspannen`, `Abend`, `Aus`.

#### Jalousie-Taster

- Gewerk `Jalousie`
- Kurzdruck: Stop / Lamellen
- Langdruck: Auf / Ab

Sinnvoll, wenn ein Hue-Taster nicht für Licht, sondern für eine KNX-Funktion im Raum genutzt werden soll.

#### Auswahl des Gewerks

Wählen Sie das Gewerk passend zur gewünschten KNX-Funktion. Die sichtbaren Parameter und Kommunikationsobjekte passen sich automatisch an. Wenn eine Taste unerwartete Objekte zeigt, ist meist das falsche Gewerk ausgewählt.

