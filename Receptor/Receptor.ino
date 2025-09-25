#include <Arduino.h>
#include <RF24.h>
#include <LiquidCrystal_I2C.h>
#include <RF24Network.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>

// ===== WiFi =====
const char* ssid = "Redmi Note 12";
const char* password = "999999999";
WebServer server(80);

// ===== NRF24 =====
RF24 radio(4, 5);
RF24Network network(radio);

const uint16_t Master = 00;
const uint16_t Slave  = 01;

// Estrutura dos dados
struct Payload {
  float temperatura;
  float duty;
  double latitude;
  double longitude;
};
Payload datos;

LiquidCrystal_I2C lcd(0x27, 20, 4);

void setup() {
  Serial.begin(115200);
  SPI.begin();
  radio.begin();
  network.begin(90, Slave);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi conectado");
  Serial.println(WiFi.localIP());

  // Rotas
  server.on("/dados", []() {
    String json = "{";
    json += "\"temperatura\":" + String(datos.temperatura, 2) + ",";
    json += "\"duty\":" + String(datos.duty, 2) + ",";
    json += "\"latitude\":" + String(datos.latitude, 6) + ",";
    json += "\"longitude\":" + String(datos.longitude, 6);
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  server.begin();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0,0);
  lcd.print("Esperando dados...");
}

void loop() {
  server.handleClient();
  network.update();

  while (network.available()) {
    RF24NetworkHeader header;
    network.read(header, &datos, sizeof(datos));

    Serial.print("Recebido -> Temp: ");
    Serial.print(datos.temperatura);
    Serial.print(" | Duty: ");
    Serial.print(datos.duty);
    Serial.print(" | Lat: ");
    Serial.print(datos.latitude, 6);
    Serial.print(" | Lng: ");
    Serial.println(datos.longitude, 6);

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("T:");
    lcd.print(datos.temperatura,1);
    lcd.print((char)223);
    lcd.print("C");

    lcd.setCursor(0,1);
    lcd.print("Lat:");
    lcd.print(datos.latitude,2);

    lcd.setCursor(0,2);
    lcd.print("Lng:");
    lcd.print(datos.longitude,2);
  }
}
