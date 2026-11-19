/*
  =====================================================================
  PROJETO: Sistema de monitoramento de temperatura do estoque
  SENSORES NESSE CÓDIGO:
    - DHT22   -> Zona B (estoque seco, temperatura ambiente)
    - DS18B20 -> Zona A (freezer)
  =====================================================================
  Cada zona tem seu próprio conjunto de 3 LEDs (verde/amarelo/vermelho),
  e só UM LED fica aceso por vez, por zona. A leitura e os LEDs são
  atualizados a cada 5 segundos. A cada 30 segundos, as duas leituras
  são enviadas ao site (Django, no Render) via HTTP POST, uma request
  para cada zona, usando o MESMO token do ESP32.
  =====================================================================
*/

// ---------------------------------------------------------------
// BIBLIOTECAS
// ---------------------------------------------------------------
#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
#include <OneWire.h>            // necessária para o DS18B20
#include <DallasTemperature.h>  // necessária para o DS18B20
#include <HTTPClient.h>         // pra enviar os dados pro site
#include <WiFiClientSecure.h>   // pra falar HTTPS com o site (Render usa HTTPS)

// ---------------------------------------------------------------
// CREDENCIAIS (Wi-Fi, URL do site e tokens dos sensores)
// Ficam no arquivo "secrets.h", na mesma pasta deste sketch, que NÃO vai
// para o GitHub (está no .gitignore). Para configurar:
//   1) copie "secrets.example.h" para "secrets.h"
//   2) preencha SSID, senha, URL do site e os tokens
// Os tokens vêm do Django Admin (/admin/) > "Sensores ESP32". Cada sensor
// fica travado na sua zona, então há um token por zona; o site recusa
// qualquer envio que use o token errado para outra zona.
// ---------------------------------------------------------------
#include "secrets.h"
// Disponibiliza: WIFI_SSID, WIFI_PASSWORD, SERVER_URL, TOKEN_ZONA_A, TOKEN_ZONA_B

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
const char *serverUrl = SERVER_URL;
const char *tokenZonaA = TOKEN_ZONA_A; // DS18B20 - Zona A (freezer)
const char *tokenZonaB = TOKEN_ZONA_B; // DHT22   - Zona B (estoque seco)

// A cada quantos milissegundos enviamos as leituras pro site
const unsigned long INTERVALO_ENVIO = 30000; // 30 segundos
unsigned long ultimoEnvio = 0;

WebServer server(80);

// ---------------------------------------------------------------
// SENSOR DHT22 - Zona B (estoque seco)
// ---------------------------------------------------------------
#define PINO_DHT22 4
DHT dht(PINO_DHT22, DHT22);

// ---------------------------------------------------------------
// SENSOR DS18B20 - Zona A (freezer)
// O DS18B20 usa o protocolo "OneWire" (um fio só pra dado).
// Por isso precisa desses dois objetos: um pro barramento (oneWire)
// e outro pra gerenciar o sensor em cima desse barramento (sensorA).
// ---------------------------------------------------------------
#define PINO_DS18B20 13  // GPIO13 (GPIO5 é pino de boot, evitar)
OneWire oneWire(PINO_DS18B20);
DallasTemperature sensorA(&oneWire);

// ---------------------------------------------------------------
// PINOS DOS LEDS - Zona B (DHT22)
// ---------------------------------------------------------------
#define LED_VERDE_ZONA_B    18
#define LED_AMARELO_ZONA_B  19
#define LED_VERMELHO_ZONA_B 21

// ---------------------------------------------------------------
// PINOS DOS LEDS - Zona A (DS18B20)
// ---------------------------------------------------------------
#define LED_VERDE_ZONA_A    22
#define LED_AMARELO_ZONA_A  23
#define LED_VERMELHO_ZONA_A 25

// ---------------------------------------------------------------
// LIMITES DE TEMPERATURA - Zona B (estoque seco, ambiente tolerante)
// ---------------------------------------------------------------
#define B_TEMP_MIN_IDEAL   18.0   // abaixo disso = frio demais (amarelo)
#define B_TEMP_MAX_IDEAL   25.0   // até aqui = verde
#define B_TEMP_MAX_ATENCAO 28.0   // até aqui = amarelo; acima = vermelho

