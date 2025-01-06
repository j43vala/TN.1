#include <WiFi.h>
#include <SPI.h>
#include "RF24.h"
#include <Max31855.h>
#include <WebServer.h>
#include <Preferences.h>
#include <DNSServer.h>
#include "printf.h"
#include <ModbusIP_ESP8266.h>  // Modbus TCP server library for ESP8266/ESP32
#include <typeinfo>

// nRF24 pinout
#define CE_PIN 18
#define CSN_PIN_RF 20
// MAX31855 pinout
#define MAXCS 5
// Configuration pinout
#define CONFIG_PIN 19
// SPI pinout
#define CSK_PIN 6
#define MISO_PIN 2
#define MOSI_PIN 7
//battery voltage check
#define BATT_PIN 3
//led pins
#define ORANGE_LED 15
#define BLUE_LED 21

const int baseRegs[] = {0, 50, 100, 150, 200, 250};

// WiFi and Web Server Setup
DNSServer dnsServer;
const byte DNS_PORT = 53;
WebServer server(80);

/// Modbus TCP Server
ModbusIP mb;

String ssid; // Global variable for SSID
String password; // Global variable for Password

String hostname = "wzero_receiver";
const char* AP_SSID = "wzero_receiver";
const char* AP_PASS = "password";

IPAddress apIP(192, 168, 4, 1);
IPAddress subnet(255, 255, 255, 0);

String ipaddress = "0.0.0.0";
Preferences preferences;

RTC_DATA_ATTR int deviceID = 1;
RTC_DATA_ATTR float sleepTime = 5;
RTC_DATA_ATTR String mode = "R";
RTC_DATA_ATTR String setNumber = "1";
RTC_DATA_ATTR String writeAddress = "0Node"; // Default write address
RTC_DATA_ATTR String readAddress = "1Node";  // Default read address

// Instantiate an object for the nRF24L01 transceiver
RF24 radio(CE_PIN, CSN_PIN_RF, 2000000);
MAX31855 thermocouple(MAXCS, 2000000);

// Deep sleep
#define uS_TO_S_FACTOR 1000000ULL
RTC_DATA_ATTR unsigned long bootCount = 0;
RTC_DATA_ATTR unsigned long lastBootTime = 0;
unsigned long setupTime;

unsigned long lastRecieveMillis;

// AP mode
void setupAP();
void handleRoot();
void handleSave();
void readPreferences();
void savePreferences(String mode, String setNumber, int deviceID, float sleepTime, String readAddress, String writeAddress);
void savePreferencesIp(String ipaddress);
void runNormalMode();
void setupRF();
void loopTransmit();
void loopReceive();
void setupModbusTCP();
float readBatteryVoltage();
int getBatteryPercentage(float voltage);

// Forward declaration of update_registers
bool settingMode = false;

struct PayloadStruct {
  int deviceID;
  uint8_t payloadID;
  float batteryPercentage;
  float batteryVoltage;
  float temperature;
};

void update_registers(PayloadStruct payload);

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  SPI.begin(CSK_PIN, MISO_PIN, MOSI_PIN);
  pinMode(CONFIG_PIN, INPUT_PULLUP);
  pinMode(ORANGE_LED, OUTPUT);
  pinMode(BLUE_LED, OUTPUT);

  int configPinState = digitalRead(CONFIG_PIN);
  preferences.begin("config", false);

  // Check if the configuration button is pressed
  if (configPinState == LOW) {
    Serial.println("Entering Settings Mode");
    setupAP();
    settingMode = true;
  } else {
    Serial.println("Entering Normal Mode");
    runNormalMode();
  }
  printf_begin();
  setupTime = millis();
}
bool radioConfigured = 0;
void loop() {
  if (settingMode) {
    dnsServer.processNextRequest();
    server.handleClient();
  } else {

    if (mode == "T") {
      loopTransmit();  // Transmitter mode loop
    } else if (mode == "R") {
      loopReceive();  // Receiver mode loop
      mb.task();
      delay(10);
    }
  }
}

float readBatteryVoltage() {
    int raw = analogRead(BATT_PIN);
    return raw * (3.3 / 4095.0) * 2; // Adjust with voltage divider factor
}

int getBatteryPercentage(float voltage) {
  if (voltage >= 4.2) return 100;
  if (voltage <= 3.0) return 0;
  return (int)((voltage - 3.0) * 100 / (4.2 - 3.0));
}

