/**
 * Steuerung eines Tores
 * Kommunikation zwischen weitern ESP32 die Tore steuern über ESP_NOW
 * Web- und Telegram Interface, + Kommands über Serial
 * Temperaturmessen und anzeigen auf Display + interfaces
 *
 * @author Bataev Suliman
 * @author Danzinger Elias
 * @version 2024-04-27 1.0
 */

// Librarys includen, die nötig sind um alle Funktionen korrekt einbinden zu können
#include <ESP32Servo.h>                       // Für Servo
#include <DHT.h>                              // Für DHT Sensor
#include <Adafruit_Sensor.h>                  // Für DHT Sensor
#include <esp_now.h>                          // Für ESP-NOW
#include <WiFi.h>                             // Für ESP-NOW, Webserver und Telegramm interface
#include <AsyncTCP.h>                         // Für Webserver
#include <ESPAsyncWebServer.h>                // Für Webserver
#include <WiFiClientSecure.h>                 // Für Telegramm interface
#include <UniversalTelegramBot.h>             // Für Telegramm interface
#include <ArduinoJson.h>                      // Für Telegramm interface

#define THIS 3                                // Gibt an welches der 3 ESP32 - Boards dieses Board ist
#define LED_BUILTIN 2                         // Eingabaute LED, verwendung zur Statusüberprüfung / debugging
#define TOR_OUT 13                            // Output PIN an dem der Servo hängt, verwendung zur Steuerung des Servos (Tors)
#define DHTPIN 32                             // Input PIN an dem der DHT-Sensor angeschlossen ist, verwendung zum messen von Temperatur und Luftfeuchtigkeit
#define DHTTYPE DHT22                         // DHT 22 (AM2302), Typ des anegschlossenen DHT-Sensors

// Daten des Netzwerkes, um den Webserver und Telegramm bot verwenden zu können,
// muss man im selben Netz sein, wie der ESP32
const char* ssid = "E";                       // Netzwerkname
const char* password = "xxxxxxxx";            // Passwort des Netzwerks

// Struktur von über ESP - NOW versendeten / eingelesenen Nachrichten
typedef struct struct_message {
  byte src;
  byte dest;
  byte action;
  
  String temp;
  String hum;
  bool gateState;
} struct_message;

struct_message income;                        // neues struct_message Objekt income deklarieren, in das eingehende Nachrichten kopiert werden
struct_message outcome;                       // neues struct_message Objekt outcome deklarieren, das über ESP-NOW versendet wird

esp_now_peer_info_t peerInfoOne;              // Variable für Peer Info des 1. Boards
esp_now_peer_info_t peerInfoTwo;              // Variable für Peer Info des 2. Boards
esp_now_peer_info_t peerInfoThree;            // Variable für Peer Info des 3. Boards

uint8_t one[] = {0x40, 0x22, 0xD8, 0x56, 0x82, 0x14};   // MAC-Adresse des 1. ESP32
uint8_t two[] = {0xB4, 0x8A, 0x0A, 0x57, 0xBE, 0x18};   // MAC-Adresse des 2. ESP32
uint8_t three[] = {0x40, 0x22, 0xD8, 0x52, 0xE0, 0xEC}; // MAC-Adresse des 3. ESP32

Servo servo1;                                 // Neues Servo Objekt servo1
DHT dht(DHTPIN, DHTTYPE);                     // Neues DHT Objekt dht, mit DHTPIN (32) und DHTTYPE (DHT22) initialisiert
AsyncWebServer server(80);                    // Erstellen eines AsyncWebServer Objekts server auf Port 80


// Array der bot Tokens, um über THIS, auf das jeweilige Token zugreifen zu können
String token[] = {" ", "7081776857:AAGZu1PSEB-_zNQeIemqdzPqNRixJOczWWI", "7184436868:AAF1fVyoowA9ns-H9hUX_xU0NsBxQBXoLCI", "6720615447:AAGt-R-hI56hcwnogYpZLOU_bw3on5Cy8qs"};
// Initialize Telegram BOT
String BOTtoken =token[THIS];                 // Bot Token mit dem sich der ESP32 mit dem Telegramm bot verbinden kann 
                                              // (bekommt man vom BotFather wenn man einen neuen Bot erstellt ("/ newbot"))
String CHAT_ID= "7005634945";                 // Chat ID, muss mit der eigenen Übereinstimmen, sodass der ESP32 Befehle bearbeitet 
                                              // (bekommt man vom IDBot mit: "/ start")
WiFiClientSecure client;                      // Neues WiFiClientSecure Objekt client für die HTTPS Kommunikation mit dem Telegramm bot
UniversalTelegramBot bot(BOTtoken, client);   // Erstellen eines UniversalTelegramBot Objekts bot, mit BOTtoken und client initialisiert

// Auffangvariablen für Daten bei ESP-NOW Nachrichten
//bool dataReceived = false;
bool gateState = false;                       // Speichert den Zustand dieses Servos (Tors): true -> offen, false -> geschlossen
byte dest_send;                               // Speichert die Zielboardnr. beim Senden von Nachrichten über ESP-NOW
byte action_send;                             // Speichert die zu versendende Aktion
byte dest_income;                             // Speichert die Zielboardnr. einer empfangenen Nachricht (sollte dieser ESP32 sein)
byte action_income;                           // Speichert die empfnagene Aktion, die durchzuführen ist
byte src_income;                              // Speichert die Source Boardnr. einer eingegangenen Nachricht
byte count_send = 0;                          // Zählt die Anzahl an Sendeversuchen von Nachrichten mit