// ---------------------------------------------------------------
// LIMITES DE TEMPERATURA - Zona A (freezer)
// Aqui quanto mais frio, melhor - não existe "frio demais".
// ---------------------------------------------------------------
#define A_TEMP_MAX_IDEAL   -15.0  // até -15°C (ou menos) = verde
#define A_TEMP_MAX_ATENCAO -8.0   // até -8°C = amarelo; acima = vermelho (risco de descongelar)

// ---------------------------------------------------------------
// Nomes para os 3 estados possíveis, em vez de números soltos.
// ---------------------------------------------------------------
enum StatusNivel { VERDE = 0, AMARELO = 1, VERMELHO = 2, NENHUM = -1 };

// Guardam o ÚLTIMO status de cada zona, pra só avisar no Serial
// quando o status realmente mudar.
StatusNivel statusAnteriorB = NENHUM;
StatusNivel statusAnteriorA = NENHUM;

// ---------------------------------------------------------------
// FUNÇÕES: decidem o status a partir da temperatura de cada zona.
// Como as faixas são diferentes, cada zona tem sua própria função.
// ---------------------------------------------------------------
StatusNivel statusZonaB(float t) {
  if (t < B_TEMP_MIN_IDEAL) return AMARELO;
  if (t <= B_TEMP_MAX_IDEAL) return VERDE;
  if (t <= B_TEMP_MAX_ATENCAO) return AMARELO;
  return VERMELHO;
}

StatusNivel statusZonaA(float t) {
  if (t <= A_TEMP_MAX_IDEAL) return VERDE;
  if (t <= A_TEMP_MAX_ATENCAO) return AMARELO;
  return VERMELHO;
}

// ---------------------------------------------------------------
// FUNÇÃO GENÉRICA: acende o LED certo de UMA zona e apaga os
// outros dois dessa mesma zona.
// ---------------------------------------------------------------
void acenderLed(StatusNivel s, int pinoVerde, int pinoAmarelo, int pinoVermelho) {
  digitalWrite(pinoVerde,    s == VERDE    ? HIGH : LOW);
  digitalWrite(pinoAmarelo,  s == AMARELO  ? HIGH : LOW);
  digitalWrite(pinoVermelho, s == VERMELHO ? HIGH : LOW);
}

// ---------------------------------------------------------------
// Texto e cor de cada status (usados no Serial e na página web)
// ---------------------------------------------------------------
const char* textoStatus(StatusNivel s) {
  switch (s) {
    case VERDE:    return "Temperatura adequada";
    case AMARELO:  return "Atencao: variacao estranha";
    case VERMELHO: return "ALERTA! Temperatura alarmante";
    default:       return "Sem leitura";
  }
}

const char* corStatus(StatusNivel s) {
  switch (s) {
    case VERDE:    return "#2e7d32";
    case AMARELO:  return "#f9a825";
    case VERMELHO: return "#c62828";
    default:       return "#757575";
  }
}

// ---------------------------------------------------------------
// FUNÇÃO: avisa no Serial Monitor quando o status de UMA zona
// muda. Recebe o nome da zona só pra identificar na mensagem.
// ---------------------------------------------------------------
void avisarSeMudou(const char* nomeZona, StatusNivel novoStatus,
                    StatusNivel &statusAnterior, float temperatura) {
  if (novoStatus == statusAnterior) return;

  Serial.print("[");
  Serial.print(nomeZona);
  Serial.print("] ");

  if (novoStatus == VERMELHO) {
    Serial.print("ALERTA!! Temperatura alarmante - ");
    Serial.print(temperatura);
    Serial.println(" C - risco de prejuizo!");
  } else {
    Serial.println(textoStatus(novoStatus));
  }

  statusAnterior = novoStatus;
}