void setupAP() {
   // Set the hostname
  WiFi.setHostname(hostname.c_str()); // Set the hostname to the desired value

  // turn on leds before station mode
  blueLedController(HIGH);
  orangeLedController(HIGH);

  Serial.println("Starting Access Point...");
  WiFi.softAP(AP_SSID, "");
  WiFi.softAPConfig(apIP, apIP, subnet);
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/", handleRoot);
  // server.on("/wifi-setup", handleWifiScan);
  server.on("/save", HTTP_POST, handleSave);
  server.begin();

  Serial.println("AP Mode Started. Connect to: " + String(AP_SSID));
  Serial.println("ESP32 AP IP Address: " + apIP.toString());
}

void handleRoot() {
  readPreferences(); // Load saved preferences
  String html = "<html>"
                "<head>"
                "<meta name='viewport' content='width=device-width, initial-scale=1.0'>"
                "<style>"
                "body { background-color: whitesmoke; color: brown; font-family: Arial, sans-serif; margin: 0; padding: 0; }"
                "h1 { text-align: center; margin: 20px 0; font-size: 24px; }"
                ".card { max-width: 500px; margin: 20px auto; padding: 20px; border: 1px solid #ccc; border-radius: 10px; background: white; box-shadow: 0 4px 8px rgba(0, 0, 0, 0.2); }"
                ".info { display: flex; align-items: center; justify-content: space-between; padding: 10px; margin-bottom: 15px; border: 1px solid #ccc; border-radius: 5px; background-color: #f9f9f9; }"
                "form { margin-top: 20px; }"
                "form div { display: flex; align-items: center; margin-bottom: 15px; }"
                "form label { width: 40%; color: darkblue; font-weight: bold; }"
                "form input, form select { flex: 1; padding: 8px; border: 1px solid #ccc; border-radius: 5px; box-sizing: border-box; }"
                ".submit-container { margin-top: 20px; }"
                ".submit-container input[type='submit'] { width: 100%; background-color: brown; color: white; border: none; cursor: pointer; font-size: 16px; padding: 10px; border-radius: 5px; }"
                ".submit-container input[type='submit']:hover { background-color: darkred; }"
                ".refresh-btn { width: auto; background-color: brown; color: white; border: none; cursor: pointer; font-size: 12px; padding: 6px 12px; border-radius: 5px; margin-left: 10px; }"
                ".refresh-btn:hover { background-color: darkred; }"
                "@media (max-width: 600px) {"
                "  body { font-size: 14px; }"
                "  h1 { font-size: 20px; }"
                "  .card { width: 90%; padding: 15px; }"
                "  .info label, form label { width: 100%; margin-bottom: 5px; }"
                "  .info { flex-direction: column; align-items: flex-start; }"
                "  .info span { width: 100%; }"
                "  form div { flex-direction: column; align-items: flex-start; }"
                "} "
                "</style>"
                "<script>"
                "function toggleFields() {"
                "  var mode = document.getElementById('mode').value;"
                "  if (mode === 'R') {"
                "    document.getElementById('readAddressField').style.display = 'block';"
                "    document.getElementById('sendNodeField').style.display = 'none';"
                "  } else {"
                "    document.getElementById('readAddressField').style.display = 'none';"
                "    document.getElementById('sendNodeField').style.display = 'block';"
                "  }"
                "} "
                "window.onload = function() { toggleFields(); };"
                "function togglePassword() {"
                "  const passwordField = document.getElementById('password');"
                "  const button = event.target;"
                "  if (passwordField.type === 'password') {"
                "    passwordField.type = 'text';"
                "    button.textContent = 'Hide';"
                "  } else {"
                "    passwordField.type = 'password';"
                "    button.textContent = 'Show';"
                "  }"
                "} "
                "</script>"
                "</head>"
                "<body>"
                "<h1>WZERO CIRCUIT CONF</h1>";

  // Show the IP address above the form if mode is "Receiver"
  if (mode == "R") {
    html += "<h2>Device Info</h2>"
       "<div class='info'>"
       "<span>IP Address: " + ipaddress + "</span>"
       "</div>";
  }

  html += "<h2>Configuration</h2>"
          "<div class='card'>"
          "<form action='/save' method='POST'>"
          "<div>"
          "<span for='mode'>Select Mode:  "
          "<select id='mode' name='mode'>"
          "<option value='R'" + String(mode == "R" ? " selected" : "") + ">Receive</option>"
          "<option value='T'" + String(mode == "T" ? " selected" : "") + ">Transmit</option>"
          "</select></span></div>"

          "<div class='info'>"
          "<div><label>Set Number:</label>"
          "<select name='set_number'>"
          "<option value='1'" + String(setNumber == "1" ? " selected" : "") + ">1</option>"
          "<option value='2'" + String(setNumber == "2" ? " selected" : "") + ">2</option>"
          "<option value='3'" + String(setNumber == "3" ? " selected" : "") + ">3</option>"
          "<option value='4'" + String(setNumber == "4" ? " selected" : "") + ">4</option>"
          "<option value='5'" + String(setNumber == "5" ? " selected" : "") + ">5</option>"
          "</select></div>"

          "<div><label>Device ID:</label>"
          "<select name='device_id'>"
          "<option value='1'" + String(deviceID == 1 ? " selected" : "") + ">1</option>"
          "<option value='2'" + String(deviceID == 2 ? " selected" : "") + ">2</option>"
          "<option value='3'" + String(deviceID == 3 ? " selected" : "") + ">3</option>"
          "<option value='4'" + String(deviceID == 4 ? " selected" : "") + ">4</option>"
          "<option value='5'" + String(deviceID == 5 ? " selected" : "") + ">5</option>"
          "<option value='6'" + String(deviceID == 6 ? " selected" : "") + ">6</option>"
          "</select></div>"

          "<div><label>Sleep Time (seconds):</label>"
          "<input type='number' step='0.01' name='sleep_time' value='" + String(sleepTime, 2) + "'></div>"
          "<div><label>Select Wi-Fi:</label>"
          "<div style='display: flex; align-items: center;'>"
          "<select name='ssid'>";

  // Scan for Wi-Fi networks
  int n = WiFi.scanNetworks();
  for (int i = 0; i < n; ++i) {
    html += "<option value='" + String(WiFi.SSID(i)) + "'>" + String(WiFi.SSID(i)) + "</option>";
  }
  html += "</select>"
          "<button type='button' class='refresh-btn' onclick='location.reload();'>Refresh</button>"
          "</div></div>"

          "<div><label>Password:</label>"
          "<div style='display: flex; align-items: center;'>"
          "<input type='password' id='password' name='password' required style='flex: 1;'>"
          "<button type='button' class='refresh-btn' onclick='togglePassword()'>Show</button>"
          "</div></div>"
          "</div>"
          "<div class='submit-container'><input type='submit' value='Save'></div>"
          "</form>"
          "</div>"
          "</body>"
          "</html>";

  server.send(200, "text/html", html); // Send the HTML response
}