String temperature1, temperature2, temperature3;  // Auffangvariablen temperature1, ..., für die Temperatur des jeweiligen ESP32
String humidity1, humidity2, humidity3;           // Auffangvariablen humidity1, ..., für die Luftfeuchtigkeit des jeweiligen ESP32
String gateState1, gateState2, gateState3;        // Auffangvariablen gateState1, ..., für den Status des Servos (Tors) der jeweiligen ESP32

//String success;
int boardHere;                                // Speichert bei Befehlen über den Telegramm bot / Serial, die Boardnr. für die der Befehl gilt

unsigned long startTime;                      // Speichert die Startzeit einer Zeitspezifischen Aufgabe

/*
 * HTML | CSS | JS Code des Webservers.
 * JS Methoden rufen Methoden unten im ESP32 Cod auf, die dann auf die jeweilige Anfrage vom Webserver reagieren
 * Placeholder (Bspl: "%TEMPERATUREONE%"), sollen durch jeweilige Werte (Temperatur des 1. ESP32) ersetzt werden
 */
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <title>ESP Web Server</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    html {font-family: Arial; display: inline-block; text-align: center;}
    h2 {font-size: 3.0rem;}
    p {font-size: 3.0rem;}
    body {max-width: 600px; margin:0px auto; padding-bottom: 25px;}
    .switch {position: relative; display: inline-block; width: 120px; height: 68px} 
    .switch input {display: none}
    .slider {position: absolute; top: 0; left: 0; right: 0; bottom: 0; background-color: #ccc; border-radius: 34px}
    .slider:before {position: absolute; content: ""; height: 52px; width: 52px; left: 8px; bottom: 8px; background-color: #fff; -webkit-transition: .4s; transition: .4s; border-radius: 68px}
    input:checked+.slider {background-color: #2196F3}
    input:checked+.slider:before {-webkit-transform: translateX(52px); -ms-transform: translateX(52px); transform: translateX(52px)}
  </style>
</head>
<body>
  <h1>ESP - Server</h1>
  <p>
      <h2>TOR 1</h2>

      <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
  <br />
      <span class="dht-labels">Temperature</span> 
      <span id="temperature1">%TEMPERATUREONE%</span>
      <sup class="units">&deg;C</sup>
  <br />
      <i class="fas fa-tint" style="color:#00add6;"></i> 
      <span class="dht-labels">Humidity</span>
      <span id="humidity1">%HUMIDITYONE%</span>
      <sup class="units">&percnt;</sup>
  <br />
      <span class="dht-labels">TOR</span>
      <span id="gate1">%GATEONE%</span>
  <br />
      <button onclick="openGate(1)">open</button>
      <button onclick="closeGate(1)">close</button>
  </p>
      <h2>TOR 2</h2>

      <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
      <span class="dht-labels">Temperature</span> 
      <span id="temperature2">%TEMPERATURETWO%</span>
      <sup class="units">&deg;C</sup>
  <br />
      <i class="fas fa-tint" style="color:#00add6;"></i> 
      <span class="dht-labels">Humidity</span>
      <span id="humidity2">%HUMIDITYTWO%</span>
      <sup class="units">&percnt;</sup>
  <br />
      <span class="dht-labels">TOR</span>
      <span id="gate2">%GATETWO%</span>
  <br />
      <button onclick="openGate(2)">open</button>
      <button onclick="closeGate(2)">close</button>
  </p>
      <h2>TOR 3</h2>

      <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
      <span class="dht-labels">Temperature</span> 
      <span id="temperature3">%TEMPERATURETHREE%</span>
      <sup class="units">&deg;C</sup>
  <br />
      <i class="fas fa-tint" style="color:#00add6;"></i> 
      <span class="dht-labels">Humidity</span>
      <span id="humidity3">%HUMIDITYTHREE%</span>
      <sup class="units">&percnt;</sup>
  <br />
      <span class="dht-labels">TOR</span>
      <span id="gate3">%GATETHREE%</span>
  <br />
      <button onclick="openGate(3)">open</button>
      <button onclick="closeGate(3)">close</button>
  </p>
    
    <p>
      <button onclick="updateValues(0)">Aktualisieren</button>
    </p>
<script>
var loadedOnce = false;
window.onload = function() {
    if(loadOnce){
      location.reload(true); // Seite neu laden, wobei der Cache ignoriert wird
    }else{
      loadOnce = true;
    }
            
};
function openGate(nr){
  let action = 1;
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      console.log("Funktion auf dem ESP32 aufgerufen!");
    }
  };
  var url = "/doSomething?param1=" + nr + "&param2=" + action; // Parameter in der URL
  xhttp.open("GET", url, true);
  xhttp.send();
}
function closeGate(nr){
  let action = 2;
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      console.log("Funktion auf dem ESP32 aufgerufen!");
    }
  };
  var url = "/doSomething?param1=" + nr + "&param2=" + action; // Parameter in der URL
  xhttp.open("GET", url, true);
  xhttp.send();
}
function updateValues(nr){
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      console.log("Funktion auf dem ESP32 aufgerufen!");
    }
  };
  var action = 5; 
  var url = "/doSomething?param1=" + nr + "&param2=" + action; // Parameter in der URL
  xhttp.open("GET", url, true);
  xhttp.send();
}
</script>
</body>
</html>
)rawliteral";

