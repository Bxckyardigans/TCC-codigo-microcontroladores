#include <Arduino.h>
#include <RF24.h>
#include <RF24Network.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <LiquidCrystal_I2C.h>
#include <ESPmDNS.h>
#include <NetworkClient.h>

WebServer server(80);

// ===== NRF24 =====
RF24 radio(4, 5);
RF24Network network(radio);

const uint16_t Master = 00;
const uint16_t Slave  = 01;

// ===== Estrutura dos dados recebidos =====
struct Payload {
  float temperatura;
  double latitude;
  double longitude;
};

Payload datos;

// ===== LCD =====
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ===== Alerta =====
#define BUZZER_PIN    17
#define LED_VERDE     15
#define LED_VERMELHO  2

#define TEMP_MAX 30.0
#define TEMP_MIN 10.0

unsigned long ultimoSom = 0;
bool alertaAtivo = false;

// ===== Função para tocar alerta =====
void tocarAlerta() {
  // Evita tocar o buzzer continuamente
  if (millis() - ultimoSom > 1000) {
    tone(BUZZER_PIN, 1000, 300); // 1kHz por 300ms
    ultimoSom = millis();
  }
}

// ====== SETUP ======
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  SPI.begin();

  // ===== Inicializa LEDs e buzzer =====
  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  // ===== NRF24 =====
  if (!radio.begin()) {
    Serial.println("❌ Falha ao iniciar o NRF24L01!");
  } else {
    Serial.println("✅ NRF24L01 inicializado com sucesso!");
  }

  network.begin(90, Slave);
  radio.setDataRate(RF24_2MBPS);
  radio.setPALevel(RF24_PA_MAX);

  // ===== WiFi Manager =====
  WiFiManager wm;
  bool res = wm.autoConnect("ColdRemote", "monovac123");

  lcd.init();
  lcd.backlight();

  if (!res) {
    Serial.println("Falha ao conectar WiFi");
    lcd.setCursor(0, 0);
    lcd.print("WiFi: ERRO");
    digitalWrite(LED_VERMELHO, HIGH);
    tocarAlerta();
  } else {
    Serial.println("WiFi conectado!");
    Serial.println(WiFi.localIP());
    Serial.println(WiFi.SSID());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Rede:");
    lcd.print(WiFi.SSID());
    digitalWrite(LED_VERDE, HIGH);
  }

  // ===== Web Server =====

  // ===== Configurações WiFi =====
if (!MDNS.begin("ColdRemote")) {
    Serial.println("Error setting up MDNS responder!");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("mDNS responder started");

  server.on("/dados", []() {
    String json = "{";
    json += "\"temperatura\":" + String(datos.temperatura, 2) + ",";
    json += "\"latitude\":" + String(datos.latitude, 6) + ",";
    json += "\"longitude\":" + String(datos.longitude, 6);
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });

  // Start TCP (HTTP) server
  Serial.println("TCP server started");
  server.begin();

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);

  lcd.setCursor(0, 1);
  lcd.print("Aguardando dados...");
}

// ===== LOOP =====
void loop() {
  server.handleClient();
  network.update();

  // ===== Verifica WiFi =====
  if (!WiFi.isConnected()) {
    lcd.setCursor(0, 3);
    lcd.print("WiFi desconectado! ");
    digitalWrite(LED_VERMELHO, HIGH);
    digitalWrite(LED_VERDE, LOW);
    tocarAlerta();
  }

  // ===== Recepção de dados NRF24 =====
  while (network.available()) {
    RF24NetworkHeader header;
    network.read(header, &datos, sizeof(datos));

    Serial.printf("📥 Recebido: Temp %.2f | Lat %.6f | Lng %.6f\n",
                  datos.temperatura, datos.latitude, datos.longitude);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("TEMP:");
    lcd.print(datos.temperatura, 1);
    lcd.print((char)223);
    lcd.print("C");

    lcd.setCursor(0, 1);
    lcd.print("Lat:");
    lcd.print(datos.latitude, 2);

    lcd.setCursor(0, 2);
    lcd.print("Lng:");
    lcd.print(datos.longitude, 2);

    lcd.setCursor(0, 3);
    lcd.print("IP:");
    lcd.print(WiFi.localIP());

    // ===== Controle de temperatura =====
    if (datos.temperatura > TEMP_MAX || datos.temperatura < TEMP_MIN) {
      digitalWrite(LED_VERMELHO, HIGH);
      digitalWrite(LED_VERDE, LOW);
      tocarAlerta();
      lcd.setCursor(10, 0);
      lcd.print("ALERTA!");
      alertaAtivo = true;
    } else {
      digitalWrite(LED_VERMELHO, LOW);
      digitalWrite(LED_VERDE, HIGH);
      alertaAtivo = false;
    }
  }
}
