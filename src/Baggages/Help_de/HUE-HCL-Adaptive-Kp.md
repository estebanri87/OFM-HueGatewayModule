### HCL-Adaptive-Kp

*(Nur bei Konstantlichtregelung, Closed-Loop)*

Proportionalverstärkung des Reglers. Höhere Werte reagieren schneller, können aber bei trägen Sensoren instabil werden:

- **0.5**: Langsame, sehr stabile Regelung
- **1.0**: Standardwert, ausgewogen
- **1.5**: Schnellere Reaktion
- **2.0**: Aggressiv, nur bei stabilen Sensoren empfohlen

Empfehlung: Mit `1.0` beginnen und bei Bedarf anpassen.