// ---------------------------------------------------------------
// PÁGINA WEB LOCAL: mostra as duas zonas, cada uma no seu card.
// ---------------------------------------------------------------
void handleRoot() {
  char msg[3500];

  float tempB = readDHTTemperature();
  float umidB = readDHTHumidity();
  float tempA = readDS18B20Temperature();

  StatusNivel statusB = (tempB == -999) ? NENHUM : statusZonaB(tempB);
  StatusNivel statusA = (tempA == -999) ? NENHUM : statusZonaA(tempA);

  snprintf(msg, sizeof(msg),

  "<html>\
  <head>\
    <meta http-equiv='refresh' content='5'/>\
    <meta name='viewport' content='width=device-width, initial-scale=1'>\
    <link rel='stylesheet' href='https://use.fontawesome.com/releases/v5.7.2/css/all.css' integrity='sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr' crossorigin='anonymous'>\
    <title>Estoque - Zonas A e B</title>\
    <style>\
      * { box-sizing: border-box; }\
      body {\
        margin: 0; min-height: 100vh; display: flex; flex-direction: column;\
        align-items: center; justify-content: center; gap: 20px;\
        font-family: 'Segoe UI', Arial, sans-serif;\
        background: linear-gradient(135deg, #e8f0fe, #f5f5f5); padding: 20px 0;\
      }\
      .card {\
        background: white; border-radius: 20px; box-shadow: 0 8px 24px rgba(0,0,0,0.15);\
        padding: 25px 35px; max-width: 420px; width: 90%%; text-align: center;\
      }\
      h2 { font-size: 1.3rem; color: #333; margin-bottom: 20px; }\
      .leitura { display: flex; justify-content: space-around; margin-bottom: 20px; }\
      .leitura div { text-align: center; }\
      .leitura .valor { font-size: 2.0rem; font-weight: bold; color: #222; }\
      .leitura .rotulo { font-size: 0.85rem; color: #777; }\
      .status-box { padding: 15px; border-radius: 14px; color: white; font-size: 1.05rem; font-weight: bold; }\
    </style>\
  </head>\
  <body>\
    <div class='card'>\
      <h2>ESTOQUE SECO &mdash; ZONA B</h2>\
      <div class='leitura'>\
        <div><i class='fas fa-thermometer-half' style='color:#ca3517;'></i>\
          <div class='valor'>%.1f&deg;C</div><div class='rotulo'>Temperatura</div></div>\
        <div><i class='fas fa-tint' style='color:#00add6;'></i>\
          <div class='valor'>%.1f%%</div><div class='rotulo'>Umidade</div></div>\
      </div>\
      <div class='status-box' style='background-color:%s;'>%s</div>\
    </div>\
    <div class='card'>\
      <h2>FREEZER &mdash; ZONA A</h2>\
      <div class='leitura'>\
        <div><i class='fas fa-snowflake' style='color:#2196f3;'></i>\
          <div class='valor'>%.1f&deg;C</div><div class='rotulo'>Temperatura</div></div>\
      </div>\
      <div class='status-box' style='background-color:%s;'>%s</div>\
    </div>\
  </body>\
</html>",
           tempB, umidB, corStatus(statusB), textoStatus(statusB),
           tempA, corStatus(statusA), textoStatus(statusA)
          );

  server.send(200, "text/html", msg);
}

