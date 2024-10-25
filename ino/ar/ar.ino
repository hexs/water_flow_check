#include <WiFiS3.h>
#include <ArduinoJson.h>
#include <EEPROM.h>

const int idAddress = 0;
const int machineNameAddress = idAddress + sizeof(int);
const int fixMoveAddress = machineNameAddress + 20;

//char ssid[] = "APHTV125";
//char pass[] = "#aphtv125@";
char ssid[] = "S000";
char pass[] = "00000000";

String mac_address = "";
int id;
String machine_name;
String fix_move;

//IPAddress local_ip(192, 168, 125, 243);
//IPAddress gateway(192, 168, 125, 254);
//IPAddress subnet(255, 255, 255, 0);
//IPAddress dns_server(192, 168, 225, 50);

WiFiServer server(80);

void setup() {
  Serial.begin(9600);

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

  if (WiFi.status() == WL_NO_MODULE) {
    Serial.println("Communication with WiFi module failed!");
    while (true);
  }
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

  //  WiFi.config(local_ip, dns_server, gateway, subnet);
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
  WiFiClient client = server.available();
  if (client) {
    String currentLine = "";
    String postData = "";
    bool isPostRequest = false;

    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        Serial.write(c);

        if (c == '\n') {
          // If the current line is blank, you got two newline characters in a row.
          // That's the end of the client HTTP request, so send a response:
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
            // Check the request type
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
    // Close the connection
    client.stop();
    Serial.println("Client disconnected.");
  }
}

void sendIndexPage(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<!DOCTYPE HTML>");
  client.println("<html>");
  client.println("<head><title>Arduino Web Server</title></head>");
  client.println("<body>");
  client.println("<h1>Welcome to Arduino Web Server</h1>");
  client.println("<p><a href='/json'>JSON Data</a></p>");
  client.println("<p><a href='/setting'>Settings</a></p>");
  client.println("</body>");
  client.println("</html>");
}

void sendJsonData(WiFiClient& client) {
  StaticJsonDocument<200> doc;
  doc["MAC Address"] = mac_address;
  doc["id"] = id;
  doc["machine_name"] = machine_name;
  doc["fix_move"] = fix_move;
  doc["status"] = "ok";

  JsonArray result = doc.createNestedArray("result");
  for (int i = 0; i < 6; i++) {
    result.add(random(2)); // Randomly add either a 0 or a 1
  }

  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:application/json");
  client.println("Connection: close");
  client.println();
  serializeJson(doc, client);
}

void send404(WiFiClient& client) {
  client.println("HTTP/1.1 404 Not Found");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body><h1>404: Not Found</h1></body></html>");
}
void sendSettingsPage(WiFiClient& client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-type:text/html");
  client.println("Connection: close");
  client.println();
  client.println("<html><body>");
  client.println("<form action=\"/setting\" method=\"POST\">");
  client.println("<label for=\"id\">ID:</label>");
  client.println("<input type=\"text\" id=\"id\" name=\"id\" required><br><br>");
  client.println("<label for=\"machine_name\">Machine Name:</label>");
  client.println("<input type=\"text\" id=\"machine_name\" name=\"machine_name\" required><br><br>");
  client.println("<label>Fix/Move:</label><br>");
  client.println("<input type=\"radio\" id=\"fix\" name=\"fix_move\" value=\"fix\" checked>");
  client.println("<label for=\"fix\">Fix</label><br>");
  client.println("<input type=\"radio\" id=\"move\" name=\"fix_move\" value=\"move\">");
  client.println("<label for=\"move\">Move</label><br><br>");
  client.println("<input type=\"submit\" value=\"Submit\">");
  client.println("</form>");
  client.println("</body></html>");
}

void handlePostData(WiFiClient& client, String postData) {
  int idIndex = postData.indexOf("id=");
  int machineNameIndex = postData.indexOf("machine_name=");
  int fixMoveIndex = postData.indexOf("fix_move=");

  if (idIndex != -1 && machineNameIndex != -1 && fixMoveIndex != -1) {
    id = postData.substring(idIndex + 3, postData.indexOf('&', idIndex)).toInt();
    machine_name = postData.substring(machineNameIndex + 13, postData.indexOf('&', machineNameIndex));
    fix_move = postData.substring(fixMoveIndex + 9);

    Serial.print("Updated ID: ");
    Serial.println(id);
    Serial.print("Updated Machine Name: ");
    Serial.println(machine_name);
    Serial.print("Updated Fix/Move: ");
    Serial.println(fix_move);

    // Write updated values to EEPROM
    EEPROM.write(idAddress, id);

    for (int i = 0; i < machine_name.length(); i++) {
      EEPROM.write(machineNameAddress + i, machine_name[i]);
    }

    for (int i = machine_name.length(); i < 20; i++) { // Clear remaining space
      EEPROM.write(machineNameAddress + i, '\0');
    }

    for (int i = 0; i < fix_move.length(); i++) {
      EEPROM.write(fixMoveAddress + i, fix_move[i]);
    }

    for (int i = fix_move.length(); i < 4; i++) { // Clear remaining space
      EEPROM.write(fixMoveAddress + i, '\0');
    }
  }

  // Send HTTP response
  client.println("HTTP/1.1 302 Found");
  client.println("Location: /setting");
  client.println("Connection: close");
  client.println();
}
