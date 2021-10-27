#include <Arduino.h>
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <ETH.h>
#include <Update.h>
#include <esp_wifi.h>

#define RELAY1 32
#define RELAY2 33

#define W0 14
#define W1 13
#define INPUT_1 15
#define INPUT_2 17

#define BUTTON 34

// free pin = GPIO16

AsyncWebServer server(80);

//create UDP instance for IO
WiFiUDP udp1;
WiFiUDP udp2;
String Input1_IP = "Not Set";
String Input2_IP = "Not Set";
String Port1 = "Not Set";
String Port2 = "Not Set";

// Global variables
bool isAPmodeOn = false;
bool shouldReboot = false;
String value_login[3];
bool userFlag = false;
static bool eth_connected = false;
IPAddress local_IP_STA, gateway_STA, subnet_STA, primaryDNS;

// Wiegand variables
bool activateWeigand = false, activateRFID = false;
bool wiegand_flag = false;
bool already_working = false;
String pulseWidth = "90";
String interPulseGap = "90";
String plate_number = "";
String database_url = "Not Set";

// Outputs variables
String Delay1 = "10";
String Delay2 = "10";
String status1 = "OFF";
String status2 = "OFF";
String authentication = "";
bool needManualCloseRelayOne = false;
bool needManualCloseRelayTwo = false;
unsigned int startTimeRelayOne = 0;
unsigned int startTimeRelayTwo = 0;
unsigned int currentTimeRelayOne = 0;
unsigned int currentTimeRelayTwo = 0;

// Global array to read data from network.txt
String v[7];

String ssid_ap = (String) "Metrici-" + WiFi.macAddress().substring(WiFi.macAddress().length() - 5, WiFi.macAddress().length());
String password_ap = "metrici@admin";
IPAddress local_ap(192, 168, 100, 10);
IPAddress subnet_ap(255, 255, 255, 0);

String strlog;

//------------------------- struct circular_buffer
struct ring_buffer
{
  ring_buffer(size_t cap) : buffer(cap) {}

  bool empty() const { return sz == 0; }
  bool full() const { return sz == buffer.size(); }

  void push(String str)
  {
    if (last >= buffer.size())
      last = 0;
    buffer[last] = str;
    ++last;
    if (full())
      first = (first + 1) % buffer.size();
    else
      ++sz;
  }
  void print() const
  {
    strlog = "";
    if (first < last)
      for (size_t i = first; i < last; ++i)
      {
        strlog += (buffer[i] + "<br>");
      }
    else
    {
      for (size_t i = first; i < buffer.size(); ++i)
      {
        strlog += (buffer[i] + "<br>");
      }
      for (size_t i = 0; i < last; ++i)
      {
        strlog += (buffer[i] + "<br>");
      }
    }
  }

private:
  std::vector<String> buffer;
  size_t first = 0;
  size_t last = 0;
  size_t sz = 0;
};
//------------------------- struct circular_buffer
ring_buffer circle(10);

//------------------------- logOutput(String)
void logOutput(String string1)
{
  circle.push(string1);
  Serial.println(string1);
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case SYSTEM_EVENT_ETH_START:
    Serial.println("ETH Started");
    //set eth hostname here
    if (!ETH.setHostname("Metrici-MultiController-Eth"))
    {
      Serial.println("Ethernet hostname failed to configure");
    }
    break;
  case SYSTEM_EVENT_ETH_CONNECTED:
    Serial.println("ETH Connected");
    break;
  case SYSTEM_EVENT_ETH_GOT_IP:
    eth_connected = true;
    Serial.print("ETH MAC: ");
    Serial.print(ETH.macAddress());
    Serial.print(", IPv4: ");
    Serial.print(ETH.localIP());
    if (ETH.fullDuplex())
    {
      Serial.print(", FULL_DUPLEX");
    }
    Serial.print(", ");
    Serial.print(ETH.linkSpeed());
    Serial.println("Mbps");
    break;
  case SYSTEM_EVENT_ETH_DISCONNECTED:
    Serial.println("ETH Disconnected");
    eth_connected = false;
    break;
  case SYSTEM_EVENT_ETH_STOP:
    Serial.println("ETH Stopped");
    eth_connected = false;
    break;
  default:
    break;
  }
}

//------------------------- fileReadLines()
void fileReadLines(File file, String x[])
{
  int i = 0;
  while (file.available())
  {
    String line = file.readStringUntil('\n');
    line.trim();
    x[i] = line;
    i++;
    // logOutput((String)"Line: " + line);
  }
}

String readString(File s)
{
  // Read from file witout yield
  String ret;
  int c = s.read();
  while (c >= 0)
  {
    ret += (char)c;
    c = s.read();
  }
  return ret;
}

// Add files to the /files table
String &addDirList(String &HTML)
{
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  int count = 0;
  while (file)
  {
    String fileName = file.name();
    File dirFile = SPIFFS.open(fileName, "r");
    size_t filelen = dirFile.size();
    dirFile.close();
    String filename_temp = fileName;
    filename_temp.replace("/", "");
    String directories = "<form action=\"/files\" method=\"POST\">\r\n<tr><td align=\"center\">";
    if (fileName.indexOf(".txt") > 0)
    {
      directories += "<input type=\"hidden\" name = \"filename\" value=\"" + fileName + "\">" + filename_temp;
      directories += "</td>\r\n<td align=\"center\">" + String(filelen, DEC);
      directories += "</td><td align=\"center\"><button style=\"margin-right:20px;\" type=\"submit\" name= \"download\">Download</button><button type=\"submit\" name= \"delete\">Delete</button></td>\r\n";
      directories += "</tr></form>\r\n~directories~";
      HTML.replace("~directories~", directories);
      count++;
    }
    file = root.openNextFile();
  }
  HTML.replace("~directories~", "");

  HTML.replace("~count~", String(count, DEC));
  HTML.replace("~total~", String(SPIFFS.totalBytes() / 1024, DEC));
  HTML.replace("~used~", String(SPIFFS.usedBytes() / 1024, DEC));
  HTML.replace("~free~", String((SPIFFS.totalBytes() - SPIFFS.usedBytes()) / 1024, DEC));

  return HTML;
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final)
{
  if (filename.indexOf(".bin") > 0)
  {
    if (!index)
    {
      logOutput("The update process has started...");
      // if filename includes spiffs, update the spiffs partition
      int cmd = (filename.indexOf("spiffs") > -1) ? U_SPIFFS : U_FLASH;
      if (!Update.begin(UPDATE_SIZE_UNKNOWN, cmd))
      {
        Update.printError(Serial);
      }
    }

    if (Update.write(data, len) != len)
    {
      Update.printError(Serial);
    }

    if (final)
    {
      if (filename.indexOf("spiffs") > -1)
      {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">The device entered AP Mode ! Please connect to it.</div>");
      }
      else
      {
        request->send(200, "text/html", "<div style=\"margin:0 auto; text-align:center; font-family:arial;\">Congratulation ! </br> You have successfully updated the device to the latest version. </br>Please wait 10 seconds until the device reboots, then press on the \"Go Home\" button to go back to the main page.</br></br> <form method=\"post\" action=\"http://" + local_IP_STA.toString() + "\"><input type=\"submit\" name=\"goHome\" value=\"Go Home\"/></form></div>");
      }

      if (!Update.end(true))
      {
        Update.printError(Serial);
      }
      else
      {
        logOutput("Update complete");
        Serial.flush();
        ESP.restart();
      }
    }
  }
  else if (filename.indexOf(".txt") > 0)
  {
    if (!index)
    {
      logOutput((String) "Started uploading: " + filename);
      // open the file on first call and store the file handle in the request object
      request->_tempFile = SPIFFS.open("/" + filename, "w");
    }
    if (len)
    {
      // stream the incoming chunk to the opened file
      request->_tempFile.write(data, len);
    }
    if (final)
    {
      logOutput((String)filename + " was successfully uploaded! File size: " + index + len);
      // close the file handle as the upload is now done
      request->_tempFile.close();
      request->redirect("/files");
    }
  }
}