// ---------------------------------------------------------------
// FUNÇÃO GENÉRICA: envia UMA leitura de temperatura para o site
// Django, via HTTP POST, no formato JSON que a view
// "receber_temperatura" espera: {"token","zona","temperatura"}.
// Recebe o token certo da zona, a letra da zona ("A" ou "B") e o
// valor lido, assim serve pros dois sensores sem repetir código.
// ---------------------------------------------------------------
void enviarTemperaturaParaSite(const char* token, const char* zona, float temperatura) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.print("[Zona ");
    Serial.print(zona);
    Serial.println("] WiFi desconectado, nao foi possivel enviar ao site.");
    return;
  }

  HTTPClient http;
  WiFiClientSecure client;
  client.setInsecure(); // ignora validação de certificado (ok pra TCC)

  if (!http.begin(client, serverUrl)) {
    Serial.println("Falha ao iniciar conexao HTTP com o site.");
    return;
  }

  http.addHeader("Content-Type", "application/json");

  String payload = String("{\"token\":\"") + token +
                    "\",\"zona\":\"" + zona +
                    "\",\"temperatura\":" + String(temperatura, 2) + "}";

  int httpCode = http.POST(payload);

  if (httpCode > 0) {
    String resposta = http.getString();
    Serial.print("[Zona ");
    Serial.print(zona);
    Serial.print("] Site respondeu HTTP ");
    Serial.print(httpCode);
    Serial.print(" - ");
    Serial.println(resposta);
  } else {
    Serial.print("[Zona ");
    Serial.print(zona);
    Serial.print("] Erro ao enviar pro site: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ---------------------------------------------------------------
// SETUP: roda uma vez, quando o ESP32 liga.
// ---------------------------------------------------------------
void setup(void) {

  Serial.begin(115200);
  dht.begin();
  sensorA.begin(); // inicializa o barramento do DS18B20

  int pinosLed[] = {
    LED_VERDE_ZONA_B, LED_AMARELO_ZONA_B, LED_VERMELHO_ZONA_B,
    LED_VERDE_ZONA_A, LED_AMARELO_ZONA_A, LED_VERMELHO_ZONA_A
  };
  for (int i = 0; i < 6; i++) {
    pinMode(pinosLed[i], OUTPUT);
    digitalWrite(pinosLed[i], LOW);
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.println("");
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 40) { // ~20s de tentativa
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\nFalha ao conectar no WiFi. Verifique SSID/senha/banda (2.4GHz).");
  } else {
    Serial.println("");
    Serial.print("Conectado a ");
    Serial.println(ssid);
    Serial.print("Endereco IP: ");
    Serial.println(WiFi.localIP());
  }

  if (MDNS.begin("esp32")) {
    Serial.println("MDNS iniciado");
  }

  server.on("/", handleRoot);
  server.begin();
  Serial.println("Servidor HTTP iniciado");
}

// ---------------------------------------------------------------
// LOOP: roda pra sempre. A cada 5 segundos, lê os dois sensores,
// decide o status de cada zona e atualiza os LEDs. A cada
// INTERVALO_ENVIO, envia as duas leituras pro site.
// ---------------------------------------------------------------
void loop() {

  server.handleClient();

  // --- Zona B (DHT22) ---
  float tempB = readDHTTemperature();
  float umidB = readDHTHumidity();

  if (tempB != -999) {
    Serial.print("[Zona B] Temperatura: ");
    Serial.print(tempB);
    Serial.print(" C | Umidade: ");
    Serial.println(umidB);

    StatusNivel statusB = statusZonaB(tempB);
    acenderLed(statusB, LED_VERDE_ZONA_B, LED_AMARELO_ZONA_B, LED_VERMELHO_ZONA_B);
    avisarSeMudou("Zona B", statusB, statusAnteriorB, tempB);
  }

  // --- Zona A (DS18B20) ---
  float tempA = readDS18B20Temperature();

  if (tempA != -999) {
    Serial.print("[Zona A] Temperatura: ");
    Serial.println(tempA);

    StatusNivel statusA = statusZonaA(tempA);
    acenderLed(statusA, LED_VERDE_ZONA_A, LED_AMARELO_ZONA_A, LED_VERMELHO_ZONA_A);
    avisarSeMudou("Zona A", statusA, statusAnteriorA, tempA);
  }

  // --- Envio pro site, a cada INTERVALO_ENVIO, pras duas zonas ---
  if (millis() - ultimoEnvio >= INTERVALO_ENVIO) {
    if (tempB != -999) enviarTemperaturaParaSite(tokenZonaB, "B", tempB);
    if (tempA != -999) enviarTemperaturaParaSite(tokenZonaA, "A", tempA);
    ultimoEnvio = millis();
  }

  delay(5000); // atualiza a cada 5 segundos
}

// ---------------------------------------------------------------
// FUNÇÕES AUXILIARES DE LEITURA
// Devolvem -999 em caso de falha, pra facilitar a checagem.
// ---------------------------------------------------------------
float readDHTTemperature() {
  float t = dht.readTemperature();
  if (isnan(t)) {
    Serial.println("Falha ao ler temperatura do DHT22!");
    return -999;
  }
  return t;
}

float readDHTHumidity() {
  float h = dht.readHumidity();
  if (isnan(h)) {
    Serial.println("Falha ao ler umidade do DHT22!");
    return -999;
  }
  return h;
}

float readDS18B20Temperature() {
  sensorA.requestTemperatures();           // pede ao sensor pra fazer uma leitura
  float t = sensorA.getTempCByIndex(0);    // pega o resultado (sensor número 0 do barramento)
  if (t == DEVICE_DISCONNECTED_C) {
    Serial.println("Falha ao ler o DS18B20! Verifique a fiacao/pull-up.");
    return -999;
  }
  return t;
}