/*
 * Wird beim Versenden der Webseite aufgerufen,
 * und ersetzt alle Platzhalter der Seite mit Werten, die derzeit in den jeweiligen Auffangvariablen gespeichert sind
 * (Sollten vorm verschicken der Webseite wenn möglich aktualisiert werden)
 * @param var : const String&, gibt den Platzhlater an, der durch einen Wert ersetzt werden soll
 * @return String, gibt den Wert, der statt dem Platzhlater eingefügt wird zurück
 */
String processor(const String& var) {
  if (var == "GATEONE") {
    return gateState1;                        // Für den Platzhalter GATEONE, gib gateState1 (Status des Servos (Tors) beim 1. ESP32) zurück
  } else if (var == "TEMPERATUREONE") {
    return temperature1;                      // Für den Platzhalter TEMPERATUREONE, gib temperature1 (temperatur beim 1. ESP32) zurück
  } else if (var == "HUMIDITYONE") {
    return humidity1;                         // Für den Platzhalter HUMIDITYONE, gib humidity1 (Luftfeuchtigkeit beim 1. ESP32) zurück
  } else if (var == "GATETWO") {
    return gateState2;                        // Für den Platzhalter GATETWO, gib gateState2 (Status des Servos (Tors) beim 2. ESP32) zurück
  } else if (var == "TEMPERATURETWO") {
    return temperature2;                      // Für den Platzhalter TEMPERATURETWO, gib temperature2 (temperatur beim 2. ESP32) zurück
  } else if (var == "HUMIDITYTWO") {
    return humidity2;                         // Für den Platzhalter HUMIDITYTWo, gib humidity2 (Luftfeuchtigkeit beim 2. ESP32) zurück
  } else if (var == "GATETHREE") {
    return gateState3;                        // Für den Platzhalter GATETHREE, gib gateState3 (Status des Servos (Tors) beim 3. ESP32) zurück
  } else if (var == "TEMPERATURETHREE") {
    return temperature3;                      // Für den Platzhalter TEMPERATURETHREE, gib temperature3 (temperatur beim 3. ESP32) zurück
  } else if (var == "HUMIDITYTHREE") {
    return humidity3;                         // Für den Platzhalter HUMIDITYTHREE, gib humidity3 (Luftfeuchtigkeit beim 3. ESP32) zurück
  }
  return String();                            // Bei einem ungültigen Platzhlater gib einen leeren String ("") zurück
}
// DHT22 Sensor
/*
 * readDHTTemperature() liest die Temperatur dieses ESP32 über den DHT22 in °C ein
 * @return String, die Temperatur (float), oder "--" wenn nicht richtig eingelesen werden konnte
 */
String readDHTTemperature() {
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  // Read temperature as Celsius (the default)
  float t = dht.readTemperature();            // dht.readTemperature() gibt die Temperatur in °C zurück, die wir in t : float auffangen
  // Check if any reads failed and exit early (to try again).
  if (isnan(t)) {                             // konnte nicht eingelesen werden: Fehlermeldung, "--" zurückgeben
    Serial.println("Failed to read from DHT sensor!");  
    return "--";
  }
  else {                                      // sonst: Temperatur über Serial ausgeben und als String zurückgeben
    Serial.println(t);
    return String(t);
  }
}
/*
 * readDHTHumidity() liest die Luftfeuchtigkeit dieses ESP32 über den DHT22 in % ein
 * @return String, gibt die Luftfeuchtigkeit (float), oder "--" wenn nicht richtig eingelesen werden konnte zurück
 */
String readDHTHumidity() {
  // Sensor readings may also be up to 2 seconds 'old' (its a very slow sensor)
  float h = dht.readHumidity();               // dht.readHumidity() gibt die Luftfeuchtigkeit in % zurück, die wir in h : float auffangen
  if (isnan(h)) {                             // konnte nicht eingelesen werden: Fehlermeldung, "--" zurückgeben
    Serial.println("Failed to read from DHT sensor!");
    return "--";
  }
  else {                                      // sonst: Luftfeuchtigkeit über Serial ausgeben und als String zurückgeben
    Serial.println(h);
    return String(h);
  }
}
// Servo (Tor)
/*
 * openGate() stellt den Servo auf 90° ein (-> ~"öffnet das Tor")
 * Während sich der Servo bewegt, leuchtet die eingebaute LED des ESP32 auf
 * gateState wird aktualisisert
 */
void openGate() {
  digitalWrite(LED_BUILTIN, HIGH);            // Eingebaute LED zum leuchten bringen
  servo1.write(90);                           // Servo auf 90° einstellen, "das Tor öffnen"
  delay(500);                                 // Zeit in der sich der Servo bewegen kann
  gateState = true;                           // Zustand des Servos (Tors) aktualisieren
  digitalWrite(LED_BUILTIN, LOW);             // Eingebaute LED wieder ausschalten
}
/*
 * closeGate() stellt den Servo auf 0° ein (-> ~"schließt das Tor")
 * Während sich der Servo bewegt, leuchtet die eingebaute LED des ESP32 auf
 * gateState wird aktualisisert
 */
