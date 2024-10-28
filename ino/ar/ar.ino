#include <Wire.h>
#include "LiquidCrystal_I2C.h"
#include <WiFiS3.h>
#include <ArduinoJson.h>
#include <EEPROM.h>
#include "Arduino_LED_Matrix.h"
#define FLOW_FACTOR (1.0 / 7.5) // Pulse (Hz) = [7.5 x Flow Rate Q (L / min)] ±3%
#define NUM_SENSORS 6

LiquidCrystal_I2C lcds[NUM_SENSORS] = {
  LiquidCrystal_I2C (0x20, 16, 2),
  LiquidCrystal_I2C (0x21, 16, 2),
  LiquidCrystal_I2C (0x22, 16, 2),
  LiquidCrystal_I2C (0x23, 16, 2),
  LiquidCrystal_I2C (0x24, 16, 2),
  LiquidCrystal_I2C (0x25, 16, 2)
};

int FLOW_PINS[NUM_SENSORS] = {2, 3, 6, 8, 15, 16};
int pulse_counts[NUM_SENSORS] = {0};
ArduinoLEDMatrix matrix;

void on_trigger_handle0() {pulse_counts[0]++;}
void on_trigger_handle1() {pulse_counts[1]++;}
void on_trigger_handle2() {pulse_counts[2]++;}
void on_trigger_handle3() {pulse_counts[3]++;}
void on_trigger_handle4() {pulse_counts[4]++;}
void on_trigger_handle5() {pulse_counts[5]++;}
void (*interrupt_handlers[NUM_SENSORS])() = {
  on_trigger_handle0, on_trigger_handle1, on_trigger_handle2, on_trigger_handle3, on_trigger_handle4, on_trigger_handle5
};

byte frame[8][12] = {
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }
};

int idAddress = 0;
int machineNameAddress = idAddress + sizeof(int);
int fixMoveAddress = machineNameAddress + 20;

char ssid[] = "APHTV125";
char pass[] = "#aphtv125@";
//char ssid[] = "S000";
//char pass[] = "00000000";

String mac_address = "";
long rssi;
int id;
String machine_name;
String fix_move;
float flows[NUM_SENSORS] = {0};

WiFiServer server(80);

void setup() {
  Serial.begin(9600);
  matrix.begin();
  for (int i = 0; i < NUM_SENSORS; i++) {
    lcds[i].begin();
    lcds[i].backlight();
    pinMode(FLOW_PINS[i], INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_PINS[i]), interrupt_handlers[i], RISING);
  }
  getMACAddress();
  EEPROMread();

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }
  IPAddress local_ip(192, 168, 125, 243);
  IPAddress dns_server(192, 168, 225, 50);
  IPAddress gateway(192, 168, 125, 254);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_ip, dns_server, gateway, subnet);
  WiFi.begin(ssid, pass);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  server.begin();
}

void loop() {
  ////  Measurement  ////
  static unsigned long lastMeasurementTime = 0;
  unsigned long currentTime = millis();
  if (currentTime - lastMeasurementTime >= 1000) { // 1 second
    noInterrupts();
    int end_pulse_counts[NUM_SENSORS];
    for (int i = 0; i < NUM_SENSORS; i++) {
      end_pulse_counts[i] = pulse_counts[i];
      pulse_counts[i] = 0;
    }
    interrupts();

    lastMeasurementTime = currentTime;

    for (int i = 0; i < NUM_SENSORS; i++) {
      flows[i] = end_pulse_counts[i] * FLOW_FACTOR;
      updateLCD(i, flows[i]);
    }
  }

  ////  Web server  ////
  rssi = WiFi.RSSI();
  // -30 to -90 dB
  // -30 => good     => 12LED
  // -90 => not good => 0LED
  int n_led = map(rssi, -30, -90, 12, 0);
  for (int i = 0; i < 12; i++) {
    if (n_led > i)
      frame[7][i] = 1;
    else
      frame[7][i] = 0;
  }
  matrix.renderBitmap(frame, 8, 12);

  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String postData = "";
    bool isPostRequest = false;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // Serial.write(c);

        if (c == '\n') {
          if (currentLine[0] == 'G' or currentLine[0] == 'P')
            Serial.println(currentLine);
          if (currentLine.length() == 0) {
            if (isPostRequest) {
              while (client.available()) {
                char c = client.read();
                postData += c;
              }
              handlePostData(client, postData);
            }
            break;
          } else {
            if (currentLine.startsWith("GET / ")) {
              sendIndexPage(client);
            } else if (currentLine.startsWith("GET /json")) {
              sendJsonData(client);
            } else if (currentLine.startsWith("GET /lamp_on")) {
              digitalWrite(LED_BUILTIN, HIGH);
              sendIndexPage(client);
            } else if (currentLine.startsWith("GET /lamp_off")) {
              digitalWrite(LED_BUILTIN, LOW);
              sendIndexPage(client);
            } else if (currentLine.startsWith("GET /setting")) {
              sendSettingsPage(client);
            } else if (currentLine.startsWith("POST /setting")) {
              isPostRequest = true;
            }
            currentLine = "";
          }
        } else if (c != '\r') {
          currentLine += c;
        }
      }
    }
    client.stop();
  }
}

