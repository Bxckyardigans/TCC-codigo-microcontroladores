#include <OneWire.h>
#include <DallasTemperature.h>
#include <SPI.h>
#include <RF24.h>
#include <RF24Network.h>
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>

// ======================= PINOS ===========================
#define ONE_WIRE_BUS 15
#define LED_VERDE 13
#define LED_VERMELHO 12
#define BUZZER 14
#define RELE_PELTIER 27
#define RELE_VENTOINHA 26

#define SD_MOSI 33
#define SD_MISO 34
#define SD_SCK  25
#define SD_CS   32

SPIClass spiSD(HSPI);

// ======================= OBJETOS ==========================
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
RF24 radio(4, 5);  // CE, CSN
RF24Network network(radio);
TinyGPSPlus gps;
HardwareSerial gpsSerial(1);
LiquidCrystal_I2C lcd(0x27, 20, 4);

// ======================= ENDEREÇOS ========================
const uint16_t Master = 00;
const uint16_t Slave  = 01;

// ======================= ESTRUTURA DE DADOS ===============
struct Payload {
  float temperatura;
  double latitude;
  double longitude;
};

Payload dados;

static bool peltierLigada = false;
static unsigned long tempoDesligamentoVentoinha = 0;

float tempMaxima = 30.0;
float tempMinima = 15.0;

double lastLat = 0.0, lastLng = 0.0;

unsigned long previousMillisSend = 0;
unsigned long previousMillisLog = 0;

bool sdDisponivel = false;
bool ultimoEstadoSD = false;
unsigned long ultimoCheckSD = 0;


// ======================= FUNÇÕES AUXILIARES =========================
void carregarUltimaLocalizacao() {
  if (!SD.exists("/ultima.txt")) {
    Serial.println("ℹ️ Nenhum arquivo de localização encontrado.");
    return;
  }
  File f = SD.open("/ultima.txt");
  if (f) {
    String latStr = f.readStringUntil(',');
    String lngStr = f.readStringUntil('\n');
    lastLat = latStr.toDouble();
    lastLng = lngStr.toDouble();
    f.close();
    Serial.printf("📍 Última localização carregada: %.6f, %.6f\n", lastLat, lastLng);
  }
}

void salvarUltimaLocalizacao(double lat, double lng) {
  if (!sdDisponivel) return;
  File f = SD.open("/ultima.txt", FILE_WRITE);
  if (f) {
    String dataStr = "00/00/0000";
    String horaStr = "00:00:00";

    if (gps.date.isValid() && gps.time.isValid()) {
      dataStr = String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year());
      horaStr = String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second());
    }

    // Salva tudo em uma linha
    f.printf("%.6f,%.6f,%s,%s\n", lat, lng, dataStr.c_str(), horaStr.c_str());
    f.close();
    Serial.printf("💾 Última localização salva: %.6f, %.6f às %s %s\n", lat, lng, dataStr.c_str(), horaStr.c_str());
  } else {
    Serial.println("⚠️ Falha ao salvar última localização!");
  }
}

void salvarRegistroSD(float temp, double lat, double lng, bool refrig, bool venti) {
  if (!sdDisponivel) return;
  File log = SD.open("/log.csv", FILE_APPEND);
  if (log) {
    String dataStr = "00/00/0000";
    String horaStr = "00:00:00";
    if (gps.date.isValid() && gps.time.isValid()) {
      dataStr = String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year());
      horaStr = String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second());
    }
    log.printf("%s;%s;%.2f;%.6f;%.6f;%d;%d\n",
               dataStr.c_str(), horaStr.c_str(), temp, lat, lng,
               refrig ? 1 : 0, venti ? 1 : 0);
    log.close();
    Serial.println("💾 Registro salvo no SD");
  }
}

// ======================= INICIALIZAÇÃO SD =========================
bool inicializarSD() {
  Serial.println("🔍 Inicializando SD...");
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  if (!SD.begin(SD_CS, spiSD)) {
    Serial.println("❌ Falha ao inicializar SD!");
    lcd.setCursor(0, 1);
    lcd.print("SD: ERRO         ");
    return false;
  }

  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("⚠️ Nenhum cartão SD detectado!");
    lcd.setCursor(0, 1);
    lcd.print("SD: AUSENTE      ");
    return false;
  }

  Serial.print("📀 Tipo: ");
  switch (cardType) {
    case CARD_MMC: Serial.println("MMC"); break;
    case CARD_SD: Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default: Serial.println("DESCONHECIDO"); break;
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("💾 Tamanho: %llu MB\n", cardSize);

  // Cria cabeçalho do arquivo se não existir
  if (!SD.exists("/log.csv")) {
    File log = SD.open("/log.csv", FILE_WRITE);
    if (log) {
      log.println("Data;Hora;Temperatura;Latitude;Longitude;Refrig;Ventoinha");
      log.close();
    }
  }

  lcd.setCursor(0, 1);
  lcd.print("SD OK ");
  lcd.print(cardSize);
  lcd.print("MB   ");
  delay(500);
  lcd.clear();

  return true;
}

