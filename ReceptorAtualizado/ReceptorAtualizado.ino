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

// ===== PINOS =====
#define BUZZER_PIN    17
#define LED_VERDE     15
#define LED_VERMELHO  2
#define PINO_RESET_WIFI  16

// ===== OBJETOS =====
RF24 radio(4, 5);
RF24Network network(radio);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

// ===== ESTRUTURAS =====
const uint16_t Master = 00;
const uint16_t Slave  = 01;

struct Payload {
  float temperatura;
  double latitude;
  double longitude;
};

Payload datos;

// ===== VARIÁVEIS =====
unsigned long ultimoSom = 0;
bool alertaAtivo = false;
bool wifiAnterior = true;
unsigned long ultimoCheckWifi = 0;

// ===== PROTÓTIPOS =====
void tocarAlerta();
void verificarWiFi();
void exibirStatusModulos(bool nrfOK, bool lcdOK, bool wifiOK);

// ===== FUNÇÃO ALERTA =====
void tocarAlerta() {
  if (millis() - ultimoSom > 1000) {
    tone(BUZZER_PIN, 1000, 300);
    ultimoSom = millis();
  }
}

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  SPI.begin();
  WiFi.mode(WIFI_STA);

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(PINO_RESET_WIFI, INPUT_PULLUP);

  digitalWrite(LED_VERDE, LOW);
  digitalWrite(LED_VERMELHO, LOW);

  // ===== LCD =====
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");

  // ===== LIMPAR REDE SE BOTÃO PRESSIONADO =====
  if (digitalRead(PINO_RESET_WIFI) == LOW) {
    Serial.println("🧹 Limpando WiFi salvo...");
    WiFi.disconnect(true, true);
    delay(1000);
  }

  // ===== NRF =====
  bool nrfOK = radio.begin();
  if (nrfOK) {
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    network.begin(90, Slave);
    Serial.println("✅ NRF24L01 inicializado!");
  } else {
    Serial.println("❌ Falha no NRF24L01!");
  }

  // ===== WIFI =====
  WiFiManager wm;
    
    lcd.setCursor(0, 0);
    lcd.print("Sem wifi conectado!");
    lcd.setCursor(0, 1);
    lcd.print("conecte-se em");
    lcd.setCursor(0, 2);
    lcd.print("ColdRemote");
    
  bool wifiOK = wm.autoConnect("ColdRemote", "monovac123");

  if (wifiOK) {
    Serial.println("✅ WiFi conectado!");
  } else {
    Serial.println("❌ Falha ao conectar WiFi!");
    digitalWrite(LED_VERMELHO, HIGH);
    tone(BUZZER_PIN, 600, 300);
    delay(300);
    noTone(BUZZER_PIN);
  }

  // ===== MDNS =====
  if (!MDNS.begin("ColdRemote")) {
    Serial.println("❌ Erro MDNS");
  } else {
    Serial.println("✅ mDNS iniciado");
  }

  // ===== EXIBE STATUS INICIAL =====
  exibirStatusModulos(nrfOK, wifiOK);

  // ===== WEBSERVER =====
  server.on("/dados", []() {
    String json = "{";
    json += "\"temperatura\":" + String(datos.temperatura, 2) + ",";
    json += "\"latitude\":" + String(datos.latitude, 6) + ",";
    json += "\"longitude\":" + String(datos.longitude, 6) + ",";
    json += "\"status\":\"" + String(alertaAtivo ? "alerta!" : "ok") + "\"";
    json += "}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
  });
  server.begin();
  MDNS.addService("http", "tcp", 80);

  lcd.setCursor(0, 3);
  lcd.print("Aguardando dados...");
}

// ===== LOOP =====
void loop() {
  server.handleClient();
  network.update();
  verificarWiFi();

  // ===== RECEPÇÃO NRF =====
  while (network.available()) {
    RF24NetworkHeader header;
    network.read(header, &datos, sizeof(datos));

    Serial.printf("📥 Temp %.2f | Lat %.6f | Lng %.6f\n",
                  datos.temperatura, datos.latitude, datos.longitude);

    if (wifiAnterior) {
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
    }

    // ===== ALERTA TEMPERATURA =====
    if (datos.temperatura > 30.0 || datos.temperatura < 10.0) {
      digitalWrite(LED_VERMELHO, HIGH);
      digitalWrite(LED_VERDE, LOW);
      lcd.setCursor(12, 0);
      lcd.print("ALERTA!");
      tocarAlerta();
      alertaAtivo = true;
    } else {
      digitalWrite(LED_VERMELHO, LOW);
      digitalWrite(LED_VERDE, HIGH);
      alertaAtivo = false;
    }
  }
}

// ===== VERIFICA CONEXÃO WIFI =====
void verificarWiFi() {
  if (millis() - ultimoCheckWifi > 3000) {
    ultimoCheckWifi = millis();

    if (WiFi.status() != WL_CONNECTED) {
      if (wifiAnterior) {
        Serial.println("⚠️ WiFi desconectado!");
        lcd.setCursor(0, 3);
        lcd.print("WiFi: DESCONECTADO ");
        digitalWrite(LED_VERMELHO, HIGH);
        digitalWrite(LED_VERDE, LOW);
        tocarAlerta();
        wifiAnterior = false;
      }
      WiFi.reconnect();
    } else {
      if (!wifiAnterior) {
        Serial.println("✅ WiFi reconectado!");
        lcd.setCursor(0, 3);
        lcd.print("WiFi: ");
        lcd.print(WiFi.SSID());
        digitalWrite(LED_VERDE, HIGH);
        digitalWrite(LED_VERMELHO, LOW);
        wifiAnterior = true;
      }
    }
  }
}

// ===== EXIBE STATUS INICIAL =====
void exibirStatusModulos(bool nrfOK, bool wifiOK) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NRF: "); lcd.print(nrfOK ? "OK" : "ERRO");
  lcd.setCursor(0, 1);
  lcd.print("WiFi: "); lcd.print(wifiOK ? "OK" : "ERRO");

  delay(2500);
  lcd.clear();
}