//------------------------- processor()
String processor(const String &var)
{
  circle.print();
  if (var == "PH_Version")
    return String("v2.1");
  else if (var == "PH_SSID")
    return WiFi.SSID();
  else if (var == "PH_Gateway")
    return gateway_STA.toString();
  else if (var == "PH_Subnet")
    return subnet_STA.toString();
  else if (var == "PH_DNS")
    return primaryDNS.toString();
  else if (var == "PH_Timer1")
    return Delay1;
  else if (var == "PH_Timer2")
    return Delay2;
  else if (var == "PH_Status1")
    return status1;
  else if (var == "PH_Status2")
    return status2;
  else if (var == "PH_Auth")
    return authentication;
  else if (var == "PH_IP_Addr")
    return local_IP_STA.toString();
  else if (var == "PH_Input1")
    return Input1_IP;
  else if (var == "PH_Port1")
    return Port1;
  else if (var == "PH_Input2")
    return Input2_IP;
  else if (var == "PH_Port2")
    return Port2;
  else if (var == "PH_databaseURL")
    return database_url;
  else if (var == "PH_Pulse")
    return pulseWidth;
  else if (var == "PH_Gap")
    return interPulseGap;
  else if (var == "PLACEHOLDER_LOGS")
    return String(strlog);
  return String();
}

class Wiegand
{
private:
  std::vector<bool> wiegandArray;
  std::vector<bool> facilityCode = std::vector<bool>(8, false);
  std::vector<bool> cardNumber = std::vector<bool>(16, false);
  String s1;

  void calculateFacilityCode(uint8_t dec)
  {
    for (int8_t i = 7; i >= 0; --i)
    {
      facilityCode[i] = dec & 1;
      dec >>= 1;
    }
  }
  void calculateCardNumber(uint16_t dec)
  {
    for (int8_t i = 15; i >= 0; --i)
    {
      cardNumber[i] = dec & 1;
      dec >>= 1;
    }
  }
  void calculateWiegand()
  {
    wiegandArray.clear();
    wiegandArray.reserve(facilityCode.size() + cardNumber.size());
    wiegandArray.insert(wiegandArray.end(), facilityCode.begin(), facilityCode.end());
    wiegandArray.insert(wiegandArray.end(), cardNumber.begin(), cardNumber.end());
    // check for parity of the first 12 bits
    bool even = 0;
    for (int i = 0; i < 12; i++)
    {
      even ^= wiegandArray[i];
    }
    // check for parity of the last 12 bits
    bool odd = 1;
    for (int i = 12; i < 24; i++)
    {
      odd ^= wiegandArray[i];
    }
    // add 0 or 1 as first bit (leading parity bit - even) based on the number of 'ones' in the first 12 bits
    wiegandArray.insert(wiegandArray.begin(), even);
    // add 0 or 1 as last bit (trailing parity bit - odd) based on the number of 'ones' in the last 12 bits
    wiegandArray.push_back(odd);
  }

public:
  void update(String fCode, String cNumber)
  {
    // while (fCode.length() < 3)
    // {
    //   fCode = String(0) + fCode;
    // }
    // while (cNumber.length() < 5)
    // {
    //   cNumber = String(0) + cNumber;
    // }
    s1 = fCode + cNumber;
    calculateFacilityCode(fCode.toInt());
    calculateCardNumber(cNumber.toInt());
    calculateWiegand();
  }
  // returns a 26 length std::vector<bool>
  std::vector<bool> getWiegandBinary()
  {
    return wiegandArray;
  }
  // returns a 26 length String
  String getWiegandBinaryInString()
  {
    String binary_array = "";
    for (bool i : wiegandArray)
    {
      binary_array += (String)i;
    }
    return binary_array;
  }
  // returns an 8 characters long String
  String getCardID()
  {
    return s1;
  }

  std::vector<bool> getFacilityCode_vector()
  {
    return facilityCode;
  }
  std::vector<bool> getCardNumber_vector()
  {
    return cardNumber;
  }
  uint8_t getFacilityCode_int()
  {
    return s1.substring(0, 3).toInt();
  }
  uint16_t getCardNumber_int()
  {
    return s1.substring(3, 8).toInt();
  }
} card;

//------------------------- void ethernetConfig(String x[])
void ethernetConfig(String x[])
{
  if (x[0] == "WiFi")
    return;
  ETH.begin();

  if (x[1] != NULL && //Local IP
      x[1].length() != 0 &&
      x[2] != NULL && // Gateway
      x[2].length() != 0 &&
      x[3] != NULL && // Subnet
      x[3].length() != 0 &&
      x[4] != NULL && // DNS
      x[4].length() != 0)
  {
    local_IP_STA.fromString(x[1]);
    gateway_STA.fromString(x[2]);
    subnet_STA.fromString(x[3]);
    primaryDNS.fromString(x[4]);

    if (!ETH.config(local_IP_STA, gateway_STA, subnet_STA, primaryDNS))
    {
      logOutput("Couldn't configure STATIC IP ! Obtaining DHCP IP !");
    }
  }
  else
  {
    logOutput("Obtaining DHCP IP !");
  }

  int ki = 0;
  while (!eth_connected && ki < 20)
  {
    ki++;
    delay(1000);
    Serial.println((String) "[" + ki + "] - Establishing ETHERNET Connection ... ");
  }
  if (!eth_connected)
  {
    logOutput("(1) Could not access Network ! Trying again...");
    logOutput("Controller will restart in 5 seconds !");
    delay(5000);
    ESP.restart();
  }

  local_IP_STA = ETH.localIP();
  gateway_STA = ETH.gatewayIP();
  subnet_STA = ETH.subnetMask();
  primaryDNS = ETH.dnsIP();
  logOutput((String) "IP address: " + local_IP_STA.toString());
  logOutput((String) "Gateway: " + gateway_STA.toString());
  logOutput((String) "Subnet: " + subnet_STA.toString());
  logOutput((String) "DNS: " + primaryDNS.toString());
}

//------------------------- wifiConfig()
void wifiConfig(String x[])
{
  if (x[0] == "Ethernet")
    return;
  WiFi.mode(WIFI_MODE_STA);
  // WiFi.disconnect();
  // WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE);
  if (!WiFi.setHostname("Metrici-MultiController-WiFi"))
  {
    Serial.println("WiFi hostname failed to configure");
  }
  if (x[1] != NULL && // SSID
      x[1].length() != 0 &&
      x[2] != NULL && // Password
      x[2].length() != 0 &&
      x[3] != NULL && //Local IP
      x[3].length() != 0 &&
      x[4] != NULL && // Gateway
      x[4].length() != 0 &&
      x[5] != NULL && // Subnet
      x[5].length() != 0 &&
      x[6] != NULL && // DNS
      x[6].length() != 0)
  {
    local_IP_STA.fromString(x[3]);
    gateway_STA.fromString(x[4]);
    subnet_STA.fromString(x[5]);
    primaryDNS.fromString(x[6]);
    if (!WiFi.config(local_IP_STA, gateway_STA, subnet_STA, primaryDNS))
    {
      logOutput("Couldn't configure STATIC IP ! Starting DHCP IP !");
    }
    delay(50);
    WiFi.begin(x[1].c_str(), x[2].c_str());
    WiFi.setSleep(false);
  }
  else if (x[1] != NULL && // SSID
           x[1].length() != 0 &&
           x[2] != NULL && // Password
           x[2].length() != 0)
  {
    WiFi.begin(x[1].c_str(), x[2].c_str());
    WiFi.setSleep(false);
  }

  int k = 0;
  while (WiFi.status() != WL_CONNECTED && k < 20)
  {
    k++;
    delay(1000);
    Serial.println((String) "[" + k + "] - Establishing WiFi Connection ... ");
  }
  if (WiFi.status() != WL_CONNECTED)
  {
    logOutput("(1) Could not access Wireless Network ! Trying again...");
    logOutput("Controller will restart in 5 seconds !");
    delay(5000);
    ESP.restart();
  }
  local_IP_STA = WiFi.localIP();
  gateway_STA = WiFi.gatewayIP();
  subnet_STA = WiFi.subnetMask();
  primaryDNS = WiFi.dnsIP();
  logOutput((String) "Connected to " + x[1] + " with IP addres: " + local_IP_STA.toString());
  logOutput((String) "Gateway: " + gateway_STA.toString());
  logOutput((String) "Subnet: " + subnet_STA.toString());
  logOutput((String) "DNS: " + primaryDNS.toString());
}

