### Taste Kurzdruck

Bestimmt die Aktion beim kurzen Tastendruck. Die verfügbaren Optionen hängen vom Gewerk ab:

**Gewerk Licht:**
- **Kein Kurzdruck**: Kurzdruck ohne KNX-Aktion (sinnvoll z. B. für reine Dimmtaster)
- **Schalten**: Togglet den Schaltzustand (EIN/AUS wechselnd). Standardwert.
- **Szene abrufen**: Ruft eine KNX-Szene ab (DPT 17.001). Die Szenennummer wird unterhalb eingeblendet.

**Gewerk Jalousie:**
- **Kein Kurzdruck**: Kurzdruck ohne KNX-Aktion
- **Lamelle Auf**: Sendet `Auf`-Befehl (DPT 1.008)
- **Lamelle Ab**: Sendet `Ab`-Befehl (DPT 1.008)

**Gewerk Medien:**
- **Play/Pause**: Togglet Play/Pause (DPT 1.001)

**Gewerk Generisch:**
- **Objekt A**: Sendet auf das primäre KO (DPT 1.001)

Hinweis: Bei **Kein Kurzdruck** wird für diese Taste kein Kurzdruck-KO in ETS eingeblendet.