// ======================= SETUP ============================
void setup() {
  Serial.begin(115200);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");

  sdDisponivel = inicializarSD();
  lcd.clear();
  if (sdDisponivel) {
  Serial.println("✅ SD OK");  
  carregarUltimaLocalizacao();  
  }
  else Serial.println("❌ SD ERRO");

  lcd.setCursor(0, 0);
  lcd.print(sdDisponivel ? "SD: OK     " : "SD: ERRO   ");

  delay(1000);

  sensors.begin();
  lcd.setCursor(0, 1);
  lcd.print("TEMP: OK     ");
  Serial.println("✅ Sensor de temperatura OK");

  delay(1000);

  // Teste do GPS
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);
  lcd.setCursor(0, 2);
  lcd.print("GPS: OK      ");
  Serial.println("✅ GPS OK");

  delay(1000);

  // Teste do NRF24
  SPI.begin();
  if (!radio.begin()) {
    lcd.setCursor(0, 3);
    lcd.print("NRF24: ERRO  ");
    Serial.println("❌ NRF24L01 não encontrado!");
  } else {
    radio.setChannel(90);
    network.begin(90, Master);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    lcd.setCursor(0, 3);
    lcd.print("NRF24: OK    ");
    Serial.println("✅ NRF24 OK");
  }

  delay(1500);
  lcd.clear();

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELE_PELTIER, OUTPUT);
  pinMode(RELE_VENTOINHA, OUTPUT);
  digitalWrite(RELE_PELTIER, LOW);
  digitalWrite(RELE_VENTOINHA, LOW);

  lcd.clear();
}

// ======================= LOOP =============================
void loop() {
  network.update();
  sensors.requestTemperatures();
  dados.temperatura = sensors.getTempCByIndex(0);

  while (gpsSerial.available() > 0) gps.encode(gpsSerial.read());

  if (gps.location.isValid()) {
    dados.latitude  = gps.location.lat();
    dados.longitude = gps.location.lng();
    lastLat = dados.latitude;
    lastLng = dados.longitude;
  } else {
    dados.latitude  = lastLat;
    dados.longitude = lastLng;
  }

  lcd.setCursor(0, 0);
  lcd.printf("TEMP:%.1f%cC ", dados.temperatura, 223);
  lcd.setCursor(0, 1);
  lcd.printf("LAT:%.4f", dados.latitude);
  lcd.setCursor(0, 2);
  lcd.printf("LON:%.4f", dados.longitude);

  bool refrig = digitalRead(RELE_PELTIER);
  bool venti  = digitalRead(RELE_VENTOINHA);

  lcd.setCursor(0, 3);
  lcd.print(refrig ? "Resfri" : "-----------");
  lcd.setCursor(10, 3);
  lcd.print(venti ? "Venti" : "----------");

  lcd.setCursor(14, 0);
  if (dados.temperatura < tempMinima || dados.temperatura > tempMaxima) {
    lcd.print("ALERTA");
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
    tone(BUZZER, 250);
  } else {
    lcd.print(" IDEAL");
    digitalWrite(LED_VERDE, HIGH);
    digitalWrite(LED_VERMELHO, LOW);
    noTone(BUZZER);
  }

  // Controle térmico
  if (dados.temperatura >= tempMaxima - 1.5) {
    digitalWrite(RELE_PELTIER, HIGH);
    digitalWrite(RELE_VENTOINHA, HIGH);
    peltierLigada = true;
  } else if (dados.temperatura <= tempMaxima - 2.0 && peltierLigada) {
    digitalWrite(RELE_PELTIER, LOW);
    tempoDesligamentoVentoinha = millis();
    peltierLigada = false;
  }

  if (!peltierLigada && digitalRead(RELE_VENTOINHA) == HIGH) {
    if (millis() - tempoDesligamentoVentoinha >= 30000) {
      digitalWrite(RELE_VENTOINHA, LOW);
    }
  }

  unsigned long currentMillis = millis();

  // Envio via rádio a cada 5 segundos
  if (currentMillis - previousMillisSend >= 5000) {
    previousMillisSend = currentMillis;
    enviarDadosNRF24();
  }

  // Gravação SD a cada 5 minutos
  if (sdDisponivel && (currentMillis - previousMillisLog >= 300000)) { // 5 min = 300000 ms
    previousMillisLog = currentMillis;
    salvarRegistroSD(dados.temperatura, dados.latitude, dados.longitude,
                     digitalRead(RELE_PELTIER), digitalRead(RELE_VENTOINHA));
    salvarUltimaLocalizacao(dados.latitude, dados.longitude);
  }

    // --- MONITORAMENTO DE SD ---
  if (millis() - ultimoCheckSD >= 3000) { // verifica a cada 3 segundos
    ultimoCheckSD = millis();

    bool presente = (SD.cardType() != CARD_NONE);
    if (presente != ultimoEstadoSD) {
      ultimoEstadoSD = presente;
      if (presente) {
        Serial.println("💽 SD reconectado!");
        lcd.setCursor(0, 1);
        lcd.print("SD CONECTADO     ");
        sdDisponivel = inicializarSD();
      } else {
        Serial.println("⚠️ SD removido!");
        lcd.setCursor(0, 1);
        lcd.print("SD REMOVIDO      ");
        sdDisponivel = false;
      }
    }
  }
}

// ======================= ENVIO NRF24 ======================
void enviarDadosNRF24() {
  RF24NetworkHeader header(Slave);
  network.write(header, &dados, sizeof(dados));

  Serial.printf("📡 Enviado -> Temp: %.2f | Lat: %.6f | Lng: %.6f\n",
                dados.temperatura, dados.latitude, dados.longitude);
}