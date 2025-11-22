
#include <Arduino.h>
#include <SPI.h>
#include <RF24.h>
#include <RF24Network.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <LiquidCrystal_I2C.h>
#include <ESPmDNS.h>
#include <mbedtls/gcm.h>
#include <mbedtls/aes.h>

// ===== PINOS =====
#define BUZZER_PIN       17
#define LED_VERDE        15
#define LED_VERMELHO     2
#define PINO_RESET_WIFI  16

// ===== HARDWARE =====
RF24 radio(4, 5);            // CE, CSN
RF24Network network(radio);
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer server(80);

// ===== NODOS =====
const uint16_t Master = 00;
const uint16_t Slave  = 01;

// ===== STRUCT PAYLOAD =====
struct Payload {
  float temperatura;
  double latitude;
  double longitude;
};

// ===== CRYPTO (mesma chave do transmissor) =====
const uint8_t AES_KEY[16] = {
  0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,
  0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F,0x10
};
const size_t IV_LEN = 12;
const size_t TAG_LEN = 16;
const size_t NRF_PAYLOAD_MAX = 32;

struct FragHeader {
  uint8_t version;
  uint8_t flags;
  uint32_t seq;
} __attribute__((packed));

const size_t FRAG_HDR_SIZE = sizeof(FragHeader); // 6 bytes
const size_t FRAG_PAYLOAD_MAX = NRF_PAYLOAD_MAX - FRAG_HDR_SIZE; // 26 bytes

// ===== REASSEMBLY =====
uint32_t assembling_seq = 0;
uint8_t *assem_buf = NULL;
size_t assem_len = 0;
size_t assem_capacity = 0;
uint32_t last_seq_accepted = 0;

// ===== VARS GLOBAIS PARA /dados =====
float temperaturaAtual = 0.0;
double latAtual = 0.0;
double lonAtual = 0.0;
String dataHoraAtual = ""; // formato ISO-like ou GPS time

// ===== ESTADO E UI =====
unsigned long ultimoSom = 0;
bool alertaAtivo = false;
bool wifiAnterior = true;
unsigned long ultimoCheckWifi = 0;
unsigned long ultimoPacote = 0;

// ===== AUX =====
uint32_t read_u32be(const uint8_t *p){
  return ((uint32_t)p[0]<<24) | ((uint32_t)p[1]<<16) | ((uint32_t)p[2]<<8) | (uint32_t)p[3];
}

bool aes_gcm_decrypt(const uint8_t *key, const uint8_t *iv, size_t iv_len,
                     const uint8_t *aad, size_t aad_len,
                     const uint8_t *ct, size_t ct_len,
                     const uint8_t *tag,
                     uint8_t *pt_out) {
  mbedtls_gcm_context gcm;
  mbedtls_gcm_init(&gcm);
  if (mbedtls_gcm_setkey(&gcm, MBEDTLS_CIPHER_ID_AES, key, 128) != 0) {
    mbedtls_gcm_free(&gcm);
    return false;
  }
  int ret = mbedtls_gcm_auth_decrypt(&gcm, ct_len, iv, iv_len,
                                     aad, aad_len,
                                     tag, TAG_LEN,
                                     ct, pt_out);
  mbedtls_gcm_free(&gcm);
  return ret == 0;
}

void reset_assembly() {
  if (assem_buf) free(assem_buf);
  assem_buf = NULL;
  assem_len = 0;
  assem_capacity = 0;
  assembling_seq = 0;
}

void append_to_assembly(const uint8_t *data, size_t len) {
  if (assem_len + len > assem_capacity) {
    // aumentar capacidade (pelo menos 1KB)
    assem_capacity = max((size_t)1024, assem_len + len);
    assem_buf = (uint8_t*)realloc(assem_buf, assem_capacity);
  }
  memcpy(assem_buf + assem_len, data, len);
  assem_len += len;
}

// ===== FUNÇÕES DO SISTEMA (UI/ALERTA/WiFi) =====
void tocarAlerta() {
  if (millis() - ultimoSom > 1000) {
    tone(BUZZER_PIN, 1000, 300);
    ultimoSom = millis();
  }
}