void closeGate() {
  digitalWrite(LED_BUILTIN, HIGH);            // Eingebaute LED zum leuchten bringen
  servo1.write(0);                            // Servo auf 0° einstellen, "das Tor schließen"
  delay(500);                                 // Zeit in der sich der Servo bewegen kann
  gateState = false;                          // Zustand des Servos (Tors) aktualisieren
  digitalWrite(LED_BUILTIN, LOW);             // Eingebaute LED wieder ausschalten
}
/*
 * gate() ändert den derzeitigen Zustand des Servos (Tors)
 * (ist der Servo auf 90° offen wird er auf 0° geschlossen und umgekehrt)
 */
void gate(){
  if(gateState){
    closeGate();                              // Servo (Tor) ist offen -> wird geschlossen
  }else{
    openGate();                               // Servo (Tor) ist geschlossen -> wird geöffnet
  }
}
// ESP-NOW
/*
 * OnDataSent() wird aufgerufen, wenn eine Nachricht über ESP-NOW gesendet wird
 * Gibt aus, ob das Senden erfolgreich war oder nicht
 * War das Senden nicht erfolgreich, wird die Nachricht erneut versendet (sofern nicht bereits 5 mal probiert zu senden)
 */
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status){
  Serial.print("\r\nLast Packet Send Status:\t");
  Serial.print(status == ESP_NOW_SEND_SUCCESS ? " :) " : " :( ");
  if (status == 0) {
    Serial.print(" :) ");                     // Über Serial " :)" ausgeben, wenn erfolgreich gesendet werden konnte
  }else {
    Serial.println(" :( ");                   // Über Serial " :(" asugeben, wenn nicht gesendet werden konnte
    if(count_send < 5){                       // Wurde noch keine 5x versucht die Nachricht zu senden
      // retry
      send(dest_send, action_send);           // Sende sie erneut (hier ist dest_send und action_send wichtig)
      count_send++;                           // Versuch in count_send mitzählen
    }
  }
}
/*
 * OnDataRecv() wird aufgerufen, wenn eine Nachricht über ESP-NOW empfangen wurde
 * Kopiert die Nachricht in income : struct_message, und reagiert auf sie. (Je nach übertragener action)
 */
void OnDataRecv(const uint8_t * mac, const uint8_t *incomeData, int len){
  memcpy(&income, incomeData, sizeof(income));// Kopieren der Nachricht mit memcpy()
  Serial.print("Bytes received: ");           // Ausgeben, wie viele Bytes empfangen wurden
  Serial.println(len);

  // Code der je nach income das Tor öffnet / schließt

  dest_income = income.dest;                  // Die Zielboardnr. der Nachricht in dest_income speichern. (Sollte dieser ESP32 sein)
  action_income = income.action;              // Die Aktion, die wir ausführen sollen in action_income speichern
  src_income = income.src;                    // Die Source Boardnr. in src_income speichern
  if(dest_income == THIS){                    // Ist dieses Board gemeint, behandle die übertragene Aktion:
    if(action_income == 0){
      sendAllData(src_income);                // bei 0: sende alle meine Daten (hum, temp, gateState) an den Versender der Nachricht
    }else if(action_income == 1){
      openGate();                             // bei 1: öffne den Servo (das Tor)
    }else if(action_income == 2){
      closeGate();                            // bei 2: schließe den Servo (das Tor)
    }else if(action_income == 3){
      gate();                                 // bei 3: ändere den Zustand des Servos (Tors)
    }else if(action_income == 5){             // bei 5: wir haben die Daten des Sender-Boards erhlaten, speichern die Werte in den jeweiligen Auffangvariablen
      if(src_income == 1){
        temperature1 = income.temp;           // In temperature1, ..., wenn die Nachricht vom 1. ESP32 kam
        humidity1 = income.hum;
        if(income.gateState){
          gateState1 = "offen";
        }else{
          gateState1 = "geschlossen";
        }
      }else if(src_income == 2){
        temperature2 = income.temp;           // In temperature2, ..., wenn die Nachricht vom 2. ESP32 kam
        humidity2 = income.hum;
        if(income.gateState){
          gateState2 = "offen";
        }else{
          gateState2 = "geschlossen";
        }
      }else if(src_income == 3){
        temperature3 = income.temp;           // In temperature3, ..., wenn die Nachricht vom 3. ESP32 kam
        humidity3 = income.hum;
        if(income.gateState){
          gateState3 = "offen";
        }else{
          gateState3 = "geschlossen";
        }
      }
      income.temp = "--";                     // schreibe einen Platzhlater für temp und hum in die income : struct_message
      income.hum = "--";
    } 
  }
}
/*
 * send() versendet eine Nachricht über ESP-NOW
 * sollte nicht direkt aufgerufen werden (außer von OnDataSent() / espNow()))
 * @param toBoard : byte, die Zielboardnr. der Nachricht
 * @param action : byte, die zu versendende Aktion
 */
