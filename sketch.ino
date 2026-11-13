/*
 * ============================================================
 *  HAWK - Sistema Inteligente de Travessia Acessivel
 *  FIAP Global Solution 2026.2 | CitizenTech | Edge Computing
 *  Integrantes: [Nome completo - RM] (preencher todos)
 * ------------------------------------------------------------
 *  CAMADA 1 - DEVICE (este firmware, ESP32):
 *    - Potenciometro  = confianca (0-100%) de que ha uma pessoa com mobilidade
 *                       reduzida na faixa (no Wokwi substitui a camera/YOLO)
 *    - 3 LEDs veiculo + 2 LEDs pedestre + buzzer + LCD = resposta local
 *    - Botao          = injeta FALHA de sensor/camera (testa o modo seguro)
 *  CAMADA 2 - BACK-END: MQTT (broker publico; JSON ou FIWARE Ultralight)
 *  CAMADA 3 - DASHBOARD: dashboard.html (assina o mesmo topico MQTT)
 *
 *  Travessia ADAPTATIVA: enquanto a pessoa ainda e detectada na faixa perto
 *  do fim do prazo, o tempo e estendido (+3s por vez) ate o teto de 20s.
 *  Estados:  0 NORMAL | 1 DETECTADO | 2 TRAVESSIA | 3 LIBERACAO | 4 FALHA
 *  A decisao e tomada AQUI na borda: sem rede o semaforo continua seguro.
 * ============================================================
 */
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>   // LCD 16x2 I2C (SDA=21, SCL=22)

// ---------- Rede / MQTT ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* TOPICO_TELEMETRIA = "fiap-hawk-gs2026/cruzamento01/telemetria"; // device -> dashboard
const char* TOPICO_CAMERA     = "fiap-hawk-gs2026/cruzamento01/camera";     // YOLO/dashboard -> device
const char* TOPICO_FIWARE     = "/TEF/hawk001/attrs";                       // IoT Agent Ultralight
#define DEBUG_JSON 0       // 1 = imprime tambem o JSON bruto no Serial Monitor
#define USAR_FIWARE_UL 0   // 1 = publica tambem em Ultralight para o IoT Agent FIWARE

// ---------- Pinos ----------
const int PIN_POT = 34, PIN_BTN_FALHA = 4, PIN_BUZZER = 23;
const int PIN_V_VERM = 25, PIN_V_AMAR = 26, PIN_V_VERDE = 27;   // semaforo de veiculos
const int PIN_P_VERM = 32, PIN_P_VERDE = 33;                    // semaforo de pedestres

// ---------- Limites (justificados no video) ----------
const float LIMIAR_CONF        = 70.0;  // % minimo: abaixo disso e ruido/falso positivo
const int   JANELA_MEDIA       = 5;     // amostras da media movel (5 x 100ms = 0,5s)
const unsigned long T_AMOSTRA       = 100;   // intervalo de leitura da confianca
const unsigned long T_CONFIRMA      = 1500;  // confianca alta por 1,5s => deteccao valida
const unsigned long T_AMARELO       = 3000;  // amarelo p/ veiculos frearem com seguranca
const unsigned long T_TRAV_MIN      = 8000;  // verde minimo de pedestre (garantia de seguranca)
const unsigned long T_TRAV_MAX      = 20000; // teto absoluto: nunca prende o transito
const unsigned long T_EXTENSAO      = 3000;  // cada extensao acrescenta 3s a travessia
const unsigned long T_MARGEM_EXT    = 2000;  // decide estender nos ultimos 2s do prazo atual
const unsigned long T_FAIXA_LIVRE   = 1000;  // sem deteccao por 1s => faixa livre (pessoa saiu)
const unsigned long T_LIBERACAO     = 2500;  // pedestre vermelho + veiculo amarelo antes do verde
const unsigned long T_COOLDOWN      = 4000;  // evita nova travessia imediata apos liberar
const int   MAX_LEITURAS_INVALIDAS = 3; // 3 invalidas seguidas => FALHA
const unsigned long T_RECUPERA      = 3000;  // leituras OK por 3s => sai da FALHA
const unsigned long T_CAMERA_VALIDA = 3000;  // mensagem MQTT da camera vale por 3s
const unsigned long T_PUBLICA       = 1000;  // telemetria a cada 1s (+ a cada mudanca de estado)
const unsigned long T_LCD           = 250;   // atualiza o LCD 4x por segundo
const unsigned long T_CICLO_FALHA   = 16000; // ciclo fixo do modo seguro: 6s verde, 2s amarelo, 8s pedestre

