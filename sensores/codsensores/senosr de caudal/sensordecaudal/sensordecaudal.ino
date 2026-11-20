#define SENSOR_PIN 4   // Cable amarillo al pin D4 (GPIO 4)

// Constante K obtenida mediante la calibración de 5 muestras
const float K_FACTOR = 600.0; // 600 pulsos equivalen a 1 Litro

volatile unsigned long contadorPulsos = 0;
unsigned long ultimoTiempo = 0;
float volumenTotalLitros = 0.0;

// Rutina de Interrupción (ISR)
void IRAM_ATTR contarPulsosISR() {
  contadorPulsos++;
}

void setup() {
  Serial.begin(115200);
  
  pinMode(SENSOR_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(SENSOR_PIN), contarPulsosISR, RISING);
  
  Serial.println("\n==================================================");
  Serial.println(" MONITOREO DE CAUDAL - ESCALERA HIDROPONICA NFT");
  Serial.println("==================================================");
  Serial.println("Sistema iniciado y midiendo flujo...\n");
}

void loop() {
  // Muestra y calcula el caudal cada 1 segundo (1000 ms)
  if (millis() - ultimoTiempo >= 1000) {
    ultimoTiempo = millis();
    
    // Lectura atómica de la variable de interrupción
    noInterrupts();
    unsigned long pulsosEnUnSegundo = contadorPulsos;
    contadorPulsos = 0; // Reinicia el contador para el siguiente segundo
    interrupts();
    
    // 1. Frecuencia de pulsos (Hz = pulsos / segundo)
    float frecuenciaHz = (float)pulsosEnUnSegundo;
    
    // 2. Caudal instantáneo en Litros por Minuto (L/min)
    // Formula: Q = (frecuencia / K) * 60
    float caudalLmin = (frecuenciaHz / K_FACTOR) * 60.0;
    
    // 3. Acumulado de agua que pasó en este segundo
    float litrosEnEsteSegundo = caudalLmin / 60.0;
    volumenTotalLitros += litrosEnEsteSegundo;
    
    // Impresión de variables para el informe / monitor serie
    Serial.print("Frecuencia: ");
    Serial.print(frecuenciaHz, 1);
    Serial.print(" Hz | Caudal: ");
    Serial.print(caudalLmin, 2);
    Serial.print(" L/min | Volumen Total: ");
    Serial.print(volumenTotalLitros, 3);
    Serial.println(" L");
  }
}