void send(byte toBoard, byte action){
    outcome.src = THIS;                       // in die outcome : struct_message die versendet wird Basisdaten speichern
    outcome.dest = toBoard;                   // (Unsere Boardnr. als Source, übergebene Zielboardnr. als dest, übergebene zu versendened Aktion)
    outcome.action = action;

    dest_send = toBoard;                      // Die Zielboardnr. auch in dest_send speichern
    action_send = action;                     // Die Aktion auch in action_send speichern (damit man die Daten außerhalb dieser Methode auch noch kennt)

    esp_err_t result;                         // in result : esp_err_t wird der Sendestatus des Nachrichtenversendens über ESP-NOW aufgefangen 
    switch(toBoard) {                         // Je nach Zielboardnr. wird eine Nachricht an:
        case 1:                               // 1. ESP32 gesendet
  // esp_now_send() versendet Nachrichten und erfordert 3. Parameter: (Ziel-MAC-Adresse, Nachricht, Anzahl übertragener Bytes)
          result = esp_now_send(one, (uint8_t *) &outcome, sizeof(outcome));
          break;
        case 2:                               // 2. ESP32 gesendet
          result = esp_now_send(two, (uint8_t *) &outcome, sizeof(outcome));
          break;
        case 3:                               // 3. ESP32 gesendet
          result = esp_now_send(three, (uint8_t *) &outcome, sizeof(outcome));
          break;
        default:                              // Bei ungültiger Boardnr.:
          result = ESP_LOG_ERROR;             // ESP_LOG_ERROR als Platzhalter in result speichern
          break;
      }
      if (result == ESP_OK) {                 // Konnte erfolgreich gesendet werden: erfolgsmeldung 
        Serial.print("Sent with success. Further: "); //(Heißt nicht das die Nachricht auch gut ankam, das wird dann erst in OnDataSent() festgestellt)!
      }else if (result == ESP_LOG_ERROR) {    // Fehlermeldung für eine ungültige Boardnr.
        Serial.println("Wrong board");
      }else {                                 // Sonst: Fehlermedlung, Nachricht konnte nicht versendet werden
        Serial.println("Error sending the data" + result);
    }
}
/*
 * sendAllData() speichert aktuelle Temperatur, Luftfeuchtigkeit und gateState in outcome message,
 * sodass alle Daten über ESP-versendet werden können.
 * Sendet die Daten mit korrekter Aktions nr.
 * @param dest : byte, Zielboardnr. , des Boards, der nach unseren Daten gefragt hat
 */
void sendAllData(byte dest){
  outcome.temp = readDHTTemperature();        // Einlesen und Speichern der Temperatur in die outcome : struct_message, die versendet wird
  outcome.hum = readDHTHumidity();            // Einlesen und Speichern der Luftfeuchtigkeit
  outcome.gateState = gateState;              // Speichern des gateStates

  espNow(dest, 5);                            // Versenden der Nachricht über ESP-NOW mit richtiger Aktion (5 = Daten werden versendet)
}
/*
 * espNow() versendet Nachrichten über ESP-NOW und sollte dafür aufgerufen werden
 * Überprüft, ob nicht eigentlich dieser ESP32 gemeint ist, und die Aktion hier durchgeführt werden soll
 * Ermöglicht das erneute Senden von Nachrichten, wenn diese nicht beim ersten Mal verschickt weden konnten
 * @param toBoard : int, Zielboardnr.
 * @param action : int, Aktion, die der Ziel ESP32 durchführen soll
 */
void espNow(int toBoard, int action){
  if(toBoard == THIS){                        // War dieser ESP32 gemeint, führe die Aktion für diesen ESP32 aus
    switch(action){
      case 1:
        openGate();                           // bei 1: öffne den Servo (das Tor)
        break;
      case 2:
        closeGate();                          // bei 2: schließe den Servo (das Tor)
        break;
      case 3:
        gate();                               // bei 3: ändere den Zustand des Servos (Tors)
      case 0:                                 // bei 0: speichere unsere Daten aktualisiert in die jeweiligen Auffangvariablen
        if(THIS == 1){
          temperature1 = readDHTTemperature();
          humidity1 = readDHTHumidity();
          if(gateState){
            gateState1 = "offen";
          }else{
            gateState1 = "geschlossen";
          }
        }else if(THIS == 2){
          temperature1 = readDHTTemperature();
          humidity2 = readDHTHumidity();
          if(gateState){
            gateState2 = "offen";
          }else{
            gateState2 = "geschlossen";
          }
        }else if(THIS == 3){
          temperature3 = readDHTTemperature();
          humidity3 = readDHTHumidity();
          if(gateState){
            gateState3 = "offen";
          }else{
            gateState3 = "geschlossen";
          }
        }
    }
  }else{                                      // sonst: versende eine Nachricht an den jeweiligen ESP32
    count_send = 0;                           // count_send auf 0 setzen, zählt die Sendeversuche mit
    //dataReceived = false;
    send(toBoard, action);                    // Nachricht über send() senden.
    delay(500);
  }
}
// Websever
/*
 * refreshAllData() aktualisert in den Auffangvariablen (temperature1, humidity2, ...) alle Werte
 * Gibt die aktualisierten Daten auch noch über Serial aus
 */