// ---------- Estados ----------
enum Estado { NORMAL = 0, DETECTADO = 1, TRAVESSIA = 2, LIBERACAO = 3, FALHA = 4 };
const char* NOMES[] = { "NORMAL", "DETECTADO", "TRAVESSIA", "LIBERACAO", "FALHA" };

Estado estado = NORMAL;
unsigned long tEstado = 0;

// ---------- Variaveis de leitura ----------
float bufConf[JANELA_MEDIA]; int idxConf = 0, qtdConf = 0;
float confMedia = 0;
int   leiturasInvalidas = 0;
bool  falhaSimulada = false;
const char* fonteConf = "potenciometro";

float camConf = 0; unsigned long tUltimaCamera = 0;   // dado vindo do YOLO via MQTT

unsigned long tUltAmostra = 0, tUltPublica = 0, tUltMqtt = 0, tUltLcd = 0;
unsigned long tConfirmaIni = 0, tLivreIni = 0, tFimCiclo = 0, tOkDesde = 0;
unsigned long tEsperaIni = 0, esperaMs = 0, travessiaMs = 0;
unsigned long prazoTravMs = T_TRAV_MIN;   // prazo atual da travessia (cresce se houver extensao)
int extensoes = 0;                        // quantas vezes a travessia foi estendida
const char* motivoFim = "-";
bool buzzerLigado = false;
int  ultBtn = HIGH; unsigned long tBtn = 0;

// ---------- LCD ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);
String lcdAnt0 = "", lcdAnt1 = "";

WiFiClient wifi;
PubSubClient mqtt(wifi);

// ============================================================
//  Atuadores
// ============================================================
void setLeds(bool vr, bool va, bool vv, bool pr, bool pv) {
  digitalWrite(PIN_V_VERM, vr);  digitalWrite(PIN_V_AMAR, va);  digitalWrite(PIN_V_VERDE, vv);
  digitalWrite(PIN_P_VERM, pr);  digitalWrite(PIN_P_VERDE, pv);
}

// Buzzer: liga/desliga so quando muda (evita chamar tone() em todo loop)
void buzzer(bool ligar) {
  if (ligar && !buzzerLigado)      tone(PIN_BUZZER, 2000);
  else if (!ligar && buzzerLigado) noTone(PIN_BUZZER);
  buzzerLigado = ligar;
}

// Padrao sonoro por estado: periodo e duracao do bip (acessibilidade p/ pessoas cegas)
void atualizaBuzzer() {
  unsigned long periodo = 0, bip = 0;
  if (estado == DETECTADO)      { periodo = 1000; bip = 200; }   // aviso lento
  else if (estado == TRAVESSIA) { periodo = 500;  bip = 150; }   // ritmo rapido = pode atravessar
  else if (estado == FALHA)     { periodo = 2000; bip = 100; }   // bip raro = modo seguro
  if (periodo == 0) { buzzer(false); return; }
  buzzer((millis() % periodo) < bip);
}

// ============================================================
//  Sensor (confianca da deteccao)
// ============================================================
// Usa a camera (MQTT) se chegou mensagem recente, senao o potenciometro.
bool lerConfianca(float &out) {
  if (tUltimaCamera != 0 && millis() - tUltimaCamera < T_CAMERA_VALIDA) {
    fonteConf = "camera_mqtt";
    out = camConf;
  } else {
    fonteConf = "potenciometro";
    out = analogRead(PIN_POT) * 100.0 / 4095.0;
  }
  return !(isnan(out) || out < 0.0 || out > 100.0);   // fora de 0-100 = leitura invalida
}

