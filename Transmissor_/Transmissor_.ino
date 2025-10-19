#include <OneWire.h> //dados por 1 fio
#include <DallasTemperature.h> //sensor de temperatura
#include <SPI.h>
#include <RF24.h> // modulo radio
#include <RF24Network.h> // modulo radio
#include <TinyGPSPlus.h> // gps
#include <HardwareSerial.h> 
#include <LiquidCrystal_I2C.h> //display i2c
#include <SD.h> // cartao sd

// ======================= PINOS ===========================
#define ONE_WIRE_BUS 15
#define LED_VERDE 12
#define LED_VERMELHO 13
#define BUZZER 14
#define RELE_PELTIER 27
#define RELE_VENTOINHA 26

//cartao sd com pinos customizados
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

// ======================= FUNÇÕES AUXILIARES =========================
void carregarUltimaLocalizacao() {
  if (!SD.exists("/ultima.txt")) { // se o arquivo não existir
    Serial.println("ℹ️ Nenhum arquivo de localização encontrado.");
    return;
  }
  File f = SD.open("/ultima.txt");
  if (f) { //carrega ultima localização
    String latStr = f.readStringUntil(',');
    String lngStr = f.readStringUntil('\n');
    lastLat = latStr.toDouble();
    lastLng = lngStr.toDouble();
    f.close();
    Serial.printf("📍 Última localização carregada: %.6f, %.6f\n", lastLat, lastLng); 
  }
}

void salvarUltimaLocalizacao(double lat, double lng) {
  if (!sdDisponivel) return; // se o cartão estiver disponivel
  File f = SD.open("/ultima.txt", FILE_WRITE);
  if (f) {
    f.printf("%.6f,%.6f\n", lat, lng);
    f.close();
  }
}

void salvarRegistroSD(float temp, double lat, double lng, bool refrig, bool venti) { //salva tudo no log.csv
  if (!sdDisponivel) return;
  File log = SD.open("/log.csv", FILE_APPEND);
  if (log) {
    String dataStr = "00/00/0000";
    String horaStr = "00:00:00";
    if (gps.date.isValid() && gps.time.isValid()) {
      dataStr = String(gps.date.day()) + "/" + String(gps.date.month()) + "/" + String(gps.date.year());
      horaStr = String(gps.time.hour()) + ":" + String(gps.time.minute()) + ":" + String(gps.time.second());
    }
    log.printf("%s,%s,%.2f,%.6f,%.6f,%d,%d\n",
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

  //identificação do tipo do cartão
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
      log.println("Data,Hora,Temperatura,Latitude,Longitude,Refrig,Ventoinha");
      log.close();
    }
  }

  lcd.setCursor(0, 1);
  lcd.print("SD OK ");
  lcd.print(cardSize);
  lcd.print("MB  ");
  return true;
}

// ======================= SETUP ============================
void setup() {
  Serial.begin(115200);
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);

  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");

  sdDisponivel = inicializarSD();
  if (sdDisponivel) carregarUltimaLocalizacao();

  sensors.begin();

  pinMode(LED_VERDE, OUTPUT);
  pinMode(LED_VERMELHO, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(RELE_PELTIER, OUTPUT);
  pinMode(RELE_VENTOINHA, OUTPUT);
  digitalWrite(RELE_PELTIER, LOW);
  digitalWrite(RELE_VENTOINHA, LOW);

  SPI.begin();
  if (!radio.begin()) {
    Serial.println("❌ NRF24L01 não encontrado!");
    lcd.setCursor(0, 1);
    lcd.print("NRF24: ERRO      ");
  } else {
    Serial.println("✅ NRF24L01 detectado");
    radio.setChannel(90);
    network.begin(90, Master);
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    lcd.setCursor(0, 1);
    lcd.print("NRF24: OK        ");
  }

  delay(1500);
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
  lcd.print(refrig ? "Resfri" : "-------");
  lcd.setCursor(10, 3);
  lcd.print(venti ? "Venti" : "-------");

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
}

// ======================= ENVIO NRF24 ======================
void enviarDadosNRF24() {
  RF24NetworkHeader header(Slave);
  network.write(header, &dados, sizeof(dados));

  Serial.printf("📡 Enviado -> Temp: %.2f | Lat: %.6f | Lng: %.6f\n",
                dados.temperatura, dados.latitude, dados.longitude);
}