void refreshAllData(){
  Serial.println("refreshing");
  if(1 != THIS){                              // wenn dieser ESP32 nicht der 1. ist:
    espNow(1, 0);                             // sende über ESP-NOW eine Anfrage an den 1. ESP32, um seinen Werte zu bekommen
    //delay(1500);
  }else{                                      // sonst, aktualisiere unsere Daten, und speichere sie in die jeweilgen Variablen
    humidity1 = readDHTHumidity();
    temperature1 = readDHTTemperature();
    if(gateState){
      gateState1 = "offen";
    }else{
      gateState1 = "geschlossen";
    }
  }
  if(2 != THIS){                              // wenn dieser ESP32 nicht der 2. ist:
    espNow(2, 0);                             // sende über ESP-NOW eine Anfrage an den 2. ESP32, um seinen Werte zu bekommen
  }else{                                      // sonst, aktualisiere unsere Daten, und speichere sie in die jeweilgen Variablen
    humidity2 = readDHTHumidity();
    temperature2 = readDHTTemperature();
    if(gateState){
      gateState2 = "offen";
    }else{
      gateState2 = "geschlossen";
    }
  }
  if(3 != THIS){                              // wenn dieser ESP32 nicht der 3. ist:
    espNow(3, 0);                             // sende über ESP-NOW eine Anfrage an den 3. ESP32, um seinen Werte zu bekommen
  }else{                                      // sonst, aktualisiere unsere Daten, und speichere sie in die jeweilgen Variablen
    humidity3 = readDHTHumidity();
    temperature3 = readDHTTemperature();
    if(gateState){
      gateState3 = "offen";
    }else{
      gateState3 = "geschlossen";
    }
  }
  Serial.println(generateLongText());         // Über generateLongText() alle Daten über Serial ausgeben
}
/*
 * generateLongText() stellt einen Text zusammen, der alle Werte aus den Auffangvariablen (temperature1, humidity2, ...) darlegt.
 * (Wäre sinnvoll, die Werte davor zu aktualisieren)
 * @return String, gibt den Text zurück
 */
String generateLongText() {
  // Zusammenstellen des langen Textes
  String longText = "Aktuelle Werte:\n";
  longText += "TOR1: ";
  longText += "Temperatur: " + temperature1 + " ";
  longText += "Luftfeuchtigkeit: " + humidity1 + " ";
  longText += "Torstatus: " + gateState1 + " ";
  longText += "\nTOR2: ";
  longText += "Temperatur: " + temperature2 + " ";
  longText += "Luftfeuchtigkeit: " + humidity2 + " ";
  longText += "Torstatus: " + gateState2 + " ";
  longText += "\nTOR3: ";
  longText += "Temperatur: " + temperature3 + " ";
  longText += "Luftfeuchtigkeit: " + humidity3 + " ";
  longText += "Torstatus: " + gateState3 + " ";
  
  return longText;
}
/*
 * handleWebServer() wird vom Webserver aufgerufen, und reagiert auf die Anfragen von diesem.
 * @param request, enthält die Request von der Website (auch mit den Parametern)
 */
void handleWebServer(AsyncWebServerRequest *request){
  int board = request->arg("param1").toInt(); // Erster Parameterwert aus der URL erfassen
  int action = request->arg("param2").toInt(); // Zweiter Parameterwert aus der URL erfassen
  if(action == 5){                            // Ist die Aktion == 5, soll der Webserver aktualisiert werden:
    refreshAllData();                         // Werte in den Auffangvariablen aktualisieren
    delay(500);
    request->redirect("/");                   // Webseite neu senden, sodass alle Platzhalter durch aktuelle Werte erstzt werden
  }else{                                      // sonst:
    espNow(board, action);                    // Mit dem Aufrufen von espNow() mit den jeweiligen Parametern auf die Anfrage reagieren (ist möglich)
  }
}
/*
 * connectToWifi(), versucht sich mit dem Wlan zu verbinden, wenn noch keine Verbindung besteht
 * setzt den Webserver und den Telegramm bot auf
 */
void connectToWifi(){

  // Versucht den Telegramm bot zu starten, und sich mit dem Wlan zu verbinden
  if(WiFi.status() != WL_CONNECTED){
    WiFi.begin(ssid, password);
    startTime = millis(); // Startzeit erfassen
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 3000) {
      delay(1000);
      Serial.println("Connecting to WiFi..");
   }
  }
  if(WiFi.status() != WL_CONNECTED){
    Serial.println("Could not connect to Wifi");  
  }else{                                      // Konnte sich mit dem Wlan verbunden werden
    Serial.println(WiFi.localIP());           // IP-Adresse ausgeben
                                              // Webserver aufsetzen
    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
      request->send_P(200, "text/html", index_html, processor);
    });
    
  server.on("/doSomething", HTTP_GET, [](AsyncWebServerRequest *request){ handleWebServer(request); }); // Route für die Behandlung der Funktion mit Parametern
     // Send a GET request to <ESP_IP>/update?state=<inputMessage>
    server.on("/update", HTTP_GET, [](AsyncWebServerRequest* request) {
      String inputMessage;
      String inputParam;
      // GET input1 value on <ESP_IP>/update?state=<inputMessage>
      if (request->hasParam("state")) {
        inputMessage = request->getParam("state")->value();
        digitalWrite(13, inputMessage.toInt());
      } else {
        inputMessage = "No message sent";
      }
      Serial.println(inputMessage);
      request->send(200, "text/plain", "OK");
    });

    // Send a GET request to <ESP_IP>/state
    server.on("/state", HTTP_GET, [](AsyncWebServerRequest* request) {
      request->send(200, "text/plain", String(digitalRead(13)));
    });

    // Start server
    server.begin();
  }
}
// Befehle über Serial / Telegramm bot
/*
 * interprettext() kann auf Befehle von Serial / Telegramm bot reagieren
 * @param textToInterpret : String, enthält den Befehl
 * @return String, gibt eine Meldung zurück, (v.a. wichtig beim Befehl Data, wo dann die Daten zurückgegeben werden)
 */