void sendIndexPage(WiFiClient & client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println("");

  client.println("<!DOCTYPE html>");
  client.println("<html lang=\"en\">");
  client.println("<head>");
  client.println("<title>Flow Check</title>");
  style(client);
  client.println("</head>");
  client.println("<body>");
  client.println("<h1>Flow Check</h1>");
  head_link(client);
  client.println("<div class=\"status-container\">");
  client.println("    <div class=\"status-item\">");
  client.println("        <span class=\"status-label\">Mac Address:</span>");
  client.println("        <span>" + String(mac_address) + "</span>");
  client.println("    </div>");
  client.println("    <div class=\"status-item\">");
  client.println("        <span class=\"status-label\">WiFi Signal Strength:</span>");
  client.println("        <span>" + String(rssi) + " dB</span>");
  client.println("    </div>");
  client.println("</div>");
  client.println("<div class=\"status-container\">");
  client.println("    <div class=\"status-item\">");
  client.println("        <span class=\"status-label\">ID:</span>");
  client.println("        <span>" + String(id) + "</span>");
  client.println("    </div>");
  client.println("    <div class=\"status-item\">");
  client.println("        <span class=\"status-label\">Machine Name:</span>");
  client.println("        <span>" + String(machine_name) + "</span>");
  client.println("    </div>");
  client.println("    <div class=\"status-item\">");
  client.println("        <span class=\"status-label\">Fix/Move:</span>");
  client.println("        <span>" + String(fix_move) + "</span>");
  client.println("    </div>");
  client.println("</div>");
  client.println("<div class=\"status-container\">");
  for (int i = 0; i < NUM_SENSORS; i++) {
    client.println("    <div class=\"status-item\">");
    client.println("        <span class=\"status-label\">flow" + String(i + 1) + ":</span>");
    client.println("        <span>" + String(flows[i]) + " L / min</span>");
    client.println("    </div>");
  }
  client.println("</div>");
  client.println("</body>");
  client.println("</html>");
}

void sendJsonData(WiFiClient & client) {
  StaticJsonDocument<200> doc;
  doc["mac address"] = mac_address;
  doc["id"] = id;
  doc["machine name"] = machine_name;
  doc["fix_move"] = fix_move;
  doc["status"] = "ok";
  JsonArray result = doc.createNestedArray("result");
  for (int i = 0; i < NUM_SENSORS; i++) {
    result.add(flows[i]);
  }
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:application/json");
  client.println("Connection: close");
  client.println();
  serializeJson(doc, client);
}

void send404(WiFiClient & client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>404: Not Found</h1></body></html>");
}

