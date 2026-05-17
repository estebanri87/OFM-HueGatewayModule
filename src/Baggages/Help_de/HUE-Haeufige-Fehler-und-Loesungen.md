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

### Lichtmanager (HCL) — siehe OFM-LightManager

Troubleshooting zu Lichtmanager-Parametern, HCL-KOs, Saison-Profil und adaptiver Helligkeit ist in der Applikationsbeschreibung OFM-LightManager zusammengefasst.

Hinweis: Im HueGateway muss am Kanal ein CT- oder RGB-Lampentyp gewählt und der gewünschte Lichtmanager über **Lichtmanager Zuordnung** zugewiesen sein, damit HCL-Sollwerte wirken.

### Status fehlt
- Sync auf **Nur Hue zu KNX** oder **Bidirektional** gesetzt?
- Polling-Intervall sinnvoll gesetzt (`0` deaktiviert zyklisches Polling)?
- Status-KO mit GA verbunden?
- Zieltyp/Hue Ziel korrekt und auflösbar?
- Bei Raum/Zone: Rückmeldeverhalten mit Hue-App-Änderungen gesondert verifizieren.

### Raum/Zone meldet unerwartete Werte
- Bei Raum- und Zonen-Zielen bildet der Status nicht immer den exakten Zustand jedes Einzelgeräts ab.
- Dieses Verhalten ist systembedingt und sollte im Projekt mit dem gewünschten Zieltyp getestet werden.

### Hue-Status kommt stark verzögert an
- Polling-Intervall zu hoch?
- Bei vielen Kanälen bewusst größere Werte gesetzt?
- Hue-App-Änderungen werden nicht sofort gepusht, sondern gemäß Abfrageintervall übernommen.

### Szenen reagieren nicht wie erwartet
- DPT 18.001 korrekt verwendet?
- Richtige Szenennummer im Slot hinterlegt?
- `Szene speichern` nur aktivieren, wenn Speicherbefehle wirklich genutzt werden.
- Für Steckdosen sind keine Szenen verfügbar.

