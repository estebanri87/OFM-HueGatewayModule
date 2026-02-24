### Häufige Fehler und Lösungen

### Bridge wird nicht gefunden
- mDNS/VLAN prüfen oder auf manuelle IP wechseln.

### Authentifizierung schlägt fehl
- Pairing-Fenster abgelaufen → neu triggern und Link-Button erneut drücken.

### Hue-Ziel reagiert nicht
- Zieltyp und **Hue Ziel (Light-/Room-/Zone-ID oder Name)** prüfen.
- Bei Legacy-Projektierung zusätzlich **Hue Lampen-ID (UUID)** prüfen.
- Kanal deaktiviert?
- Sync-Richtung passend?

### Status fehlt
- Polling > 0?
- Sync auf Hue→KNX oder Bidirektional?
- Status-KO mit GA verbunden?

### HCL wirkt nicht
- HCL global aktiviert?
- Manager zugewiesen?
- Min. 2 gültige Stützpunkte?
- Globale/spezifische HCL-Sperre aktiv?