// Logic to map readAddress to writeAddress based on set_number
void handleSave() {
  Serial.println("Handle Save called");
  
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  Serial.println("Received SSID: " + ssid);
  Serial.println("Received Password: " + password);
  String mode = server.arg("mode");
  setNumber = server.arg("set_number");
  deviceID = server.arg("device_id").toInt();
  String readAddress = server.arg("readAddress");
  String writeAddress = server.arg("writeAddress");
  sleepTime = server.arg("sleep_time").toFloat();

 

  // Convert setNumber to an integer
  int setNumberInt = setNumber.toInt();

    switch (setNumberInt) {
      case 1:
        readAddress = "0Node";
        writeAddress = "1Node";
        break;
      case 2:
        readAddress = "2Node";
        writeAddress = "3Node";
        break;
      case 3:
        readAddress = "4Node";
        writeAddress = "5Node";
        break;
      case 4:
        readAddress = "6Node";
        writeAddress = "7Node";
        break;
      case 5:
        readAddress = "8Node";
        writeAddress = "9Node";
        break;
      default:
        readAddress = "0";
        writeAddress = "1";
    }

  // Save preferences (assuming savePreferences function is implemented)
  savePreferences(mode, setNumber, deviceID, sleepTime, readAddress, writeAddress, ssid, password);

  // Send response back to the user
  server.send(200, "text/html", "Settings saved! <a href='/'>Go back</a>");
  delay(1000);
  ESP.restart();
}

void savePreferences(String mode, String setNumber, int deviceID, float sleepTime, String readAddress, String writeAddress,String ssid,String password) {
  preferences.putInt("deviceID", deviceID);
  preferences.putFloat("sleepTime", sleepTime);
  preferences.putString("mode", mode);
  preferences.putString("setNumber",  String(setNumber));
  preferences.putString("readAddress", readAddress);
  preferences.putString("writeAddress", writeAddress);
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  
}

void savePreferencesIp( String ipaddress){
  preferences.putString("ipaddress", ipaddress);
}

void readPreferences() {
  if (mode == "R") {
    ssid = preferences.getString("ssid", ""); // Default to empty string if not found
    password = preferences.getString("password", ""); // Default to empty string if not found
    // Print the SSID and password
    Serial.print("SSID: ");
    Serial.println(ssid); // Print the SSID
    Serial.print("Password: ");
    Serial.println(password);

    setNumber = preferences.getString("setNumber", setNumber);
    deviceID = preferences.getInt("deviceID",deviceID);
    sleepTime = preferences.getFloat("sleepTime", sleepTime);
    mode = preferences.getString("mode",mode);
    readAddress = preferences.getString("readAddress", "0Node");
    writeAddress = preferences.getString("writeAddress", "1Node");
    ipaddress = preferences.getString("ipaddress",ipaddress);
  }
}

