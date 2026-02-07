#include <Wire.h>
#include <Adafruit_BMP085.h>
#include <MPU6050.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <AESLib.h>
#include <Hash.h>

// Wi-Fi credentials
const char* ssid = "Your_SSID";
const char* password = "Your_PASSWORD";

// Web server
ESP8266WebServer server(80);

// Sensor objects
Adafruit_BMP085 bmp;
MPU6050 mpu;
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Sensor values
float tempC, pressure, accX, accY, accZ;

// Encrypted buffer
char encryptedData[128];
AESLib aesLib;
byte aesKey[] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
                  0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F };

// Login credentials (hashed using SHA1)
const String loginUser = "admin";
const String hashedLoginPass = "d033e22ae348aeb5660fc2140aec35850c4da997"; // SHA1 of 'admin'

// Brute force protection
int failedAttempts = 0;
unsigned long lastFailedTime = 0;
const unsigned long lockoutDuration = 30000; // 30 seconds

// HTML templates
String htmlHeader = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Login</title></head><body>";
String htmlFooter = "</body></html>";

// Function prototypes
void handleLogin();
void handleDashboard();
void handleNotFound();
bool isAuthenticated();
String sha1Hash(String input);

void setup() {
  Serial.begin(115200);
  Wire.begin();

  if (!bmp.begin()) {
    Serial.println("Could not find BMP180 sensor!");
    while (1);
  }

  mpu.initialize();
  if (!mpu.testConnection()) {
    Serial.println("MPU6050 connection failed!");
    while (1);
  }

  lcd.init();
  lcd.backlight();

  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleLogin);
  server.on("/login", HTTP_POST, handleLogin);
  server.on("/dashboard", HTTP_GET, handleDashboard);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("Server started");
}

void loop() {
  tempC = bmp.readTemperature();
  pressure = bmp.readPressure() / 100.0;

  mpu.getAcceleration(&accX, &accY, &accZ);
  accX = accX / 16384.0;
  accY = accY / 16384.0;
  accZ = accZ / 16384.0;

  lcd.setCursor(0, 0);
  lcd.print("T:");
  lcd.print(tempC, 1);
  lcd.print("C P:");
  lcd.print(pressure, 0);
  lcd.setCursor(0, 1);
  lcd.print("X:");
  lcd.print(accX, 1);
  lcd.print(" Y:");
  lcd.print(accY, 1);

  server.handleClient();
}

void handleLogin() {
  if (millis() - lastFailedTime < lockoutDuration && failedAttempts >= 5) {
    server.send(403, "text/html", htmlHeader + "<h3>Too many failed attempts. Try again later.</h3>" + htmlFooter);
    return;
  }

  if (server.method() == HTTP_POST) {
    if (server.hasArg("username") && server.hasArg("password")) {
      String user = server.arg("username");
      String pass = sha1Hash(server.arg("password"));

      if (user == loginUser && pass == hashedLoginPass) {
        failedAttempts = 0;
        server.sendHeader("Location", "/dashboard", true);
        server.send(302, "text/plain", "");
        return;
      } else {
        failedAttempts++;
        lastFailedTime = millis();
        server.send(200, "text/html", htmlHeader + "<h3>Login Failed</h3><a href='/'>Try again</a>" + htmlFooter);
        return;
      }
    }
  }

  String loginForm = htmlHeader;
  loginForm += "<h2>Login</h2>";
  loginForm += "<form method='POST' action='/login'>";
  loginForm += "Username: <input type='text' name='username'><br>";
  loginForm += "Password: <input type='password' name='password'><br><br>";
  loginForm += "<input type='submit' value='Login'></form>";
  loginForm += htmlFooter;
  server.send(200, "text/html", loginForm);
}

void handleDashboard() {
  char plain[128];
  snprintf(plain, sizeof(plain), "T:%.1fC P:%.0fhPa X:%.2f Y:%.2f Z:%.2f", tempC, pressure, accX, accY, accZ);
  aesLib.encrypt(plain, encryptedData, aesKey);

  String page = htmlHeader;
  page += "<h2>Tire Sensor Dashboard</h2>";
  page += "<p><b>Encrypted Data:</b><br>" + String(encryptedData) + "</p>";
  page += htmlFooter;

  server.send(200, "text/html", page);
}

void handleNotFound() {
  server.send(404, "text/html", htmlHeader + "<h3>404 - Page Not Found</h3>" + htmlFooter);
}

String sha1Hash(String input) {
  SHA1 sha1;
  sha1.reset();
  sha1.update(input.c_str(), input.length());
  byte* hashBytes = sha1.result();
  String hashString = "";
  for (int i = 0; i < 20; i++) {
    if (hashBytes[i] < 16) hashString += "0";
    hashString += String(hashBytes[i], HEX);
  }
  return hashString;
}
