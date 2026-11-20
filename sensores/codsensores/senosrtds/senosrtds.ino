#define TdsSensorPin 34
#define VREF 3.3

float factorCalibracion = 0.31;
float temperatura = 25.0; // ajusta según medición

int leerPromedio(int pin, int muestras = 20) {
  long suma = 0;
  for (int i = 0; i < muestras; i++) {
    suma += analogRead(pin);
    delay(10);
  }
  return suma / muestras;
}

void setup() {
  Serial.begin(115200);
  analogReadResolution(12);
}

void loop() {
  int lectura = leerPromedio(TdsSensorPin);
  float voltaje = lectura * (VREF / 4095.0);

  float compensacion = 1.0 + 0.02 * (temperatura - 25.0);
  float voltajeCompensado = voltaje / compensacion;

  float tds = (133.42 * pow(voltajeCompensado, 3)
              - 255.86 * pow(voltajeCompensado, 2)
              + 857.39 * voltajeCompensado) * 0.5;

  tds = tds * factorCalibracion;

  Serial.print("TDS: ");
  Serial.print(tds);
  Serial.println(" ppm");

  delay(1000);
}