//------------------------- startWiFiAP()
void startWiFiAP()
{
  delay(50);
  if (SPIFFS.exists("/network.txt"))
    SPIFFS.remove("/network.txt");

  logOutput("Starting AP ... ");
  logOutput(WiFi.softAP(ssid_ap.c_str(), password_ap.c_str()) ? (String)ssid_ap + " ready" : "WiFi.softAP failed ! (Password must be at least 8 characters long )");
  delay(100);
  logOutput("Setting AP configuration ... ");
  logOutput(WiFi.softAPConfig(local_ap, local_ap, subnet_ap) ? "Ready" : "Failed!");
  delay(100);
  logOutput("Soft-AP IP address: ");
  logOutput(WiFi.softAPIP().toString());

  // while(WiFi.softAPgetStationNum() == 0)
  // {
  //   if(digitalRead(RELAY1) != HIGH)
  //     digitalWrite(RELAY1, HIGH);
  // }
  // digitalWrite(RELAY1, LOW);

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/favicon.ico", "image/ico"); });
  server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/newMaster.css", "text/css"); });
  server.on("/jsMaster.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/jsMaster.js", "text/javascript"); });
  server.on("/jquery-3.6.0.slim.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/jquery-3.6.0.slim.min.js", "text/javascript"); });
  server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/logo.png", "image/png"); });
  server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor); });

  server.on("/register", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/createUser.html", "text/html", false, processor); });
  server.on("/chooseIP", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/chooseIP.html", "text/html", false, processor); });
  server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/dhcpIP_AP.html", "text/html", false, processor); });
  server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/staticIP_AP.html", "text/html", false, processor); });
  server.on("/logs", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(SPIFFS, "/events.html", "text/html", false, processor); });

  server.on("/register", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              if (request->hasArg("register"))
              {
                int params = request->params();
                String values_user[params];
                for (int i = 0; i < params; i++)
                {
                  AsyncWebParameter *p = request->getParam(i);
                  if (p->isPost())
                  {
                    logOutput((String) "POST[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
                    values_user[i] = p->value();
                  }
                  else
                  {
                    logOutput((String) "GET[" + p->name().c_str() + "]: " + p->value().c_str() + "\n");
                  }
                } // for(int i=0;i<params;i++)

                if (values_user[0] != NULL && values_user[0].length() > 4 &&
                    values_user[1] != NULL && values_user[1].length() > 7)
                {
                  File userWrite = SPIFFS.open("/user.txt", "w");
                  if (!userWrite)
                    logOutput("ERROR_INSIDE_POST ! Couldn't open file to write USER credentials !");
                  userWrite.println(values_user[0]); // Username
                  userWrite.println(values_user[1]); // Password
                  userWrite.close();
                  logOutput("Username and password saved !");
                  request->redirect("/chooseIP");
                }
                else
                  request->redirect("/register");
              }
              else if (request->hasArg("skip"))
              {
                request->redirect("/chooseIP");
              }
              else if (request->hasArg("import"))
              {
                request->redirect("/files");
              }
              else
              {
                request->redirect("/register");
              }
            }); // server.on("/register", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request)
            {
              if (request->hasParam("filename", true))
              { // Check for files
                if (request->hasArg("download"))
                { // Download file
                  Serial.println("Download Filename: " + request->arg("filename"));
                  AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
                  response->addHeader("Server", "ESP Async Web Server");
                  request->send(response);
                  return;
                }
                else if (request->hasArg("delete"))
                { // Delete file
                  if (SPIFFS.remove(request->getParam("filename", true)->value()))
                  {
                    logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
                  }
                  else
                  {
                    logOutput("Could not delete file. Try again !");
                  }
                  request->redirect("/files");
                }
              }
              else if (request->hasArg("restart_device"))
              {
                request->send(200, "text/plain", "The device will reboot shortly !");
                shouldReboot = true;
              }

              String HTML PROGMEM; // HTML code
              File pageFile = SPIFFS.open("/files_AP.html", "r");
              if (pageFile)
              {
                HTML = readString(pageFile);
                pageFile.close();
                HTML = addDirList(HTML);
                AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
                response->addHeader("Server", "ESP Async Web Server");
                request->send(response);
              }
            });

  server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              if (request->hasArg("saveStatic"))
              {
                int params = request->params();
                String values_static_post[params];

                for (int i = 0; i < params; i++)
                {
                  AsyncWebParameter *p = request->getParam(i);
                  if (p->isPost())
                  {
                    logOutput((String) "POST[" + p->name() + "]: " + p->value());
                    values_static_post[i] = p->value();
                  }
                  else
                  {
                    logOutput((String) "GET[" + p->name() + "]: " + p->value());
                  }
                } // for(int i=0;i<params;i++)

                if (values_static_post[0] == "WiFi")
                {
                  if (values_static_post[1] != NULL &&
                      values_static_post[1].length() != 0 &&
                      values_static_post[2] != NULL &&
                      values_static_post[2].length() != 0 &&
                      values_static_post[3] != NULL &&
                      values_static_post[3].length() != 0 &&
                      values_static_post[4] != NULL &&
                      values_static_post[4].length() != 0 &&
                      values_static_post[5] != NULL &&
                      values_static_post[5].length() != 0 &&
                      values_static_post[6] != NULL &&
                      values_static_post[6].length() != 0)
                  {
                    File inputsWrite = SPIFFS.open("/network.txt", "w");
                    if (!inputsWrite)
                      logOutput("ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !");
                    inputsWrite.println(values_static_post[0]); // Connection Type ? WiFi : Ethernet
                    inputsWrite.println(values_static_post[1]); // SSID
                    inputsWrite.println(values_static_post[2]); // Password
                    inputsWrite.println(values_static_post[3]); // Local IP
                    inputsWrite.println(values_static_post[4]); // Gateway
                    inputsWrite.println(values_static_post[5]); // Subnet
                    inputsWrite.println(values_static_post[6]); // DNS
                    inputsWrite.close();
                    logOutput("Configuration saved !");
                    request->redirect("/logs");
                    shouldReboot = true;
                  }
                  else
                    request->redirect("/staticIP");
                }
                else if (values_static_post[0] == "Ethernet")
                {
                  if (values_static_post[3] != NULL &&
                      values_static_post[3].length() != 0 &&
                      values_static_post[4] != NULL &&
                      values_static_post[4].length() != 0 &&
                      values_static_post[5] != NULL &&
                      values_static_post[5].length() != 0 &&
                      values_static_post[6] != NULL &&
                      values_static_post[6].length() != 0)
                  {
                    File inputsWrite = SPIFFS.open("/network.txt", "w");
                    if (!inputsWrite)
                      logOutput("ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !");
                    inputsWrite.println(values_static_post[0]); // Connection Type ? WiFi : Ethernet
                    inputsWrite.println(values_static_post[3]); // Local IP
                    inputsWrite.println(values_static_post[4]); // Gateway
                    inputsWrite.println(values_static_post[5]); // Subnet
                    inputsWrite.println(values_static_post[6]); // DNS
                    inputsWrite.close();
                    logOutput("Configuration saved !");
                    request->redirect("/logs");
                    shouldReboot = true;
                  }
                  else
                    request->redirect("/staticIP");
                } // if(values_static_post[0] == "WiFi")
              }
              else
              {
                request->redirect("/staticIP");
              } // if(request->hasArg("saveStatic"))
            }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest *request)
            {
              if (request->hasArg("saveDHCP"))
              {
                int params = request->params();
                String values_dhcp_post[params];

                for (int i = 0; i < params; i++)
                {
                  AsyncWebParameter *p = request->getParam(i);
                  if (p->isPost())
                  {
                    logOutput((String) "POST[" + p->name() + "]: " + p->value());
                    values_dhcp_post[i] = p->value();
                  }
                  else
                  {
                    logOutput((String) "GET[" + p->name() + "]: " + p->value());
                  }
                }

                if (values_dhcp_post[0] == "WiFi")
                {
                  if (values_dhcp_post[1] != NULL && values_dhcp_post[1].length() != 0 &&
                      values_dhcp_post[2] != NULL && values_dhcp_post[2].length() != 0)
                  {
                    File inputsWrite = SPIFFS.open("/network.txt", "w");
                    if (!inputsWrite)
                      logOutput("ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
                    inputsWrite.println(values_dhcp_post[0]); // Connection Type ? WiFi : Ethernet
                    inputsWrite.println(values_dhcp_post[1]); // SSID
                    inputsWrite.println(values_dhcp_post[2]); // Password
                    inputsWrite.close();
                    logOutput("Configuration saved !");
                    request->redirect("/logs");
                    shouldReboot = true;
                  }
                  else
                    request->redirect("/dhcpIP");
                }
                else if (values_dhcp_post[0] == "Ethernet")
                {
                  File inputsWrite = SPIFFS.open("/network.txt", "w");
                  if (!inputsWrite)
                    logOutput("ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
                  inputsWrite.println(values_dhcp_post[0]); // Connection Type ? WiFi : Ethernet
                  inputsWrite.close();
                  logOutput("Configuration saved !");
                  request->redirect("/logs");
                  shouldReboot = true;
                }
              }
              else
              {
                request->redirect("/dhcpIP");
              } // if(request->hasArg("saveStatic"))
            }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)

  server.onFileUpload(handleUpload);
  server.onNotFound([](AsyncWebServerRequest *request)
                    { request->redirect("/register"); });
  server.begin(); //-------------------------------------------------------------- server.begin()

} // void startWiFiAP()