void runNormalMode() {
  readPreferences();
  // setup transmitter or reciever
  setupRF();
  
}

// Function to convert a string address to uint64_t
uint64_t stringToAddress(const String& address) {
    return *(uint64_t*)address.c_str();
}

void setupRF() {
  if (!radio.begin()) {
    Serial.println("Radio initialization failed!");
  orangeLedController(HIGH);
    blueLedController(LOW);

    while (1) {
      delay(300);
      orangeLedToggle();
      blueLedToggle();
      };
  }

  radio.setPALevel(RF24_PA_LOW);

  if (mode == "T") {  // Transmitter mode
    radio.openWritingPipe(stringToAddress(writeAddress));
    radio.openReadingPipe(1, stringToAddress(readAddress));
    
    // orange led on at transmission mode
    orangeLedController(HIGH);
  } else if (mode == "R") {  // Receiver mode
    radio.openWritingPipe(stringToAddress(readAddress));
    radio.openReadingPipe(1, stringToAddress(writeAddress));
    radio.startListening();

    // connect to wifi
    connectWifi();

    // leds after station mode
    blueLedController(HIGH);
    orangeLedController(LOW);
  }

}

void connectWifi(){

  Serial.print("Connecting to WiFi SSID: ");
  Serial.println(ssid); // Add this line to print the SSID
  WiFi.begin(ssid.c_str(), password.c_str()); // Use the ssid and password from the UI

  // Print the Wi-Fi connection status
  Serial.print("WiFi connection status: ");
  Serial.println(WiFi.status()); // This prints the current WiFi status

  // WiFi.begin(DEFAULT_SSID, DEFAULT_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected. Retrying...");
    delay(500);
  }
  // Print the IP address assigned by the router
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Convert IPAddress to String and save it
  ipaddress = WiFi.localIP().toString();
  savePreferencesIp(ipaddress);
  setupModbusTCP();

}

void loopTransmit() {

  // unsigned long currentTime = millis();

  // if (currentTime - lastTransmissionTime >= 100) {
    PayloadStruct payload;

    payload.deviceID = deviceID;
    payload.payloadID = bootCount;
    payload.batteryVoltage = readBatteryVoltage();
    payload.batteryPercentage = getBatteryPercentage(payload.batteryVoltage);
    payload.temperature = thermocouple.readCelsius();

    Serial.print("Battery Voltage: ");
    Serial.println(payload.batteryVoltage);
    Serial.print("Battery Percentage: ");
    Serial.println(payload.batteryPercentage);

    if (!isnan(payload.temperature)) {
      Serial.print("Temperature: ");
      Serial.println(payload.temperature);
    } else {
      Serial.println("Error reading temperature.");
    }

    bool report = radio.write(&payload, sizeof(payload));
    if (report) {
      Serial.println("Transmission successful!");

      // Reset bootCount if it reaches 255
      if (bootCount >= 255) {
        bootCount = 0; // Reset bootCount
      } else {
        bootCount++; // Increment bootCount
      }

      // TODO add in payload for debug
      lastBootTime = millis();
      enterSleep();
      
    } else {
      Serial.println("Transmission failed or timed out");
    }
  // }
}

void orangeLedController(uint8_t state){
  digitalWrite(ORANGE_LED, state);
}

void blueLedController(uint8_t state){
  digitalWrite(BLUE_LED, state);
}

void orangeLedToggle(){
  uint8_t currState = digitalRead(ORANGE_LED);
  orangeLedController(!currState);
}

void blueLedToggle(){
  uint8_t currState = digitalRead(BLUE_LED);
  blueLedController(!currState);
}


void enterSleep(){
  // Serial.println("Entering deep sleep for " + String(sleepTime) + " seconds...");
  esp_sleep_enable_timer_wakeup(sleepTime * uS_TO_S_FACTOR);
  orangeLedController(LOW);
  blueLedController(LOW);
  esp_deep_sleep_start();
}

int recieveOffDelay = 500;
bool recievedOneMessage = 0;