// Media movel: suaviza o ruido da confianca
float mediaMovel(float novo) {
  bufConf[idxConf] = novo;
  idxConf = (idxConf + 1) % JANELA_MEDIA;
  if (qtdConf < JANELA_MEDIA) qtdConf++;
  float soma = 0;
  for (int i = 0; i < qtdConf; i++) soma += bufConf[i];
  return soma / qtdConf;
}

void amostraSensores() {
  float c;
  if (lerConfianca(c)) { confMedia = mediaMovel(c); leiturasInvalidas = 0; }
  else if (leiturasInvalidas < 99) leiturasInvalidas++;    // invalida e contada; so varias seguidas viram falha
}

bool sensoresOk() {
  return !falhaSimulada && leiturasInvalidas < MAX_LEITURAS_INVALIDAS;
}

void lerBotao() {   // debounce simples: alterna a falha simulada
  int b = digitalRead(PIN_BTN_FALHA);
  if (b == LOW && ultBtn == HIGH && millis() - tBtn > 250) {
    falhaSimulada = !falhaSimulada; tBtn = millis();
    Serial.printf("\n*** BOTAO: falha simulada %s ***\n", falhaSimulada ? "LIGADA" : "DESLIGADA");
  }
  ultBtn = b;
}

// ============================================================
//  Maquina de estados (decisao na borda)
// ============================================================
void entrarEstado(Estado novo) {
  Serial.println();
  Serial.println("==================================================");
  Serial.printf (" [%6.1fs]  ESTADO: %s  ->  %s\n", millis() / 1000.0, NOMES[estado], NOMES[novo]);
  Serial.printf (" confianca %.0f%%\n", confMedia);
  if (novo == LIBERACAO || (novo == NORMAL && estado == FALHA))
    Serial.printf(" motivo: %s | travessia: %lu ms | extensoes: %d\n", motivoFim, travessiaMs, extensoes);
  Serial.println("==================================================");
  estado = novo; tEstado = millis();
  tLivreIni = 0; tConfirmaIni = 0;
  if (novo == TRAVESSIA) { esperaMs = millis() - tEsperaIni; prazoTravMs = T_TRAV_MIN; extensoes = 0; }
  tUltPublica = 0;                          // forca publicacao imediata
}

void atualizaMaquina() {
  unsigned long agora = millis(), dt = agora - tEstado;

  // Qualquer estado -> FALHA se a leitura for invalida
  if (estado != FALHA && !sensoresOk()) { motivoFim = "sensor_invalido"; entrarEstado(FALHA); return; }

  switch (estado) {
    case NORMAL:
      setLeds(0, 0, 1, 1, 0);                               // veiculo verde, pedestre vermelho
      if (confMedia >= LIMIAR_CONF && agora - tFimCiclo >= T_COOLDOWN) {
        if (tConfirmaIni == 0) { tConfirmaIni = agora; tEsperaIni = agora; }
        if (agora - tConfirmaIni >= T_CONFIRMA) entrarEstado(DETECTADO);  // persistiu => nao e falso positivo
      } else tConfirmaIni = 0;
      break;

    case DETECTADO:
      setLeds(0, 1, 0, 1, 0);                               // amarelo p/ veiculos
      if (dt >= T_AMARELO) entrarEstado(TRAVESSIA);
      break;

    case TRAVESSIA: {
      setLeds(1, 0, 0, 0, 1);                               // veiculos parados, pedestre liberado
      bool aindaNaFaixa = (confMedia >= LIMIAR_CONF);       // camera ainda ve a pessoa atravessando
      if (aindaNaFaixa) tLivreIni = 0; else if (tLivreIni == 0) tLivreIni = agora;
      bool faixaLivre = (tLivreIni != 0 && agora - tLivreIni >= T_FAIXA_LIVRE);

      // EXTENSAO: perto do fim do prazo, se a pessoa ainda esta na faixa, ganha mais tempo
      if (aindaNaFaixa && dt + T_MARGEM_EXT >= prazoTravMs && prazoTravMs < T_TRAV_MAX) {
        prazoTravMs = min(prazoTravMs + T_EXTENSAO, T_TRAV_MAX);
        extensoes++;
        tUltPublica = 0;                                    // avisa o dashboard na hora
        Serial.printf(">>> EXTENSAO #%d: pessoa ainda atravessando, novo prazo = %lu s\n", extensoes, prazoTravMs / 1000);
      }

      if (dt >= prazoTravMs && faixaLivre) { motivoFim = "faixa_livre"; travessiaMs = dt; entrarEstado(LIBERACAO); }
      else if (dt >= T_TRAV_MAX)           { motivoFim = "timeout";     travessiaMs = dt; entrarEstado(LIBERACAO); }
      break;
    }

    case LIBERACAO:
      setLeds(0, 1, 0, 1, 0);                               // pedestre vermelho, veiculo amarelo
      if (dt >= T_LIBERACAO) { tFimCiclo = agora; entrarEstado(NORMAL); }
      break;

    case FALHA: {                                           // ciclo fixo com travessia minima garantida
      unsigned long t = dt % T_CICLO_FALHA;
      if (t < 6000)      setLeds(0, 0, 1, 1, 0);
      else if (t < 8000) setLeds(0, 1, 0, 1, 0);
      else               setLeds(1, 0, 0, 0, 1);
      if (sensoresOk()) { if (tOkDesde == 0) tOkDesde = agora; } else tOkDesde = 0;
      if (tOkDesde != 0 && agora - tOkDesde >= T_RECUPERA) { tOkDesde = 0; motivoFim = "recuperado"; tFimCiclo = agora; entrarEstado(NORMAL); }
      break;
    }
  }
}

