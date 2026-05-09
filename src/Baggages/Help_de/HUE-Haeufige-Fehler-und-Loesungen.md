### Häufige Fehler und Lösungen

### Bridge wird nicht gefunden
- mDNS/VLAN prüfen oder auf manuelle IP wechseln.
- Bei getrennten Netzsegmenten funktioniert die automatische Suche meist nicht.

### Authentifizierung schlägt fehl
- Pairing-Fenster abgelaufen → neu triggern und Link-Button erneut drücken.

### Hue-Ziel reagiert nicht
- Zieltyp und **Hue Ziel (Light-/Room-/Zone-ID)** prüfen.
- Bei Legacy-Projektierung zusätzlich **Hue Lampen-ID (UUID)** prüfen.
- Kanal deaktiviert?
- Sync-Richtung passend?

### Lichtmanager-Parameter oder HCL-KOs fehlen
- Lichtmanager global aktiviert?
- Kanal ist wirklich ein CT- oder RGB-Kanal?
- Erst nach Aktivierung des globalen Lichtmanagers werden Zuordnung und Sperrparameter sichtbar.

### Status fehlt
- Sync auf **Nur Hue zu KNX** oder **Bidirektional** gesetzt?
- Polling-Intervall sinnvoll gesetzt (`0` deaktiviert zyklisches Polling)?
- Status-KO mit GA verbunden?
- Zieltyp/Hue Ziel korrekt und auflösbar?
- Bei Raum/Zone: Rückmeldeverhalten mit Hue-App-Änderungen gesondert verifizieren.

### Raum/Zone meldet unerwartete Werte
- Bei Raum- und Zonen-Zielen bildet der Status nicht immer den exakten Zustand jedes Einzelgeräts ab.
- Dieses Verhalten ist systembedingt und sollte im Projekt mit dem gewünschten Zieltyp getestet werden.

### Lichtmanager wirkt nicht
- Lichtmanager global aktiviert?
- Manager zugewiesen?
- Bei `FixedTime`/`SunPosition`: mind. 2 gültige Stützpunkte?
- Bei `Manual`: gewünschte manuelle Farbtemperatur gesetzt und optionaler Helligkeitsverlauf passend parametriert?
- Bei `Astronomischer Sonnenstand`: sinnvolle Astro-Min/Max-Werte gesetzt?
- Globale/spezifische Sperre aktiv?

### Hue-Status kommt stark verzögert an
- Polling-Intervall zu hoch?
- Bei vielen Kanälen bewusst größere Werte gesetzt?
- Hue-App-Änderungen werden nicht sofort gepusht, sondern gemäß Abfrageintervall übernommen.

### Szenen reagieren nicht wie erwartet
- DPT 18.001 korrekt verwendet?
- Richtige Szenennummer im Slot hinterlegt?
- `Szene speichern` nur aktivieren, wenn Speicherbefehle wirklich genutzt werden.
- Für Steckdosen sind keine Szenen verfügbar.

### Saison-Profil schaltet nicht um
- Saison-Modus ist `Standard`? → dann sind Sommer-Stützpunkte absichtlich deaktiviert.
- Bei Modus `Festes Datum`: Start- und Ende-Datum korrekt eingetragen? Datum liegt im aktiven Bereich?
- Bei Modus `Auto-DST`: Systemzeit korrekt? DST-Erkennung setzt korrekte Uhrzeit voraus.
- Bei Modus `Per Objekt`: KO `LM x: Sommer aktiv` mit GA verbunden und Wert `1` gesendet?
- Im Sommer-Profil mindestens 2 Stützpunkte mit **Sommer Aktiv = Ja** vorhanden (bei `FixedTime`/`SunPosition`)?