void exibirStatusModulos(bool nrfOK, bool wifiOK) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("NRF: "); lcd.print(nrfOK ? "OK" : "ERRO");
  lcd.setCursor(0, 1);
  lcd.print("WiFi: "); lcd.print(wifiOK ? "OK" : "ERRO");
  delay(1500);
  lcd.clear();
}

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

// ===== ENDPOINT /dados =====
void handleDadosEndpoint() {
  String json = "{";
  json += "\"temperatura\":" + String(temperaturaAtual, 2) + ",";
  json += "\"latitude\":" + String(latAtual, 6) + ",";
  json += "\"longitude\":" + String(lonAtual, 6) + ",";
  json += "\"dataHora\":\"" + dataHoraAtual + "\"";
  json += "}";
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.send(200, "application/json", json);
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

  // LCD
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Inicializando...");

  // NRF
  bool nrfOK = radio.begin();
  if (nrfOK) {
    radio.setDataRate(RF24_2MBPS);
    radio.setPALevel(RF24_PA_MAX);
    network.begin(90, Slave);
    Serial.println("✅ NRF24L01 inicializado!");
  } else {
    Serial.println("❌ Falha no NRF24L01!");
  }

  // WiFiManager (ap hotspot se necessário)
  WiFiManager wm;
  lcd.setCursor(0, 0);
  lcd.print("Conectar WiFi...");
  bool wifiOK = wm.autoConnect("ColdRemote", "monovac123");
  if (wifiOK) {
    Serial.println("✅ WiFi conectado!");
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("WiFi: ");
    lcd.print(WiFi.SSID());
  } else {
    Serial.println("❌ Falha ao conectar WiFi!");
    digitalWrite(LED_VERMELHO, HIGH);
  }

  // mDNS
  if (MDNS.begin("colremote")) {
    Serial.println("mDNS iniciado: http://colremote.local");
  } else {
    Serial.println("Erro ao iniciar mDNS");
  }

  // WebServer
  server.on("/dados", HTTP_GET, handleDadosEndpoint);
  server.begin();
  MDNS.addService("http", "tcp", 80);

  exibirStatusModulos(nrfOK, wifiOK);

  lcd.setCursor(0, 3);
  lcd.print("Aguardando dados...");
  last_seq_accepted = 0;
  ultimoPacote = millis();
}

