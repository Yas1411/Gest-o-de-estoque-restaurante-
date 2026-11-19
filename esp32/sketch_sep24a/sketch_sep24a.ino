#include <WiFi.h>
#include <WiFiClient.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <DHT.h>
// Copie secrets.example.h para secrets.h e preencha (secrets.h não vai para o GitHub)
#include "secrets.h"
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;
WebServer server(80);
DHT dht(22, DHT22);
#define LED_UMIDADE 15
#define LED_TEMPERATURA 18
#define LED_VERDE 19
void handleRoot() {
char msg[1500];
snprintf(msg, 1500,
"<html>\
 <head>\ <meta http-equiv='refresh' content='4'/>\ <meta name='viewport' content='width=device-width, initial-scale=1'>\ <link rel='stylesheet' href='https://use.fontawesome.com/releases/v5.7.2/css/all.css' integrity='sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr' crossorigin='anonymous'>\ <title>TEMPERATURA E UMIDADE DO ESTOQUE (ZONA A)</title>\ <style>\ html { font-family: Arial; display: inline-block; margin: 0px auto; text-align: center;}\ h2 { font-size: 3.0rem; }\ p { font-size: 3.0rem; }\ .units { font-size: 1.2rem; }\ .dht-labels{ font-size: 1.5rem; vertical-align:middle; padding-bottom: 15px;}\ </style>\ </head>\ <body>\ <h2>TEMPERATURA E UMIDADE DO ESTOQUE (ZONA B)</h2>\ <p>\ <i class='fas fa-thermometer-half' style='color:
#ca3517;'></i>\ <span class='dht-labels'>Temperature</span>\ <span>%.2f</span>\ <sup class='units'>&deg;C</sup>\ </p>\ <p>\ <i class='fas fa-tint' style='color:
#00add6;'></i>\ <span class='dht-labels'>Humidity</span>\ <span>%.2f</span>\ <sup class='units'>&percnt;</sup>\ </p>\ </body>\ </html>", readDHTTemperature(), readDHTHumidity() ); 
server.send(200, "text/html", msg);
}
void setup(void) {
Serial.begin(115200);
dht.begin();
pinMode(LED_UMIDADE, OUTPUT);
pinMode(LED_TEMPERATURA, OUTPUT);
pinMode(LED_VERDE, OUTPUT);
digitalWrite(LED_UMIDADE, LOW);
digitalWrite(LED_TEMPERATURA, LOW);
digitalWrite(LED_VERDE, LOW);
WiFi.mode(WIFI_STA);
WiFi.begin(ssid, password);
Serial.println("");
while (WiFi.status() != WL_CONNECTED) {
delay(500);
Serial.print(".");
}
Serial.println("");
Serial.print("Connected to ");
Serial.println(ssid);
Serial.print("IP address: ");
Serial.println(WiFi.localIP());
if (MDNS.begin("esp32")) {
Serial.println("MDNS responder started");
}
server.on("/", handleRoot);
server.begin();
Serial.println("HTTP server started");
}
void loop() {
server.handleClient();
float umidade = dht.readHumidity();
float temperatura = dht.readTemperature();
if (!isnan(umidade)) {
Serial.print("Umidade: ");
Serial.println(umidade);
// LED vermelho
if (umidade >= 80.0) {
digitalWrite(LED_UMIDADE, HIGH);
} else {
digitalWrite(LED_UMIDADE, LOW);
}
// LED verde
if (umidade >= 60.0) {
digitalWrite(LED_VERDE, HIGH);
} else {
digitalWrite(LED_VERDE, LOW);
}
}
if (!isnan(temperatura)) {

Serial.print("Temperatura: ");  
Serial.println(temperatura);

if (temperatura >= 20.0) {  
  digitalWrite(LED_TEMPERATURA, HIGH);  
} else {  
  digitalWrite(LED_TEMPERATURA, LOW);  
}
}
delay(1000);
}
float readDHTTemperature() {
float t = dht.readTemperature();
if (isnan(t)) {
Serial.println("Failed to read from DHT sensor!");
return -1;
} else {
Serial.println(t);
return t;
}
}
float readDHTHumidity() {
float h = dht.readHumidity();
if (isnan(h)) {
Serial.println("Failed to read from DHT sensor!");
return -1;
} else {
Serial.println(h);
return h;
}
}