String interpretText(String textToInterpret){
   textToInterpret.trim(); // Leerzeichen am Anfang und Ende entfernen

  // Eingabe in Operation und Board-Nummer aufteilen
  String actionHere = textToInterpret.substring(0, textToInterpret.indexOf(' ')); // Der Teil der Zeichenfolge vor dem Leerzeichen ist die Operation
  boardHere = THIS;
  boardHere = textToInterpret.substring(textToInterpret.indexOf(' ') + 1).toInt(); // Der Teil der Zeichenfolge nach dem Leerzeichen ist die Board-Nummer, die in eine Ganzzahl konvertiert wird

  // Debug-Ausgabe
  Serial.print("Operation: ");
  Serial.print(actionHere);
  Serial.print(" Board Number: :");
  Serial.println(boardHere);

  if(actionHere == "close"){
    espNow(boardHere, 2);                     // beim Befehl close, wird über espNow() der jeweilige Servo (/Tor) geschlossen
    return "closing gate " + boardHere;
  }else if( actionHere == "open"){
    espNow(boardHere, 1);                     // beim Befehl open, wird über espNow() der jeweilige Servo (/Tor) geöffnet
    return "opening gate " + boardHere;
  }else if(actionHere == "wifi"){
    connectToWifi();                          // beim Befehl wifi wird die Methode connectToWifi() aufgerufen
    return "connecting to wifi";
  }else if(actionHere == "Data"){             // beim Befehl Data werden über espNow() die jeweiligen Daten aktualisiert und als String zurückgegeben
    // refreshAllData
    espNow(boardHere, 0);
    if(boardHere == 1){
      String returnString = "TOR 1";
      returnString += "\nTemperatur: " + temperature1;
      returnString += "\nHumidity: " + humidity1;
      returnString += "\nTor: " + gateState1;
      return returnString;
    }else if(boardHere == 2){
      String returnString = "TOR 2";
      returnString += "\nTemperatur: " + temperature2;
      returnString += "\nHumidity: " + humidity2;
      returnString += "\nTor: " + gateState2;
      return returnString;
    }else{
      String returnString = "TOR 3";
      returnString += "\nTemperatur: " + temperature3;
      returnString += "\nHumidity: " + humidity3;
      returnString += "\nTor: " + gateState3;
      return returnString;
    }
  }
}
/*
 * operateSerial() liest Befehle über Serial ein und verwendet die Methode interpreteText, 
 * um auf die Befehle zu reagieren
 * liest nur den Ersten Befehl ein, weitere gleichzeitig verfügbare Befehle werden verworfen
 */
void operateSerial(){
  String inputHereSerial = Serial.readStringUntil('\n'); // Eingabe lesen, bis ein Zeilenumbruch (\n) erreicht ist

  Serial.println(interpretText(inputHereSerial));         // über interpretText() auf den Befehl reagieren und Meldung über Serial ausgeben

  income.temp = "--";
  income.hum = "--";
  income.gateState = false;

  if(Serial.available() > 0){                             // weitere gleichzeitig verfügbare Befehle verwerefen
    Serial.read();
  }
}
/*
 * handleNewMessages() reagiert auf Nachrichten vom Telegramm bot.
 * kann den Befehl /start mit einer Wilkommensmessage und weiter Befehle (siehe interpretText()) bearbeiten
 * Achtet darauf, dass die chatID übereinstimmt
 * @param numNewMessages : int, gibt die Anzahl neuer Nachrichten an
 */
void handleNewMessages(int numNewMessages) {
  for (int i=0; i<numNewMessages; i++) {      // geht die Übergebene Anzahl an neuen Nachrichten durch
    Serial.print("got telegram messages");
    String chat_id = String(bot.messages[i].chat_id);
    if (chat_id != CHAT_ID) {                 // Stimmt die Chat ID nicht überein wird eine Fehlermeldung ausgegeben und abgebrochen
      bot.sendMessage(chat_id, "Unauthorized user", "");
      continue;
    }
    
    String text = bot.messages[i].text;       // Liest eine eingehende Nachricht über den Telegramm bot ein und speichert sie in text : String

    if (text == "/start") {                   // Beim Befehl /start wird eine Wilkommensnachricht ausgegeben
      String welcome = "Welcome.\n";
      welcome += "Use the following commands:\n\n";
      welcome += "/data - get all Infos of an ESP\n";
      welcome += "/open - open gate of an ESP\n";
      welcome += "/close - close gate of an ESP\n";
      welcome += "Add a number 1 - 3 after the Text to choose ESP\n";
      bot.sendMessage(chat_id, welcome, "");
      continue;
    }
    
    String outputString = interpretText(text);  // reagiert auf andere Befehle über die Methode interpretText() und speichert die Meldung

    bot.sendMessage(chat_id, outputString, ""); // gibt die Meldung aus
    
    bot.sendMessage(chat_id, "Operated", ""); // gibt "Operated" aus 
  }
}