// ============================================================
//  LCD: mostra ao cidadao o que o semaforo esta fazendo (sem acentos)
// ============================================================
String ajusta16(String t) { while (t.length() < 16) t += ' '; return t.substring(0, 16); }

void escreveLcd(String l0, String l1) {      // so reescreve a linha que mudou (evita piscar)
  l0 = ajusta16(l0); l1 = ajusta16(l1);
  if (l0 != lcdAnt0) { lcd.setCursor(0, 0); lcd.print(l0); lcdAnt0 = l0; }
  if (l1 != lcdAnt1) { lcd.setCursor(0, 1); lcd.print(l1); lcdAnt1 = l1; }
}

void atualizaLcd() {
  char b[24];
  unsigned long dt = millis() - tEstado;
  switch (estado) {
    case NORMAL:
      snprintf(b, sizeof(b), "Conf:%3.0f%% %s", confMedia, strcmp(fonteConf, "camera_mqtt") == 0 ? "CAM" : "POT");
      escreveLcd("HAWK   NORMAL", b); break;
    case DETECTADO:
      escreveLcd("PESSOA DETECTADA", "Parando veiculos"); break;
    case TRAVESSIA: {
      unsigned long resta = (prazoTravMs > dt) ? (prazoTravMs - dt) / 1000 : 0;
      snprintf(b, sizeof(b), "Resta %2lus Ext:%d", resta, extensoes);
      escreveLcd("PODE ATRAVESSAR!", b); break;
    }
    case LIBERACAO:
      escreveLcd("TRAVESSIA OK", "Liberando fluxo"); break;
    case FALHA:
      escreveLcd("MODO SEGURO", falhaSimulada ? "Falha simulada" : "Leitura invalida"); break;
  }
}

// ============================================================
//  MQTT (camadas 2 e 3)
// ============================================================
void aoReceber(char* topico, byte* payload, unsigned int len) {
  // Espera algo como {"cadeirante":true,"conf":87}
  String msg; for (unsigned int i = 0; i < len; i++) msg += (char)payload[i];
  int p = msg.indexOf("\"conf\":");
  if (p >= 0) camConf = msg.substring(p + 7).toFloat();
  if (msg.indexOf("\"cadeirante\":false") >= 0) camConf = 0;
  tUltimaCamera = millis();
  Serial.printf("[MQTT<-camera] %s\n", msg.c_str());
}

