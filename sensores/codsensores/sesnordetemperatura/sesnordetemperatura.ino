#include <OneWire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 4

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

int numSensores;

// 🔧 Offsets individuales (AJUSTA ESTOS)
float offsets[5] = {
  2.95,  // Sensor 0
  0.0,   // Sensor 1
  0.0,   // Sensor 2
  0.0,   // Sensor 3
  0.0    // Sensor 4
};

void setup() {
  Serial.begin(9600);
  sensors.begin();

  numSensores = sensors.getDeviceCount();

  Serial.print("Sensores detectados: ");
  Serial.println(numSensores);
}

void loop() {
  sensors.requestTemperatures();

  for (int i = 0; i < numSensores; i++) {
    float tempC = sensors.getTempCByIndex(i);
    float tempCalibrada = tempC + offsets[i];

    Serial.print("Sensor ");
    Serial.print(i);
    Serial.print(": ");

    Serial.print(tempCalibrada);
    Serial.println(" °C");
  }

  Serial.println("----------------");
  delay(2000);
}