// put your setup code here, to run once:
void setup() {
  // ----- setup ------------------------------------------------------------------------------------
    pinMode(LED_BUILTIN, OUTPUT);             // Definiert LED_BUILTIN  als eine OUTPUT PIN (ausgabe PIN)
    digitalWrite(LED_BUILTIN, HIGH);          // Schreibt HIGH auf LED_BUILTIN ... bringt sie zum Leuchten
    
    pinMode(TOR_OUT, OUTPUT);                 // Definiert TOR_OUT als eine OUTPUT PIN (ausgabe PIN)
    pinMode(DHTPIN, INPUT);                   // Definiert DHTPIN  als eine INPUT PIN (eingabe PIN)
    
    Serial.begin(9600);                       // Serielle Kommunikation mit einer Baud-rate 9600 initialisieren

    WiFi.mode(WIFI_STA);                      // Diesen ESP32 als eine WIFI_STA setzen

  // ----- TOR --------------------------------------------------------------------------------------

    servo1.attach(TOR_OUT);                   // dem Servo Objekt servo1 seinen PIN zuweisen
    closeGate();                              // Schließt das Tor

  // ----- DHT --------------------------------------------------------------------------------------

    dht.begin();                              // DHT22 Sensor initialisieren

  // ----- ESP - NOW --------------------------------------------------------------------------------

    if (esp_now_init() != ESP_OK) {           // ESP-NOW Initialisieren und Erfolgsmeldung ausgeben
      Serial.println("Error initializing ESP-NOW");
      return;
    }else{
      Serial.println("Initialized ESP-NOW");
    }

    esp_now_register_send_cb(OnDataSent);     // Initialisieren der OnDataSent() funktion

    // Alle peers, mit denen dieser ESP32 über ESP-NOW kommunizieren möchte Initialisieren
    /*
      Registrieren und Hinzufügen aller Peers, mit denen über ESP-NOW kommuniziert werden soll
      mit ESP32 die nicht als peer hinzugefügt werden kann nicht kommuniziert werden
      (es wird über THIS != 1 (...) darauf geachtet, dass man sich nicht selbst als peer hinzufügt)
    */
    // 1
    if(THIS != 1){
      Serial.println("Adding peer one..");
      // Register peer
      memcpy(peerInfoOne.peer_addr, one, 6);
      peerInfoOne.channel = 0;
      peerInfoOne.encrypt = false;

      // Add peer
      if (esp_now_add_peer(&peerInfoOne) != ESP_OK){
        Serial.println("Failed to add peer one");
        return;
      }
    }
    // 2
    if(THIS != 2){
      Serial.println("Adding peer two..");
      // Register peer
      memcpy(peerInfoTwo.peer_addr, two, 6);
      peerInfoTwo.channel = 0;
      peerInfoTwo.encrypt = false;

      // Add peer
      if (esp_now_add_peer(&peerInfoTwo) != ESP_OK){
        Serial.println("Failed to add peer two");
        return;
      }
    }
    // 3
    if(THIS != 3){
      // Register peer
      Serial.println("Adding peer three..");
      memcpy(peerInfoThree.peer_addr, three, 6);
      peerInfoThree.channel = 0;
      peerInfoThree.encrypt = false;

      // Add peer
      if (esp_now_add_peer(&peerInfoThree) != ESP_OK){
        Serial.println("Failed to add peer three");
        return;
      }
    }

    esp_now_register_recv_cb(OnDataRecv);     // Initialiseren der OnDataRecv() funktion

  // ----- Telegram + Webserver ---------------------------------------------------------------------

    refreshAllData();                         // Versuchen erstmalig alle Daten zu erhalten
    WiFi.begin(ssid, password);               // Verbindung zum WLAN mit SSID uns Passwort herstellen
    startTime = millis(); // Startzeit erfassen
    client.setCACert(TELEGRAM_CERTIFICATE_ROOT);  // Stammzertifikat für den Telegramm bot setzen
    while (WiFi.status() != WL_CONNECTED && millis() - startTime < 3000) {
      delay(1000);
      Serial.println("Connecting to WiFi.."); // 3 Sekunden lang abwarten und versuchen sich mit dem WLAN zu verbinden
   }
    connectToWifi();                          // connectToWifi() aufrufen, um den Webserver aufzusetzen

  // ----- setup ------------------------------------------------------------------------------------

    digitalWrite(LED_BUILTIN, LOW);           // Schreibt LOW auf LED_BUILTIN ... schaltet sie wieder aus
    startTime = millis();                     // Speichert derzeitige Zeit (nach Programmstart) in startTime
}
// put your main code here, to run repeatedly:
void loop() {
  
  // Alle 10 Sekunden
  if(millis() - startTime > 10000){

    // Telegram bot Nachrichten einlesen und darauf reagieren
      handleNewMessages(bot.getUpdates(bot.last_message_received + 1));
    
    startTime = millis();                       // Startzeit aktualisieren, um wieder 10 Sekunden abwarten zu können
  }
  // dauerhaft:
  // Reading von Serial
    if(Serial.available() > 0){                 // wenn Serial Befehle eingegangen sind, auf diese reagieren
      operateSerial();
    }
}