void conectaMqtt() {   // nao bloqueante: tenta a cada 5s e o semaforo segue funcionando offline
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected() || millis() - tUltMqtt < 5000) return;
  tUltMqtt = millis();
  String cid = "hawk001-" + String((uint32_t)esp_random(), HEX);
  if (mqtt.connect(cid.c_str())) { mqtt.subscribe(TOPICO_CAMERA); Serial.println("[MQTT] conectado ao broker"); }
}

// Linha de status legivel (1x por segundo)
void imprimeStatus() {
  unsigned long dt = millis() - tEstado;
  char extra[40] = "";
  if (estado == TRAVESSIA)
    snprintf(extra, sizeof(extra), " | resta %2lus | ext %d", (prazoTravMs > dt) ? (prazoTravMs - dt) / 1000 : 0, extensoes);
  Serial.printf("[%6.1fs] %-9s | conf %3.0f%% | %s | MQTT %s%s\n",
                millis() / 1000.0, NOMES[estado], confMedia,
                strcmp(fonteConf, "camera_mqtt") == 0 ? "CAMERA" : "POT   ",
                mqtt.connected() ? "ok" : "--", extra);
}

void publica() {
  char json[400];
  snprintf(json, sizeof(json),
    "{\"id\":\"hawk001\",\"estado\":\"%s\",\"cod\":%d,\"conf\":%.1f,"
    "\"fonte\":\"%s\",\"falha\":%s,\"espera_ms\":%lu,\"travessia_ms\":%lu,\"motivo\":\"%s\","
    "\"extensoes\":%d,\"prazo_ms\":%lu}",
    NOMES[estado], (int)estado, confMedia, fonteConf,
    sensoresOk() ? "false" : "true", esperaMs, travessiaMs, motivoFim,
    extensoes, prazoTravMs);
  imprimeStatus();                                         // resumo legivel no Serial Monitor
#if DEBUG_JSON
  Serial.println(json);
#endif
  if (!mqtt.connected()) return;
  mqtt.publish(TOPICO_TELEMETRIA, json);
#if USAR_FIWARE_UL
  char ul[180];   // Ultralight 2.0: atributo|valor|atributo|valor
  snprintf(ul, sizeof(ul), "e|%d|c|%.1f|f|%d|w|%lu|t|%lu|x|%d",
           (int)estado, confMedia, sensoresOk() ? 0 : 1, esperaMs, travessiaMs, extensoes);
  mqtt.publish(TOPICO_FIWARE, ul);
#endif
}

// ============================================================
void setup() {
  Serial.begin(115200);
  int saidas[] = { PIN_V_VERM, PIN_V_AMAR, PIN_V_VERDE, PIN_P_VERM, PIN_P_VERDE, PIN_BUZZER };
  for (int p : saidas) pinMode(p, OUTPUT);
  pinMode(PIN_BTN_FALHA, INPUT_PULLUP);
  setLeds(0, 0, 1, 1, 0);

  Wire.begin(21, 22);                                      // I2C do LCD
  lcd.init(); lcd.backlight();
  escreveLcd("HAWK", "Iniciando...");

  Serial.println("\n##################################################");
  Serial.println("#   HAWK - Travessia Acessivel | FIAP GS 2026.2   #");
  Serial.println("##################################################");
  Serial.println("Legenda: conf = confianca da deteccao");
  Serial.println("         POT = potenciometro | CAMERA = YOLO via MQTT\n");

  WiFi.begin(WIFI_SSID, WIFI_PASS);                        // conexao em segundo plano
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(aoReceber);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(2);                                // nao trava a borda se a rede cair
  tEstado = millis();
}

void loop() {
  conectaMqtt();
  mqtt.loop();
  lerBotao();
  if (millis() - tUltAmostra >= T_AMOSTRA) { tUltAmostra = millis(); amostraSensores(); }
  atualizaMaquina();
  atualizaBuzzer();
  if (millis() - tUltLcd >= T_LCD) { tUltLcd = millis(); atualizaLcd(); }
  if (millis() - tUltPublica >= T_PUBLICA) { tUltPublica = millis(); publica(); }
}
