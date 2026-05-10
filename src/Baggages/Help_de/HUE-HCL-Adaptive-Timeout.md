### HCL-Adaptive-Timeout

Maximale Zeit in Minuten ohne neuen Lux-Messwert, bevor der Sensor als ausgefallen gilt und die adaptive Regelung pausiert wird. Nach Ablauf folgt der Lichtmanager wieder nur der HCL-Kurve.

- `0` = kein Timeout (nicht empfohlen — bei Sensorausfall bleibt die letzte Reduktion dauerhaft aktiv).
- Empfehlung: 5–15 Minuten, abhängig vom Sendeintervall des Sensors.