//------------------------- listAllFiles()
void listAllFiles()
{
  File root = SPIFFS.open("/");
  File file = root.openNextFile();
  while (file)
  {
    Serial.print("FILE: ");
    String fileName = file.name();
    Serial.println(fileName);
    file = root.openNextFile();
  }
  file.close();
  root.close();
}

void setup()
{
  enableCore1WDT();
  delay(100);
  Serial.begin(115200);
  delay(100);

  WiFi.onEvent(WiFiEvent);

  //setting the pins for Outputs
  pinMode(RELAY1, OUTPUT);
  pinMode(RELAY2, OUTPUT);
  digitalWrite(RELAY1, LOW);
  digitalWrite(RELAY2, LOW);

  //setting the pins for Wiegand
  pinMode(W0, OUTPUT);
  pinMode(W1, OUTPUT);
  digitalWrite(W0, HIGH);
  digitalWrite(W1, HIGH);

  //setting the pins for Inputs
  pinMode(INPUT_1, INPUT_PULLUP);
  pinMode(INPUT_2, INPUT_PULLUP);
  pinMode(BUTTON, INPUT_PULLUP);

  int q = 0;
  if (digitalRead(BUTTON) == LOW)
  {
    q++;
    delay(2000);
    if (digitalRead(BUTTON) == LOW)
    {
      q++;
    }
  }

  Serial.println("State of Induction PINS: ");
  Serial.print("INPUT_1 (GPIO 15): ");
  Serial.println(digitalRead(INPUT_1));
  Serial.print("INPUT_2 (GPIO 17): ");
  Serial.println(digitalRead(INPUT_2));

  delay(100);

  if (!SPIFFS.begin(true))
  {
    Serial.println("An Error has occurred while mounting SPIFFS ! Formatting in progress");
    return;
  }

  listAllFiles();

  if (q == 2)
  {
    if (SPIFFS.exists("/network.txt"))
      SPIFFS.remove("/network.txt");
    if (SPIFFS.exists("/user.txt"))
      SPIFFS.remove("/user.txt");
    if (SPIFFS.exists("/outputs.txt"))
      SPIFFS.remove("/outputs.txt");
    if (SPIFFS.exists("/inputs.txt"))
      SPIFFS.remove("/inputs.txt");
    if (SPIFFS.exists("/wiegand.txt"))
      SPIFFS.remove("/wiegand.txt");
    logOutput((String) "WARNING: Reset button was pressed ! AP will start momentarily");
  }

  //--- Check if /network.txt exists. If not then create one
  if (!SPIFFS.exists("/network.txt"))
  {
    File inputsCreate = SPIFFS.open("/network.txt", "w");
    if (!inputsCreate)
      logOutput("Couldn't create /network.txt");
    logOutput("Was /network.txt created ?");
    logOutput(SPIFFS.exists("/network.txt") ? "Yes" : "No");
    inputsCreate.close();
  }

  //--- Check if /network.txt is empty (0 bytes) or not (>8)
  //--- println adds /r and /n, which means 2 bytes
  //--- 2 lines = 2 println = 4 bytes
  File networkRead = SPIFFS.open("/network.txt");
  if (!networkRead)
    logOutput("ERROR ! Couldn't open file to read !");

  if (networkRead.size() > 8)
  {
    //--- Read SSID, Password, Local IP, Gateway, Subnet, DNS from file
    //--- and store them in v[]
    fileReadLines(networkRead, v);
    networkRead.close();
    if (v[0] == "WiFi")
    { // WiFi ?
      esp_wifi_set_ps(WIFI_PS_NONE);
      //--- Start ESP in Station Mode using the above credentials
      wifiConfig(v);
    }
    else if (v[0] == "Ethernet")
    { // Ethernet ?
      ethernetConfig(v);
    } // if(v[0] == "WiFi")

    if (SPIFFS.exists("/user.txt"))
    {
      File userRead = SPIFFS.open("/user.txt", "r");
      if (!userRead)
        logOutput("Couldn't read /user.txt");
      fileReadLines(userRead, value_login);
      if (value_login[0].length() > 4 && value_login[0] != NULL &&
          value_login[1].length() > 7 && value_login[1] != NULL)
      {
        userFlag = true;
        authentication = String(value_login[0] + ":" + value_login[1] + "@");
      }
    }

    if (SPIFFS.exists("/outputs.txt"))
    {
      String values_config[2];
      File configRead = SPIFFS.open("/outputs.txt", "r");
      if (!configRead)
        logOutput("ERROR_INSIDE_CONFIG_FILE_READ ! Couldn't open /configFile to read values!");
      fileReadLines(configRead, values_config);
      if (values_config[0] != NULL || values_config[0].length() != 0)
      {
        Delay1 = values_config[0];
      }
      else
      {
        values_config[0] = "Default Timer 1: " + Delay1;
      }
      if (values_config[1] != NULL || values_config[1].length() != 0)
      {
        Delay2 = values_config[1];
      }
      else
      {
        values_config[1] = "Default Timer 2: " + Delay2;
      }
      logOutput("Present Inputs Configuration: ");
      for (int k = 0; k < 2; k++)
      {
        logOutput((String)(k + 1) + ": " + values_config[k]);
      }
      configRead.close();
    }

    if (SPIFFS.exists("/inputs.txt"))
    {
      String values_config[4];
      File configRead = SPIFFS.open("/inputs.txt", "r");
      if (!configRead)
        logOutput("ERROR_INSIDE_CONFIG_FILE_READ ! Couldn't open /configFile to read values!");
      fileReadLines(configRead, values_config);
      if (values_config[0] != NULL || values_config[0].length() != 0)
      {
        Input1_IP = values_config[0];
      }
      if (values_config[1] != NULL || values_config[1].length() != 0)
      {
        Port1 = values_config[1];
      }
      if (values_config[2] != NULL || values_config[2].length() != 0)
      {
        Input2_IP = values_config[2];
      }
      if (values_config[3] != NULL || values_config[3].length() != 0)
      {
        Port2 = values_config[3];
      }
      logOutput("Present Outputs Configuration: ");
      for (int k = 0; k < 4; k++)
      {
        logOutput((String)(k + 1) + ": " + values_config[k]);
      }
      configRead.close();
    }

    if (SPIFFS.exists("/wiegand.txt"))
    {
      String values_config[3];
      File configRead = SPIFFS.open("/wiegand.txt", "r");
      if (!configRead)
        logOutput("ERROR_INSIDE_CONFIG_FILE_READ ! Couldn't open /configFile to read values!");
      fileReadLines(configRead, values_config);
      if (values_config[0] != NULL || values_config[0].length() != 0)
      {
        database_url = values_config[0];
      }
      else
      {
        values_config[0] = "By default Database URL is not set.";
      }
      if (values_config[1] != NULL || values_config[1].length() != 0)
      {
        pulseWidth = values_config[1];
      }
      else
      {
        values_config[1] = "Default Pulse Width (µs): " + pulseWidth;
      }
      if (values_config[2] != NULL || values_config[2].length() != 0)
      {
        interPulseGap = values_config[2];
      }
      else
      {
        values_config[2] = "Default Inter Pulse Gap (µs): " + interPulseGap;
      }
      logOutput("Present Wiegand Configuration: ");
      for (int k = 0; k < 3; k++)
      {
        logOutput((String)(k + 1) + ": " + values_config[k]);
      }
      configRead.close();
    }

    server.on("/newMaster.css", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/newMaster.css", "text/css"); });
    server.on("/jsMaster.js", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/jsMaster.js", "text/javascript"); });
    server.on("/jquery-3.6.0.slim.min.js", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/jquery-3.6.0.slim.min.js", "text/javascript"); });
    server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/favicon.ico", "image/ico"); });
    server.on("/logo.png", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/logo.png", "image/png"); });
    server.on("/events_placeholder.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/events_placeholder.html", "text/html", false, processor); });
    server.on("/relay_status1.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/relay_status1.html", "text/html", false, processor); });
    server.on("/relay_status2.html", HTTP_GET, [](AsyncWebServerRequest *request)
              { request->send(SPIFFS, "/relay_status2.html", "text/html", false, processor); });

    server.on("/relay1/on", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  digitalWrite(RELAY1, HIGH);
                  status1 = "ON";
                  logOutput("Relay1 is ON");
                  if (Delay1.toInt() == 0)
                  {
                    needManualCloseRelayOne = true;
                    logOutput(" Relay 1 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayOne = false;
                    logOutput(" Relay 1 will automatically close in " + Delay1 + " seconds !");
                  }
                  startTimeRelayOne = millis();
                  // Serial.println("Do I get here ? /relay1/on");
                  request->send(200, "text/plain", "Relay 1 is ON");
                }
                else
                {
                  digitalWrite(RELAY1, HIGH);
                  status1 = "ON";
                  logOutput(" Relay 1 is ON");
                  if (Delay1.toInt() == 0)
                  {
                    needManualCloseRelayOne = true;
                    logOutput(" Relay 1 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayOne = false;
                    logOutput(" Relay 1 will automatically close in " + Delay1 + " seconds !");
                  }
                  startTimeRelayOne = millis();
                  // Serial.println("Do I get here ? /relay1/on");
                  request->send(200, "text/plain", "Relay 1 is ON");
                }
              });

    server.on("/relay1/off", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  digitalWrite(RELAY1, LOW);
                  status1 = "OFF";
                  logOutput(" Relay 1 is OFF");
                  needManualCloseRelayOne = true;
                  // Serial.println("Do I get here ? /relay1/off");
                  request->send(200, "text/plain", "Relay 1 is OFF");
                }
                else
                {
                  digitalWrite(RELAY1, LOW);
                  status1 = "OFF";
                  logOutput(" Relay 1 is OFF");
                  needManualCloseRelayOne = true;
                  // Serial.println("Do I get here ? /relay1/off");
                  request->send(200, "text/plain", "Relay 1 is OFF");
                }
              });

    server.on("/relay2/on", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  digitalWrite(RELAY2, HIGH);
                  status2 = "ON";
                  logOutput("Relay 2 is ON");
                  if (Delay2.toInt() == 0)
                  {
                    needManualCloseRelayTwo = true;
                    logOutput("Relay 2 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayTwo = false;
                    logOutput("Relay 2 will automatically close in " + Delay2 + " seconds !");
                  }
                  startTimeRelayTwo = millis();
                  // Serial.println("Do I get here ? /relay2/on");
                  request->send(200, "text/plain", "Relay 2 is ON");
                }
                else
                {
                  digitalWrite(RELAY2, HIGH);
                  status2 = "ON";
                  logOutput("Relay 2 is ON");
                  if (Delay2.toInt() == 0)
                  {
                    needManualCloseRelayTwo = true;
                    logOutput("Relay 2 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayTwo = false;
                    logOutput("Relay 2 will automatically close in " + Delay2 + " seconds !");
                  }
                  startTimeRelayTwo = millis();
                  // Serial.println("Do I get here ? /relay2/on");
                  request->send(200, "text/plain", "Relay 2 is ON");
                }
              });

    server.on("/relay2/off", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  digitalWrite(RELAY2, LOW);
                  status2 = "OFF";
                  logOutput("Relay 2 is OFF");
                  needManualCloseRelayTwo = true;
                  // Serial.println("Do I get here ? /relay2/off");
                  request->send(200, "text/plain", "Relay 2 is OFF");
                }
                else
                {
                  digitalWrite(RELAY2, LOW);
                  status2 = "OFF";
                  logOutput("Relay 2 is OFF");
                  needManualCloseRelayTwo = true;
                  // Serial.println("Do I get here ? /relay2/off");
                  request->send(200, "text/plain", "Relay 2 is OFF");
                }
              });

    server.on("/home", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/index.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/index.html", "text/html", false, processor);
                }
              });

    server.on("/inputs-outputs", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/IO_settings.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/IO_settings.html", "text/html", false, processor);
                }
              });

    server.on("/dhcpIP", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/dhcpIP_STA.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/dhcpIP_STA.html", "text/html", false, processor);
                }
              });

    server.on("/staticIP", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/staticIP_STA.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/staticIP_STA.html", "text/html", false, processor);
                }
              });

    server.on("/wiegand-settings", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/wiegand_settings.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/wiegand_settings.html", "text/html", false, processor);
                }
              });

    server.on("/update", HTTP_GET, [](AsyncWebServerRequest *request)
              {
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
                }
                else
                {
                  request->send(SPIFFS, "/update_page.html", "text/html", false, processor);
                }
              });

    server.on("/inputs-outputs", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if (digitalRead(RELAY1))
                {
                  status1 = "ON";
                }
                else
                {
                  status1 = "OFF";
                }
                if (digitalRead(RELAY2))
                {
                  status2 = "ON";
                }
                else
                {
                  status2 = "OFF";
                }
                if (request->hasArg("send_outputs"))
                {
                  String b = request->arg("getDelay1");
                  String d = request->arg("getDelay2");

                  if (b.length() != 0)
                  {
                    Delay1 = b;
                    logOutput((String) "POST[getDelay1]: " + Delay1);
                  }
                  if (d.length() != 0)
                  {
                    Delay2 = d;
                    logOutput((String) "POST[getDelay2]: " + Delay2);
                  }

                  File configWrite = SPIFFS.open("/outputs.txt", "w");
                  if (!configWrite)
                    logOutput("ERROR_INSIDE_DELAY_POST ! Couldn't open file to save DELAY !");
                  configWrite.println(Delay1);
                  configWrite.println(Delay2);
                  configWrite.close();

                  request->redirect("/inputs-outputs");
                }
                else if (request->hasArg("send_inputs"))
                {
                  String a = request->arg("getInput1");
                  String b = request->arg("getPort1");
                  String c = request->arg("getInput2");
                  String d = request->arg("getPort2");

                  if (a.length() != 0)
                  {
                    Input1_IP = a;
                    logOutput((String) "POST[getInput1]: " + Input1_IP);
                  }
                  if (b.length() != 0)
                  {
                    Port1 = b;
                    logOutput((String) "POST[getPort1]: " + Port1);
                  }
                  if (c.length() != 0)
                  {
                    Input2_IP = c;
                    logOutput((String) "POST[getInput2]: " + Input2_IP);
                  }
                  if (d.length() != 0)
                  {
                    Port2 = d;
                    logOutput((String) "POST[getPort2]: " + Port2);
                  }

                  File configWrite = SPIFFS.open("/inputs.txt", "w");
                  if (!configWrite)
                    logOutput("ERROR_INSIDE_DELAY_POST ! Couldn't open file to save DELAY !");
                  configWrite.println(Input1_IP);
                  configWrite.println(Port1);
                  configWrite.println(Input2_IP);
                  configWrite.println(Port2);
                  configWrite.close();

                  request->redirect("/inputs-outputs");
                }
                else if (request->hasArg("relay1_on"))
                {
                  Serial.println("relay1 /on button was PRESSED");
                  digitalWrite(RELAY1, HIGH);
                  status1 = "ON";
                  logOutput(" Relay 1 is ON");
                  if (Delay1.toInt() == 0)
                  {
                    needManualCloseRelayOne = true;
                    logOutput(" Relay 1 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayOne = false;
                    logOutput(" Relay 1 will automatically close in " + Delay1 + " seconds !");
                  }
                  startTimeRelayOne = millis();
                  request->redirect("/inputs-outputs");
                }
                else if (request->hasArg("relay1_off"))
                {
                  Serial.println("relay1 /off button was PRESSED");
                  logOutput("You have manually closed Relay 1");
                  digitalWrite(RELAY1, LOW);
                  status1 = "OFF";
                  needManualCloseRelayOne = true; // I pressed the /off button and the timer won't start
                  logOutput(" Relay 1 is OFF");
                  request->redirect("/inputs-outputs");
                }
                else if (request->hasArg("relay2_on"))
                {
                  Serial.println("relay2 /on button was PRESSED");
                  digitalWrite(RELAY2, HIGH);
                  status2 = "ON";
                  logOutput("Relay 2 is ON");
                  if (Delay2.toInt() == 0)
                  {
                    needManualCloseRelayTwo = true;
                    logOutput("Relay 2 will remain open until it is manually closed !");
                  }
                  else
                  {
                    needManualCloseRelayTwo = false;
                    logOutput("Relay 2 will automatically close in " + Delay2 + " seconds !");
                  }
                  startTimeRelayTwo = millis();
                  request->redirect("/inputs-outputs");
                }
                else if (request->hasArg("relay2_off"))
                {
                  Serial.println("relay2 /off button was PRESSED");
                  logOutput("You have manually closed Relay 2");
                  digitalWrite(RELAY2, LOW);
                  status2 = "OFF";
                  needManualCloseRelayTwo = true; // I pressed the button and the timer won't start
                  logOutput("Relay 2 is OFF");
                  request->redirect("/inputs-outputs");
                }
                else
                {
                  request->redirect("/inputs-outputs");
                }
              });

    server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request)
              {
                // Serial.print("/files, request: ");stop
                // Serial.println(request->methodToString());
                if (userFlag)
                {
                  if (!request->authenticate(value_login[0].c_str(), value_login[1].c_str()))
                    return request->requestAuthentication(NULL, false);
                  if (request->hasParam("filename", true))
                  { // Download file
                    if (request->hasArg("download"))
                    { // File download
                      Serial.println("Download Filename: " + request->arg("filename"));
                      AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
                      response->addHeader("Server", "ESP Async Web Server");
                      request->send(response);
                      return;
                    }
                    else if (request->hasArg("delete"))
                    { // Delete file
                      if (SPIFFS.remove(request->getParam("filename", true)->value()))
                      {
                        logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
                      }
                      else
                      {
                        logOutput("Could not delete file. Try again !");
                      }
                      request->redirect("/files");
                    }
                  }
                  else if (request->hasArg("restart_device"))
                  {
                    request->send(200, "text/plain", "The device will reboot shortly !");
                    ESP.restart();
                  }
                  String HTML PROGMEM; // HTML code
                  File pageFile = SPIFFS.open("/files_STA.html", "r");
                  if (pageFile)
                  {
                    HTML = readString(pageFile);
                    pageFile.close();
                    HTML = addDirList(HTML);
                    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
                    response->addHeader("Server", "ESP Async Web Server");
                    request->send(response);
                  }
                }
                else
                {
                  if (request->hasParam("filename", true))
                  { // Download file
                    if (request->hasArg("download"))
                    { // File download
                      Serial.println("Download Filename: " + request->arg("filename"));
                      AsyncWebServerResponse *response = request->beginResponse(SPIFFS, request->arg("filename"), String(), true);
                      response->addHeader("Server", "ESP Async Web Server");
                      request->send(response);
                      return;
                    }
                    else if (request->hasArg("delete"))
                    { // Delete file
                      if (SPIFFS.remove(request->getParam("filename", true)->value()))
                      {
                        logOutput((String)request->getParam("filename", true)->value().c_str() + " was deleted !");
                      }
                      else
                      {
                        logOutput("Could not delete file. Try again !");
                      }
                      request->redirect("/files");
                    }
                  }
                  else if (request->hasArg("restart_device"))
                  {
                    request->send(200, "text/plain", "The device will reboot shortly !");
                    ESP.restart();
                  }
                  String HTML PROGMEM; // HTML code
                  File pageFile = SPIFFS.open("/files_STA.html", "r");
                  if (pageFile)
                  {
                    HTML = readString(pageFile);
                    pageFile.close();
                    HTML = addDirList(HTML);
                    AsyncWebServerResponse *response = request->beginResponse_P(200, "text/html", HTML.c_str(), processor);
                    response->addHeader("Server", "ESP Async Web Server");
                    request->send(response);
                  }
                }
              }); // server.on("/files", HTTP_ANY, [](AsyncWebServerRequest *request)

    server.on("/staticIP", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if (request->hasArg("saveStatic"))
                {
                  int params = request->params();
                  String values_static_post[params];
                  for (int i = 0; i < params; i++)
                  {
                    AsyncWebParameter *p = request->getParam(i);
                    if (p->isPost())
                    {
                      logOutput((String) "POST[" + p->name() + "]: " + p->value());
                      values_static_post[i] = p->value();
                    }
                    else
                    {
                      logOutput((String) "GET[" + p->name() + "]: " + p->value());
                    }
                  } // for(int i=0;i<params;i++)

                  if (values_static_post[0] == "WiFi")
                  {
                    if (values_static_post[1] != NULL &&
                        values_static_post[1].length() != 0 &&
                        values_static_post[2] != NULL &&
                        values_static_post[2].length() != 0 &&
                        values_static_post[3] != NULL &&
                        values_static_post[3].length() != 0 &&
                        values_static_post[4] != NULL &&
                        values_static_post[4].length() != 0 &&
                        values_static_post[5] != NULL &&
                        values_static_post[5].length() != 0 &&
                        values_static_post[6] != NULL &&
                        values_static_post[6].length() != 0)
                    {
                      File inputsWrite = SPIFFS.open("/network.txt", "w");
                      if (!inputsWrite)
                        logOutput("ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !");
                      inputsWrite.println(values_static_post[0]); // Connection Type ? WiFi : Ethernet
                      inputsWrite.println(values_static_post[1]); // SSID
                      inputsWrite.println(values_static_post[2]); // Password
                      inputsWrite.println(values_static_post[3]); // Local IP
                      inputsWrite.println(values_static_post[4]); // Gateway
                      inputsWrite.println(values_static_post[5]); // Subnet
                      inputsWrite.println(values_static_post[6]); // DNS
                      inputsWrite.close();
                      logOutput("Configuration saved !");
                      request->send(200, "text/html", (String) "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>Please wait 10 seconds and then press on the \"Go Home\" button to return to the main page.</br></br>If you can't return to the main page please check the entered values.</br></br><form method=\"post\" action=\"http://" + values_static_post[3] + "\"><input type=\"submit\" value=\"Go Home\"/></form></div>");
                      shouldReboot = true;
                    }
                    else
                      request->redirect("/staticIP");
                  }
                  else if (values_static_post[0] == "Ethernet")
                  {
                    if (values_static_post[3] != NULL &&
                        values_static_post[3].length() != 0 &&
                        values_static_post[4] != NULL &&
                        values_static_post[4].length() != 0 &&
                        values_static_post[5] != NULL &&
                        values_static_post[5].length() != 0 &&
                        values_static_post[6] != NULL &&
                        values_static_post[6].length() != 0)
                    {
                      File inputsWrite = SPIFFS.open("/network.txt", "w");
                      if (!inputsWrite)
                        logOutput("ERROR_INSIDE_POST ! Couldn't open file to write Static IP credentials !");
                      inputsWrite.println(values_static_post[0]); // Connection Type ? WiFi : Ethernet
                      inputsWrite.println(values_static_post[3]); // Local IP
                      inputsWrite.println(values_static_post[4]); // Gateway
                      inputsWrite.println(values_static_post[5]); // Subnet
                      inputsWrite.println(values_static_post[6]); // DNS
                      inputsWrite.close();
                      logOutput("Configuration saved !");
                      Serial.println("New Static IP Address: " + values_static_post[3]);
                      request->send(200, "text/html", (String) "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>Please wait 10 seconds and then press on the \"Go Home\" button to return to the main page.</br></br>If you can't return to the main page please check the entered values.</br></br><form method=\"post\" action=\"http://" + values_static_post[3] + "\"><input type=\"submit\" value=\"Go Home\"/></form></div>");
                      shouldReboot = true;
                    }
                    else
                      request->redirect("/staticIP");
                  } // if(values_static_post[0] == "WiFi")
                }
                else
                {
                  request->redirect("/staticIP");
                } // if(request->hasArg("saveStatic"))
              }); // server.on("/staticLogin", HTTP_POST, [](AsyncWebServerRequest * request)

    server.on("/dhcpIP", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if (request->hasArg("saveDHCP"))
                {
                  int params = request->params();
                  String values_dhcp_post[params];
                  for (int i = 0; i < params; i++)
                  {
                    AsyncWebParameter *p = request->getParam(i);
                    if (p->isPost())
                    {
                      logOutput((String) "POST[" + p->name() + "]: " + p->value());
                      values_dhcp_post[i] = p->value();
                    }
                    else
                    {
                      logOutput((String) "GET[" + p->name() + "]: " + p->value());
                    }
                  }

                  if (values_dhcp_post[0] == "WiFi")
                  {
                    if (values_dhcp_post[1] != NULL && values_dhcp_post[1].length() != 0 &&
                        values_dhcp_post[2] != NULL && values_dhcp_post[2].length() != 0)
                    {
                      File inputsWrite = SPIFFS.open("/network.txt", "w");
                      if (!inputsWrite)
                        logOutput("ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
                      inputsWrite.println(values_dhcp_post[0]); // Connection Type ? WiFi : Ethernet
                      inputsWrite.println(values_dhcp_post[1]); // SSID
                      inputsWrite.println(values_dhcp_post[2]); // Password
                      inputsWrite.close();
                      logOutput("Configuration saved !");
                      request->send(200, "text/html", "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>You can get this device's IP by looking through your AP's DHCP List.");
                      shouldReboot = true;
                    }
                    else
                      request->redirect("/dhcpIP");
                  }
                  else if (values_dhcp_post[0] == "Ethernet")
                  {
                    File inputsWrite = SPIFFS.open("/network.txt", "w");
                    if (!inputsWrite)
                      logOutput("ERROR_INSIDE_POST ! Couldn't open file to write DHCP IP credentials !");
                    inputsWrite.println(values_dhcp_post[0]); // Connection Type ? WiFi : Ethernet
                    inputsWrite.close();
                    logOutput("Configuration saved !");
                    request->send(200, "text/html", "<div style=\"text-align:center; font-family:arial;\">Congratulation !</br></br>You have successfully changed the networks settings.</br></br>The device will now restart and try to apply the new settings.</br></br>You can get this device's IP by looking through your AP's DHCP List.");
                    shouldReboot = true;
                  }
                }
                else
                {
                  request->redirect("/dhcpIP");
                } // if(request->hasArg("saveStatic"))
              }); // server.on("/dhcpLogin", HTTP_POST, [](AsyncWebServerRequest * request)

    server.on("/wiegand-settings", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if (request->hasArg("send_url"))
                {
                  String url = request->arg("getDatabase");
                  String pulse = request->arg("getPulse");
                  String gap = request->arg("getGap");

                  if (url.length() != 0)
                  {
                    database_url = url;
                    logOutput((String) "POST[getDatabase]: " + database_url);
                  }
                  if (pulse.length() != 0)
                  {
                    pulseWidth = pulse;
                    logOutput((String) "POST[getPulse]: " + pulseWidth);
                  }
                  if (gap.length() != 0)
                  {
                    interPulseGap = gap;
                    logOutput((String) "POST[getGap]: " + interPulseGap);
                  }
                  File configWrite = SPIFFS.open("/wiegand.txt", "w");
                  if (!configWrite)
                    logOutput("ERROR_INSIDE_DELAY_POST ! Couldn't open file to save Wiegand configuration !");
                  configWrite.println(database_url);
                  configWrite.println(pulseWidth);
                  configWrite.println(interPulseGap);
                  configWrite.close();
                  request->redirect("/wiegand-settings");
                }
                else
                {
                  request->redirect("/wiegand-settings");
                }
              });

    server.on("/wiegand", HTTP_POST, [](AsyncWebServerRequest *request)
              {
                if (already_working)
                {
                  request->send(200, "text/plain", "Warning: Another Wiegand is being sent.");
                }
                else
                {
                  if (request->hasArg("number"))
                  {
                    plate_number = request->arg("number");
                    wiegand_flag = true;
                    Serial.println("(Inside POST /wiegand) - Plate Number: " + plate_number);
                    // size_t size = request->contentLength();
                    // Serial.print("Post Size: ");
                    // Serial.println(size);
                    // logOutput((String)"The Plate Number is: " + plate_number);
                    request->send(200, "text/plain", (String) "Plate Number: " + plate_number + " received. Sending Wiegand ID.");
                  }
                  else
                  {
                    Serial.println("Post did not have a 'number' field.");
                    wiegand_flag = false;
                    request->send(200, "text/plain", "Post did not have a 'number' field.");
                  }
                }
              });

    server.onFileUpload(handleUpload);
    server.onNotFound([](AsyncWebServerRequest *request)
                      { request->redirect("/home"); });
    server.begin();
  }
  else
  {
    networkRead.close();
    // isAPmodeOn = true;
    logOutput(WiFi.mode(WIFI_AP) ? "Controller went in AP Mode !" : "Controller couldn't go in AP_MODE. AP_STA_MODE will start.");
    startWiFiAP();
  }
  // for debugging Wiegand
  // wiegand_flag = true;
  // plate_number = "B059811";
  // database_url = "https://dev2.metrici.ro/io/lpr/get_wiegand_id.php";
}

