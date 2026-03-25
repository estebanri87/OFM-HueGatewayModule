### Polling-Intervall

Status-Abfrageintervall in Sekunden.
Empfehlung: `5..30 s` je nach Kanalzahl und Netzlast.

Wichtig:
- `0` = zyklisches Polling für diesen Kanal deaktiviert.
- Bei `>0` wird der Kanal gemäß Intervall aus Hue gelesen (abhängig von Sync-Richtung).
- Nach KNX-Kommandos erfolgt zusätzlich ein kurzer Fast-Track-Statusabgleich.

Praxisempfehlungen:
- Einzelne Lampen: meist `5..15 s`
- Räume oder Zonen: meist `15..30 s`
- Viele aktive Kanäle: größere Werte wählen, um Bridge und Netzwerk zu entlasten

Zu kleine Werte bringen in großen Projekten oft keinen echten Mehrwert, erzeugen aber mehr HTTP-Last auf der Hue-Bridge.