// ===== LOOP =====
void loop() {
  // reset WiFi button
  if (digitalRead(PINO_RESET_WIFI) == LOW) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Limpando WiFi...");
    Serial.println("🧹 Limpando WiFi e reiniciando...");
    WiFi.disconnect(true, true);
    delay(1000);
    lcd.setCursor(0, 1);
    lcd.print("Reiniciando...");
    delay(1500);
    ESP.restart();
  }

  server.handleClient();   // atende /dados
  network.update();
  verificarWiFi();

  // timeout: sem pacotes
  if (millis() - ultimoPacote > 30000) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sem dados do NRF!");
    lcd.setCursor(0, 1);
    lcd.print("ATENCAO!");
    digitalWrite(LED_VERDE, LOW);
    digitalWrite(LED_VERMELHO, HIGH);
    if (millis() - ultimoSom > 3000) {
      tone(BUZZER_PIN, 1000, 300);
      ultimoSom = millis();
    }
  }

  // ===== RECEPÇÃO RF (fragmentada) =====
  while (network.available()) {
    RF24NetworkHeader header;
    uint8_t packet[NRF_PAYLOAD_MAX];
    // Note: network.read copia até sizeof(packet) bytes.
    // Se o pacote enviado foi menor, o restante do buffer pode continuar com zeros.
    network.read(header, packet, sizeof(packet));
    ultimoPacote = millis();

    // Interpretar header do fragment
    if (FRAG_HDR_SIZE > NRF_PAYLOAD_MAX) {
      Serial.println("FRAG_HDR_SIZE > NRF_PAYLOAD_MAX (!)");
      continue;
    }
    FragHeader hdr;
    memcpy(&hdr, packet, FRAG_HDR_SIZE);
    uint32_t seq = hdr.seq;

    // Descobrir o comprimento real do fragment payload.
    // Heurística: procurar último byte não-zero; se todos zeros assumimos FRAG_PAYLOAD_MAX
    size_t real_len = 0;
    for (int i = FRAG_HDR_SIZE; i < NRF_PAYLOAD_MAX; ++i) {
      if (packet[i] != 0) real_len = i - FRAG_HDR_SIZE + 1;
    }
    if (real_len == 0) real_len = FRAG_PAYLOAD_MAX;

    // Primeiro fragmento?
    if (hdr.flags & 0x01) {
      reset_assembly();
      assembling_seq = seq;
      append_to_assembly(packet + FRAG_HDR_SIZE, real_len);
    } else {
      if (assembling_seq != seq) {
        Serial.printf("Fragment seq mismatch: got %u expected %u\n", seq, assembling_seq);
        continue;
      }
      append_to_assembly(packet + FRAG_HDR_SIZE, real_len);
    }

    // Último fragmento -> processar
    if (hdr.flags & 0x02) {
      if (seq <= last_seq_accepted) {
        Serial.printf("Replay/old seq %u <= last %u\n", seq, last_seq_accepted);
        reset_assembly();
        continue;
      }

      if (assem_len < IV_LEN + TAG_LEN) {
        Serial.println("assembled too small");
        reset_assembly();
        continue;
      }

      const uint8_t *iv = assem_buf;
      const uint8_t *tag = assem_buf + assem_len - TAG_LEN;
      const uint8_t *ct = assem_buf + IV_LEN;
      size_t ct_len = assem_len - IV_LEN - TAG_LEN;

      // AAD = version(1) + seq(4)
      uint8_t aad[5];
      aad[0] = 0x01;
      aad[1] = (seq>>24)&0xFF; aad[2] = (seq>>16)&0xFF; aad[3] = (seq>>8)&0xFF; aad[4] = seq & 0xFF;

      uint8_t *plaintext = (uint8_t*)malloc(ct_len + 1);
      bool ok = aes_gcm_decrypt(AES_KEY, iv, IV_LEN, aad, sizeof(aad), ct, ct_len, tag, plaintext);
      if (ok) {
        // plaintext deve conter a struct Payload em binário
        if (ct_len == sizeof(Payload)) {
          Payload p;
          memcpy(&p, plaintext, sizeof(Payload));

          // Atualiza variáveis globais para o endpoint /dados
          temperaturaAtual = p.temperatura;
          latAtual = p.latitude;
          lonAtual = p.longitude;
          // Timestamp simples (não é NTP; usa millis se não tiver GPS)
          dataHoraAtual = ""; // tenta extrair tempo do header GPS? (receptor não possui GPS aqui)
          // você pode adaptar pra setar dataHora via NTP ou via campo enviado pelo transmissor

          // Log serial e LCD
          Serial.printf("📥 Decrypted seq=%u -> Temp %.2f | Lat %.6f | Lng %.6f\n",
                        seq, p.temperatura, p.latitude, p.longitude);

          // Atualizar display
          lcd.clear();
          lcd.setCursor(0, 0);
          lcd.print("TEMP:");
          lcd.print(temperaturaAtual, 1);
          lcd.print((char)223);
          lcd.print("C");

          lcd.setCursor(0, 1);
          lcd.print("Lat:");
          lcd.print(latAtual, 4);

          lcd.setCursor(0, 2);
          lcd.print("Lng:");
          lcd.print(lonAtual, 4);

          lcd.setCursor(0, 3);
          if (WiFi.status() == WL_CONNECTED) lcd.print("IP: "), lcd.print(WiFi.localIP());
          last_seq_accepted = seq;

          // Alarme
          if (temperaturaAtual > 30.0 || temperaturaAtual < 10.0) {
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

        } else {
          Serial.printf("Decrypted size mismatch: %u != %u\n", (unsigned)ct_len, (unsigned)sizeof(Payload));
        }
      } else {
        Serial.printf("Auth failed for seq=%u\n", seq);
      }

      free(plaintext);
      reset_assembly();
    }
  } // end while network.available()
}
