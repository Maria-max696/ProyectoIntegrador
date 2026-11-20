#include "HX711.h"

#define DT 4
#define SCK 5

HX711 balanza;

// Factor calculado con los 456 g
float factorCalibracion = 1230.0;

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("================================");
  Serial.println("  CELDA DE CARGA CZL601");
  Serial.println("================================");

  balanza.begin(DT, SCK);

  delay(2000);

  Serial.println("Retira todo el peso...");
  delay(3000);

  // Poner la balanza en cero
  balanza.set_scale();
  balanza.tare(20);

  // Aplicar factor de calibracion
  balanza.set_scale(factorCalibracion);

  Serial.println("TARA COMPLETADA");
  Serial.println("Factor: 1230");
  Serial.println();
}

void loop() {

  float peso = balanza.get_units(10);

  Serial.print("Peso: ");
  Serial.print(peso, 2);
  Serial.println(" g");

  delay(1000);
}