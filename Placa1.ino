#include <SPI.h>
#include "mcp_can.h"
#include <Wire.h>

#define SPI_MOSI 11
#define SPI_MISO 12
#define SPI_SCK  13
#define CAN_CS   10

const int EchoPin = 5;
const int TriggerPin = 6;

const int EchoPin2 = 21;
const int TriggerPin2 = 19;


MCP_CAN CAN(CAN_CS);

void setup() {
  Serial.begin(9600);

  pinMode(TriggerPin, OUTPUT);
  pinMode(EchoPin, INPUT);
  pinMode(TriggerPin2, OUTPUT);
  pinMode(EchoPin2, INPUT);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI, CAN_CS);

  if (CAN_OK == CAN.begin(MCP_ANY, CAN_500KBPS, MCP_8MHZ)) {
    Serial.println("CAN BUS listo (transmisor)");
    CAN.setMode(MCP_NORMAL);
  } else {
    Serial.println("Esperando conexión CAN...");
    while (1);
  }
}

int ping(int TriggerPin, int EchoPin) {
  long duration, distanceCm;
  
  digitalWrite(TriggerPin, LOW);
  delayMicroseconds(4);
  digitalWrite(TriggerPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(TriggerPin, LOW);
  
  duration = pulseIn(EchoPin, HIGH, 25000); // 25 ms timeout
  distanceCm = duration * 10 / 292 / 2;
  return distanceCm;
}

void loop() {
  int cmEntrada = ping(TriggerPin, EchoPin);     // Sensor de entrada
  int cmSalida = ping(TriggerPin2, EchoPin2);    // Sensor de salida
  
  // Empaquetar datos a enviar por CAN
  byte data[8] = {
    highByte(cmEntrada), lowByte(cmEntrada),
    highByte(cmSalida), lowByte(cmSalida),
    0, 0, 0, 0
  };

  CAN.sendMsgBuf(0x100, 0, 8, data);

  Serial.print("Distancia entrada: ");
  Serial.print(cmEntrada);
  Serial.print(" cm, salida: ");
  Serial.print(cmSalida);
  Serial.println(" cm");

  delay(1000);
}