void loopReceive() {
  PayloadStruct payload;
  
  int timeDelta = millis() - lastRecieveMillis;
  if ( timeDelta> recieveOffDelay & recievedOneMessage){
    // Serial.println("blue led low condition ");
    blueLedController(LOW);
  }

  if (radio.available()) {
    radio.read(&payload, sizeof(payload));
    Serial.println("Received Payload:");
    Serial.print("Device ID: ");
    Serial.println(payload.deviceID);
    Serial.print("Payload ID: ");
    Serial.println(payload.payloadID);
    Serial.print("BatteryVoltage: ");
    Serial.println(payload.batteryVoltage);
    Serial.print("batteryPercentage: ");
    Serial.println(payload.batteryPercentage);
    Serial.print("Temperature: ");
    Serial.println(payload.temperature);
    update_registers(payload);

    recievedOneMessage = 1;
    lastRecieveMillis = millis();
    blueLedController(HIGH);
  }
}

void update_registers(PayloadStruct payload);

float readFloatFromRegisters(uint16_t reg1, uint16_t reg2) {
    uint32_t combined = ((uint32_t)reg2 << 16) | reg1; // Combine the two registers
    float* floatPtr = (float*)&combined; // Cast to float pointer
    return *floatPtr; // Dereference to get the float value
}

// Define the union for float conversion
union FloatInt {
    float f;
    uint32_t i;
};

void update_registers(PayloadStruct payload) {

  int base = baseRegs[payload.deviceID - 1];
 
    // Convert deviceID to float and store in two 16-bit registers
    FloatInt deviceIDFloat;
    deviceIDFloat.f = static_cast<float>(payload.deviceID);
    mb.Hreg(base + 1, deviceIDFloat.i & 0xFFFF); // Lower 16 bits
    mb.Hreg(base + 2, deviceIDFloat.i >> 16);    // Upper 16 bits

    // Convert payloadID to float and store in two 16-bit registers
    FloatInt payloadIDFloat;
    payloadIDFloat.f = static_cast<float>(payload.payloadID);
    mb.Hreg(base + 3, payloadIDFloat.i & 0xFFFF); // Lower 16 bits
    mb.Hreg(base + 4, payloadIDFloat.i >> 16);    // Upper 16 bits

    // Convert payloadID to float and store in two 16-bit registers
    FloatInt batteryPercentageFloat;
    batteryPercentageFloat.f = static_cast<float>(payload.batteryPercentage);
    mb.Hreg(base + 5, batteryPercentageFloat.i & 0xFFFF); // Lower 16 bits
    mb.Hreg(base + 6, batteryPercentageFloat.i >> 16);    // Upper 16 bits

  // Convert batteryVoltage to two 16-bit registers
    uint16_t* voltagePtr = (uint16_t*)&payload.batteryVoltage;
    mb.Hreg(base + 7, voltagePtr[0]); // Lower 16 bits
    mb.Hreg(base + 8, voltagePtr[1]); // Upper 16 bits

    // Convert temperature to two 16-bit registers
    uint16_t* tempPtr = (uint16_t*)&payload.temperature;
    mb.Hreg(base + 9, tempPtr[0]); // Lower 16 bits
    mb.Hreg(base + 10, tempPtr[1]); // Upper 16 bits

}
void update_registers(PayloadStruct payload);

void setupModbusTCP() {
  mb.server();

  int numDevices = 6;

  for (int device = 0; device < numDevices; device++) {
    int base = baseRegs[device];
    mb.addHreg(base, device, 20);

    mb.onSetHreg(base, cbSetDeviceReg1, 1);
    mb.onSetHreg(base + 1, cbSetDeviceReg2, 1);
    mb.onSetHreg(base + 2, cbSetDeviceReg3, 1);
    mb.onSetHreg(base + 3, cbSetDeviceReg4, 1);
    mb.onSetHreg(base + 4, cbSetDeviceReg5, 1);
    mb.onSetHreg(base + 5, cbSetDeviceReg6, 1);
    mb.onSetHreg(base + 6, cbSetDeviceReg7, 1);
    mb.onSetHreg(base + 7, cbSetDeviceReg8, 1);
    mb.onSetHreg(base + 8, cbSetDeviceReg9, 1);
    mb.onSetHreg(base + 9, cbSetDeviceReg10, 1);
    mb.onSetHreg(base + 10, cbSetDeviceReg11, 1);
  }
}

uint16_t cbSetDeviceReg1(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg2(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg3(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg4(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}


uint16_t cbSetDeviceReg5(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}


uint16_t cbSetDeviceReg6(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}


uint16_t cbSetDeviceReg7(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg8(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg9(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg10(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}

uint16_t cbSetDeviceReg11(TRegister* reg, uint16_t val) {
  // Serial.printf("Register %d updated: %d\n", reg->address.address,val);
  reg->value = val;
  return val;
}