String Port1_Old = "";
String Port2_Old = "";
bool wasPressed1 = false;
bool wasPressed2 = false;
unsigned int timer1 = 0;
unsigned int timer2 = 0;
unsigned int timerDelta = 0;
bool counting1 = false;
bool counting2 = false;
bool udpStarted1 = false;
bool udpStarted2 = false;

void loop()
{
  delay(2);

  // Reboot Check
  if (shouldReboot)
  {
    logOutput("Restarting in 5 seconds...");
    delay(5000);
    server.reset();
    ESP.restart();
  }

  // Reconnect Routine;
  if (v[0] == "WiFi")
  {
    if (!WiFi.isConnected())
    {
      wifiConfig(v);
    }
  }
  else if (v[0] == "Ethernet")
  {
    if (!eth_connected)
    {
      ethernetConfig(v);
    }
  }

  // Outputs Routine
  currentTimeRelayOne = millis();
  currentTimeRelayTwo = millis();

  if (startTimeRelayOne != 0 && !needManualCloseRelayOne)
  {
    if ((currentTimeRelayOne - startTimeRelayOne) > (Delay1.toInt() * 1000))
    {
      digitalWrite(RELAY1, LOW);
      status1 = "OFF";
      logOutput(" Relay 1 closed");
      startTimeRelayOne = 0;
    }
  }

  if (startTimeRelayTwo != 0 && !needManualCloseRelayTwo)
  {
    if ((currentTimeRelayTwo - startTimeRelayTwo) > (Delay2.toInt() * 1000))
    {
      digitalWrite(RELAY2, LOW);
      status2 = "OFF";
      logOutput("Relay 2 closed");
      startTimeRelayTwo = 0;
    }
  }

  // Wiegand Routine
  
  if (wiegand_flag)
  {
    already_working = true;
    wiegand_flag = false;
    String copyNumber = plate_number;
    String wiegandID = "";
    int pulse = pulseWidth.toInt();
    int gap = interPulseGap.toInt();
    HTTPClient wieg;
    wieg.begin((String)database_url + "?plate_number=" + copyNumber);
    int httpCode = wieg.GET();
    if (httpCode > 0)
    {
      wiegandID = wieg.getString();
      Serial.println((String) "HTTP Code: " + httpCode);
      Serial.println((String) "Wiegand ID Payload: " + wiegandID);
    }
    else
    {
      logOutput("Error on HTTP request ! Please check if Database URL is correct.");
    }
    wieg.end();

    if (wiegandID.substring(0, wiegandID.indexOf(',')) != "-1" && wiegandID.substring((wiegandID.indexOf(',') + 1)) != "-1")
    {
      std::vector<bool> _array;
      card.update(wiegandID.substring(0, wiegandID.indexOf(',')), wiegandID.substring((wiegandID.indexOf(',') + 1)));
      _array = card.getWiegandBinary();
      Serial.print("Sending Wiegand: ");
      for (bool i : _array)
      {
        Serial.print(i);
      }
      Serial.print('\n');
      String binary_wiegand_string = card.getWiegandBinaryInString();
      logOutput((String)"Binary Wiegand: " + binary_wiegand_string);
      Serial.print("binary_wiegand_string length: ");
      Serial.println(binary_wiegand_string.length());
      portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
      portENTER_CRITICAL(&mux);
      for (int i = 0; i < 26; i++)
      {
        if (_array[i] == 0)
        {
          digitalWrite(W0, LOW);
          delayMicroseconds(pulse);
          digitalWrite(W0, HIGH);
        }
        else
        {
          digitalWrite(W1, LOW);
          delayMicroseconds(pulse);
          digitalWrite(W1, HIGH);
        }
        delayMicroseconds(gap);
      }
      portEXIT_CRITICAL(&mux);

      Serial.println();
      logOutput((String) "Plate Number: " + copyNumber + " - Wiegand ID: " + card.getCardID());

      Serial.println('\n');
      // DEBUG
      // Serial.println((String)"Facility Code: " + card.getFacilityCode_int());
      // std::vector<bool> fCode = card.getFacilityCode_vector();
      // Serial.print("Facility Code: ");
      // for(bool i : fCode) {
      //   Serial.print(i);
      // }
      // Serial.println('\n');

      // Serial.println((String)"Card Number: " + card.getCardNumber_int());
      // std::vector<bool> cNumber = card.getCardNumber_vector();
      // Serial.print("Card Number: ");
      // for(bool i : cNumber) {
      //   Serial.print(i);
      // }
      // Serial.println('\n');
      already_working = false;
    }
    else
    {
      logOutput((String) "Plate Number: " + copyNumber + " does not have a Wiegand ID.");
      already_working = false;
    }
  }

  // Inductive Loop Routine
  timerDelta = millis();
  // Initialise UDP 1
  if (Port1_Old != Port1 && Port1 != "Not Set")
  {
    Port1_Old = Port1;
    Serial.println("New INPUT1 Port: " + Port1_Old);
    udp1.stop();
    delay(100);
    udp1.begin(Port1.toInt());
    // only for first initialization
    udpStarted1 = true;
  }
  // check for bounce-back
  if (digitalRead(INPUT_1) == LOW && counting1 == false)
  {
    counting1 = true;
    timer1 = millis();
  }
  // check if INPUT_1 is still LOW after 100 ms
  if ((timerDelta - timer1) > 500)
  {
    counting1 = false;
    //Send UDP packet if trigger was sent from Input1 only once
    if (digitalRead(INPUT_1) == LOW && wasPressed1 == false)
    {
      wasPressed1 = true;
      logOutput("Trigger1 received.");
      uint8_t buffer[19] = "statechange,201,1\r";
      // send packet to server
      if (Input1_IP.length() != 0 && Input1_IP != "Not Set" && udpStarted1)
      {
        udp1.beginPacket(Input1_IP.c_str(), Port1.toInt());
        udp1.write(buffer, sizeof(buffer));
        delay(30);
        logOutput(udp1.endPacket() ? "UDP Packet 1 sent" : "WARNING: UDP Packet 1 not sent.");
        memset(buffer, 0, 19);
      }
      else
      {
        logOutput("ERROR ! Invalid IP Address for INPUT 1. Please enter Metrici Server's IP !");
      }
    }
  }

  if (digitalRead(INPUT_1) == HIGH && wasPressed1 == true)
  {
    wasPressed1 = false;
  }

  // Initialise UDP 2
  if (Port2_Old != Port2 && Port2 != "Not Set")
  {
    Port2_Old = Port2;
    Serial.println("New INPUT2 Port: " + Port2_Old);
    udp2.stop();
    delay(100);
    udp2.begin(Port2.toInt());
    // only for first initialization
    udpStarted2 = true;
  }

  // check for bounce-back
  if (digitalRead(INPUT_2) == LOW && counting2 == false)
  {
    counting2 = true;
    timer2 = millis();
  }
  // check if INPUT_2 is still LOW after 100 ms
  if ((timerDelta - timer2) > 500)
  {
    counting2 = false;
    //Send UDP packet if trigger was sent from Input2 only once
    if (digitalRead(INPUT_2) == LOW && wasPressed2 == false)
    {
      wasPressed2 = true;
      logOutput("Trigger2 received.");
      uint8_t buffer[19] = "statechange,201,1\r";
      // send packet to server
      if (Input2_IP.length() != 0 && Input2_IP != "Not Set" && udpStarted2)
      {
        udp2.beginPacket(Input2_IP.c_str(), Port2.toInt());
        udp2.write(buffer, sizeof(buffer));
        delay(30);
        logOutput(udp2.endPacket() ? "UDP Packet 2 sent" : "WARNING: UDP Packet 2 not sent.");
        memset(buffer, 0, 19);
      }
      else
      {
        logOutput("ERROR ! Invalid IP Address for INPUT 2. Please enter Metrici Server's IP !");
      }
    }
  }
  if (digitalRead(INPUT_2) == HIGH && wasPressed2 == true)
  {
    wasPressed2 = false;
  }

  // Serial.print("Heap Size: ");
  // Serial.println(ESP.getHeapSize());
  // Serial.print("Free Heap: ");
  // Serial.println(ESP.getFreeHeap());
  // Serial.print("Max Alloc Heap: ");
  // Serial.println(ESP.getMaxAllocHeap());
  // delay(2000);
}
