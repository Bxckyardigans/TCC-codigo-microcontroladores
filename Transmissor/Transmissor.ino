#include <RF24Network.h>
#include <RF24.h>
#include <SPI.h>
#include <OneWire.h>
#include <LiquidCrystal_I2C.h>
#include <DallasTemperature.h>
#include <TinyGPSPlus.h>

// ===== Sensor Temp =====
#define ONE_WIRE_BUS 15
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// ===== NRF24L01 =====
RF24 radio(4, 5);
RF24Network network(radio);

// Endereços
const uint16_t Master = 00;
const uint16_t Slave  = 01;

// ===== GPS =====
#include <HardwareSerial.h>
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);  // UART1 do ESP32
#define GPS_RX 16
#define GPS_TX 17

// ===== Sinais Visuais/Avisos =====
#define LED_VERDE 12
#define LED_VERMELHO 13
#define BUZZER 14

// Estrutura para mandar dados
struct Payload {
  float temperatura;
  float duty;
  double latitude;
  double longitude;
};
Payload datos;

unsigned long previousMillis = 0;

LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  Serial.begin(115200);

  // Temp
  sensors.begin();

  // GPS
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("GPS iniciado...");

  // NRF24
  SPI.begin();
  radio.begin();
  if (radio.isChipConnected()) {
    Serial.println("NRF24L01 detectado!");
  } else {
    Serial.println("ERRO: NRF24L01 NAO RESPONDE.");
  }
  network.begin(90, Master);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Esperando dados...");

  // Pinos
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);
  digitalWrite(BUZZER, LOW);
}

void loop() {
  network.update();

  // Ler GPS continuamente
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= 2000) { // envia a cada 2s
    previousMillis = currentMillis;

    // Temperatura
    sensors.requestTemperatures(); 
    float tempC = sensors.getTempCByIndex(0);

    datos.temperatura = (tempC != DEVICE_DISCONNECTED_C) ? tempC : -999;
    datos.duty = 4567; // exemplo
    datos.latitude  = gps.location.isValid() ? gps.location.lat() : 0.0;
    datos.longitude = gps.location.isValid() ? gps.location.lng() : 0.0;

    RF24NetworkHeader header(Slave);
    bool ok = network.write(header, &datos, sizeof(datos));

    Serial.print("Temp: "); Serial.print(datos.temperatura);
    Serial.print(" | Duty: "); Serial.print(datos.duty);
    Serial.print(" | Lat: "); Serial.print(datos.latitude, 6);
    Serial.print(" | Lng: "); Serial.print(datos.longitude, 6);
    Serial.print(" | Status: "); Serial.println(ok ? "OK" : "FALHA");

    // ===== Controle LEDs, buzzer e display =====
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("T:");
    lcd.print(datos.temperatura,1);
    lcd.print((char)223);
    lcd.print("C");

    // Mostrar lat/lng
    lcd.setCursor(0,1);
    lcd.print("Lat:");
    lcd.print(datos.latitude,2);

    lcd.setCursor(0,2);
    lcd.print("Lng:");
    lcd.print(datos.longitude,2);

    // Verificação da temperatura (15°C a 30°C como faixa aceitável)
    lcd.setCursor(15,0); // canto sup. direito
    if (datos.temperatura >= 15 && datos.temperatura <= 30) {
      lcd.print("OK");
      digitalWrite(LED_VERDE, HIGH);
      digitalWrite(LED_VERMELHO, LOW);
      digitalWrite(BUZZER, LOW);
    } else {
      lcd.print("!!");
      digitalWrite(LED_VERDE, LOW);
      digitalWrite(LED_VERMELHO, HIGH);
      tone(BUZZER, 1000); // buzzer tocando (1kHz)
    }
  } else {
    noTone(BUZZER); // buzzer desligado fora da verificação
  }
}