void sendSettingsPage(WiFiClient & client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();

  client.println("<!DOCTYPE html>");
  client.println("<html lang=\"en\">");
  client.println("<head>");
  client.println("<title>Settings</title>");
  style(client);
  client.println("</head>");
  client.println("<body>");
  client.println("<h1>Settings</h1>");
  head_link(client);
  client.println("<form action=\"/setting\" method=\"POST\">");
  client.println("    <fieldset>");
  client.println("        <legend>ID:</legend>");
  client.println("        <input type=\"text\" id=\"id\" name=\"id\" value=\"" + String(id) + "\">");
  client.println("        <a class=\"setting-note\">* ID is a value between 0 - 255.</a>");
  client.println("    </fieldset>");
  client.println("    <fieldset>");
  client.println("        <legend>Machine Name:</legend>");
  client.println("        <input type=\"text\" id=\"machine_name\" name=\"machine_name\" value=\"" + String(machine_name) + "\">");
  client.println("        <a class=\"setting-note\">* Machine Name must not exceed 20 characters.</a>");
  client.println("    </fieldset>");
  client.println("    <fieldset>");
  client.println("        <legend>Fix/Move:</legend>");
  String checked = (fix_move == "fix") ? "checked"  : "";
  client.println("        <input type=\"radio\" id=\"fix\" name=\"fix_move\" value=\"fix\"" + checked + ">");
  client.println("        <label for=\"fix\">Fix</label><br>");
  checked = (fix_move == "move") ? "checked"  : "";
  client.println("        <input type=\"radio\" id=\"move\" name=\"fix_move\" value=\"move\"" + checked + ">");
  client.println("        <label for=\"move\">Move</label>");
  client.println("    </fieldset>");
  client.println("    <input type=\"submit\" value=\"Submit\">");
  client.println("</form>");
  client.println("</body>");
  client.println("</html>");
}

void handlePostData(WiFiClient & client, String postData) {
  int idIndex = postData.indexOf("id=");
  int machineNameIndex = postData.indexOf("machine_name=");
  int fixMoveIndex = postData.indexOf("fix_move=");

  if (idIndex != -1 && machineNameIndex != -1 && fixMoveIndex != -1) {
    id = postData.substring(idIndex + 3, postData.indexOf('&', idIndex)).toInt();
    machine_name = postData.substring(machineNameIndex + 13, postData.indexOf('&', machineNameIndex));
    fix_move = postData.substring(fixMoveIndex + 9);
    //Serial.print("Updated ID: ");
    //Serial.println(id);
    //Serial.print("Updated Machine Name: ");
    //Serial.println(machine_name);
    //Serial.print("Updated Fix/Move: ");
    //Serial.println(fix_move);
    EEPROM.write(idAddress, id);
    for (int i = 0; i < machine_name.length(); i++)
      EEPROM.write(machineNameAddress + i, machine_name[i]);
    for (int i = machine_name.length(); i < 20; i++)
      EEPROM.write(machineNameAddress + i, '\0');
    for (int i = 0; i < fix_move.length(); i++)
      EEPROM.write(fixMoveAddress + i, fix_move[i]);
    for (int i = fix_move.length(); i < 4; i++)
      EEPROM.write(fixMoveAddress + i, '\0');
  }
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /setting");
  client.println("Connection: close");
  client.println();
}

void head_link(WiFiClient & client) {
  client.println("<div class=\"nav-links\">");
  client.println("    <a class =\"button_page\" href='/'>Home</a>");
  client.println("    <a class =\"button_page\" href='/json'>JSON Data</a>");
  client.println("    <a class =\"button_page\" href='/setting'>Settings</a>");
  client.println("</div>");
}

void style(WiFiClient & client) {
  client.println("<style>");
  client.println("body {");
  client.println("    font-family: Arial, sans-serif;");
  client.println("    line-height: 1.6;");
  client.println("    color: #333;");
  client.println("    max-width: 600px;");
  client.println("    margin: 0 auto;");
  client.println("    padding: 20px;");
  client.println("    background-color: #f4f4f4;");
  client.println("}");
  client.println("h1 {");
  client.println("    color: #2c3e50;");
  client.println("    text-align: center;");
  client.println("    border-bottom: 2px solid #3498db;");
  client.println("    padding-bottom: 10px;");
  client.println("}");
  client.println("a.button_page {");
  client.println("    color: #3498db;");
  client.println("    text-decoration: none;");
  client.println("    padding: 10px;");
  client.println("    background-color: #eee;");
  client.println("    border-radius: 4px;");
  client.println("    cursor: pointer;");
  client.println("}");
  client.println("a.button_page:hover {");
  client.println("    text-decoration: underline;");
  client.println("    background-color: #dfdfdf;");
  client.println("}");
  client.println("a.setting-note {");
  client.println("    font-size: 14px;");
  client.println("    color: #ccc;");
  client.println("}");
  client.println(".nav-links {");
  client.println("    display: flex;");
  client.println("    justify-content: space-around;");
  client.println("    margin-bottom: 20px;");
  client.println("}");
  client.println(".status-container {");
  client.println("    background-color: white;");
  client.println("    border-radius: 8px;");
  client.println("    padding: 20px;");
  client.println("    margin-bottom: 10px;");
  client.println("    box-shadow: 0 2px 4px rgba(0,0,0,0.1);");
  client.println("}");
  client.println(".status-item {");
  client.println("    margin-bottom: 10px;");
  client.println("}");
  client.println(".status-label {");
  client.println("    font-weight: bold;");
  client.println("    color: #2c3e50;");
  client.println("}");
  client.println("");
  client.println("form {");
  client.println("    background-color: white;");
  client.println("    padding: 20px;");
  client.println("    border-radius: 8px;");
  client.println("    box-shadow: 0 2px 4px rgba(0,0,0,0.1);");
  client.println("}");
  client.println("fieldset {");
  client.println("    border: 1px solid #ddd;");
  client.println("    border-radius: 4px;");
  client.println("    padding: 10px;");
  client.println("    margin-bottom: 15px;");
  client.println("}");
  client.println("legend {");
  client.println("    font-weight: bold;");
  client.println("    color: #2c3e50;");
  client.println("    padding: 0 5px;");
  client.println("}");
  client.println("input[type=\"text\"] {");
  client.println("    width: 100%;");
  client.println("    padding: 8px;");
  client.println("    margin: 5px 0;");
  client.println("    border: 1px solid #ddd;");
  client.println("    border-radius: 4px;");
  client.println("    box-sizing: border-box;");
  client.println("}");
  client.println("input[type=\"radio\"] {");
  client.println("    margin-right: 5px;");
  client.println("}");
  client.println("label {");
  client.println("    margin-right: 15px;");
  client.println("}");
  client.println("input[type=\"submit\"] {");
  client.println("    background-color: #3498db;");
  client.println("    color: white;");
  client.println("    padding: 10px 15px;");
  client.println("    border: none;");
  client.println("    border-radius: 4px;");
  client.println("    cursor: pointer;");
  client.println("    font-size: 16px;");
  client.println("}");
  client.println("input[type=\"submit\"]:hover {");
  client.println("    background-color: #2980b9;");
  client.println("}");
  client.println("</style>");
}

void getMACAddress() {
  byte mac[6];
  WiFi.macAddress(mac);
  for (int i = 5; i >= 0; i--) {
    if (mac[i] < 16)
      mac_address += "0";
    mac_address += String(mac[i], HEX);
    if (i > 0)
      mac_address += ":";
  }
  Serial.print("MAC Address: ");
  Serial.println(mac_address);
}

void EEPROMread() {
  id = EEPROM.read(idAddress);

  char machineNameBuffer[21];
  for (int i = 0; i < 20; i++) {
    machineNameBuffer[i] = EEPROM.read(machineNameAddress + i);
  }
  machineNameBuffer[20] = '\0';
  machine_name = String(machineNameBuffer);

  char fixMoveBuffer[5];
  for (int i = 0; i < 4; i++) {
    fixMoveBuffer[i] = EEPROM.read(fixMoveAddress + i);
  }
  fixMoveBuffer[4] = '\0';
  fix_move = String(fixMoveBuffer);
}

void updateLCD(int index, float flow) {
  lcds[index].clear();
  lcds[index].setCursor(0, 0);
  lcds[index].print("Flow rate ");
  lcds[index].print(index + 1);
  lcds[index].print(": ");
  lcds[index].setCursor(0, 1);
  lcds[index].print(flow, 2);
  lcds[index].print(" L/min");
}
