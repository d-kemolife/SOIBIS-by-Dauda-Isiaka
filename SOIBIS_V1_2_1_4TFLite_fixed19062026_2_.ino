// The correct order should be:
// 1. BMP180, MPU6050_Sensor, BuzzerManager
// 2. NTPTimeManager, WiFiManager, WiFiConfigMenu
// 3. PreferenceManager
// 4. SensorManager
// 5. IrrigationController  <-- Move this BEFORE WebSocketManager
// 6. SDCardManager         <-- Move this BEFORE WebSocketManager
// 7. GoogleSheetsManager
// 8. TelegramManager       <-- NEW: Added Telegram support
// 9. WebSocketManager      <-- Now can use IrrigationController and SDCardManager
// 10. WebServerManager

// Here's the complete code with corrected order and Telegram integration:

/*******************************************************************************
 * SECURED & OPTIMIZED IOT-BASED IRRIGATION SYSTEM -
 *    BY DAUDA OLORUNKEMI ISIAKA - PHD23CSC005
 *******************************************************************************/

#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <LittleFS.h>
#include <SD.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <Preferences.h>
#include <cmath>
#include <WiFiUdp.h>
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>  // NEW: Telegram library
#include "ml_irrigation.h"

// =============================================================================
// CONFIGURATION SECTION
// =============================================================================

// Default AP Mode Configuration
const char* AP_SSID = "SOIBIS";
const char* AP_PASSWORD = "admin123";

// Default WiFi Configuration
const char* DEFAULT_SSID = "";
const char* DEFAULT_PASSWORD = "";

// Web Server Configuration
#define WEB_SERVER_PORT 80
#define WEB_SOCKET_PORT 81
#define MAX_CLIENTS 5
#define HISTORY_SIZE 100
#define SERIAL_BUFFER_SIZE 1024

// NTP Configuration
WiFiUDP udp;                               // <-- The UDP object
NTPClient timeClient(udp, "pool.ntp.org", 3600, 60000); // <-- The actual timeClient variable
#define NTP_SERVER "pool.ntp.org"
#define TIME_ZONE_OFFSET 0

// I2C Addresses
#define BMP180_I2C_ADDRESS 0x77
#define MPU6050_I2C_ADDRESS 0x68

// Preferences namespace
#define PREF_NAMESPACE "irrigation"
#define PREF_VERSION 10

// Fixed pin definitions
const int PIN_SANDY_MOISTURE = 1;
const int PIN_CLAY_MOISTURE = 2;
const int PIN_LOAMY_MOISTURE = 3;
const int PIN_DHT11 = 4;
const int PIN_SANDY_PUMP = 5;
const int PIN_CLAY_PUMP = 6;
const int PIN_LOAMY_PUMP = 7;
const int PIN_BUZZER = 21;

// I2C Pins
const int PIN_I2C_SDA = 17;
const int PIN_I2C_SCL = 18;

// SPI Pins for SD Card
const int PIN_SD_CS = 10;
const int PIN_SD_SCK = 12;
const int PIN_SD_MISO = 13;
const int PIN_SD_MOSI = 11;

// LED Pins
const int PIN_RED_LED = 14;
const int PIN_YELLOW_LED = 15;
const int PIN_GREEN_LED = 16;

// Forward declarations
class BuzzerManager;
class SensorManager;
class WebSocketManager;
class IrrigationController;
class SDCardManager;
class TelegramManager;  // NEW: Forward declaration

// =============================================================================
// GLOBAL OBJECTS
// =============================================================================

AsyncWebServer server(WEB_SERVER_PORT);
WebSocketsServer webSocket(WEB_SOCKET_PORT);
Preferences preferences;
DHT dht(PIN_DHT11, DHT11);
//WiFiUDP udp;
//NTPClient timeClient(udp, "pool.ntp.org", 3600, 60000); // UTC+1 (3600 offset)
SPIClass sdSPI(HSPI);
// ML inference result — updated every sensor-read cycle
MLIrrigationModel::MLPrediction mlPrediction = {0, 0, 0, false, false, false, false};
bool mlOverrideEnabled = true;  // set false via serial cmd "ML OFF" to disable ML



// =============================================================================
// ADVANCED CALIBRATION STRUCTURE
// =============================================================================

struct AdvancedCalibration {
  uint8_t version = PREF_VERSION;
  
  struct TemperatureCalibration {
    float offset = 0.0;
    float slope = 1.0;
    float reference = 25.0;
    bool useReference = false;
  } tempCalibration;
  
  struct HumidityCalibration {
    float offset = 0.0;
    float slope = 1.0;
    float reference = 50.0;
    bool useReference = false;
  } humidityCalibration;
  
  struct PressureCalibration {
    float offset = 0.0;
    float slope = 1.0;
    float reference = 1013.25;
    bool useReference = false;
    float altitudeCorrection = 0.0;
    bool useDirectCalibration = false;
    float calibrationFactor = 1.0;
  } pressureCalibration;
  
  struct WiFiCredentials {
    char ssid[32] = "";
    char password[64] = "";
  } wifiCredentials;
  
  struct WebServerConfig {
    bool enabled = true;
    bool requireAuth = false;
    char username[32] = "admin";
    char password[32] = "admin";
    int sessionTimeout = 3600;
    bool enableWebSockets = true;
    bool enableLiveData = true;
    int updateInterval = 1000;
    bool enableGraphs = true;
    int graphHistory = HISTORY_SIZE;
    bool enableSerialTerminal = true;
    int serialBufferSize = SERIAL_BUFFER_SIZE;
  } webserver;
  
  struct GoogleSheetsConfig {
    bool enabled = false;
    char scriptURL[256] = "";
    int logInterval = 60000;
    bool logSensors = true;
    bool logPumps = true;
    bool logEvents = true;
  } googleSheets;
  
  struct SDConfig {
    bool enabled = true;
    int logInterval = 300000;
    bool logRawData = false;
    bool logCalibratedData = true;
    int maxFiles = 50;
    int maxFileSize = 1024 * 1024;
  } sd;
  
  struct SoilCalibration {
    int rawDry = 3000;
    int rawWet = 1500;
    int offset = 0;
    float slope = 1.0;
    bool enabled = true;
  } sandy, clay, loamy;
  
  struct DHT11Calibration {
    float tempOffset = 0.0;
    float humOffset = 0.0;
    float tempSlope = 1.0;
    float humSlope = 1.0;
    bool enabled = true;
  } dht11;
  
  struct BMP180Calibration {
    float pressureOffset = 0.0;
    float tempOffset = 0.0;
    float altitudeOffset = 0.0;
    float pressureSlope = 1.0;
    float tempSlope = 1.0;
    bool enabled = true;
    float seaLevelPressure = 1013.25;
  } bmp180;
  
  struct MPU6050Calibration {
    float accelXOffset = 0.0;
    float accelYOffset = 0.0;
    float accelZOffset = 0.0;
    float gyroXOffset = 0.0;
    float gyroYOffset = 0.0;
    float gyroZOffset = 0.0;
    float movementThreshold = 0.5;
    int tamperAlertDelay = 60000;
    bool enabled = true;
    bool sendAlerts = true;
    bool enableBuzzer = true;
    bool webAlerts = true;
    float accelBaselineX = 0.0;
    float accelBaselineY = 0.0;
    float accelBaselineZ = 1.0;
    float gyroBaselineX = 0.0;
    float gyroBaselineY = 0.0;
    float gyroBaselineZ = 0.0;
  } mpu6050;
  
  struct WiFiConfig {
    bool enabled = true;
    bool autoConnect = true;
    int reconnectInterval = 30000;
    int maxRetries = 10;
    bool dhcpEnabled = true;
    char hostname[32] = "soibis";
    bool apModeFallback = true;
    char apSSID[32] = "SOIBIS";
    char apPassword[32] = "admin123";
  } wifi;
  
  struct NTPConfig {
    bool enabled = true;
    int updateInterval = 3600000;
    int timezoneOffset = 0;
    char server[64] = "pool.ntp.org";
  } ntp;
  
  struct BuzzerConfig {
    bool enabled = true;
    int alertDuration = 5000;
    int alertFrequency = 2000;
    int beepDuration = 200;
  } buzzer;
  
  struct SystemControl {
    int sensorReadInterval = 30000;
    int statusPrintInterval = 30000;
    bool enableSerialOutput = true;
    bool enableWebSerial = true;
    bool enableLEDs = true;
    bool use24HourFormat = true;
    bool autoStart = true;
    bool manualMode = false;
    float simulatedRainfall = 0.0;
    bool rainSimulationActive = false;
    unsigned long rainSimulationStart = 0;
  } system;
  
  struct IrrigationControl {
    float sandyWaterThreshold = 50.0;
    float clayWaterThreshold = 40.0;
    float loamyWaterThreshold = 45.0;
    float humidityThreshold = 75.0;
    float tempThresholdLow = 10.0;
    float tempThresholdHigh = 35.0;
    float dewPointMargin = 2.5;
    int wateringDuration = 10000;
    int minWateringInterval = 7200000;
    int maxPumpRuntime = 120000;
    bool checkRainCondition = true;
    bool checkDewPoint = true;
    bool checkTemperature = true;
    bool checkHumidity = true;
  } irrigation;
  
  // NEW: Telegram Configuration
  struct TelegramConfig {
    bool enabled = false;
    char botToken[64] = "";
    char chatId[32] = "";
    bool notifyAlerts = true;
    bool notifyMovement = true;
    bool notifyRain = true;
    bool notifyPumpStatus = true;
    int notifyInterval = 3600000;  // 1 hour between notifications
    unsigned long lastNotifyTime = 0;
  } telegram;
  
} calibration;

// =============================================================================
// DATA STRUCTURES
// =============================================================================
#include "sensor_types.h"

struct PumpStatus {
  bool sandyActive;
  bool clayActive;
  bool loamyActive;
  bool sandyManual;
  bool clayManual;
  bool loamyManual;
  unsigned long lastWateringTime;
  unsigned long pumpStartTime;
  bool waterAvailable;
  int sandyRuntime;
  int clayRuntime;
  int loamyRuntime;
  unsigned long lastGoogleSheetsLog = 0;
  unsigned long lastSDLog = 0;
};

struct SystemState {
  bool isCalibrating;
  bool wifiConnected;
  bool sdCardPresent;
  bool securityAlert;
  unsigned long lastAlertTime;
  bool calibrationLoaded;
  bool systemEnabled;
  bool securityEnabled;
  bool wifiEnabled;
  bool ntpSynced;
  bool buzzerEnabled;
  bool irrigationActive;
  bool manualMode;
  bool webServerRunning;
  bool googleSheetsEnabled;
  bool sdEnabled;
  String lastError;
  String ipAddress;
  String googleSheetsURL;
  String serialBuffer;
  bool webSerialEnabled;
  String sdCardStatus;
  unsigned long sdCardSize = 0;
  unsigned long sdCardUsed = 0;
  bool apModeActive = false;
};

struct HistoricalData {
  float sandyPercent[HISTORY_SIZE];
  float clayPercent[HISTORY_SIZE];
  float loamyPercent[HISTORY_SIZE];
  float temperature[HISTORY_SIZE];
  float humidity[HISTORY_SIZE];
  float pressure[HISTORY_SIZE];
  float altitude[HISTORY_SIZE];
  float dewPoint[HISTORY_SIZE];
  bool sandyPump[HISTORY_SIZE];
  bool clayPump[HISTORY_SIZE];
  bool loamyPump[HISTORY_SIZE];
  String timestamps[HISTORY_SIZE];
  int index = 0;
  bool isFull = false;
};

SensorData currentData;
PumpStatus pumpStatus;
SystemState systemState;
HistoricalData historicalData;

// SD Card file management
File sdFile;
String currentLogFilename = "";
unsigned long sdFileSize = 0;
int sdFileCount = 0;

// =============================================================================
// UTILITY FUNCTIONS
// =============================================================================

float calculateDewPoint(float temperature, float humidity) {
  if (isnan(temperature) || isnan(humidity) || humidity <= 0) return NAN;
  const float a = 17.27;
  const float b = 237.7;
  float alpha = ((a * temperature) / (b + temperature)) + log(humidity / 100.0);
  return (b * alpha) / (a - alpha);
}

bool checkRainCondition(float temperature, float humidity, float pressure) {
  if (!calibration.irrigation.checkRainCondition) return false;
  if (isnan(temperature) || isnan(humidity) || isnan(pressure)) return false;
  
  if (calibration.system.rainSimulationActive) return true;
  
  if (humidity < 70) return false;
  
  float dewPoint = calculateDewPoint(temperature, humidity);
  if (isnan(dewPoint)) return false;
  
  float dewPointDepression = temperature - dewPoint;
  
  if (humidity > 85 && dewPointDepression < 2.5) return true;
  if (pressure < 1000 && humidity > 80) return true;
  
  static float lastPressure = 1013.25;
  float pressureDrop = lastPressure - pressure;
  lastPressure = pressure;
  
  return (pressureDrop > 3 && humidity > 75);
}

String getTimestamp() {
  static unsigned long startMillis = millis();
  unsigned long elapsed = millis() - startMillis;
  unsigned long seconds = elapsed / 1000;
  
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "%02lu:%02lu:%02lu",
           (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
  return String(buffer);
}

String getDateString() {
  if (timeClient.isTimeSet()) {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime(&epochTime);
    
    char buffer[20];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d",
             ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday);
    return String(buffer);
  }
  
  unsigned long days = millis() / 86400000;
  char buffer[20];
  snprintf(buffer, sizeof(buffer), "day%03lu", days + 1);
  return String(buffer);
}

String getDateTime() {
  if (timeClient.isTimeSet()) {
    time_t epochTime = timeClient.getEpochTime();
    struct tm *ptm = gmtime(&epochTime);
    
    char buffer[30];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d:%02d",
             ptm->tm_year + 1900, ptm->tm_mon + 1, ptm->tm_mday,
             ptm->tm_hour, ptm->tm_min, ptm->tm_sec);
    return String(buffer);
  }
  return getTimestamp();
}

String formatFloat(float value, int decimals = 1) {
  if (isnan(value)) return "N/A";
  char buffer[20];
  dtostrf(value, 0, decimals, buffer);
  return String(buffer);
}

// =============================================================================
// FIXED SOIL MOISTURE SENSOR MAPPING - INTUITIVE SCALE
// Dry = 0%, Wet = 100%
// =============================================================================

// Improved mapToPercent function with intuitive scaling
float mapToPercent(int reading, int rawDry, int rawWet) {
  // Ensure readings are within valid range (0-4095 for 12-bit ADC)
  reading = constrain(reading, 0, 4095);
  
  // Ensure calibration values are valid
  if (rawDry <= rawWet) {
    // Invalid calibration - use defaults
    rawDry = 3000;  // Dry value (high reading)
    rawWet = 1500;  // Wet value (low reading)
  }
  
  // For soil moisture sensors:
  // - Higher raw value = Drier soil
  // - Lower raw value = Wetter soil
  // So we invert the mapping: Dry (high raw) = 0%, Wet (low raw) = 100%
  
  // Clamp reading to calibration range
  if (reading >= rawDry) return 0.0;      // Dryer than dry calibration -> 0%
  if (reading <= rawWet) return 100.0;    // Wetter than wet calibration -> 100%
  
  // Calculate percentage (inverted: dry=high raw, wet=low raw)
  float percent = 100.0 * (rawDry - reading) / (rawDry - rawWet);
  
  // Constrain to valid range
  return constrain(percent, 0.0, 100.0);
}

// Function to read moisture sensor with averaging
int readMoistureSensor(int pin, int samples = 10) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delay(10);
  }
  return sum / samples;
}

void addToSerialBuffer(const String& message) {
  if (!calibration.system.enableWebSerial) return;
  
  systemState.serialBuffer += "[" + getTimestamp() + "] " + message + "\n";
  
  int maxSize = calibration.webserver.serialBufferSize;
  if (systemState.serialBuffer.length() > maxSize) {
    systemState.serialBuffer = systemState.serialBuffer.substring(systemState.serialBuffer.length() - maxSize);
  }
}

// =============================================================================
// BMP180 SENSOR DRIVER
// =============================================================================

class BMP180 {
private:
  int16_t ac1, ac2, ac3;
  uint16_t ac4, ac5, ac6;
  int16_t b1, b2;
  int16_t mb, mc, md;
  int32_t b5;
  
  uint16_t readUT() {
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    if (Wire.write(0xF4) != 1) return 0;
    if (Wire.write(0x2E) != 1) return 0;
    if (Wire.endTransmission() != 0) return 0;
    delay(5);
    
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    if (Wire.write(0xF6) != 1) return 0;
    if (Wire.endTransmission() != 0) return 0;
    
    if (Wire.requestFrom(BMP180_I2C_ADDRESS, 2) != 2) return 0;
    delay(10);
    
    uint16_t ut = Wire.read() << 8;
    ut |= Wire.read();
    return ut;
  }
  
  uint32_t readUP() {
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    Wire.write(0xF4);
    Wire.write(0x34 + (3 << 6));
    if (Wire.endTransmission() != 0) return 0;
    delay(26);
    
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    Wire.write(0xF6);
    if (Wire.endTransmission() != 0) return 0;
    
    if (Wire.requestFrom(BMP180_I2C_ADDRESS, 3) != 3) return 0;
    delay(10);
    
    uint32_t up = Wire.read() << 16;
    up |= Wire.read() << 8;
    up |= Wire.read();
    return up >> 8;
  }
  
  int32_t calculateTrueTemperature(uint16_t ut) {
    int32_t x1 = ((int32_t)ut - (int32_t)ac6) * (int32_t)ac5 >> 15;
    int32_t x2 = ((int32_t)mc << 11) / (x1 + md);
    b5 = x1 + x2;
    return (b5 + 8) >> 4;
  }
  
  int32_t calculateTruePressure(uint32_t up) {
    if (b5 == 0) return 0;
    
    int32_t b6 = b5 - 4000;
    int32_t x1 = (b2 * (b6 * b6 >> 12)) >> 11;
    int32_t x2 = ac2 * b6 >> 11;
    int32_t x3 = x1 + x2;
    int32_t b3 = ((((int32_t)ac1 * 4 + x3) << 3) + 2) >> 2;
    
    x1 = ac3 * b6 >> 13;
    x2 = (b1 * (b6 * b6 >> 12)) >> 16;
    x3 = ((x1 + x2) + 2) >> 2;
    uint32_t b4 = ac4 * (uint32_t)(x3 + 32768) >> 15;
    uint32_t b7 = ((uint32_t)up - b3) * (50000 >> 3);
    
    int32_t p;
    if (b7 < 0x80000000) {
      p = (b7 * 2) / b4;
    } else {
      p = (b7 / b4) * 2;
    }
    
    x1 = (p >> 8) * (p >> 8);
    x1 = (x1 * 3038) >> 16;
    x2 = (-7357 * p) >> 16;
    p = p + ((x1 + x2 + 3791) >> 4);
    return p;
  }
  
public:
  bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) {
      return false;
    }
    
    return readCalibration();
  }
  
  bool readCalibration() {
    Wire.beginTransmission(BMP180_I2C_ADDRESS);
    Wire.write(0xAA);
    if (Wire.endTransmission() != 0) return false;
    
    if (Wire.requestFrom(BMP180_I2C_ADDRESS, 22) != 22) {
      delay(10);
      return false;
    }
    delay(10);
    
    ac1 = Wire.read() << 8 | Wire.read();
    ac2 = Wire.read() << 8 | Wire.read();
    ac3 = Wire.read() << 8 | Wire.read();
    ac4 = Wire.read() << 8 | Wire.read();
    ac5 = Wire.read() << 8 | Wire.read();
    ac6 = Wire.read() << 8 | Wire.read();
    b1 = Wire.read() << 8 | Wire.read();
    b2 = Wire.read() << 8 | Wire.read();
    mb = Wire.read() << 8 | Wire.read();
    mc = Wire.read() << 8 | Wire.read();
    md = Wire.read() << 8 | Wire.read();
    
    return true;
  }
  
  float readRawTemperature() {
    uint16_t ut = readUT();
    if (ut == 0) return NAN;
    return calculateTrueTemperature(ut) / 10.0;
  }
  
  float readRawPressure() {
    uint16_t ut = readUT();
    if (ut == 0) return NAN;
    
    uint32_t up = readUP();
    if (up == 0) return NAN;
    
    calculateTrueTemperature(ut);
    int32_t pressure = calculateTruePressure(up);
    return pressure / 100.0;
  }
  
  float readRawAltitude(float seaLevelPressure = 1013.25) {
    float pressure = readRawPressure();
    if (isnan(pressure) || pressure <= 0) return NAN;
    return 44330.0 * (1.0 - pow(pressure / seaLevelPressure, 1.0/5.255));
  }
  
  float readCalibratedTemperature() {
    float temp = readRawTemperature();
    if (isnan(temp)) return NAN;
    return (temp * calibration.bmp180.tempSlope) + calibration.bmp180.tempOffset;
  }
  
  float readCalibratedPressure() {
    float pressure = readRawPressure();
    if (isnan(pressure)) return NAN;
    
    if (calibration.pressureCalibration.useDirectCalibration) {
      return pressure * calibration.pressureCalibration.calibrationFactor;
    } else {
      return (pressure * calibration.pressureCalibration.slope) + calibration.pressureCalibration.offset;
    }
  }
  
  float readCalibratedAltitude() {
    float altitude = readRawAltitude(calibration.bmp180.seaLevelPressure);
    if (isnan(altitude)) return NAN;
    return altitude + calibration.bmp180.altitudeOffset;
  }
};

// =============================================================================
// MPU6050 SENSOR DRIVER
// =============================================================================

class MPU6050_Sensor {
private:
  int16_t accelX, accelY, accelZ;
  int16_t gyroX, gyroY, gyroZ;
  int16_t tempRaw;
  float accelScale = 16384.0;
  float gyroScale = 131.0;
  bool baselineSet = false;
  unsigned long lastMovementTime = 0;
  int consecutiveMovements = 0;
  
  bool writeRegister(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(MPU6050_I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return (Wire.endTransmission() == 0);
  }
  
public:
  bool begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    
    Wire.beginTransmission(MPU6050_I2C_ADDRESS);
    if (Wire.endTransmission() != 0) return false;
    
    if (!writeRegister(0x6B, 0x00)) return false;
    delay(100);
    if (!writeRegister(0x1C, 0x00)) return false;
    if (!writeRegister(0x1B, 0x00)) return false;
    if (!writeRegister(0x1A, 0x03)) return false;
    
    delay(100);
    return true;
  }
  
  bool readData() {
    Wire.beginTransmission(MPU6050_I2C_ADDRESS);
    Wire.write(0x3B);
    if (Wire.endTransmission() != 0) return false;
    
    if (Wire.requestFrom(MPU6050_I2C_ADDRESS, 14) != 14) {
      return false;
    }
    
    accelX = Wire.read() << 8 | Wire.read();
    accelY = Wire.read() << 8 | Wire.read();
    accelZ = Wire.read() << 8 | Wire.read();
    tempRaw = Wire.read() << 8 | Wire.read();
    gyroX = Wire.read() << 8 | Wire.read();
    gyroY = Wire.read() << 8 | Wire.read();
    gyroZ = Wire.read() << 8 | Wire.read();
    
    return true;
  }
  
  float getAccelerationX() { 
    float value = (accelX / accelScale) + calibration.mpu6050.accelXOffset;
    return constrain(value, -2.0, 2.0);
  }
  
  float getAccelerationY() { 
    float value = (accelY / accelScale) + calibration.mpu6050.accelYOffset;
    return constrain(value, -2.0, 2.0);
  }
  
  float getAccelerationZ() { 
    float value = (accelZ / accelScale) + calibration.mpu6050.accelZOffset;
    return constrain(value, -2.0, 2.0);
  }
  
  float getGyroX() { 
    return (gyroX / gyroScale) + calibration.mpu6050.gyroXOffset;
  }
  
  float getGyroY() { 
    return (gyroY / gyroScale) + calibration.mpu6050.gyroYOffset;
  }
  
  float getGyroZ() { 
    return (gyroZ / gyroScale) + calibration.mpu6050.gyroZOffset;
  }
  
  float getTotalAcceleration() {
    float ax = getAccelerationX();
    float ay = getAccelerationY();
    float az = getAccelerationZ();
    return sqrt(ax * ax + ay * ay + az * az);
  }
  
  float getTotalGyro() {
    float gx = getGyroX();
    float gy = getGyroY();
    float gz = getGyroZ();
    return sqrt(gx * gx + gy * gy + gz * gz);
  }
  
  void setBaseline() {
    if (!readData()) return;
    
    int samples = 100;
    float sumAccelX = 0, sumAccelY = 0, sumAccelZ = 0;
    float sumGyroX = 0, sumGyroY = 0, sumGyroZ = 0;
    
    for (int i = 0; i < samples; i++) {
      if (readData()) {
        sumAccelX += getAccelerationX();
        sumAccelY += getAccelerationY();
        sumAccelZ += getAccelerationZ();
        sumGyroX += getGyroX();
        sumGyroY += getGyroY();
        sumGyroZ += getGyroZ();
      }
      delay(10);
    }
    
    calibration.mpu6050.accelBaselineX = sumAccelX / samples;
    calibration.mpu6050.accelBaselineY = sumAccelY / samples;
    calibration.mpu6050.accelBaselineZ = sumAccelZ / samples;
    calibration.mpu6050.gyroBaselineX = sumGyroX / samples;
    calibration.mpu6050.gyroBaselineY = sumGyroY / samples;
    calibration.mpu6050.gyroBaselineZ = sumGyroZ / samples;
    
    baselineSet = true;
  }
  
  bool isMoving() {
    if (!calibration.mpu6050.enabled) return false;
    if (!baselineSet) {
      setBaseline();
      return false;
    }
    
    if (!readData()) return false;
    
    float ax = getAccelerationX();
    float ay = getAccelerationY();
    float az = getAccelerationZ();
    float gx = getGyroX();
    float gy = getGyroY();
    float gz = getGyroZ();
    
    float accelDev = sqrt(
      pow(ax - calibration.mpu6050.accelBaselineX, 2) +
      pow(ay - calibration.mpu6050.accelBaselineY, 2) +
      pow(az - calibration.mpu6050.accelBaselineZ, 2)
    );
    
    float gyroDev = sqrt(
      pow(gx - calibration.mpu6050.gyroBaselineX, 2) +
      pow(gy - calibration.mpu6050.gyroBaselineY, 2) +
      pow(gz - calibration.mpu6050.gyroBaselineZ, 2)
    );
    
    bool movement = (accelDev > calibration.mpu6050.movementThreshold) ||
                    (gyroDev > calibration.mpu6050.movementThreshold * 5);
    
    if (movement) {
      consecutiveMovements++;
      lastMovementTime = millis();
      
      calibration.mpu6050.accelBaselineX = calibration.mpu6050.accelBaselineX * 0.95 + ax * 0.05;
      calibration.mpu6050.accelBaselineY = calibration.mpu6050.accelBaselineY * 0.95 + ay * 0.05;
      calibration.mpu6050.accelBaselineZ = calibration.mpu6050.accelBaselineZ * 0.95 + az * 0.05;
      calibration.mpu6050.gyroBaselineX = calibration.mpu6050.gyroBaselineX * 0.95 + gx * 0.05;
      calibration.mpu6050.gyroBaselineY = calibration.mpu6050.gyroBaselineY * 0.95 + gy * 0.05;
      calibration.mpu6050.gyroBaselineZ = calibration.mpu6050.gyroBaselineZ * 0.95 + gz * 0.05;
    } else {
      consecutiveMovements = max(0, consecutiveMovements - 1);
    }
    
    return movement;
  }
  
  int getMovementIntensity() {
    if (!readData()) return 0;
    
    float ax = getAccelerationX();
    float ay = getAccelerationY();
    float az = getAccelerationZ();
    
    float accelMag = sqrt(ax*ax + ay*ay + az*az);
    float deviation = abs(accelMag - 1.0);
    
    return constrain((int)(deviation * 100), 0, 100);
  }
  
  void calibrate(int duration = 5000) {
    Serial.println("Calibrating MPU6050... Place device on stable surface!");
    
    float sumAccelX = 0, sumAccelY = 0, sumAccelZ = 0;
    float sumGyroX = 0, sumGyroY = 0, sumGyroZ = 0;
    int samples = 0;
    unsigned long startTime = millis();
    
    while (millis() - startTime < duration) {
      if (readData()) {
        sumAccelX += accelX / accelScale;
        sumAccelY += accelY / accelScale;
        sumAccelZ += accelZ / accelScale;
        sumGyroX += gyroX / gyroScale;
        sumGyroY += gyroY / gyroScale;
        sumGyroZ += gyroZ / gyroScale;
        samples++;
        
        if (samples % 100 == 0) {
          Serial.printf("Calibration progress: %d/%d samples\n", samples, duration/10);
        }
      }
      delay(10);
    }
    
    if (samples > 0) {
      calibration.mpu6050.accelXOffset = -sumAccelX / samples;
      calibration.mpu6050.accelYOffset = -sumAccelY / samples;
      calibration.mpu6050.accelZOffset = -(sumAccelZ / samples - 1.0);
      calibration.mpu6050.gyroXOffset = -sumGyroX / samples;
      calibration.mpu6050.gyroYOffset = -sumGyroY / samples;
      calibration.mpu6050.gyroZOffset = -sumGyroZ / samples;
      
      Serial.println("MPU6050 calibration complete!");
      baselineSet = false;
      setBaseline();
    }
  }
  
  String getStatus() {
    if (!calibration.mpu6050.enabled) return "Disabled";
    
    readData();
    float accelMag = getTotalAcceleration();
    float gyroMag = getTotalGyro();
    int intensity = getMovementIntensity();
    
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "Accel:%.2fg Gyro:%.2f°/s Intensity:%d%%",
             accelMag, gyroMag, intensity);
    return String(buffer);
  }
};

// =============================================================================
// BUZZER MANAGER
// =============================================================================

class BuzzerManager {
private:
  static bool isActive;
  static unsigned long alertStartTime;
  static int alertFrequency;
  
public:
  static void initialize() {
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW);
    isActive = false;
    systemState.buzzerEnabled = calibration.buzzer.enabled;
  }
  
  static void enable() {
    calibration.buzzer.enabled = true;
    systemState.buzzerEnabled = true;
    Serial.println("Buzzer enabled");
  }
  
  static void disable() {
    calibration.buzzer.enabled = false;
    systemState.buzzerEnabled = false;
    stopAlert();
    Serial.println("Buzzer disabled");
  }
  
  static void toggle() {
    if (calibration.buzzer.enabled) disable();
    else enable();
  }
  
  static void startAlert(int frequency = 2000) {
    if (!systemState.buzzerEnabled) return;
    
    alertFrequency = frequency;
    alertStartTime = millis();
    isActive = true;
    tone(PIN_BUZZER, alertFrequency);
  }
  
  static void stopAlert() {
    if (isActive) {
      noTone(PIN_BUZZER);
      digitalWrite(PIN_BUZZER, LOW);
      isActive = false;
    }
  }
  
  static void beep(int duration = 200, int frequency = 1000) {
    if (!systemState.buzzerEnabled) return;
    tone(PIN_BUZZER, frequency, duration);
    delay(duration);
    noTone(PIN_BUZZER);
  }
  
  static void update() {
    if (!isActive || !systemState.buzzerEnabled) return;
    
    unsigned long currentMillis = millis();
    if (currentMillis - alertStartTime > calibration.buzzer.alertDuration) {
      stopAlert();
    }
  }
  
  static void playStartupSequence() {
    if (!systemState.buzzerEnabled) return;
    beep(100, 1000);
    delay(50);
    beep(100, 1200);
    delay(50);
    beep(100, 1500);
  }
  
  static void playAlertSequence() {
    if (!systemState.buzzerEnabled) return;
    for (int i = 0; i < 3; i++) {
      tone(PIN_BUZZER, 2000, 300);
      delay(400);
      tone(PIN_BUZZER, 1500, 300);
      delay(400);
    }
    noTone(PIN_BUZZER);
  }
  
  static void playSuccessTone() {
    if (!systemState.buzzerEnabled) return;
    tone(PIN_BUZZER, 1500, 200);
    delay(250);
    tone(PIN_BUZZER, 1800, 200);
    delay(250);
    noTone(PIN_BUZZER);
  }
  
  static void playErrorTone() {
    if (!systemState.buzzerEnabled) return;
    for (int i = 0; i < 2; i++) {
      tone(PIN_BUZZER, 800, 300);
      delay(400);
    }
    noTone(PIN_BUZZER);
  }
  
  static void playWateringTone() {
    if (!systemState.buzzerEnabled) return;
    tone(PIN_BUZZER, 1200, 100);
    delay(120);
    tone(PIN_BUZZER, 1400, 100);
    delay(120);
    tone(PIN_BUZZER, 1600, 100);
    delay(120);
    noTone(PIN_BUZZER);
  }
};

bool BuzzerManager::isActive = false;
unsigned long BuzzerManager::alertStartTime = 0;
int BuzzerManager::alertFrequency = 2000;

// =============================================================================
// NTP TIME MANAGER
// =============================================================================

class NTPTimeManager {
private:
  static bool initialized;
  static int syncFailures;
  
public:
  static bool isInitialized() {
    return initialized;
  }
  
  static bool initialize() {
    if (!calibration.ntp.enabled) {
      Serial.println("NTP disabled in calibration");
      systemState.ntpSynced = false;
      return false;
    }
    
    const char* ntpServers[] = {
      calibration.ntp.server,
      "pool.ntp.org",
      "time.google.com",
      "time.cloudflare.com",
      "time.nist.gov"
    };
    
    timeClient.begin();
    timeClient.setTimeOffset(calibration.ntp.timezoneOffset * 3600);
    timeClient.setUpdateInterval(calibration.ntp.updateInterval);
    
    for (int i = 0; i < 5; i++) {
      timeClient.setPoolServerName(ntpServers[i]);
      if (updateTime()) {
        systemState.ntpSynced = true;
        initialized = true;
        syncFailures = 0;
        Serial.printf("NTP initialized with server: %s\n", ntpServers[i]);
        Serial.printf("Current time: %s\n", getDateTime().c_str());
        return true;
      }
      delay(1000);
    }
    
    Serial.println("Failed to get time from NTP servers");
    systemState.ntpSynced = false;
    initialized = false;
    return false;
  }
  
  static bool updateTime() {
    if (!initialized && !calibration.ntp.enabled) return false;
    
    int attempts = 0;
    while (attempts < 3) {
      if (timeClient.update()) {
        systemState.ntpSynced = true;
        syncFailures = 0;
        return true;
      }
      attempts++;
      delay(500);
    }
    
    syncFailures++;
    systemState.ntpSynced = false;
    
    if (syncFailures > 10) {
      Serial.println("Too many NTP sync failures, reinitializing...");
      initialized = false;
      initialize();
    }
    
    return false;
  }
  
  static void enable() {
    calibration.ntp.enabled = true;
    Serial.println("NTP enabled");
    if (!initialized) initialize();
  }
  
  static void disable() {
    calibration.ntp.enabled = false;
    systemState.ntpSynced = false;
    timeClient.end();
    initialized = false;
    Serial.println("NTP disabled");
  }
  
  static void toggle() {
    if (calibration.ntp.enabled) disable();
    else enable();
  }
  
  static void update() {
    if (!initialized || !calibration.ntp.enabled) return;
    static unsigned long lastUpdate = 0;
    unsigned long now = millis();
    
    if (now - lastUpdate > calibration.ntp.updateInterval) {
      lastUpdate = now;
      updateTime();
    }
  }
  
  static void setTimezone(int offset) {
    calibration.ntp.timezoneOffset = offset;
    timeClient.setTimeOffset(offset * 3600);
    Serial.printf("Timezone offset set to %d hours\n", offset);
  }
  
  static void setServer(const char* server) {
    strncpy(calibration.ntp.server, server, sizeof(calibration.ntp.server) - 1);
    timeClient.setPoolServerName(server);
    updateTime();
    Serial.printf("NTP server set to: %s\n", server);
  }
};

bool NTPTimeManager::initialized = false;
int NTPTimeManager::syncFailures = 0;

// =============================================================================
// WIFI MANAGER WITH AP MODE SUPPORT
// =============================================================================

class WiFiManager {
private:
  static bool isConnecting;
  static int connectionAttempts;
  static unsigned long lastReconnectAttempt;
  static bool apModeStarted;
  
public:
  static void initialize() {
    apModeStarted = false;
    
    if (!calibration.wifi.enabled) {
      Serial.println("WiFi disabled in calibration");
      systemState.wifiEnabled = false;
      systemState.wifiConnected = false;
      return;
    }
    
    systemState.wifiEnabled = true;
    
    if (strlen(calibration.wifiCredentials.ssid) == 0) {
      Serial.println("No WiFi credentials found");
      if (calibration.wifi.apModeFallback) {
        startAPMode();
      }
      return;
    }
    
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    if (calibration.wifi.autoConnect) {
      connect();
    }
  }
  
  static void startAPMode() {
    if (apModeStarted) return;
    
    Serial.println("\n Starting Access Point Mode...");
    WiFi.mode(WIFI_AP);
    
    String apName = String(calibration.wifi.apSSID);
    String apPass = String(calibration.wifi.apPassword);
    
    bool success = WiFi.softAP(apName.c_str(), apPass.c_str());
    
    if (success) {
      systemState.apModeActive = true;
      apModeStarted = true;
      systemState.ipAddress = WiFi.softAPIP().toString();
      
      Serial.println(" AP Mode Started!");
      Serial.printf("   SSID: %s\n", apName.c_str());
      Serial.printf("   Password: %s\n", apPass.c_str());
      Serial.printf("   IP Address: %s\n", systemState.ipAddress.c_str());
      Serial.println("\n Connect to this network and open http://" + systemState.ipAddress);
      Serial.println("   Then configure WiFi through the web interface or serial monitor\n");
      
      BuzzerManager::beep(200, 1500);
      BuzzerManager::beep(200, 1800);
    } else {
      Serial.println(" Failed to start AP Mode");
    }
  }
  
  static void stopAPMode() {
    if (apModeStarted) {
      WiFi.softAPdisconnect(true);
      apModeStarted = false;
      systemState.apModeActive = false;
      Serial.println("AP Mode stopped");
    }
  }
  
  static void enable() {
    calibration.wifi.enabled = true;
    systemState.wifiEnabled = true;
    Serial.println("WiFi enabled");
    if (calibration.wifi.autoConnect) {
      stopAPMode();
      connect();
    }
  }
  
  static void disable() {
    calibration.wifi.enabled = false;
    systemState.wifiEnabled = false;
    disconnect();
    stopAPMode();
    Serial.println("WiFi disabled");
  }
  
  static void toggle() {
    if (calibration.wifi.enabled) disable();
    else enable();
  }
  
  static bool connect() {
    if (!systemState.wifiEnabled) {
      Serial.println("WiFi is disabled");
      return false;
    }
    
    if (strlen(calibration.wifiCredentials.ssid) == 0) {
      Serial.println("No WiFi credentials set");
      if (calibration.wifi.apModeFallback) {
        startAPMode();
      }
      return false;
    }
    
    if (apModeStarted) {
      stopAPMode();
    }
    
    Serial.printf("Connecting to WiFi: %s\n", calibration.wifiCredentials.ssid);
    
    isConnecting = true;
    connectionAttempts = 0;
    lastReconnectAttempt = millis();
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(calibration.wifiCredentials.ssid, calibration.wifiCredentials.password);
    WiFi.setHostname(calibration.wifi.hostname);
    
    return true;
  }
  
  static void disconnect() {
    WiFi.disconnect(true);
    systemState.wifiConnected = false;
    isConnecting = false;
    connectionAttempts = 0;
    Serial.println("WiFi disconnected");
  }
  
  static void update() {
    if (!systemState.wifiEnabled) return;
    
    if (WiFi.status() == WL_CONNECTED) {
      if (!systemState.wifiConnected) {
        systemState.wifiConnected = true;
        systemState.apModeActive = false;
        isConnecting = false;
        connectionAttempts = 0;
        systemState.ipAddress = WiFi.localIP().toString();
        
        Serial.println("\n WiFi connected!");
        Serial.printf("   SSID: %s\n", WiFi.SSID().c_str());
        Serial.printf("   IP Address: %s\n", systemState.ipAddress.c_str());
        Serial.printf("   Signal Strength: %d dBm\n", WiFi.RSSI());
        
        BuzzerManager::beep(100, 1500);
        
        if (calibration.ntp.enabled && !NTPTimeManager::isInitialized()) {
          NTPTimeManager::initialize();
        }
      }
    } else {
      if (systemState.wifiConnected) {
        systemState.wifiConnected = false;
        Serial.println("WiFi disconnected");
        BuzzerManager::beep(100, 1000);
        
        if (calibration.wifi.apModeFallback && !apModeStarted) {
          startAPMode();
        }
      }
      
      if ((isConnecting || calibration.wifi.autoConnect) && !apModeStarted) {
        unsigned long currentMillis = millis();
        
        if (currentMillis - lastReconnectAttempt > calibration.wifi.reconnectInterval) {
          lastReconnectAttempt = currentMillis;
          connectionAttempts++;
          
          if (connectionAttempts <= calibration.wifi.maxRetries) {
            Serial.printf("Reconnection attempt %d/%d\n", 
                          connectionAttempts, calibration.wifi.maxRetries);
            WiFi.reconnect();
          } else {
            isConnecting = false;
            Serial.println("Max reconnection attempts reached");
            
            if (calibration.wifi.apModeFallback) {
              startAPMode();
            }
          }
        }
      }
    }
  }
  
  static void scanNetworks() {
    if (!systemState.wifiEnabled && !apModeStarted) {
      Serial.println("WiFi is disabled");
      return;
    }
    
    Serial.println("Scanning WiFi networks...");
    
    int n = WiFi.scanNetworks();
    
    String json = "{\"type\":\"wifi_networks\",\"networks\":[";
    
    for (int i = 0; i < n; ++i) {
      if (i > 0) json += ",";
      json += "{";
      json += "\"ssid\":\"" + WiFi.SSID(i) + "\",";
      json += "\"rssi\":" + String(WiFi.RSSI(i)) + ",";
      
      String encryption;
      switch (WiFi.encryptionType(i)) {
        case WIFI_AUTH_OPEN: encryption = "OPEN"; break;
        case WIFI_AUTH_WEP: encryption = "WEP"; break;
        case WIFI_AUTH_WPA_PSK: encryption = "WPA/PSK"; break;
        case WIFI_AUTH_WPA2_PSK: encryption = "WPA2/PSK"; break;
        case WIFI_AUTH_WPA_WPA2_PSK: encryption = "WPA/WPA2/PSK"; break;
        default: encryption = "UNKNOWN"; break;
      }
      
      json += "\"encryption\":\"" + encryption + "\"";
      json += "}";
    }
    
    json += "]}";
    webSocket.broadcastTXT(json);
    WiFi.scanDelete();
  }
  
  static bool setCredentials(const char* ssid, const char* password) {
    if (strlen(ssid) == 0) {
      Serial.println("SSID cannot be empty");
      return false;
    }
    
    strncpy(calibration.wifiCredentials.ssid, ssid, sizeof(calibration.wifiCredentials.ssid) - 1);
    strncpy(calibration.wifiCredentials.password, password, sizeof(calibration.wifiCredentials.password) - 1);
    
    Serial.printf("WiFi credentials updated: %s\n", ssid);
    
    if (systemState.wifiEnabled) {
      disconnect();
      delay(1000);
      connect();
    }
    
    return true;
  }
  
  static void clearCredentials() {
    memset(calibration.wifiCredentials.ssid, 0, sizeof(calibration.wifiCredentials.ssid));
    memset(calibration.wifiCredentials.password, 0, sizeof(calibration.wifiCredentials.password));
    Serial.println("WiFi credentials cleared");
    if (systemState.wifiConnected) disconnect();
    if (calibration.wifi.apModeFallback) startAPMode();
  }
  
  static bool isAPMode() {
    return apModeStarted;
  }
};

bool WiFiManager::isConnecting = false;
int WiFiManager::connectionAttempts = 0;
unsigned long WiFiManager::lastReconnectAttempt = 0;
bool WiFiManager::apModeStarted = false;


// =============================================================================
// PREFERENCES MANAGER - MUST BE DEFINED BEFORE WIFICONFIGMENU
// =============================================================================

class PreferenceManager {
public:
  static bool saveCalibration() {
    preferences.begin(PREF_NAMESPACE, false);
    
    preferences.putUChar("version", calibration.version);
    preferences.putFloat("temp_offset", calibration.tempCalibration.offset);
    preferences.putFloat("temp_slope", calibration.tempCalibration.slope);
    preferences.putFloat("temp_ref", calibration.tempCalibration.reference);
    preferences.putBool("temp_use_ref", calibration.tempCalibration.useReference);
    
    preferences.putFloat("hum_offset", calibration.humidityCalibration.offset);
    preferences.putFloat("hum_slope", calibration.humidityCalibration.slope);
    preferences.putFloat("hum_ref", calibration.humidityCalibration.reference);
    preferences.putBool("hum_use_ref", calibration.humidityCalibration.useReference);
    
    preferences.putFloat("press_offset", calibration.pressureCalibration.offset);
    preferences.putFloat("press_slope", calibration.pressureCalibration.slope);
    preferences.putFloat("press_ref", calibration.pressureCalibration.reference);
    preferences.putBool("press_use_ref", calibration.pressureCalibration.useReference);
    preferences.putFloat("press_alt_corr", calibration.pressureCalibration.altitudeCorrection);
    preferences.putBool("press_direct", calibration.pressureCalibration.useDirectCalibration);
    preferences.putFloat("press_factor", calibration.pressureCalibration.calibrationFactor);
    
    preferences.putString("wifi_ssid", calibration.wifiCredentials.ssid);
    preferences.putString("wifi_pass", calibration.wifiCredentials.password);
    
    preferences.putBool("google_enabled", calibration.googleSheets.enabled);
    preferences.putString("google_url", calibration.googleSheets.scriptURL);
    preferences.putInt("google_interval", calibration.googleSheets.logInterval);
    preferences.putBool("google_log_sensors", calibration.googleSheets.logSensors);
    preferences.putBool("google_log_pumps", calibration.googleSheets.logPumps);
    preferences.putBool("google_log_events", calibration.googleSheets.logEvents);
    
    preferences.putBool("sd_enabled", calibration.sd.enabled);
    preferences.putInt("sd_log_interval", calibration.sd.logInterval);
    preferences.putBool("sd_log_raw", calibration.sd.logRawData);
    preferences.putBool("sd_log_cal", calibration.sd.logCalibratedData);
    preferences.putInt("sd_max_files", calibration.sd.maxFiles);
    preferences.putInt("sd_max_size", calibration.sd.maxFileSize);
    
    preferences.putBool("web_enabled", calibration.webserver.enabled);
    preferences.putBool("web_auth", calibration.webserver.requireAuth);
    preferences.putString("web_user", calibration.webserver.username);
    preferences.putString("web_pass", calibration.webserver.password);
    preferences.putInt("web_timeout", calibration.webserver.sessionTimeout);
    preferences.putBool("web_ws", calibration.webserver.enableWebSockets);
    preferences.putBool("web_live", calibration.webserver.enableLiveData);
    preferences.putInt("web_interval", calibration.webserver.updateInterval);
    preferences.putBool("web_graphs", calibration.webserver.enableGraphs);
    preferences.putInt("web_history", calibration.webserver.graphHistory);
    preferences.putBool("web_serial", calibration.webserver.enableSerialTerminal);
    preferences.putInt("web_serial_buf", calibration.webserver.serialBufferSize);
    
    preferences.putInt("sys_sensor_int", calibration.system.sensorReadInterval);
    preferences.putInt("sys_status_int", calibration.system.statusPrintInterval);
    preferences.putBool("sys_serial", calibration.system.enableSerialOutput);
    preferences.putBool("sys_web_serial", calibration.system.enableWebSerial);
    preferences.putBool("sys_leds", calibration.system.enableLEDs);
    preferences.putBool("sys_24hour", calibration.system.use24HourFormat);
    preferences.putBool("sys_auto", calibration.system.autoStart);
    preferences.putBool("sys_manual", calibration.system.manualMode);
    
    char key[32];
    snprintf(key, sizeof(key), "sandy_rawDry"); preferences.putInt(key, calibration.sandy.rawDry);
    snprintf(key, sizeof(key), "sandy_rawWet"); preferences.putInt(key, calibration.sandy.rawWet);
    snprintf(key, sizeof(key), "clay_rawDry"); preferences.putInt(key, calibration.clay.rawDry);
    snprintf(key, sizeof(key), "clay_rawWet"); preferences.putInt(key, calibration.clay.rawWet);
    snprintf(key, sizeof(key), "loamy_rawDry"); preferences.putInt(key, calibration.loamy.rawDry);
    snprintf(key, sizeof(key), "loamy_rawWet"); preferences.putInt(key, calibration.loamy.rawWet);
    
    preferences.putFloat("dht11_tempOffset", calibration.dht11.tempOffset);
    preferences.putFloat("dht11_humOffset", calibration.dht11.humOffset);
    preferences.putFloat("dht11_tempSlope", calibration.dht11.tempSlope);
    preferences.putFloat("dht11_humSlope", calibration.dht11.humSlope);
    preferences.putBool("dht11_enabled", calibration.dht11.enabled);
    
    preferences.putFloat("bmp180_pressOffset", calibration.bmp180.pressureOffset);
    preferences.putFloat("bmp180_tempOffset", calibration.bmp180.tempOffset);
    preferences.putFloat("bmp180_altOffset", calibration.bmp180.altitudeOffset);
    preferences.putFloat("bmp180_pressSlope", calibration.bmp180.pressureSlope);
    preferences.putFloat("bmp180_tempSlope", calibration.bmp180.tempSlope);
    preferences.putFloat("bmp180_seaLevel", calibration.bmp180.seaLevelPressure);
    preferences.putBool("bmp180_enabled", calibration.bmp180.enabled);
    
    preferences.putFloat("mpu_accelXOffset", calibration.mpu6050.accelXOffset);
    preferences.putFloat("mpu_accelYOffset", calibration.mpu6050.accelYOffset);
    preferences.putFloat("mpu_accelZOffset", calibration.mpu6050.accelZOffset);
    preferences.putFloat("mpu_gyroXOffset", calibration.mpu6050.gyroXOffset);
    preferences.putFloat("mpu_gyroYOffset", calibration.mpu6050.gyroYOffset);
    preferences.putFloat("mpu_gyroZOffset", calibration.mpu6050.gyroZOffset);
    preferences.putFloat("mpu_moveThresh", calibration.mpu6050.movementThreshold);
    preferences.putInt("mpu_tamperDelay", calibration.mpu6050.tamperAlertDelay);
    preferences.putBool("mpu_enabled", calibration.mpu6050.enabled);
    preferences.putBool("mpu_sendAlerts", calibration.mpu6050.sendAlerts);
    preferences.putBool("mpu_enableBuzzer", calibration.mpu6050.enableBuzzer);
    preferences.putBool("mpu_webAlerts", calibration.mpu6050.webAlerts);
    
    preferences.putBool("wifi_enabled", calibration.wifi.enabled);
    preferences.putBool("wifi_autoConnect", calibration.wifi.autoConnect);
    preferences.putInt("wifi_reconnect", calibration.wifi.reconnectInterval);
    preferences.putInt("wifi_maxRetries", calibration.wifi.maxRetries);
    preferences.putBool("wifi_dhcp", calibration.wifi.dhcpEnabled);
    preferences.putString("wifi_hostname", calibration.wifi.hostname);
    preferences.putBool("wifi_ap_fallback", calibration.wifi.apModeFallback);
    preferences.putString("wifi_ap_ssid", calibration.wifi.apSSID);
    preferences.putString("wifi_ap_password", calibration.wifi.apPassword);
    
    preferences.putBool("ntp_enabled", calibration.ntp.enabled);
    preferences.putInt("ntp_updateInt", calibration.ntp.updateInterval);
    preferences.putInt("ntp_timezone", calibration.ntp.timezoneOffset);
    preferences.putString("ntp_server", calibration.ntp.server);
    
    preferences.putBool("buzzer_enabled", calibration.buzzer.enabled);
    preferences.putInt("buzzer_alertDur", calibration.buzzer.alertDuration);
    preferences.putInt("buzzer_alertFreq", calibration.buzzer.alertFrequency);
    preferences.putInt("buzzer_beepDur", calibration.buzzer.beepDuration);
    
    preferences.putFloat("irr_sandyThresh", calibration.irrigation.sandyWaterThreshold);
    preferences.putFloat("irr_clayThresh", calibration.irrigation.clayWaterThreshold);
    preferences.putFloat("irr_loamyThresh", calibration.irrigation.loamyWaterThreshold);
    preferences.putFloat("irr_humThresh", calibration.irrigation.humidityThreshold);
    preferences.putFloat("irr_tempLow", calibration.irrigation.tempThresholdLow);
    preferences.putFloat("irr_tempHigh", calibration.irrigation.tempThresholdHigh);
    preferences.putFloat("irr_dewMargin", calibration.irrigation.dewPointMargin);
    preferences.putInt("irr_waterDur", calibration.irrigation.wateringDuration);
    preferences.putInt("irr_waterInt", calibration.irrigation.minWateringInterval);
    preferences.putInt("irr_pumpMax", calibration.irrigation.maxPumpRuntime);
    preferences.putBool("irr_checkRain", calibration.irrigation.checkRainCondition);
    preferences.putBool("irr_checkDew", calibration.irrigation.checkDewPoint);
    preferences.putBool("irr_checkTemp", calibration.irrigation.checkTemperature);
    preferences.putBool("irr_checkHum", calibration.irrigation.checkHumidity);
    
    // NEW: Telegram preferences
    preferences.putBool("telegram_enabled", calibration.telegram.enabled);
    preferences.putString("telegram_token", calibration.telegram.botToken);
    preferences.putString("telegram_chatid", calibration.telegram.chatId);
    preferences.putBool("telegram_alerts", calibration.telegram.notifyAlerts);
    preferences.putBool("telegram_movement", calibration.telegram.notifyMovement);
    preferences.putBool("telegram_rain", calibration.telegram.notifyRain);
    preferences.putBool("telegram_pump", calibration.telegram.notifyPumpStatus);
    preferences.putInt("telegram_interval", calibration.telegram.notifyInterval);
    
    preferences.putULong("savedAt", millis());
    preferences.end();
    
    Serial.println("Calibration saved to Preferences");
    return true;
  }
  
  static bool loadCalibration() {
    preferences.begin(PREF_NAMESPACE, true);
    
    uint8_t savedVersion = preferences.getUChar("version", 0);
    if (savedVersion == 0) {
      Serial.println("No saved calibration found");
      preferences.end();
      return false;
    }
    
    calibration.tempCalibration.offset = preferences.getFloat("temp_offset", calibration.tempCalibration.offset);
    calibration.tempCalibration.slope = preferences.getFloat("temp_slope", calibration.tempCalibration.slope);
    calibration.tempCalibration.reference = preferences.getFloat("temp_ref", calibration.tempCalibration.reference);
    calibration.tempCalibration.useReference = preferences.getBool("temp_use_ref", calibration.tempCalibration.useReference);
    
    calibration.humidityCalibration.offset = preferences.getFloat("hum_offset", calibration.humidityCalibration.offset);
    calibration.humidityCalibration.slope = preferences.getFloat("hum_slope", calibration.humidityCalibration.slope);
    calibration.humidityCalibration.reference = preferences.getFloat("hum_ref", calibration.humidityCalibration.reference);
    calibration.humidityCalibration.useReference = preferences.getBool("hum_use_ref", calibration.humidityCalibration.useReference);
    
    calibration.pressureCalibration.offset = preferences.getFloat("press_offset", calibration.pressureCalibration.offset);
    calibration.pressureCalibration.slope = preferences.getFloat("press_slope", calibration.pressureCalibration.slope);
    calibration.pressureCalibration.reference = preferences.getFloat("press_ref", calibration.pressureCalibration.reference);
    calibration.pressureCalibration.useReference = preferences.getBool("press_use_ref", calibration.pressureCalibration.useReference);
    calibration.pressureCalibration.altitudeCorrection = preferences.getFloat("press_alt_corr", calibration.pressureCalibration.altitudeCorrection);
    calibration.pressureCalibration.useDirectCalibration = preferences.getBool("press_direct", calibration.pressureCalibration.useDirectCalibration);
    calibration.pressureCalibration.calibrationFactor = preferences.getFloat("press_factor", calibration.pressureCalibration.calibrationFactor);
    
    String savedSSID = preferences.getString("wifi_ssid", "");
    String savedPass = preferences.getString("wifi_pass", "");
    if (savedSSID.length() > 0) strncpy(calibration.wifiCredentials.ssid, savedSSID.c_str(), sizeof(calibration.wifiCredentials.ssid) - 1);
    if (savedPass.length() > 0) strncpy(calibration.wifiCredentials.password, savedPass.c_str(), sizeof(calibration.wifiCredentials.password) - 1);
    
    calibration.googleSheets.enabled = preferences.getBool("google_enabled", calibration.googleSheets.enabled);
    String googleURL = preferences.getString("google_url", calibration.googleSheets.scriptURL);
    strncpy(calibration.googleSheets.scriptURL, googleURL.c_str(), sizeof(calibration.googleSheets.scriptURL) - 1);
    calibration.googleSheets.logInterval = preferences.getInt("google_interval", calibration.googleSheets.logInterval);
    calibration.googleSheets.logSensors = preferences.getBool("google_log_sensors", calibration.googleSheets.logSensors);
    calibration.googleSheets.logPumps = preferences.getBool("google_log_pumps", calibration.googleSheets.logPumps);
    calibration.googleSheets.logEvents = preferences.getBool("google_log_events", calibration.googleSheets.logEvents);
    
    calibration.sd.enabled = preferences.getBool("sd_enabled", calibration.sd.enabled);
    calibration.sd.logInterval = preferences.getInt("sd_log_interval", calibration.sd.logInterval);
    calibration.sd.logRawData = preferences.getBool("sd_log_raw", calibration.sd.logRawData);
    calibration.sd.logCalibratedData = preferences.getBool("sd_log_cal", calibration.sd.logCalibratedData);
    calibration.sd.maxFiles = preferences.getInt("sd_max_files", calibration.sd.maxFiles);
    calibration.sd.maxFileSize = preferences.getInt("sd_max_size", calibration.sd.maxFileSize);
    
    calibration.webserver.enabled = preferences.getBool("web_enabled", calibration.webserver.enabled);
    calibration.webserver.requireAuth = preferences.getBool("web_auth", calibration.webserver.requireAuth);
    String webUser = preferences.getString("web_user", calibration.webserver.username);
    String webPass = preferences.getString("web_pass", calibration.webserver.password);
    strncpy(calibration.webserver.username, webUser.c_str(), sizeof(calibration.webserver.username) - 1);
    strncpy(calibration.webserver.password, webPass.c_str(), sizeof(calibration.webserver.password) - 1);
    calibration.webserver.sessionTimeout = preferences.getInt("web_timeout", calibration.webserver.sessionTimeout);
    calibration.webserver.enableWebSockets = preferences.getBool("web_ws", calibration.webserver.enableWebSockets);
    calibration.webserver.enableLiveData = preferences.getBool("web_live", calibration.webserver.enableLiveData);
    calibration.webserver.updateInterval = preferences.getInt("web_interval", calibration.webserver.updateInterval);
    calibration.webserver.enableGraphs = preferences.getBool("web_graphs", calibration.webserver.enableGraphs);
    calibration.webserver.graphHistory = preferences.getInt("web_history", calibration.webserver.graphHistory);
    calibration.webserver.enableSerialTerminal = preferences.getBool("web_serial", calibration.webserver.enableSerialTerminal);
    calibration.webserver.serialBufferSize = preferences.getInt("web_serial_buf", calibration.webserver.serialBufferSize);
    
    calibration.system.sensorReadInterval = preferences.getInt("sys_sensor_int", calibration.system.sensorReadInterval);
    calibration.system.statusPrintInterval = preferences.getInt("sys_status_int", calibration.system.statusPrintInterval);
    calibration.system.enableSerialOutput = preferences.getBool("sys_serial", calibration.system.enableSerialOutput);
    calibration.system.enableWebSerial = preferences.getBool("sys_web_serial", calibration.system.enableWebSerial);
    calibration.system.enableLEDs = preferences.getBool("sys_leds", calibration.system.enableLEDs);
    calibration.system.use24HourFormat = preferences.getBool("sys_24hour", calibration.system.use24HourFormat);
    calibration.system.autoStart = preferences.getBool("sys_auto", calibration.system.autoStart);
    calibration.system.manualMode = preferences.getBool("sys_manual", calibration.system.manualMode);
    
    char key[32];
    snprintf(key, sizeof(key), "sandy_rawDry"); calibration.sandy.rawDry = preferences.getInt(key, calibration.sandy.rawDry);
    snprintf(key, sizeof(key), "sandy_rawWet"); calibration.sandy.rawWet = preferences.getInt(key, calibration.sandy.rawWet);
    snprintf(key, sizeof(key), "clay_rawDry"); calibration.clay.rawDry = preferences.getInt(key, calibration.clay.rawDry);
    snprintf(key, sizeof(key), "clay_rawWet"); calibration.clay.rawWet = preferences.getInt(key, calibration.clay.rawWet);
    snprintf(key, sizeof(key), "loamy_rawDry"); calibration.loamy.rawDry = preferences.getInt(key, calibration.loamy.rawDry);
    snprintf(key, sizeof(key), "loamy_rawWet"); calibration.loamy.rawWet = preferences.getInt(key, calibration.loamy.rawWet);
    
    calibration.dht11.tempOffset = preferences.getFloat("dht11_tempOffset", calibration.dht11.tempOffset);
    calibration.dht11.humOffset = preferences.getFloat("dht11_humOffset", calibration.dht11.humOffset);
    calibration.dht11.tempSlope = preferences.getFloat("dht11_tempSlope", calibration.dht11.tempSlope);
    calibration.dht11.humSlope = preferences.getFloat("dht11_humSlope", calibration.dht11.humSlope);
    calibration.dht11.enabled = preferences.getBool("dht11_enabled", calibration.dht11.enabled);
    
    calibration.bmp180.pressureOffset = preferences.getFloat("bmp180_pressOffset", calibration.bmp180.pressureOffset);
    calibration.bmp180.tempOffset = preferences.getFloat("bmp180_tempOffset", calibration.bmp180.tempOffset);
    calibration.bmp180.altitudeOffset = preferences.getFloat("bmp180_altOffset", calibration.bmp180.altitudeOffset);
    calibration.bmp180.pressureSlope = preferences.getFloat("bmp180_pressSlope", calibration.bmp180.pressureSlope);
    calibration.bmp180.tempSlope = preferences.getFloat("bmp180_tempSlope", calibration.bmp180.tempSlope);
    calibration.bmp180.seaLevelPressure = preferences.getFloat("bmp180_seaLevel", calibration.bmp180.seaLevelPressure);
    calibration.bmp180.enabled = preferences.getBool("bmp180_enabled", calibration.bmp180.enabled);
    
    calibration.mpu6050.accelXOffset = preferences.getFloat("mpu_accelXOffset", calibration.mpu6050.accelXOffset);
    calibration.mpu6050.accelYOffset = preferences.getFloat("mpu_accelYOffset", calibration.mpu6050.accelYOffset);
    calibration.mpu6050.accelZOffset = preferences.getFloat("mpu_accelZOffset", calibration.mpu6050.accelZOffset);
    calibration.mpu6050.gyroXOffset = preferences.getFloat("mpu_gyroXOffset", calibration.mpu6050.gyroXOffset);
    calibration.mpu6050.gyroYOffset = preferences.getFloat("mpu_gyroYOffset", calibration.mpu6050.gyroYOffset);
    calibration.mpu6050.gyroZOffset = preferences.getFloat("mpu_gyroZOffset", calibration.mpu6050.gyroZOffset);
    calibration.mpu6050.movementThreshold = preferences.getFloat("mpu_moveThresh", calibration.mpu6050.movementThreshold);
    calibration.mpu6050.tamperAlertDelay = preferences.getInt("mpu_tamperDelay", calibration.mpu6050.tamperAlertDelay);
    calibration.mpu6050.enabled = preferences.getBool("mpu_enabled", calibration.mpu6050.enabled);
    calibration.mpu6050.sendAlerts = preferences.getBool("mpu_sendAlerts", calibration.mpu6050.sendAlerts);
    calibration.mpu6050.enableBuzzer = preferences.getBool("mpu_enableBuzzer", calibration.mpu6050.enableBuzzer);
    calibration.mpu6050.webAlerts = preferences.getBool("mpu_webAlerts", calibration.mpu6050.webAlerts);
    
    calibration.wifi.enabled = preferences.getBool("wifi_enabled", calibration.wifi.enabled);
    calibration.wifi.autoConnect = preferences.getBool("wifi_autoConnect", calibration.wifi.autoConnect);
    calibration.wifi.reconnectInterval = preferences.getInt("wifi_reconnect", calibration.wifi.reconnectInterval);
    calibration.wifi.maxRetries = preferences.getInt("wifi_maxRetries", calibration.wifi.maxRetries);
    calibration.wifi.dhcpEnabled = preferences.getBool("wifi_dhcp", calibration.wifi.dhcpEnabled);
    String hostname = preferences.getString("wifi_hostname", calibration.wifi.hostname);
    strncpy(calibration.wifi.hostname, hostname.c_str(), sizeof(calibration.wifi.hostname) - 1);
    calibration.wifi.apModeFallback = preferences.getBool("wifi_ap_fallback", calibration.wifi.apModeFallback);
    String apSSID = preferences.getString("wifi_ap_ssid", calibration.wifi.apSSID);
    String apPassword = preferences.getString("wifi_ap_password", calibration.wifi.apPassword);
    strncpy(calibration.wifi.apSSID, apSSID.c_str(), sizeof(calibration.wifi.apSSID) - 1);
    strncpy(calibration.wifi.apPassword, apPassword.c_str(), sizeof(calibration.wifi.apPassword) - 1);
    
    calibration.ntp.enabled = preferences.getBool("ntp_enabled", calibration.ntp.enabled);
    calibration.ntp.updateInterval = preferences.getInt("ntp_updateInt", calibration.ntp.updateInterval);
    calibration.ntp.timezoneOffset = preferences.getInt("ntp_timezone", calibration.ntp.timezoneOffset);
    String ntpServer = preferences.getString("ntp_server", calibration.ntp.server);
    strncpy(calibration.ntp.server, ntpServer.c_str(), sizeof(calibration.ntp.server) - 1);
    
    calibration.buzzer.enabled = preferences.getBool("buzzer_enabled", calibration.buzzer.enabled);
    calibration.buzzer.alertDuration = preferences.getInt("buzzer_alertDur", calibration.buzzer.alertDuration);
    calibration.buzzer.alertFrequency = preferences.getInt("buzzer_alertFreq", calibration.buzzer.alertFrequency);
    calibration.buzzer.beepDuration = preferences.getInt("buzzer_beepDur", calibration.buzzer.beepDuration);
    
    calibration.irrigation.sandyWaterThreshold = preferences.getFloat("irr_sandyThresh", calibration.irrigation.sandyWaterThreshold);
    calibration.irrigation.clayWaterThreshold = preferences.getFloat("irr_clayThresh", calibration.irrigation.clayWaterThreshold);
    calibration.irrigation.loamyWaterThreshold = preferences.getFloat("irr_loamyThresh", calibration.irrigation.loamyWaterThreshold);
    calibration.irrigation.humidityThreshold = preferences.getFloat("irr_humThresh", calibration.irrigation.humidityThreshold);
    calibration.irrigation.tempThresholdLow = preferences.getFloat("irr_tempLow", calibration.irrigation.tempThresholdLow);
    calibration.irrigation.tempThresholdHigh = preferences.getFloat("irr_tempHigh", calibration.irrigation.tempThresholdHigh);
    calibration.irrigation.dewPointMargin = preferences.getFloat("irr_dewMargin", calibration.irrigation.dewPointMargin);
    calibration.irrigation.wateringDuration = preferences.getInt("irr_waterDur", calibration.irrigation.wateringDuration);
    calibration.irrigation.minWateringInterval = preferences.getInt("irr_waterInt", calibration.irrigation.minWateringInterval);
    calibration.irrigation.maxPumpRuntime = preferences.getInt("irr_pumpMax", calibration.irrigation.maxPumpRuntime);
    calibration.irrigation.checkRainCondition = preferences.getBool("irr_checkRain", calibration.irrigation.checkRainCondition);
    calibration.irrigation.checkDewPoint = preferences.getBool("irr_checkDew", calibration.irrigation.checkDewPoint);
    calibration.irrigation.checkTemperature = preferences.getBool("irr_checkTemp", calibration.irrigation.checkTemperature);
    calibration.irrigation.checkHumidity = preferences.getBool("irr_checkHum", calibration.irrigation.checkHumidity);
    
    // NEW: Load Telegram preferences
    calibration.telegram.enabled = preferences.getBool("telegram_enabled", calibration.telegram.enabled);
    String botToken = preferences.getString("telegram_token", "");
    String chatId = preferences.getString("telegram_chatid", "");
    strncpy(calibration.telegram.botToken, botToken.c_str(), sizeof(calibration.telegram.botToken) - 1);
    strncpy(calibration.telegram.chatId, chatId.c_str(), sizeof(calibration.telegram.chatId) - 1);
    calibration.telegram.notifyAlerts = preferences.getBool("telegram_alerts", calibration.telegram.notifyAlerts);
    calibration.telegram.notifyMovement = preferences.getBool("telegram_movement", calibration.telegram.notifyMovement);
    calibration.telegram.notifyRain = preferences.getBool("telegram_rain", calibration.telegram.notifyRain);
    calibration.telegram.notifyPumpStatus = preferences.getBool("telegram_pump", calibration.telegram.notifyPumpStatus);
    calibration.telegram.notifyInterval = preferences.getInt("telegram_interval", calibration.telegram.notifyInterval);
    
    preferences.end();
    
    Serial.println("Calibration loaded from Preferences");
    
    systemState.securityEnabled = calibration.mpu6050.enabled;
    systemState.wifiEnabled = calibration.wifi.enabled;
    systemState.buzzerEnabled = calibration.buzzer.enabled;
    systemState.googleSheetsEnabled = calibration.googleSheets.enabled;
    systemState.sdEnabled = calibration.sd.enabled;
    systemState.calibrationLoaded = true;
    systemState.manualMode = calibration.system.manualMode;
    systemState.webSerialEnabled = calibration.system.enableWebSerial;
    
    return true;
  }
  
  static void deleteCalibration() {
    preferences.begin(PREF_NAMESPACE, false);
    preferences.clear();
    preferences.end();
    Serial.println("Calibration deleted from Preferences");
  }
};

// =============================================================================
// WIFI CONFIGURATION THROUGH SERIAL MONITOR
// =============================================================================

class WiFiConfigMenu {
private:
  static bool isConfiguring;
  static int configStep;
  static String newSSID;
  static String newPassword;
  
public:
  static void startConfiguration() {
    isConfiguring = true;
    configStep = 0;
    newSSID = "";
    newPassword = "";
    
    Serial.println("\n╔══════════════════════════════════════════════════════════╗");
    Serial.println("║           WiFi CONFIGURATION MODE                       ║");
    Serial.println("╚══════════════════════════════════════════════════════════╝");
    Serial.println("\nYou are about to configure WiFi for your SOIBIS");
    Serial.println("Press ENTER to start configuration, or type 'CANCEL' to abort\n");
  }
  
  static void processInput(String input) {
    input.trim();
    
    if (configStep == 0) {
      if (input == "CANCEL" || input == "cancel") {
        Serial.println("\nWiFi configuration cancelled.");
        isConfiguring = false;
        return;
      }
      
      Serial.println("\n WiFi Configuration Started");
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println("Enter your WiFi Network Name (SSID):");
      Serial.print("> ");
      configStep = 1;
    }
    else if (configStep == 1) {
      if (input.length() == 0) {
        Serial.println(" SSID cannot be empty. Please enter a valid SSID:");
        Serial.print("> ");
        return;
      }
      
      newSSID = input;
      Serial.println("\n✓ SSID saved: " + newSSID);
      Serial.println("\nEnter your WiFi Password:");
      Serial.println("(Press ENTER if no password)");
      Serial.print("> ");
      configStep = 2;
    }
    else if (configStep == 2) {
      newPassword = input;
      
      Serial.println("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println(" Configuration Summary:");
      Serial.println("   SSID     : " + newSSID);
      if (newPassword.length() > 0) {
        Serial.println("   Password : " + String(newPassword.length()) + " characters");
      } else {
        Serial.println("   Password : (none)");
      }
      Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
      Serial.println("\nDo you want to save and connect? (YES/NO)");
      Serial.print("> ");
      configStep = 3;
    }
    else if (configStep == 3) {
      if (input == "YES" || input == "yes" || input == "Y" || input == "y") {
        Serial.println("\n Saving configuration...");
        
        strncpy(calibration.wifiCredentials.ssid, newSSID.c_str(), sizeof(calibration.wifiCredentials.ssid) - 1);
        strncpy(calibration.wifiCredentials.password, newPassword.c_str(), sizeof(calibration.wifiCredentials.password) - 1);
        
        PreferenceManager::saveCalibration();
        
        Serial.println("✓ Configuration saved to flash memory");
        Serial.println("\n Attempting to connect to WiFi...");
        
        if (WiFiManager::isAPMode()) {
          WiFiManager::stopAPMode();
        }
        
        WiFiManager::disconnect();
        delay(1000);
        WiFiManager::connect();
        
        int timeout = 30;
        while (!systemState.wifiConnected && timeout > 0) {
          delay(1000);
          WiFiManager::update();
          timeout--;
          Serial.print(".");
        }
        
        if (systemState.wifiConnected) {
          Serial.println("\n WiFi Connected Successfully!");
          Serial.println("   IP Address: " + WiFi.localIP().toString());
          Serial.println("   Signal Strength: " + String(WiFi.RSSI()) + " dBm");
          BuzzerManager::playSuccessTone();
        } else {
          Serial.println("\n Failed to connect to WiFi");
          Serial.println("   Please check your credentials and try again");
          Serial.println("   Starting AP Mode for configuration...");
          WiFiManager::startAPMode();
          BuzzerManager::playErrorTone();
        }
        
        isConfiguring = false;
      } 
      else if (input == "NO" || input == "no" || input == "N" || input == "n") {
        Serial.println("\n Configuration cancelled. No changes saved.");
        if (strlen(calibration.wifiCredentials.ssid) == 0) {
          WiFiManager::startAPMode();
        }
        Serial.println("Type 'WIFI CONFIG' to try again");
        isConfiguring = false;
      } 
      else {
        Serial.println("Please answer YES or NO");
        Serial.print("> ");
      }
    }
  }
  
  static void showWiFiStatus() {
    Serial.println("\n╔══════════════════════════════════════════════════════════╗");
    Serial.println("║                    WiFi STATUS                          ║");
    Serial.println("╚══════════════════════════════════════════════════════════╝");
    
    if (systemState.wifiConnected) {
      Serial.println(" WiFi Status: CONNECTED");
      Serial.println("   Mode        : STA (Client)");
      Serial.println("   SSID        : " + WiFi.SSID());
      Serial.println("   IP Address  : " + WiFi.localIP().toString());
      Serial.println("   MAC Address : " + WiFi.macAddress());
      Serial.println("   Signal      : " + String(WiFi.RSSI()) + " dBm");
      Serial.println("   Channel     : " + String(WiFi.channel()));
    } else if (systemState.apModeActive) {
      Serial.println("   WiFi Status: ACCESS POINT MODE");
      Serial.println("   SSID        : " + String(calibration.wifi.apSSID));
      Serial.println("   Password    : " + String(calibration.wifi.apPassword));
      Serial.println("   IP Address  : " + WiFi.softAPIP().toString());
      Serial.println("   Connected   : " + String(WiFi.softAPgetStationNum()) + " clients");
    } else {
      Serial.println("   WiFi Status: DISCONNECTED");
      
      if (strlen(calibration.wifiCredentials.ssid) > 0) {
        Serial.println("   Configured SSID: " + String(calibration.wifiCredentials.ssid));
      } else {
        Serial.println("   No WiFi credentials configured");
      }
    }
    
    Serial.println("\nCommands:");
    Serial.println("   WIFI CONFIG  - Configure new WiFi connection");
    Serial.println("   WIFI SCAN    - Scan for WiFi networks");
    Serial.println("   WIFI ON      - Enable WiFi");
    Serial.println("   WIFI OFF     - Disable WiFi");
    Serial.println("   WIFI RESET   - Clear saved credentials");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
  }
  
  static void scanNetworks() {
    Serial.println("\n Scanning for WiFi networks...");
    WiFi.scanNetworks(true);
    delay(2000);
    
    int n = WiFi.scanComplete();
    if (n <= 0) {
      Serial.println("   No networks found");
      return;
    }
    
    Serial.println("\n Available WiFi Networks:");
    Serial.println("   ┌────┬────────────────────────────────┬─────────────┐");
    Serial.println("   │ No │ SSID                           │ Signal      │");
    Serial.println("   ├────┼────────────────────────────────┼─────────────┤");
    
    for (int i = 0; i < n && i < 20; i++) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
      String signal;
      if (rssi > -50) signal = "Excellent";
      else if (rssi > -60) signal = "Good";
      else if (rssi > -70) signal = "Fair";
      else signal = "Weak";
      
      Serial.printf("   │ %2d │ %-30s │ %-11s │\n", 
                    i + 1, 
                    ssid.length() > 30 ? ssid.substring(0, 27) + "..." : ssid,
                    signal.c_str());
    }
    Serial.println("   └────┴────────────────────────────────┴─────────────┘\n");
    
    WiFi.scanDelete();
  }
  
  static void enableWiFi() {
    if (!calibration.wifi.enabled) {
      calibration.wifi.enabled = true;
      systemState.wifiEnabled = true;
      PreferenceManager::saveCalibration();
      if (WiFiManager::isAPMode()) {
        WiFiManager::stopAPMode();
      }
      WiFiManager::connect();
      Serial.println(" WiFi enabled. Attempting to connect...");
    } else {
      Serial.println("WiFi is already enabled");
    }
  }
  
  static void disableWiFi() {
    if (calibration.wifi.enabled) {
      calibration.wifi.enabled = false;
      systemState.wifiEnabled = false;
      PreferenceManager::saveCalibration();
      WiFiManager::disconnect();
      WiFiManager::stopAPMode();
      Serial.println(" WiFi disabled");
    } else {
      Serial.println("WiFi is already disabled");
    }
  }
  
  static void resetCredentials() {
    Serial.println("\n⚠️  WARNING: This will erase all saved WiFi credentials!");
    Serial.println("   Do you want to continue? (YES/NO)");
    Serial.print("> ");
    
    unsigned long startTime = millis();
    String response = "";
    while (millis() - startTime < 10000) {
      if (Serial.available()) {
        response = Serial.readStringUntil('\n');
        response.trim();
        response.toUpperCase();
        break;
      }
      delay(100);
    }
    
    if (response == "YES" || response == "Y") {
      memset(calibration.wifiCredentials.ssid, 0, sizeof(calibration.wifiCredentials.ssid));
      memset(calibration.wifiCredentials.password, 0, sizeof(calibration.wifiCredentials.password));
      PreferenceManager::saveCalibration();
      WiFiManager::disconnect();
      Serial.println(" WiFi credentials cleared");
      if (calibration.wifi.apModeFallback) {
        WiFiManager::startAPMode();
      }
      Serial.println("   Type 'WIFI CONFIG' to set up new credentials");
    } else {
      Serial.println(" Operation cancelled");
    }
  }
  
  static bool isActive() {
    return isConfiguring;
  }
  
  static void update() {
    if (isConfiguring && Serial.available()) {
      String input = Serial.readStringUntil('\n');
      processInput(input);
    }
  }
};

// Initialize static members
bool WiFiConfigMenu::isConfiguring = false;
int WiFiConfigMenu::configStep = 0;
String WiFiConfigMenu::newSSID = "";
String WiFiConfigMenu::newPassword = "";

// =============================================================================
// COMPLETE SENSOR MANAGER CLASS WITH ALL SOIL METHODS
// =============================================================================

class SensorManager {
private:
  static BMP180 bmp180;
  static MPU6050_Sensor mpu6050;
  
  static int readMoistureSensor(int pin, int samples = 10) {
    long sum = 0;
    for (int i = 0; i < samples; i++) {
      sum += analogRead(pin);
      delay(10);
    }
    return sum / samples;
  }
  
  static float mapToPercent(int reading, int rawDry, int rawWet) {
    reading = constrain(reading, 0, 4095);
    if (rawDry <= rawWet) {
      rawDry = 3000;
      rawWet = 1500;
    }
    if (reading >= rawDry) return 0.0;
    if (reading <= rawWet) return 100.0;
    float percent = 100.0 * (rawDry - reading) / (rawDry - rawWet);
    return constrain(percent, 0.0, 100.0);
  }
  
public:
  static bool initializeAll() {
    dht.begin();
    delay(2000);
    analogReadResolution(12);
    
    if (!bmp180.begin()) {
      calibration.bmp180.enabled = false;
      Serial.println("BMP180 not found!");
    } else {
      Serial.println("BMP180 initialized");
    }
    
    if (!mpu6050.begin()) {
      systemState.securityEnabled = false;
      calibration.mpu6050.enabled = false;
      Serial.println("MPU6050 not found!");
    } else {
      systemState.securityEnabled = calibration.mpu6050.enabled;
      Serial.println("MPU6050 initialized");
      mpu6050.setBaseline();
    }
    
    return true;
  }
  
  static SensorData readAllSensors() {
    SensorData data;
    
    // Initialize with NAN values
    data.rawTemperature = NAN;
    data.rawHumidity = NAN;
    data.rawPressure = NAN;
    data.rawAltitude = NAN;
    data.temperature = NAN;
    data.humidity = NAN;
    data.pressure = NAN;
    data.altitude = NAN;
    data.dewPoint = NAN;
    data.isRaining = false;
    data.deviceMoved = false;
    data.accelerationX = 0;
    data.accelerationY = 0;
    data.accelerationZ = 0;
    data.gyroX = 0;
    data.gyroY = 0;
    data.gyroZ = 0;
    data.movementIntensity = 0;
    
    // Read soil moisture sensors with raw values
    data.rawSandyMoisture = readMoistureSensor(PIN_SANDY_MOISTURE);
    data.rawClayMoisture = readMoistureSensor(PIN_CLAY_MOISTURE);
    data.rawLoamyMoisture = readMoistureSensor(PIN_LOAMY_MOISTURE);
    
    data.sandyMoisture = data.rawSandyMoisture;
    data.clayMoisture = data.rawClayMoisture;
    data.loamyMoisture = data.rawLoamyMoisture;
    
    // Calculate percentages
    data.sandyPercent = mapToPercent(data.sandyMoisture, calibration.sandy.rawDry, calibration.sandy.rawWet);
    data.clayPercent = mapToPercent(data.clayMoisture, calibration.clay.rawDry, calibration.clay.rawWet);
    data.loamyPercent = mapToPercent(data.loamyMoisture, calibration.loamy.rawDry, calibration.loamy.rawWet);
    
    // DHT11 Sensor
    if (calibration.dht11.enabled) {
      float hum = dht.readHumidity();
      float temp = dht.readTemperature();
      
      int attempts = 0;
      while ((isnan(hum) || isnan(temp)) && attempts < 5) {
        delay(100);
        hum = dht.readHumidity();
        temp = dht.readTemperature();
        attempts++;
      }
      
      data.rawHumidity = hum;
      data.rawTemperature = temp;
      
      if (!isnan(data.rawTemperature)) {
        data.temperature = (data.rawTemperature * calibration.tempCalibration.slope) + calibration.tempCalibration.offset;
      }
      
      if (!isnan(data.rawHumidity)) {
        data.humidity = (data.rawHumidity * calibration.humidityCalibration.slope) + calibration.humidityCalibration.offset;
        data.humidity = constrain(data.humidity, 0, 100);
      }
    }
    
    // BMP180 Sensor
    if (calibration.bmp180.enabled) {
      data.rawPressure = bmp180.readRawPressure();
      data.rawAltitude = bmp180.readRawAltitude(calibration.bmp180.seaLevelPressure);
      data.pressure = bmp180.readCalibratedPressure();
      data.altitude = bmp180.readCalibratedAltitude();
      
      if (!calibration.dht11.enabled || isnan(data.temperature)) {
        data.rawTemperature = bmp180.readRawTemperature();
        if (!isnan(data.rawTemperature)) {
          data.temperature = (data.rawTemperature * calibration.tempCalibration.slope) + calibration.tempCalibration.offset;
        }
      }
    }
    
    // MPU6050 Sensor
    if (systemState.securityEnabled && calibration.mpu6050.enabled) {
      if (mpu6050.readData()) {
        data.accelerationX = mpu6050.getAccelerationX();
        data.accelerationY = mpu6050.getAccelerationY();
        data.accelerationZ = mpu6050.getAccelerationZ();
        data.gyroX = mpu6050.getGyroX();
        data.gyroY = mpu6050.getGyroY();
        data.gyroZ = mpu6050.getGyroZ();
        data.deviceMoved = mpu6050.isMoving();
        data.movementIntensity = mpu6050.getMovementIntensity();
      }
    }
    
    if (!isnan(data.temperature) && !isnan(data.humidity)) {
      data.dewPoint = calculateDewPoint(data.temperature, data.humidity);
      if (!isnan(data.dewPoint) && !isnan(data.pressure)) {
        data.isRaining = checkRainCondition(data.temperature, data.humidity, data.pressure);
      }
    }
    
    data.timestamp = getTimestamp();
    data.dateTime = getDateTime();
    return data;
  }
  
  // ========== SOIL CALIBRATION FUNCTIONS ==========
  static void autoCalibrateDry(String soilType) {
    int pin = 0;
    AdvancedCalibration::SoilCalibration* cal = nullptr;
    String soilName;
    
    if (soilType == "sandy") {
      pin = PIN_SANDY_MOISTURE;
      cal = &calibration.sandy;
      soilName = "Sandy";
    } else if (soilType == "clay") {
      pin = PIN_CLAY_MOISTURE;
      cal = &calibration.clay;
      soilName = "Clay";
    } else if (soilType == "loamy") {
      pin = PIN_LOAMY_MOISTURE;
      cal = &calibration.loamy;
      soilName = "Loamy";
    }
    
    if (cal != nullptr) {
      int value = readMoistureSensor(pin, 20);
      cal->rawDry = value;
      
      Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
      Serial.printf("║     %s SOIL - AUTO DRY CALIBRATION COMPLETE               ║\n", soilName.c_str());
      Serial.println("╚══════════════════════════════════════════════════════════════╝");
      Serial.printf("    Raw value captured: %d\n", value);
      Serial.println("    This value will be used as 0% (completely dry)");
      
      addToSerialBuffer(soilName + " auto dry calibration: " + String(value));
    }
  }
  
  static void autoCalibrateWet(String soilType) {
    int pin = 0;
    AdvancedCalibration::SoilCalibration* cal = nullptr;
    String soilName;
    
    if (soilType == "sandy") {
      pin = PIN_SANDY_MOISTURE;
      cal = &calibration.sandy;
      soilName = "Sandy";
    } else if (soilType == "clay") {
      pin = PIN_CLAY_MOISTURE;
      cal = &calibration.clay;
      soilName = "Clay";
    } else if (soilType == "loamy") {
      pin = PIN_LOAMY_MOISTURE;
      cal = &calibration.loamy;
      soilName = "Loamy";
    }
    
    if (cal != nullptr) {
      int value = readMoistureSensor(pin, 20);
      cal->rawWet = value;
      
      Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
      Serial.printf("║     %s SOIL - AUTO WET CALIBRATION COMPLETE               ║\n", soilName.c_str());
      Serial.println("╚══════════════════════════════════════════════════════════════╝");
      Serial.printf("    Raw value captured: %d\n", value);
      Serial.println("    This value will be used as 100% (completely wet)");
      
      if (cal->rawDry <= cal->rawWet) {
        Serial.println("\n  WARNING: Dry value is NOT greater than Wet value!");
        Serial.println("   Please re-calibrate: DRY first, then WET");
      } else {
        Serial.println("\n Calibration successful!");
      }
      
      addToSerialBuffer(soilName + " auto wet calibration: " + String(value));
    }
  }
  
  static void resetSoilCalibration(String soilType) {
    AdvancedCalibration::SoilCalibration* cal = nullptr;
    String soilName;
    
    if (soilType == "sandy") {
      cal = &calibration.sandy;
      soilName = "Sandy";
    } else if (soilType == "clay") {
      cal = &calibration.clay;
      soilName = "Clay";
    } else if (soilType == "loamy") {
      cal = &calibration.loamy;
      soilName = "Loamy";
    }
    
    if (cal != nullptr) {
      cal->rawDry = 3000;
      cal->rawWet = 1500;
      cal->offset = 0;
      cal->slope = 1.0;
      
      Serial.printf("\n %s soil calibration reset to defaults (Dry: %d, Wet: %d)\n", 
                    soilName.c_str(), cal->rawDry, cal->rawWet);
      addToSerialBuffer(soilName + " soil calibration reset");
    }
  }
  
  static void setSoilDryValue(String soilType, int rawValue) {
    AdvancedCalibration::SoilCalibration* cal = nullptr;
    String soilName;
    
    if (soilType == "sandy") {
      cal = &calibration.sandy;
      soilName = "Sandy";
    } else if (soilType == "clay") {
      cal = &calibration.clay;
      soilName = "Clay";
    } else if (soilType == "loamy") {
      cal = &calibration.loamy;
      soilName = "Loamy";
    }
    
    if (cal != nullptr) {
      cal->rawDry = rawValue;
      
      Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
      Serial.printf("║     %s SOIL - MANUAL DRY VALUE SET                          ║\n", soilName.c_str());
      Serial.println("╚══════════════════════════════════════════════════════════════╝");
      Serial.printf("    Dry value set to: %d (0%% moisture)\n", rawValue);
      
      addToSerialBuffer(soilName + " soil dry value set to: " + String(rawValue));
    }
  }
  
  static void setSoilWetValue(String soilType, int rawValue) {
    AdvancedCalibration::SoilCalibration* cal = nullptr;
    String soilName;
    
    if (soilType == "sandy") {
      cal = &calibration.sandy;
      soilName = "Sandy";
    } else if (soilType == "clay") {
      cal = &calibration.clay;
      soilName = "Clay";
    } else if (soilType == "loamy") {
      cal = &calibration.loamy;
      soilName = "Loamy";
    }
    
    if (cal != nullptr) {
      cal->rawWet = rawValue;
      
      Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
      Serial.printf("║     %s SOIL - MANUAL WET VALUE SET                          ║\n", soilName.c_str());
      Serial.println("╚══════════════════════════════════════════════════════════════╝");
      Serial.printf("    Wet value set to: %d (100%% moisture)\n", rawValue);
      
      if (cal->rawDry <= cal->rawWet) {
        Serial.println("\n  WARNING: Dry value should be HIGHER than Wet value!");
      } else {
        Serial.println("\n Valid range!");
      }
      
      addToSerialBuffer(soilName + " soil wet value set to: " + String(rawValue));
    }
  }
  
  static void interactiveSoilCalibration() {
    Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
    Serial.println("║                 SOIL CALIBRATION MENU                           ║");
    Serial.println("╚══════════════════════════════════════════════════════════════════╝");
    
    // Read current raw values
    int sandyRaw = analogRead(PIN_SANDY_MOISTURE);
    int clayRaw = analogRead(PIN_CLAY_MOISTURE);
    int loamyRaw = analogRead(PIN_LOAMY_MOISTURE);
    
    float sandyPct = mapToPercent(sandyRaw, calibration.sandy.rawDry, calibration.sandy.rawWet);
    float clayPct = mapToPercent(clayRaw, calibration.clay.rawDry, calibration.clay.rawWet);
    float loamyPct = mapToPercent(loamyRaw, calibration.loamy.rawDry, calibration.loamy.rawWet);
    
    Serial.println("\n CURRENT SOIL READINGS:");
    Serial.println("┌────────────┬──────────┬────────────┬────────────┬─────────────┐");
    Serial.println("│ Sensor     │ Raw Now  │ Dry Cal    │ Wet Cal    │ Current %   │");
    Serial.println("├────────────┼──────────┼────────────┼────────────┼─────────────┤");
    Serial.printf("│ Sandy      │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  sandyRaw, calibration.sandy.rawDry, calibration.sandy.rawWet, sandyPct);
    Serial.printf("│ Clay       │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  clayRaw, calibration.clay.rawDry, calibration.clay.rawWet, clayPct);
    Serial.printf("│ Loamy      │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  loamyRaw, calibration.loamy.rawDry, calibration.loamy.rawWet, loamyPct);
    Serial.println("└────────────┴──────────┴────────────┴────────────┴─────────────┘");
    
    Serial.println("\n CALIBRATION METHODS:");
    Serial.println("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    Serial.println("\nMETHOD 1 - Auto Calibration (Recommended):");
    Serial.println("  1. Make sure soil is COMPLETELY DRY");
    Serial.println("  2. Type: CAL SANDY AUTODRY, CAL CLAY AUTODRY, CAL LOAMY AUTODRY");
    Serial.println("  3. Then water soil thoroughly and wait 10-15 minutes");
    Serial.println("  4. Type: CAL SANDY AUTOWET, CAL CLAY AUTOWET, CAL LOAMY AUTOWET");
    
    Serial.println("\nMETHOD 2 - Manual Value Entry:");
    Serial.println("  Set DRY value (when soil is dry, raw value is HIGH):");
    Serial.println("    CAL SANDY SETDRY 3000");
    Serial.println("    CAL CLAY SETDRY 2800");
    Serial.println("    CAL LOAMY SETDRY 3200");
    Serial.println("\n  Set WET value (when soil is wet, raw value is LOW):");
    Serial.println("    CAL SANDY SETWET 800");
    Serial.println("    CAL CLAY SETWET 900");
    Serial.println("    CAL LOAMY SETWET 750");
    
    Serial.println("\n TYPICAL RAW VALUE RANGES:");
    Serial.println("   • Completely Dry:   2500 - 3500  (Set as DRY value)");
    Serial.println("   • Slightly Moist:   1500 - 2500");
    Serial.println("   • Very Wet:         500 - 1500");
    Serial.println("   • In Water:         0 - 500      (Set as WET value)");
    
    Serial.println("\n TIPS:");
    Serial.println("   • For capacitive sensors: Dry = HIGH value, Wet = LOW value");
    Serial.println("   • Take multiple readings and average them");
    Serial.println("   • Always run SAVE after calibration");
    Serial.println("   • Use SOIL TEST to see live readings");
    Serial.println("\n╔══════════════════════════════════════════════════════════════════╗\n");
  }
  
  // ========== ENVIRONMENTAL CALIBRATION ==========
  static void calibrateTemperature(float referenceValue) {
    calibration.tempCalibration.reference = referenceValue;
    calibration.tempCalibration.useReference = true;
    SensorData data = readAllSensors();
    if (!isnan(data.rawTemperature)) {
      calibration.tempCalibration.offset = referenceValue - data.rawTemperature;
      Serial.printf("Temperature calibrated: Raw=%.1f°C, Ref=%.1f°C, Offset=%.1f°C\n",
                    data.rawTemperature, referenceValue, calibration.tempCalibration.offset);
      addToSerialBuffer("Temperature calibrated: Offset=" + String(calibration.tempCalibration.offset, 1) + "°C");
    }
  }
  
  static void calibrateHumidity(float referenceValue) {
    calibration.humidityCalibration.reference = referenceValue;
    calibration.humidityCalibration.useReference = true;
    SensorData data = readAllSensors();
    if (!isnan(data.rawHumidity)) {
      calibration.humidityCalibration.offset = referenceValue - data.rawHumidity;
      Serial.printf("Humidity calibrated: Raw=%.1f%%, Ref=%.1f%%, Offset=%.1f%%\n",
                    data.rawHumidity, referenceValue, calibration.humidityCalibration.offset);
      addToSerialBuffer("Humidity calibrated: Offset=" + String(calibration.humidityCalibration.offset, 1) + "%");
    }
  }
  
  static void calibratePressureDirect(float referencePressure) {
    SensorData data = readAllSensors();
    if (!isnan(data.rawPressure) && data.rawPressure > 0) {
      calibration.pressureCalibration.calibrationFactor = referencePressure / data.rawPressure;
      calibration.pressureCalibration.useDirectCalibration = true;
      calibration.pressureCalibration.slope = 1.0;
      calibration.pressureCalibration.offset = 0.0;
      Serial.printf("Pressure calibrated directly: Raw=%.1f hPa, Ref=%.1f hPa, Factor=%.3f\n",
                    data.rawPressure, referencePressure, calibration.pressureCalibration.calibrationFactor);
      addToSerialBuffer("Pressure calibrated directly: Factor=" + String(calibration.pressureCalibration.calibrationFactor, 3));
    }
  }
  
  static void calibrateAltitudeDirect(float referenceAltitude) {
    SensorData data = readAllSensors();
    if (!isnan(data.rawPressure) && data.rawPressure > 0) {
      float seaLevelPressure = data.rawPressure / pow(1.0 - (referenceAltitude / 44330.0), 5.255);
      calibration.bmp180.seaLevelPressure = seaLevelPressure;
      Serial.printf("Altitude calibrated directly: Current=%.1f m, Ref=%.1f m, SeaLevel=%.1f hPa\n",
                    data.altitude, referenceAltitude, seaLevelPressure);
      addToSerialBuffer("Altitude calibrated: Sea level pressure=" + String(seaLevelPressure, 1) + " hPa");
    }
  }
  
  static void simulateRainfall(float rainfallMM = 10.0) {
    calibration.system.rainSimulationActive = true;
    calibration.system.rainSimulationStart = millis();
    calibration.system.simulatedRainfall = rainfallMM;
    
    currentData.isRaining = true;
    currentData.humidity = min(100.0f, currentData.humidity + (rainfallMM * 3.0f));
    currentData.pressure = max(980.0f, currentData.pressure - (rainfallMM / 20.0f));
    
    addToSerialBuffer("RAIN SIMULATION: " + String(rainfallMM, 1) + "mm of rainfall simulated");
    Serial.printf("Rain simulation: %.1fmm - Humidity: %.1f%%, Pressure: %.1f hPa\n", 
                  rainfallMM, currentData.humidity, currentData.pressure);
  }
  
  static void updateRainSimulation() {
    if (calibration.system.rainSimulationActive) {
      if (millis() - calibration.system.rainSimulationStart > 300000) {
        calibration.system.rainSimulationActive = false;
        currentData.isRaining = false;
        calibration.system.simulatedRainfall = 0.0;
        addToSerialBuffer("Rain simulation ended");
      }
    }
  }
  
  static MPU6050_Sensor& getMPU6050() { return mpu6050; }
  
  static void diagnoseSoilSensors() {
    Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║              SOIL MOISTURE SENSOR DIAGNOSTICS               ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");
    
    int sandyRaw = readMoistureSensor(PIN_SANDY_MOISTURE, 20);
    int clayRaw = readMoistureSensor(PIN_CLAY_MOISTURE, 20);
    int loamyRaw = readMoistureSensor(PIN_LOAMY_MOISTURE, 20);
    
    Serial.println("\n CURRENT READINGS:");
    Serial.println("┌────────────┬──────────┬────────────┬────────────┬─────────────┐");
    Serial.println("│ Sensor     │ Raw Value │ Dry Cal    │ Wet Cal    │ Moisture %  │");
    Serial.println("├────────────┼──────────┼────────────┼────────────┼─────────────┤");
    
    float sandyPercent = mapToPercent(sandyRaw, calibration.sandy.rawDry, calibration.sandy.rawWet);
    Serial.printf("│ Sandy      │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  sandyRaw, calibration.sandy.rawDry, calibration.sandy.rawWet, sandyPercent);
    
    float clayPercent = mapToPercent(clayRaw, calibration.clay.rawDry, calibration.clay.rawWet);
    Serial.printf("│ Clay       │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  clayRaw, calibration.clay.rawDry, calibration.clay.rawWet, clayPercent);
    
    float loamyPercent = mapToPercent(loamyRaw, calibration.loamy.rawDry, calibration.loamy.rawWet);
    Serial.printf("│ Loamy      │ %6d  │ %6d    │ %6d    │ %7.1f%%  │\n", 
                  loamyRaw, calibration.loamy.rawDry, calibration.loamy.rawWet, loamyPercent);
    
    Serial.println("└────────────┴──────────┴────────────┴────────────┴─────────────┘");
  }
  
  static void testSoilSensors() {
    Serial.println("\n╔══════════════════════════════════════════════════════════════╗");
    Serial.println("║                  SOIL SENSOR LIVE TEST                      ║");
    Serial.println("╚══════════════════════════════════════════════════════════════╝");
    Serial.println("\nReading every 500ms. Press any key to stop...\n");
    
    unsigned long startTime = millis();
    while (millis() - startTime < 10000) {
      int sandyRaw = analogRead(PIN_SANDY_MOISTURE);
      int clayRaw = analogRead(PIN_CLAY_MOISTURE);
      int loamyRaw = analogRead(PIN_LOAMY_MOISTURE);
      
      float sandyPct = mapToPercent(sandyRaw, calibration.sandy.rawDry, calibration.sandy.rawWet);
      float clayPct = mapToPercent(clayRaw, calibration.clay.rawDry, calibration.clay.rawWet);
      float loamyPct = mapToPercent(loamyRaw, calibration.loamy.rawDry, calibration.loamy.rawWet);
      
      Serial.printf("Sandy: %4d → %5.1f%%  |  Clay: %4d → %5.1f%%  |  Loamy: %4d → %5.1f%%\n",
                    sandyRaw, sandyPct, clayRaw, clayPct, loamyRaw, loamyPct);
      delay(500);
      
      if (Serial.available()) {
        while (Serial.available()) Serial.read();
        break;
      }
    }
    Serial.println("\n Test completed\n");
  }
};

// Initialize static members
BMP180 SensorManager::bmp180;
MPU6050_Sensor SensorManager::mpu6050;

// =============================================================================
// FIXED IRRIGATION CONTROLLER - CORRECTED LOGIC
// =============================================================================

class IrrigationController {
private:
  static bool checkWaterAvailability() {
    unsigned long currentMillis = millis();
    // Check if enough time has passed since last watering
    if (currentMillis - pumpStatus.lastWateringTime < calibration.irrigation.minWateringInterval) {
      return false;
    }
    return true;
  }
  
public:
  static void initialize() {
    pumpStatus.sandyActive = false;
    pumpStatus.clayActive = false;
    pumpStatus.loamyActive = false;
    pumpStatus.sandyManual = false;
    pumpStatus.clayManual = false;
    pumpStatus.loamyManual = false;
    pumpStatus.lastWateringTime = 0;
    pumpStatus.pumpStartTime = 0;
    pumpStatus.waterAvailable = true;
    pumpStatus.sandyRuntime = 0;
    pumpStatus.clayRuntime = 0;
    pumpStatus.loamyRuntime = 0;
    
    digitalWrite(PIN_SANDY_PUMP, LOW);
    digitalWrite(PIN_CLAY_PUMP, LOW);
    digitalWrite(PIN_LOAMY_PUMP, LOW);
  }
  
  static void controlPump(int pumpPin, bool& pumpActive, bool& manualFlag, 
                          float moisturePercent, float threshold, const char* pumpName) {
    bool shouldRun = false;
    bool isManualOverride = false;
    
    // Determine if pump should run based on mode
    if (systemState.manualMode) {
      // In manual mode, only run if manual flag is set
      shouldRun = manualFlag;
      isManualOverride = manualFlag;
    } else {
      // In auto mode, run based on moisture AND water availability
      if (moisturePercent < threshold && pumpStatus.waterAvailable) {
        shouldRun = true;
        isManualOverride = false;
      } else {
        shouldRun = false;
        
        // Auto-turn off manual flags when not in manual mode
        if (manualFlag) {
          manualFlag = false;
          Serial.printf("%s pump manual override cleared (auto mode)\n", pumpName);
        }
      }
    }
    
    // Pump control logic
    if (shouldRun && !pumpActive) {
      // Start pump
      digitalWrite(pumpPin, HIGH);
      pumpActive = true;
      pumpStatus.pumpStartTime = millis();
      systemState.irrigationActive = true;
      
      Serial.printf("%s pump started - Moisture: %.1f%%, Threshold: %.1f%%, %s\n", 
                    pumpName, moisturePercent, threshold,
                    isManualOverride ? "MANUAL" : "AUTO");
      
      if (!isManualOverride) {
        BuzzerManager::playWateringTone();
      } else {
        BuzzerManager::beep(100, 1500);
      }
    }
    else if (shouldRun && pumpActive) {
      // Pump is already running - check runtime limit
      unsigned long currentMillis = millis();
      unsigned long runtime = currentMillis - pumpStatus.pumpStartTime;
      
      if (runtime >= calibration.irrigation.maxPumpRuntime) {
        digitalWrite(pumpPin, LOW);
        pumpActive = false;
        systemState.irrigationActive = false;
        pumpStatus.lastWateringTime = currentMillis;
        pumpStatus.waterAvailable = false;  // Mark that water was used
        
        Serial.printf("%s pump auto-stopped (max runtime: %lu seconds)\n", 
                      pumpName, runtime / 1000);
        
        // If it was manual mode, also clear manual flag
        if (manualFlag) {
          manualFlag = false;
          Serial.printf("%s manual mode cleared after max runtime\n", pumpName);
        }
      }
    }
    else if (!shouldRun && pumpActive) {
      // Stop pump (condition no longer met)
      digitalWrite(pumpPin, LOW);
      pumpActive = false;
      
      // Record runtime if it was an auto watering cycle
      if (!manualFlag) {
        pumpStatus.lastWateringTime = millis();
      }
      
      systemState.irrigationActive = false;
      
      if (moisturePercent >= threshold) {
        Serial.printf("%s pump stopped - Moisture target reached (%.1f%% >= %.1f%%)\n", 
                      pumpName, moisturePercent, threshold);
      } else if (!pumpStatus.waterAvailable) {
        Serial.printf("%s pump stopped - Water not available (cooldown active)\n", pumpName);
      } else if (!shouldRun && !manualFlag) {
        Serial.printf("%s pump stopped - No watering needed (Moisture: %.1f%%, Threshold: %.1f%%)\n", 
                      pumpName, moisturePercent, threshold);
      } else {
        Serial.printf("%s pump stopped\n", pumpName);
      }
    }
  }
  
  static void update() {
    // Update water availability - re-enable after minWateringInterval has passed
    static unsigned long lastWaterBlockTime = 0;
    static bool waterBlocked = false;
    
    unsigned long currentMillis = millis();
    
    if (!systemState.manualMode) {
      // Check if we're in a water cooldown period
      if (!pumpStatus.waterAvailable && !waterBlocked) {
        waterBlocked = true;
        lastWaterBlockTime = currentMillis;
        Serial.println("Water cooldown period started");
      }
      
      // Re-enable water after cooldown
      if (waterBlocked && (currentMillis - lastWaterBlockTime >= calibration.irrigation.minWateringInterval)) {
        pumpStatus.waterAvailable = true;
        waterBlocked = false;
        Serial.println("Water cooldown period ended - watering available again");
      }
      
      // Also check if any pump is currently active - if not, we can reset waterAvailable
      if (!pumpStatus.sandyActive && !pumpStatus.clayActive && !pumpStatus.loamyActive && !waterBlocked) {
        pumpStatus.waterAvailable = true;
      }
    } else {
      // In manual mode, always allow watering
      pumpStatus.waterAvailable = true;
      waterBlocked = false;
    }
    
    // Control each pump
    controlPump(PIN_SANDY_PUMP, pumpStatus.sandyActive, pumpStatus.sandyManual,
                currentData.sandyPercent, calibration.irrigation.sandyWaterThreshold, "Sandy");
    
    controlPump(PIN_CLAY_PUMP, pumpStatus.clayActive, pumpStatus.clayManual,
                currentData.clayPercent, calibration.irrigation.clayWaterThreshold, "Clay");
    
    controlPump(PIN_LOAMY_PUMP, pumpStatus.loamyActive, pumpStatus.loamyManual,
                currentData.loamyPercent, calibration.irrigation.loamyWaterThreshold, "Loamy");
    
    // Update runtime counters
    static unsigned long lastRuntimeUpdate = 0;
    if (currentMillis - lastRuntimeUpdate >= 1000) {
      lastRuntimeUpdate = currentMillis;
      if (pumpStatus.sandyActive) pumpStatus.sandyRuntime++;
      if (pumpStatus.clayActive) pumpStatus.clayRuntime++;
      if (pumpStatus.loamyActive) pumpStatus.loamyRuntime++;
    }
    
    updateLEDs();
  }
  
  static void setManualControl(String pump, bool state) {
    if (pump == "sandy") {
      // Toggle manual mode - only effective when system manual mode is ON
      if (systemState.manualMode) {
        pumpStatus.sandyManual = state;
        if (state && !pumpStatus.sandyActive) {
          digitalWrite(PIN_SANDY_PUMP, HIGH);
          pumpStatus.sandyActive = true;
          pumpStatus.pumpStartTime = millis();
          systemState.irrigationActive = true;
          Serial.println("Sandy pump manually turned ON");
        } else if (!state && pumpStatus.sandyActive && systemState.manualMode) {
          digitalWrite(PIN_SANDY_PUMP, LOW);
          pumpStatus.sandyActive = false;
          systemState.irrigationActive = false;
          Serial.println("Sandy pump manually turned OFF");
        }
      } else {
        Serial.println("Cannot control pump manually - Manual mode is disabled");
        Serial.println("Type 'MANUAL ON' to enable manual mode first");
        pumpStatus.sandyManual = false;
      }
    }
    else if (pump == "clay") {
      if (systemState.manualMode) {
        pumpStatus.clayManual = state;
        if (state && !pumpStatus.clayActive) {
          digitalWrite(PIN_CLAY_PUMP, HIGH);
          pumpStatus.clayActive = true;
          pumpStatus.pumpStartTime = millis();
          systemState.irrigationActive = true;
          Serial.println("Clay pump manually turned ON");
        } else if (!state && pumpStatus.clayActive && systemState.manualMode) {
          digitalWrite(PIN_CLAY_PUMP, LOW);
          pumpStatus.clayActive = false;
          systemState.irrigationActive = false;
          Serial.println("Clay pump manually turned OFF");
        }
      } else {
        Serial.println("Cannot control pump manually - Manual mode is disabled");
        Serial.println("Type 'MANUAL ON' to enable manual mode first");
        pumpStatus.clayManual = false;
      }
    }
    else if (pump == "loamy") {
      if (systemState.manualMode) {
        pumpStatus.loamyManual = state;
        if (state && !pumpStatus.loamyActive) {
          digitalWrite(PIN_LOAMY_PUMP, HIGH);
          pumpStatus.loamyActive = true;
          pumpStatus.pumpStartTime = millis();
          systemState.irrigationActive = true;
          Serial.println("Loamy pump manually turned ON");
        } else if (!state && pumpStatus.loamyActive && systemState.manualMode) {
          digitalWrite(PIN_LOAMY_PUMP, LOW);
          pumpStatus.loamyActive = false;
          systemState.irrigationActive = false;
          Serial.println("Loamy pump manually turned OFF");
        }
      } else {
        Serial.println("Cannot control pump manually - Manual mode is disabled");
        Serial.println("Type 'MANUAL ON' to enable manual mode first");
        pumpStatus.loamyManual = false;
      }
    }
  }
  
  static void setManualMode(bool enabled) {
    if (enabled && !systemState.manualMode) {
      // Switching to manual mode
      systemState.manualMode = true;
      calibration.system.manualMode = true;
      
      // Turn off all auto-running pumps
      if (pumpStatus.sandyActive && !pumpStatus.sandyManual) {
        digitalWrite(PIN_SANDY_PUMP, LOW);
        pumpStatus.sandyActive = false;
      }
      if (pumpStatus.clayActive && !pumpStatus.clayManual) {
        digitalWrite(PIN_CLAY_PUMP, LOW);
        pumpStatus.clayActive = false;
      }
      if (pumpStatus.loamyActive && !pumpStatus.loamyManual) {
        digitalWrite(PIN_LOAMY_PUMP, LOW);
        pumpStatus.loamyActive = false;
      }
      
      systemState.irrigationActive = false;
      pumpStatus.waterAvailable = true;
      
      Serial.println("=== MANUAL MODE ENABLED ===");
      Serial.println("You can now control pumps individually using:");
      Serial.println("  PUMP SANDY ON/OFF");
      Serial.println("  PUMP CLAY ON/OFF");
      Serial.println("  PUMP LOAMY ON/OFF");
      Serial.println("Type 'MANUAL OFF' to return to automatic mode");
    }
    else if (!enabled && systemState.manualMode) {
      // Switching to auto mode
      systemState.manualMode = false;
      calibration.system.manualMode = false;
      
      // Turn off all manual pumps
      pumpStatus.sandyManual = false;
      pumpStatus.clayManual = false;
      pumpStatus.loamyManual = false;
      
      if (pumpStatus.sandyActive) {
        digitalWrite(PIN_SANDY_PUMP, LOW);
        pumpStatus.sandyActive = false;
      }
      if (pumpStatus.clayActive) {
        digitalWrite(PIN_CLAY_PUMP, LOW);
        pumpStatus.clayActive = false;
      }
      if (pumpStatus.loamyActive) {
        digitalWrite(PIN_LOAMY_PUMP, LOW);
        pumpStatus.loamyActive = false;
      }
      
      systemState.irrigationActive = false;
      
      Serial.println("=== AUTO MODE ENABLED ===");
      Serial.println("System will now control pumps based on soil moisture levels");
    }
    else if (enabled && systemState.manualMode) {
      Serial.println("Manual mode is already enabled");
    }
    else {
      Serial.println("Auto mode is already enabled");
    }
  }
  
  static void emergencyShutdown() {
    digitalWrite(PIN_SANDY_PUMP, LOW);
    digitalWrite(PIN_CLAY_PUMP, LOW);
    digitalWrite(PIN_LOAMY_PUMP, LOW);
    pumpStatus.sandyActive = false;
    pumpStatus.clayActive = false;
    pumpStatus.loamyActive = false;
    pumpStatus.sandyManual = false;
    pumpStatus.clayManual = false;
    pumpStatus.loamyManual = false;
    systemState.irrigationActive = false;
    
    Serial.println("\n⚠️  EMERGENCY STOP ACTIVATED ⚠️");
    Serial.println("   All pumps have been stopped!");
    Serial.println("   Type 'MANUAL OFF' then 'MANUAL ON' to reset if needed\n");
    
    BuzzerManager::playAlertSequence();
  }
  
  static void testPumps() {
    Serial.println("\n=== PUMP TEST SEQUENCE ===");
    bool wasManualMode = systemState.manualMode;
    
    // Temporarily enable manual mode if not already on
    if (!wasManualMode) {
      setManualMode(true);
    }
    
    Serial.println("Testing Sandy pump for 2 seconds...");
    setManualControl("sandy", true);
    delay(2000);
    setManualControl("sandy", false);
    delay(500);
    
    Serial.println("Testing Clay pump for 2 seconds...");
    setManualControl("clay", true);
    delay(2000);
    setManualControl("clay", false);
    delay(500);
    
    Serial.println("Testing Loamy pump for 2 seconds...");
    setManualControl("loamy", true);
    delay(2000);
    setManualControl("loamy", false);
    
    // Restore previous mode
    if (!wasManualMode) {
      setManualMode(false);
    }
    
    Serial.println("Pump test complete!\n");
  }
  
  static void updateLEDs() {
    if (!calibration.system.enableLEDs) {
      digitalWrite(PIN_RED_LED, LOW);
      digitalWrite(PIN_YELLOW_LED, LOW);
      digitalWrite(PIN_GREEN_LED, LOW);
      return;
    }
    
    // Priority: Security Alert > Irrigation Active > Water Unavailable > Normal
    if (systemState.securityAlert) {
      // Red blinking - Security alert
      static unsigned long lastBlink = 0;
      static bool ledState = false;
      if (millis() - lastBlink > 300) {
        ledState = !ledState;
        digitalWrite(PIN_RED_LED, ledState ? HIGH : LOW);
        lastBlink = millis();
      }
      digitalWrite(PIN_YELLOW_LED, LOW);
      digitalWrite(PIN_GREEN_LED, LOW);
    } 
    else if (systemState.irrigationActive) {
      // Green blinking - Pump active
      static unsigned long lastBlink = 0;
      static bool ledState = false;
      if (millis() - lastBlink > 500) {
        ledState = !ledState;
        digitalWrite(PIN_GREEN_LED, ledState ? HIGH : LOW);
        lastBlink = millis();
      }
      digitalWrite(PIN_RED_LED, LOW);
      digitalWrite(PIN_YELLOW_LED, LOW);
    } 
    else if (!pumpStatus.waterAvailable && !systemState.manualMode) {
      // Yellow blinking - Water cooldown
      static unsigned long lastBlink = 0;
      static bool ledState = false;
      if (millis() - lastBlink > 1000) {
        ledState = !ledState;
        digitalWrite(PIN_YELLOW_LED, ledState ? HIGH : LOW);
        lastBlink = millis();
      }
      digitalWrite(PIN_RED_LED, LOW);
      digitalWrite(PIN_GREEN_LED, LOW);
    } 
    else if (systemState.manualMode) {
      // Yellow solid - Manual mode active
      digitalWrite(PIN_YELLOW_LED, HIGH);
      digitalWrite(PIN_RED_LED, LOW);
      digitalWrite(PIN_GREEN_LED, LOW);
    }
    else {
      // Green solid - Normal operation
      digitalWrite(PIN_GREEN_LED, HIGH);
      digitalWrite(PIN_RED_LED, LOW);
      digitalWrite(PIN_YELLOW_LED, LOW);
    }
  }
};

// =============================================================================
// ENHANCED SD CARD MANAGER WITH COMPLETE LOGGING AND CSV HEADERS
// =============================================================================

class SDCardManager {
private:
  static unsigned long lastLogTime;
  static unsigned long lastDataLogTime;
  static unsigned long lastEventLogTime;
  static unsigned long lastAlertLogTime;
  static unsigned long fileSize;
  static int fileCount;
  static String currentDataFilename;
  static String currentEventFilename;
  static String currentAlertFilename;
  static bool headersWritten;
  
  // Create new data log file with header
  static void createNewDataFile() {
    if (!systemState.sdCardPresent) return;
    
    // Generate filename with date
    String dateStr = getDateString();
    dateStr.replace("-", "");
    currentDataFilename = "/data/sensors_" + dateStr + "_" + String(millis() / 1000) + ".csv";
    
    File dataFile = SD.open(currentDataFilename, FILE_WRITE);
    if (dataFile) {
      // Write comprehensive CSV header
      dataFile.println(getDataCSVHeader());
      dataFile.close();
      Serial.println("Created new data log: " + currentDataFilename);
    }
  }
  
  // Create new event log file with header
  static void createNewEventFile() {
    if (!systemState.sdCardPresent) return;
    
    String dateStr = getDateString();
    dateStr.replace("-", "");
    currentEventFilename = "/data/events_" + dateStr + "_" + String(millis() / 1000) + ".csv";
    
    File eventFile = SD.open(currentEventFilename, FILE_WRITE);
    if (eventFile) {
      eventFile.println(getEventCSVHeader());
      eventFile.close();
      Serial.println("Created new event log: " + currentEventFilename);
    }
  }
  
  // Create new alert log file with header
  static void createNewAlertFile() {
    if (!systemState.sdCardPresent) return;
    
    String dateStr = getDateString();
    dateStr.replace("-", "");
    currentAlertFilename = "/data/alerts_" + dateStr + "_" + String(millis() / 1000) + ".csv";
    
    File alertFile = SD.open(currentAlertFilename, FILE_WRITE);
    if (alertFile) {
      alertFile.println(getAlertCSVHeader());
      alertFile.close();
      Serial.println("Created new alert log: " + currentAlertFilename);
    }
  }
  
  // Generate comprehensive CSV header for sensor data
  static String getDataCSVHeader() {
    String header = "Timestamp,DateTime,";
    header += "UnixTime,Uptime_ms,Uptime_seconds,";
    
    // Soil moisture data
    header += "Sandy_Raw,Sandy_Percent,Sandy_Status,Sandy_Threshold,";
    header += "Clay_Raw,Clay_Percent,Clay_Status,Clay_Threshold,";
    header += "Loamy_Raw,Loamy_Percent,Loamy_Status,Loamy_Threshold,";
    
    // Environmental data
    header += "Temperature_C,Temperature_F,HeatIndex_C,HeatIndex_F,";
    header += "Humidity_Percent,AbsoluteHumidity_g_m3,";
    header += "Pressure_hPa,Pressure_mmHg,Pressure_inHg,";
    header += "Altitude_m,Altitude_ft,";
    header += "DewPoint_C,DewPoint_F,";
    
    // Weather conditions
    header += "IsRaining,RainSimulated,Rainfall_mm,";
    
    // MPU6050 data
    header += "Accel_X_g,Accel_Y_g,Accel_Z_g,";
    header += "Gyro_X_dps,Gyro_Y_dps,Gyro_Z_dps,";
    header += "Movement_Detected,Movement_Intensity_Percent,";
    
    // Pump status
    header += "Pump_Sandy_Active,Pump_Sandy_Manual,Pump_Sandy_Runtime_s,";
    header += "Pump_Clay_Active,Pump_Clay_Manual,Pump_Clay_Runtime_s,";
    header += "Pump_Loamy_Active,Pump_Loamy_Manual,Pump_Loamy_Runtime_s,";
    header += "Irrigation_Active,Water_Available,";
    
    // System status
    header += "WiFi_Connected,WiFi_RSSI_dBm,WiFi_SSID,";
    header += "AP_Mode_Active,IP_Address,";
    header += "NTP_Synced,Manual_Mode,";
    header += "Security_Enabled,Security_Alert,";
    header += "Buzzer_Enabled,WebServer_Running,";
    header += "GoogleSheets_Enabled,SD_Card_Present,";
    header += "Telegram_Enabled,Telegram_Connected,";
    
    // Calibration values
    header += "Sandy_Dry_Cal,Sandy_Wet_Cal,";
    header += "Clay_Dry_Cal,Clay_Wet_Cal,";
    header += "Loamy_Dry_Cal,Loamy_Wet_Cal,";
    
    // Thresholds
    header += "Sandy_Threshold_Pct,Clay_Threshold_Pct,Loamy_Threshold_Pct,";
    header += "Temp_Threshold_Low_C,Temp_Threshold_High_C,";
    header += "Humidity_Threshold_Pct,Movement_Threshold,";
    
    // Irrigation settings
    header += "Watering_Duration_ms,Min_Watering_Interval_ms,Max_Pump_Runtime_ms,";
    header += "Check_Rain_Condition,Check_DewPoint,Check_Temperature,Check_Humidity,";
    
    // Free RAM and system info
    header += "Free_Heap_Bytes,Free_Heap_KB,Max_Alloc_Bytes,";
    header += "CPU_Freq_MHz,Sketch_Size_Bytes,Free_Sketch_Space_Bytes,";
    header += "SD_Card_Size_MB,SD_Card_Free_MB,SD_Card_Used_MB";
    
    return header;
  }
  
  // Generate CSV header for events
  static String getEventCSVHeader() {
    return "Timestamp,DateTime,UnixTime,Event_Type,Event_SubType,"
           "Description,Severity,Source,"
           "Soil_Moisture_Pct,Temperature_C,Humidity_Pct,Pressure_hPa,"
           "Pump_Active,Manual_Mode,WiFi_Connected,Free_Heap_KB";
  }
  
  // Generate CSV header for alerts
  static String getAlertCSVHeader() {
    return "Timestamp,DateTime,UnixTime,Alert_Type,Alert_Level,"
           "Description,Trigger_Value,Threshold_Value,"
           "Action_Taken,Acknowledged,Resolved_Time,"
           "Soil_Sandy_Pct,Soil_Clay_Pct,Soil_Loamy_Pct,"
           "Temperature_C,Humidity_Pct,Pressure_hPa,"
           "Movement_Intensity_Pct";
  }
  
  // Helper to get formatted data row
  static String getDataCSVRow(const SensorData& data, const PumpStatus& pumps) {
    String row = "";
    
    // Timestamp
    row += "\"" + data.timestamp + "\",";
    row += "\"" + data.dateTime + "\",";
    row += String(timeClient.getEpochTime()) + ",";
    row += String(millis()) + ",";
    row += String(millis() / 1000) + ",";
    
    // Soil moisture - Sandy
    row += String(data.rawSandyMoisture) + ",";
    row += String(data.sandyPercent, 2) + ",";
    row += getSoilStatus(data.sandyPercent, calibration.irrigation.sandyWaterThreshold) + ",";
    row += String(calibration.irrigation.sandyWaterThreshold, 1) + ",";
    
    // Soil moisture - Clay
    row += String(data.rawClayMoisture) + ",";
    row += String(data.clayPercent, 2) + ",";
    row += getSoilStatus(data.clayPercent, calibration.irrigation.clayWaterThreshold) + ",";
    row += String(calibration.irrigation.clayWaterThreshold, 1) + ",";
    
    // Soil moisture - Loamy
    row += String(data.rawLoamyMoisture) + ",";
    row += String(data.loamyPercent, 2) + ",";
    row += getSoilStatus(data.loamyPercent, calibration.irrigation.loamyWaterThreshold) + ",";
    row += String(calibration.irrigation.loamyWaterThreshold, 1) + ",";
    
    // Temperature data
    row += String(data.temperature, 2) + ",";
    row += String(celsiusToFahrenheit(data.temperature), 2) + ",";
    float heatIndexC = calculateHeatIndex(data.temperature, data.humidity);
    row += String(heatIndexC, 2) + ",";
    row += String(celsiusToFahrenheit(heatIndexC), 2) + ",";
    
    // Humidity data
    row += String(data.humidity, 2) + ",";
    row += String(calculateAbsoluteHumidity(data.temperature, data.humidity), 3) + ",";
    
    // Pressure data
    row += String(data.pressure, 2) + ",";
    row += String(data.pressure * 0.750062, 2) + ",";  // hPa to mmHg
    row += String(data.pressure * 0.02953, 3) + ",";   // hPa to inHg
    
    // Altitude data
    row += String(data.altitude, 2) + ",";
    row += String(data.altitude * 3.28084, 2) + ",";   // meters to feet
    
    // Dew point
    row += String(data.dewPoint, 2) + ",";
    row += String(celsiusToFahrenheit(data.dewPoint), 2) + ",";
    
    // Weather conditions
    row += String(data.isRaining ? 1 : 0) + ",";
    row += String(calibration.system.rainSimulationActive ? 1 : 0) + ",";
    row += String(calibration.system.simulatedRainfall, 2) + ",";
    
    // MPU6050 data
    row += String(data.accelerationX, 3) + ",";
    row += String(data.accelerationY, 3) + ",";
    row += String(data.accelerationZ, 3) + ",";
    row += String(data.gyroX, 2) + ",";
    row += String(data.gyroY, 2) + ",";
    row += String(data.gyroZ, 2) + ",";
    row += String(data.deviceMoved ? 1 : 0) + ",";
    row += String(data.movementIntensity) + ",";
    
    // Pump status
    row += String(pumps.sandyActive ? 1 : 0) + ",";
    row += String(pumps.sandyManual ? 1 : 0) + ",";
    row += String(pumps.sandyRuntime) + ",";
    row += String(pumps.clayActive ? 1 : 0) + ",";
    row += String(pumps.clayManual ? 1 : 0) + ",";
    row += String(pumps.clayRuntime) + ",";
    row += String(pumps.loamyActive ? 1 : 0) + ",";
    row += String(pumps.loamyManual ? 1 : 0) + ",";
    row += String(pumps.loamyRuntime) + ",";
    row += String(systemState.irrigationActive ? 1 : 0) + ",";
    row += String(pumps.waterAvailable ? 1 : 0) + ",";
    
    // WiFi status
    row += String(systemState.wifiConnected ? 1 : 0) + ",";
    row += String(WiFi.RSSI()) + ",";
    row += "\"" + WiFi.SSID() + "\",";
    row += String(systemState.apModeActive ? 1 : 0) + ",";
    row += "\"" + systemState.ipAddress + "\",";
    
    // System status
    row += String(systemState.ntpSynced ? 1 : 0) + ",";
    row += String(systemState.manualMode ? 1 : 0) + ",";
    row += String(systemState.securityEnabled ? 1 : 0) + ",";
    row += String(systemState.securityAlert ? 1 : 0) + ",";
    row += String(systemState.buzzerEnabled ? 1 : 0) + ",";
    row += String(systemState.webServerRunning ? 1 : 0) + ",";
    row += String(systemState.googleSheetsEnabled ? 1 : 0) + ",";
    row += String(systemState.sdCardPresent ? 1 : 0) + ",";
    row += String(calibration.telegram.enabled ? 1 : 0) + ",";
    row += String(calibration.telegram.enabled ? 1 : 0) + ",";  // FIXED: Use calibration instead of TelegramManager::isEnabled()
    
    // Calibration values
    row += String(calibration.sandy.rawDry) + ",";
    row += String(calibration.sandy.rawWet) + ",";
    row += String(calibration.clay.rawDry) + ",";
    row += String(calibration.clay.rawWet) + ",";
    row += String(calibration.loamy.rawDry) + ",";
    row += String(calibration.loamy.rawWet) + ",";
    
    // Thresholds
    row += String(calibration.irrigation.sandyWaterThreshold, 1) + ",";
    row += String(calibration.irrigation.clayWaterThreshold, 1) + ",";
    row += String(calibration.irrigation.loamyWaterThreshold, 1) + ",";
    row += String(calibration.irrigation.tempThresholdLow, 1) + ",";
    row += String(calibration.irrigation.tempThresholdHigh, 1) + ",";
    row += String(calibration.irrigation.humidityThreshold, 1) + ",";
    row += String(calibration.mpu6050.movementThreshold, 3) + ",";
    
    // Irrigation settings
    row += String(calibration.irrigation.wateringDuration) + ",";
    row += String(calibration.irrigation.minWateringInterval) + ",";
    row += String(calibration.irrigation.maxPumpRuntime) + ",";
    row += String(calibration.irrigation.checkRainCondition ? 1 : 0) + ",";
    row += String(calibration.irrigation.checkDewPoint ? 1 : 0) + ",";
    row += String(calibration.irrigation.checkTemperature ? 1 : 0) + ",";
    row += String(calibration.irrigation.checkHumidity ? 1 : 0) + ",";
    
    // System info
    row += String(ESP.getFreeHeap()) + ",";
    row += String(ESP.getFreeHeap() / 1024) + ",";
    row += String(ESP.getMaxAllocHeap()) + ",";
    row += String(ESP.getCpuFreqMHz()) + ",";
    row += String(ESP.getSketchSize()) + ",";
    row += String(ESP.getFreeSketchSpace()) + ",";
    row += String(systemState.sdCardSize) + ",";
    row += String(systemState.sdCardSize - systemState.sdCardUsed) + ",";
    row += String(systemState.sdCardUsed);
    
    return row;
  }
  
  // Helper functions for calculations
  static float celsiusToFahrenheit(float celsius) {
    if (isnan(celsius)) return NAN;
    return celsius * 9.0 / 5.0 + 32.0;
  }
  
  static float calculateHeatIndex(float tempC, float humidity) {
    if (isnan(tempC) || isnan(humidity)) return NAN;
    float tempF = celsiusToFahrenheit(tempC);
    
    // Simplified heat index formula
    float hi = 0.5 * (tempF + 61.0 + ((tempF - 68.0) * 1.2) + (humidity * 0.094));
    
    if (hi >= 80) {
      hi = -42.379 + 2.04901523 * tempF + 10.14333127 * humidity
           - 0.22475541 * tempF * humidity - 0.00683783 * tempF * tempF
           - 0.05481717 * humidity * humidity + 0.00122874 * tempF * tempF * humidity
           + 0.00085282 * tempF * humidity * humidity - 0.00000199 * tempF * tempF * humidity * humidity;
    }
    
    return (hi - 32) * 5.0 / 9.0;
  }
  
  static float calculateAbsoluteHumidity(float tempC, float humidity) {
    if (isnan(tempC) || isnan(humidity)) return NAN;
    // Magnus formula for saturation vapor pressure (hPa)
    float satVaporPressure = 6.112 * exp((17.67 * tempC) / (tempC + 243.5));
    float actualVaporPressure = satVaporPressure * (humidity / 100.0);
    // Absolute humidity in g/m³
    return (actualVaporPressure * 1000.0) / (461.5 * (tempC + 273.15));
  }
  
  static String getSoilStatus(float moisture, float threshold) {
    if (moisture < threshold * 0.5) return "CRITICAL_DRY";
    if (moisture < threshold) return "DRY";
    if (moisture < threshold * 1.2) return "OPTIMAL";
    if (moisture < 80) return "MOIST";
    return "WATERLOGGED";
  }
  
  // Log event to separate event file
  static void logEvent(const String& eventType, const String& subType, 
                       const String& description, int severity = 1) {
    if (!systemState.sdEnabled || !systemState.sdCardPresent) return;
    
    if (currentEventFilename.length() == 0) {
      createNewEventFile();
    }
    
    // Check file size and rotate if needed
    File eventFile = SD.open(currentEventFilename, FILE_APPEND);
    if (eventFile) {
      String row = "";
      row += "\"" + getTimestamp() + "\",";
      row += "\"" + getDateTime() + "\",";
      row += String(timeClient.getEpochTime()) + ",";
      row += "\"" + eventType + "\",";
      row += "\"" + subType + "\",";
      row += "\"" + description + "\",";
      row += String(severity) + ",";
      row += "\"System\",";
      row += String(currentData.sandyPercent, 1) + ",";
      row += String(currentData.temperature, 1) + ",";
      row += String(currentData.humidity, 1) + ",";
      row += String(currentData.pressure, 1) + ",";
      row += String(pumpStatus.sandyActive ? 1 : 0) + ",";
      row += String(systemState.manualMode ? 1 : 0) + ",";
      row += String(systemState.wifiConnected ? 1 : 0) + ",";
      row += String(ESP.getFreeHeap() / 1024);
      
      eventFile.println(row);
      eventFile.close();
      
      // Rotate if file too large
      if (eventFile.size() > calibration.sd.maxFileSize) {
        createNewEventFile();
      }
    }
  }
  
  // Log alert to separate alert file
  static void logAlert(const String& alertType, const String& description,
                       float triggerValue, float thresholdValue,
                       const String& actionTaken, int alertLevel = 2) {
    if (!systemState.sdEnabled || !systemState.sdCardPresent) return;
    
    if (currentAlertFilename.length() == 0) {
      createNewAlertFile();
    }
    
    File alertFile = SD.open(currentAlertFilename, FILE_APPEND);
    if (alertFile) {
      String row = "";
      row += "\"" + getTimestamp() + "\",";
      row += "\"" + getDateTime() + "\",";
      row += String(timeClient.getEpochTime()) + ",";
      row += "\"" + alertType + "\",";
      row += String(alertLevel) + ",";
      row += "\"" + description + "\",";
      row += String(triggerValue, 2) + ",";
      row += String(thresholdValue, 2) + ",";
      row += "\"" + actionTaken + "\",";
      row += "0,"; // Acknowledged flag
      row += "\","; // Resolved time (empty for now)
      row += String(currentData.sandyPercent, 1) + ",";
      row += String(currentData.clayPercent, 1) + ",";
      row += String(currentData.loamyPercent, 1) + ",";
      row += String(currentData.temperature, 1) + ",";
      row += String(currentData.humidity, 1) + ",";
      row += String(currentData.pressure, 1) + ",";
      row += String(currentData.movementIntensity);
      
      alertFile.println(row);
      alertFile.close();
      
      // Rotate if file too large
      if (alertFile.size() > calibration.sd.maxFileSize) {
        createNewAlertFile();
      }
    }
  }
  
public:
  static bool initialize() {
    if (!calibration.sd.enabled) {
      Serial.println("SD card logging is disabled");
      systemState.sdCardPresent = false;
      systemState.sdEnabled = false;
      return false;
    }
    
    Serial.print("Initializing SD card... ");
    
    // Initialize SPI with proper pins
    sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
    if (!SD.begin(PIN_SD_CS, sdSPI)) {
      Serial.println("FAILED");
      systemState.sdCardPresent = false;
      systemState.sdEnabled = false;
      systemState.sdCardStatus = "Not detected";
      return false;
    }
    
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
      Serial.println("No SD card inserted");
      systemState.sdCardPresent = false;
      systemState.sdEnabled = false;
      systemState.sdCardStatus = "No card";
      return false;
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    uint64_t usedSpace = SD.usedBytes() / (1024 * 1024);
    uint64_t freeSpace = cardSize - usedSpace;
    
    Serial.printf("SD Card Type: %s\n", 
                  cardType == CARD_MMC ? "MMC" : 
                  cardType == CARD_SD ? "SDSC" : 
                  cardType == CARD_SDHC ? "SDHC" : "UNKNOWN");
    Serial.printf("Size: %lluMB\n", cardSize);
    Serial.printf("Used: %lluMB\n", usedSpace);
    Serial.printf("Free: %lluMB\n", freeSpace);
    
    systemState.sdCardSize = cardSize;
    systemState.sdCardUsed = usedSpace;
    systemState.sdCardStatus = "OK - " + String(freeSpace) + "MB free";
    
    if (!SD.exists("/data")) {
      SD.mkdir("/data");
      Serial.println("Created /data directory");
    }
    
    systemState.sdCardPresent = true;
    systemState.sdEnabled = true;
    lastLogTime = 0;
    lastDataLogTime = 0;
    lastEventLogTime = 0;
    lastAlertLogTime = 0;
    fileSize = 0;
    fileCount = 0;
    headersWritten = false;
    currentDataFilename = "";
    currentEventFilename = "";
    currentAlertFilename = "";
    
    // Create initial files
    createNewDataFile();
    createNewEventFile();
    createNewAlertFile();
    
    // Log system startup event
    logEvent("SYSTEM", "STARTUP", "System initialized and ready", 1);
    
    Serial.println("SD card initialized successfully!");
    return true;
  }
  
  // Comprehensive data logging
  static void logData(const SensorData& data, const PumpStatus& pumps) {
    if (!systemState.sdEnabled || !systemState.sdCardPresent) return;
    
    unsigned long currentMillis = millis();
    if (currentMillis - lastDataLogTime < calibration.sd.logInterval) return;
    
    lastDataLogTime = currentMillis;
    
    // Check if we need to create a new file (daily or size limit)
    if (currentDataFilename.length() == 0) {
      createNewDataFile();
    }
    
    // Check file size and rotate if needed
    File checkFile = SD.open(currentDataFilename, FILE_READ);
    if (checkFile) {
      if (checkFile.size() > calibration.sd.maxFileSize) {
        checkFile.close();
        createNewDataFile();
      } else {
        checkFile.close();
      }
    }
    
    File dataFile = SD.open(currentDataFilename, FILE_APPEND);
    if (dataFile) {
      String dataRow = getDataCSVRow(data, pumps);
      dataFile.println(dataRow);
      dataFile.close();
      
      fileSize += dataRow.length();
      fileCount++;
      
      // Update used space periodically
      if (fileCount % 100 == 0) {
        systemState.sdCardUsed = SD.usedBytes() / (1024 * 1024);
      }
    }
  }
  
  // Log pump activation event
  static void logPumpEvent(const String& pumpName, bool activated, 
                           float moistureLevel, float threshold) {
    String eventType = "PUMP";
    String subType = activated ? "ACTIVATED" : "DEACTIVATED";
    String description = pumpName + " pump " + (activated ? "started" : "stopped") +
                         " | Moisture: " + String(moistureLevel, 1) + "%" +
                         " | Threshold: " + String(threshold, 1) + "%";
    int severity = activated ? 2 : 1;
    
    logEvent(eventType, subType, description, severity);
  }
  
  // Log security event
  static void logSecurityEvent(const String& eventType, int intensity, 
                               float threshold, const String& action) {
    String description = "Movement detected - Intensity: " + String(intensity) + "%" +
                         " | Threshold: " + String(threshold, 2);
    logAlert("SECURITY", description, intensity, threshold, action, 3);
    logEvent("SECURITY", eventType, description, 3);
  }
  
  // Log weather event
  static void logWeatherEvent(bool raining, float humidity, float pressure) {
    if (raining) {
      String description = "Rain detected - Humidity: " + String(humidity, 1) + 
                          "% | Pressure: " + String(pressure, 1) + " hPa";
      logEvent("WEATHER", "RAIN_START", description, 2);
      logAlert("WEATHER_ALERT", description, humidity, 85.0, "Irrigation delayed", 2);
    } else {
      String description = "Rain stopped";
      logEvent("WEATHER", "RAIN_STOP", description, 1);
    }
  }
  
  // Log threshold violation
  static void logThresholdViolation(const String& sensor, float value, 
                                    float threshold, const String& condition) {
    String description = sensor + " " + condition + " threshold: " + 
                        String(value, 1) + " vs " + String(threshold, 1);
    logAlert("THRESHOLD", description, value, threshold, "System monitoring", 2);
    logEvent("THRESHOLD", condition, description, 2);
  }
  
  // Log system error
  static void logError(const String& errorType, const String& details) {
    String description = errorType + ": " + details;
    logAlert("SYSTEM_ERROR", description, 0, 0, "Check system", 4);
    logEvent("ERROR", errorType, description, 4);
  }
  
  // Log WiFi status change
  static void logWiFiStatus(bool connected, const String& ssid = "", int rssi = 0) {
    String description = connected ? 
                         "WiFi connected to " + ssid + " | RSSI: " + String(rssi) + " dBm" :
                         "WiFi disconnected";
    logEvent("NETWORK", connected ? "CONNECTED" : "DISCONNECTED", description, connected ? 1 : 2);
  }
  
  // Manual trigger for immediate data logging
  static void forceLogData() {
    lastDataLogTime = millis() - calibration.sd.logInterval;
    logData(currentData, pumpStatus);
  }
  
  // Get statistics
  static void getStatistics() {
    if (!systemState.sdCardPresent) {
      Serial.println("SD card not present");
      return;
    }
    
    Serial.println("\n=== SD CARD STATISTICS ===");
    Serial.printf("Total log entries: %d\n", fileCount);
    Serial.printf("Total data size: %.2f KB\n", fileSize / 1024.0);
    Serial.printf("Current data file: %s\n", currentDataFilename.c_str());
    Serial.printf("Current event file: %s\n", currentEventFilename.c_str());
    Serial.printf("Current alert file: %s\n", currentAlertFilename.c_str());
    
    File dataFile = SD.open(currentDataFilename, FILE_READ);
    if (dataFile) {
      int lines = 0;
      while (dataFile.available()) {
        if (dataFile.read() == '\n') lines++;
      }
      Serial.printf("Lines in current data file: %d\n", lines);
      dataFile.close();
    }
  }
  
  static void enable() {
    calibration.sd.enabled = true;
    systemState.sdEnabled = true;
    Serial.println("SD card logging enabled");
    if (!systemState.sdCardPresent) initialize();
    logEvent("SYSTEM", "SD_ENABLED", "SD card logging enabled", 1);
  }
  
  static void disable() {
    calibration.sd.enabled = false;
    systemState.sdEnabled = false;
    logEvent("SYSTEM", "SD_DISABLED", "SD card logging disabled", 1);
    Serial.println("SD card logging disabled");
  }
  
  static void toggle() {
    if (calibration.sd.enabled) disable();
    else enable();
  }
  
  static void listLogFiles() {
    if (!systemState.sdCardPresent) {
      Serial.println("SD card not present");
      return;
    }
    
    File root = SD.open("/data");
    if (!root) {
      Serial.println("Failed to open data directory");
      return;
    }
    
    Serial.println("\n=== LOG FILES ===");
    Serial.println("┌────────────────────────────────────────────┬──────────┬────────────┐");
    Serial.println("│ Filename                                   │ Size     │ Modified   │");
    Serial.println("├────────────────────────────────────────────┼──────────┼────────────┤");
    
    File file = root.openNextFile();
    int count = 0;
    while (file) {
      if (!file.isDirectory()) {
        String filename = String(file.name());
        if (filename.endsWith(".csv")) {
          Serial.printf("│ %-42s │ %6d B │ %-10s │\n", 
                        filename.c_str(), 
                        file.size(),
                        getFileModifiedDate(file).c_str());
          count++;
        }
      }
      file = root.openNextFile();
    }
    Serial.println("└────────────────────────────────────────────┴──────────┴────────────┘");
    Serial.printf("Total: %d CSV files\n", count);
    root.close();
  }
  
  static String getFileModifiedDate(File& file) {
    // Return simple timestamp
    return getDateString().substring(0, 10);
  }
  
  static void deleteAllLogs() {
    if (!systemState.sdCardPresent) {
      Serial.println("SD card not present");
      return;
    }
    
    logEvent("SYSTEM", "DELETE_LOGS", "Deleting all log files", 2);
    
    File root = SD.open("/data");
    if (!root) return;
    
    File file = root.openNextFile();
    int deleted = 0;
    while (file) {
      if (!file.isDirectory()) {
        String filepath = "/data/" + String(file.name());
        if (SD.remove(filepath)) {
          deleted++;
        }
      }
      file = root.openNextFile();
    }
    root.close();
    
    Serial.printf("Deleted %d log files\n", deleted);
    
    // Recreate files
    createNewDataFile();
    createNewEventFile();
    createNewAlertFile();
    
    fileCount = 0;
    fileSize = 0;
    
    logEvent("SYSTEM", "LOGS_DELETED", String(deleted) + " files deleted", 1);
  }
  
  static void exportToCSV() {
    if (!systemState.sdCardPresent) {
      Serial.println("SD card not present");
      return;
    }
    
    // Create summary file
    String summaryFile = "/data/summary_" + getDateString() + ".csv";
    File summary = SD.open(summaryFile, FILE_WRITE);
    if (summary) {
      summary.println("Parameter,Min,Max,Average,Last_Reading");
      
      // Calculate averages from current data
      summary.printf("Sandy_Moisture,%s,%s,%s,%s\n",
                     String(getMinValue("sandy")).c_str(),
                     String(getMaxValue("sandy")).c_str(),
                     String(getAverageValue("sandy")).c_str(),
                     String(currentData.sandyPercent, 2).c_str());
      
      summary.printf("Clay_Moisture,%s,%s,%s,%s\n",
                     String(getMinValue("clay")).c_str(),
                     String(getMaxValue("clay")).c_str(),
                     String(getAverageValue("clay")).c_str(),
                     String(currentData.clayPercent, 2).c_str());
      
      summary.printf("Loamy_Moisture,%s,%s,%s,%s\n",
                     String(getMinValue("loamy")).c_str(),
                     String(getMaxValue("loamy")).c_str(),
                     String(getAverageValue("loamy")).c_str(),
                     String(currentData.loamyPercent, 2).c_str());
      
      summary.printf("Temperature,%s,%s,%s,%s\n",
                     String(getMinValue("temp")).c_str(),
                     String(getMaxValue("temp")).c_str(),
                     String(getAverageValue("temp")).c_str(),
                     String(currentData.temperature, 2).c_str());
      
      summary.close();
      Serial.println("Summary exported to: " + summaryFile);
    }
  }
  
  // Helper functions for statistics
  static float getMinValue(const String& param) {
    float minVal = 999999;
    for (int i = 0; i < HISTORY_SIZE; i++) {
      float val = 0;
      if (param == "sandy") val = historicalData.sandyPercent[i];
      else if (param == "clay") val = historicalData.clayPercent[i];
      else if (param == "loamy") val = historicalData.loamyPercent[i];
      else if (param == "temp") val = historicalData.temperature[i];
      
      if (val > 0 && val < minVal) minVal = val;
    }
    return (minVal == 999999) ? 0 : minVal;
  }
  
  static float getMaxValue(const String& param) {
    float maxVal = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
      float val = 0;
      if (param == "sandy") val = historicalData.sandyPercent[i];
      else if (param == "clay") val = historicalData.clayPercent[i];
      else if (param == "loamy") val = historicalData.loamyPercent[i];
      else if (param == "temp") val = historicalData.temperature[i];
      
      if (val > maxVal) maxVal = val;
    }
    return maxVal;
  }
  
  static float getAverageValue(const String& param) {
    float sum = 0;
    int count = 0;
    for (int i = 0; i < HISTORY_SIZE; i++) {
      float val = 0;
      if (param == "sandy") val = historicalData.sandyPercent[i];
      else if (param == "clay") val = historicalData.clayPercent[i];
      else if (param == "loamy") val = historicalData.loamyPercent[i];
      else if (param == "temp") val = historicalData.temperature[i];
      
      if (val > 0) {
        sum += val;
        count++;
      }
    }
    return (count > 0) ? sum / count : 0;
  }
  
  static void update() {
    // Periodic maintenance
    static unsigned long lastMaintenance = 0;
    unsigned long now = millis();
    
    if (now - lastMaintenance > 3600000) { // Every hour
      lastMaintenance = now;
      
      // Check and rotate files if needed
      if (currentDataFilename.length() > 0) {
        File dataFile = SD.open(currentDataFilename, FILE_READ);
        if (dataFile && dataFile.size() > calibration.sd.maxFileSize) {
          dataFile.close();
          createNewDataFile();
        } else if (dataFile) {
          dataFile.close();
        }
      }
      
      // Update used space
      if (systemState.sdCardPresent) {
        systemState.sdCardUsed = SD.usedBytes() / (1024 * 1024);
      }
    }
  }
};

// Initialize static members
unsigned long SDCardManager::lastLogTime = 0;
unsigned long SDCardManager::lastDataLogTime = 0;
unsigned long SDCardManager::lastEventLogTime = 0;
unsigned long SDCardManager::lastAlertLogTime = 0;
unsigned long SDCardManager::fileSize = 0;
int SDCardManager::fileCount = 0;
String SDCardManager::currentDataFilename = "";
String SDCardManager::currentEventFilename = "";
String SDCardManager::currentAlertFilename = "";
bool SDCardManager::headersWritten = false;

// =============================================================================
// GOOGLE SHEETS INTEGRATION
// =============================================================================

class GoogleSheetsManager {
public:
  static void logToGoogleSheets() {
    if (!systemState.googleSheetsEnabled || strlen(calibration.googleSheets.scriptURL) == 0) return;
    
    unsigned long currentMillis = millis();
    if (currentMillis - pumpStatus.lastGoogleSheetsLog < calibration.googleSheets.logInterval) return;
    
    pumpStatus.lastGoogleSheetsLog = currentMillis;
    
    HTTPClient http;
    String url = String(calibration.googleSheets.scriptURL);
    
    // Build URL with all sensor data
    url += "?timestamp=" + getDateTime();
    url += "&sandy=" + String(currentData.sandyPercent);
    url += "&sandy_raw=" + String(currentData.rawSandyMoisture);
    url += "&clay=" + String(currentData.clayPercent);
    url += "&clay_raw=" + String(currentData.rawClayMoisture);
    url += "&loamy=" + String(currentData.loamyPercent);
    url += "&loamy_raw=" + String(currentData.rawLoamyMoisture);
    url += "&temp=" + String(currentData.temperature);
    url += "&humidity=" + String(currentData.humidity);
    url += "&pressure=" + String(currentData.pressure);
    url += "&altitude=" + String(currentData.altitude);
    url += "&dewpoint=" + String(currentData.dewPoint);
    url += "&raining=" + String(currentData.isRaining ? "1" : "0");
    url += "&movement=" + String(currentData.deviceMoved ? "1" : "0");
    url += "&movement_intensity=" + String(currentData.movementIntensity);
    
    // Add pump data if enabled
    if (calibration.googleSheets.logPumps) {
      url += "&pump_sandy=" + String(pumpStatus.sandyActive ? "1" : "0");
      url += "&pump_clay=" + String(pumpStatus.clayActive ? "1" : "0");
      url += "&pump_loamy=" + String(pumpStatus.loamyActive ? "1" : "0");
      url += "&pump_sandy_manual=" + String(pumpStatus.sandyManual ? "1" : "0");
      url += "&pump_clay_manual=" + String(pumpStatus.clayManual ? "1" : "0");
      url += "&pump_loamy_manual=" + String(pumpStatus.loamyManual ? "1" : "0");
    }
    
    // Add system status
    url += "&wifi=" + String(systemState.wifiConnected ? "1" : "0");
    url += "&manual_mode=" + String(systemState.manualMode ? "1" : "0");
    url += "&security_alert=" + String(systemState.securityAlert ? "1" : "0");
    
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
      Serial.println("Data logged to Google Sheets successfully");
    } else {
      Serial.printf("Google Sheets log failed: %d\n", httpCode);
    }
    http.end();
  }
  
  static void testConnection() {
    if (strlen(calibration.googleSheets.scriptURL) == 0) {
      Serial.println("Google Sheets URL not set!");
      return;
    }
    
    HTTPClient http;
    String testURL = String(calibration.googleSheets.scriptURL) + "?test=1";
    http.begin(testURL);
    int httpCode = http.GET();
    if (httpCode > 0 && httpCode == HTTP_CODE_OK) {
      Serial.println(" Google Sheets connection successful!");
    } else {
      Serial.printf(" Google Sheets connection failed: %d\n", httpCode);
    }
    http.end();
  }
};

// =============================================================================
// TELEGRAM MANAGER - FULLY WORKING VERSION
// =============================================================================
// =============================================================================
// TELEGRAM MANAGER - WITH SECURITY ALERTS AND ACTIVATION
// =============================================================================

class TelegramManager {
private:
  static unsigned long lastUpdateId;
  static unsigned long lastPollTime;
  static bool initialized;
  static bool securityAlertSent;
  static unsigned long lastSecurityAlertTime;
  
  // URL encode function for Telegram messages
  static String urlencode(const String &s) {
    String o; 
    o.reserve(s.length() * 3);
    for (uint16_t i = 0; i < s.length(); i++) {
      uint8_t c = (uint8_t)s[i];
      if (c == ' ') o += "%20";
      else if (c == '\n') o += "%0A";
      else if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || 
               c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
      else { 
        char buf[4]; 
        snprintf(buf, sizeof(buf), "%%%02X", c); 
        o += buf; 
      }
    }
    return o;
  }
  
  // Send message to Telegram
  static bool sendMessage(const String &text) {
    if (!calibration.telegram.enabled) return false;
    if (strlen(calibration.telegram.chatId) == 0) return false;
    if (WiFi.status() != WL_CONNECTED) return false;
    
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(calibration.telegram.botToken) +
                 "/sendMessage?chat_id=" + String(calibration.telegram.chatId) +
                 "&text=" + urlencode(text) +
                 "&parse_mode=Markdown";
    
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    http.end();
    
    return (code == 200);
  }
  
  // Get system status as formatted message
  static String getSystemStatus() {
    String message = " *SYSTEM STATUS*\n\n";
    message += " *WiFi:* " + String(systemState.wifiConnected ? " Connected" : " Disconnected") + "\n";
    if (systemState.wifiConnected) {
      message += "    IP: " + systemState.ipAddress + "\n";
      message += "    Signal: " + String(WiFi.RSSI()) + " dBm\n";
    }
    message += " *Irrigation:* " + String(systemState.irrigationActive ? " ACTIVE" : " IDLE") + "\n";
    message += " *Mode:* " + String(systemState.manualMode ? " Manual" : " Auto") + "\n";
    message += " *Security:* " + String(systemState.securityEnabled ? " ENABLED" : " DISABLED") + "\n";
    message += " *Alert:* " + String(systemState.securityAlert ? " ACTIVE" : " NONE") + "\n";
    message += " *SD Card:* " + String(systemState.sdCardPresent ? " Present" : " Missing") + "\n";
    message += " *Google Sheets:* " + String(systemState.googleSheetsEnabled ? " Enabled" : " Disabled") + "\n";
    message += " *Uptime:* " + String(millis() / 86400000) + "d " + 
               String((millis() % 86400000) / 3600000) + "h " +
               String((millis() % 3600000) / 60000) + "m\n";
    if (systemState.ntpSynced) {
      message += " *Time:* " + getDateTime() + "\n";
    }
    return message;
  }
  
  // Get sensor readings
  static String getSensorReadings() {
    String message = " *SENSOR READINGS*\n\n";
    message += " *Soil Moisture:*\n";
    message += "    Sandy:  " + formatFloat(currentData.sandyPercent, 1) + "%";
    if (currentData.sandyPercent < calibration.irrigation.sandyWaterThreshold) message += " ⚠️ DRY";
    else if (currentData.sandyPercent > 80) message += "  WET";
    message += "\n";
    
    message += "    Clay:   " + formatFloat(currentData.clayPercent, 1) + "%";
    if (currentData.clayPercent < calibration.irrigation.clayWaterThreshold) message += " ⚠️ DRY";
    else if (currentData.clayPercent > 80) message += "  WET";
    message += "\n";
    
    message += "    Loamy:  " + formatFloat(currentData.loamyPercent, 1) + "%";
    if (currentData.loamyPercent < calibration.irrigation.loamyWaterThreshold) message += " ⚠️ DRY";
    else if (currentData.loamyPercent > 80) message += "  WET";
    message += "\n\n";
    
    message += " *Environment:*\n";
    message += "    Temperature: " + formatFloat(currentData.temperature, 1) + "°C\n";
    message += "    Humidity:    " + formatFloat(currentData.humidity, 1) + "%\n";
    message += "    Pressure:    " + formatFloat(currentData.pressure, 1) + " hPa\n";
    message += "    Altitude:    " + formatFloat(currentData.altitude, 1) + " m\n";
    
    if (!isnan(currentData.dewPoint)) {
      message += "    Dew Point:   " + formatFloat(currentData.dewPoint, 1) + "°C\n";
    }
    
    message += "\n☔ *Rain:* " + String(currentData.isRaining ? " YES" : "☀️ NO") + "\n";
    message += " *Movement:* " + String(currentData.deviceMoved ? " DETECTED (" + String(currentData.movementIntensity) + "%)" : " None");
    
    return message;
  }
  
  // Get pump status
  static String getPumpStatus() {
    String message = " *PUMP STATUS*\n\n";
    
    message += " *Sandy Soil Pump:*\n";
    message += "   Status: " + String(pumpStatus.sandyActive ? " RUNNING" : " OFF") + "\n";
    message += "   Mode: " + String(pumpStatus.sandyManual ? " Manual" : " Auto") + "\n";
    message += "   Runtime: " + String(pumpStatus.sandyRuntime / 60) + "m " + String(pumpStatus.sandyRuntime % 60) + "s\n";
    message += "   Threshold: " + formatFloat(calibration.irrigation.sandyWaterThreshold, 1) + "%\n\n";
    
    message += " *Clay Soil Pump:*\n";
    message += "   Status: " + String(pumpStatus.clayActive ? " RUNNING" : " OFF") + "\n";
    message += "   Mode: " + String(pumpStatus.clayManual ? "   Manual" : " Auto") + "\n";
    message += "   Runtime: " + String(pumpStatus.clayRuntime / 60) + "m " + String(pumpStatus.clayRuntime % 60) + "s\n";
    message += "   Threshold: " + formatFloat(calibration.irrigation.clayWaterThreshold, 1) + "%\n\n";
    
    message += "  *Loamy Soil Pump:*\n";
    message += "   Status: " + String(pumpStatus.loamyActive ? " RUNNING" : " OFF") + "\n";
    message += "   Mode: " + String(pumpStatus.loamyManual ? " Manual" : " Auto") + "\n";
    message += "   Runtime: " + String(pumpStatus.loamyRuntime / 60) + "m " + String(pumpStatus.loamyRuntime % 60) + "s\n";
    message += "   Threshold: " + formatFloat(calibration.irrigation.loamyWaterThreshold, 1) + "%\n";
    
    return message;
  }
  
  // Get security status
  static String getSecurityStatus() {
    String message = " *SECURITY STATUS*\n\n";
    message += "System Security: " + String(systemState.securityEnabled ? " ENABLED" : " DISABLED") + "\n";
    message += "Movement Detection: " + String(calibration.mpu6050.enabled ? " ACTIVE" : " INACTIVE") + "\n";
    message += "Movement Threshold: " + String(calibration.mpu6050.movementThreshold, 1) + "\n";
    message += "Alert Delay: " + String(calibration.mpu6050.tamperAlertDelay / 1000) + " seconds\n";
    message += "Buzzer: " + String(calibration.mpu6050.enableBuzzer ? " ENABLED" : " DISABLED") + "\n";
    message += "Telegram Alerts: " + String(calibration.mpu6050.sendAlerts ? " ENABLED" : " DISABLED") + "\n\n";
    
    if (systemState.securityAlert) {
      message += " *CURRENT ALERT ACTIVE!*\n";
      message += "Movement detected at: " + getDateTime() + "\n";
      message += "Intensity: " + String(currentData.movementIntensity) + "%\n";
    } else {
      message += " No active security alerts";
    }
    
    return message;
  }
  
  // Poll for incoming messages
  static void pollTelegram() {
    if (!calibration.telegram.enabled) return;
    if (strlen(calibration.telegram.botToken) == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + String(calibration.telegram.botToken) + "/getUpdates?timeout=0";
    if (lastUpdateId) url += "&offset=" + String(lastUpdateId + 1);
    
    http.begin(url);
    http.setTimeout(5000);
    int code = http.GET();
    
    if (code != 200) {
      http.end();
      return;
    }
    
    String res = http.getString();
    http.end();
    
    // Parse JSON response
    int pos = 0;
    while (true) {
      int u1 = res.indexOf("\"update_id\":", pos); 
      if (u1 < 0) break;
      int u2 = res.indexOf(",", u1); 
      if (u2 < 0) break;
      lastUpdateId = res.substring(u1 + 12, u2).toInt();
      
      int c1 = res.indexOf("\"chat\":{\"id\":", u2); 
      if (c1 < 0) { pos = u2; continue; }
      c1 += 13;
      int c2 = res.indexOf(",", c1); 
      if (c2 < 0) break;
      String chatIdRx = res.substring(c1, c2); 
      chatIdRx.trim();
      
      // Only accept commands from authorized chat ID
      if (chatIdRx != calibration.telegram.chatId) {
        pos = c2;
        continue;
      }
      
      int t1 = res.indexOf("\"text\":\"", c2); 
      if (t1 < 0) { pos = c2; continue; }
      t1 += 8;
      int t2 = res.indexOf("\"", t1); 
      if (t2 < 0) break;
      String text = res.substring(t1, t2);
      
      Serial.println("[Telegram] Command: " + text);
      
      // Process commands
      if (text == "/start") {
        String welcome = " *Secure & Optimized IoT-Based Irrigation System Bot*\n\n";
        welcome += "Welcome to your irrigation controller!\n\n";
        welcome += "*Available Commands:*\n";
        welcome += " /status - Get system status\n";
        welcome += " /sensors - Read all sensors\n";
        welcome += " /pumps - Check pump status\n";
        welcome += " /security - Check security status\n";
        welcome += " /security_on - Enable security system\n";
        welcome += " /security_off - Disable security system\n";
        welcome += " /water [soil] [on|off] - Control pumps\n";
        welcome += " /threshold [soil] [0-100] - Set moisture threshold\n";
        welcome += " /manual [on|off] - Enable/disable manual mode\n";
        welcome += " /rain [mm] - Simulate rainfall\n";
        welcome += " /stats - System statistics\n";
        welcome += " /restart - Restart the system\n";
        welcome += " /help - Show this help";
        sendMessage(welcome);
      }
      else if (text == "/status") {
        sendMessage(getSystemStatus());
      }
      else if (text == "/sensors") {
        sendMessage(getSensorReadings());
      }
      else if (text == "/pumps") {
        sendMessage(getPumpStatus());
      }
      else if (text == "/security") {
        sendMessage(getSecurityStatus());
      }
      else if (text == "/security_on") {
        calibration.mpu6050.enabled = true;
        systemState.securityEnabled = true;
        calibration.mpu6050.sendAlerts = true;
        PreferenceManager::saveCalibration();
        sendMessage(" *SECURITY SYSTEM ENABLED*\n\nMovement detection is now active.\nYou will receive alerts if movement is detected.");
        Serial.println("Security system enabled via Telegram");
      }
      else if (text == "/security_off") {
        calibration.mpu6050.enabled = false;
        systemState.securityEnabled = false;
        PreferenceManager::saveCalibration();
        sendMessage(" *SECURITY SYSTEM DISABLED*\n\nMovement detection is now off.\nNo alerts will be sent.");
        Serial.println("Security system disabled via Telegram");
      }
      else if (text.startsWith("/water ")) {
        String params = text.substring(7);
        params.trim();
        int spaceIndex = params.indexOf(' ');
        
        if (spaceIndex != -1) {
          String soil = params.substring(0, spaceIndex);
          String action = params.substring(spaceIndex + 1);
          action.trim();
          
          bool state = (action == "on");
          soil.toLowerCase();
          
          if (soil == "sandy" || soil == "clay" || soil == "loamy") {
            IrrigationController::setManualControl(soil, state);
            sendMessage(" " + soil + " pump turned " + String(state ? "ON" : "OFF"));
          } else {
            sendMessage(" Invalid soil type. Use: sandy, clay, or loamy");
          }
        } else {
          sendMessage(" Usage: /water [sandy|clay|loamy] [on|off]\nExample: /water sandy on");
        }
      }
      else if (text.startsWith("/threshold ")) {
        String params = text.substring(10);
        params.trim();
        int spaceIndex = params.indexOf(' ');
        
        if (spaceIndex != -1) {
          String soil = params.substring(0, spaceIndex);
          float threshold = params.substring(spaceIndex + 1).toFloat();
          
          if (threshold >= 0 && threshold <= 100) {
            if (soil == "sandy") {
              calibration.irrigation.sandyWaterThreshold = threshold;
            } else if (soil == "clay") {
              calibration.irrigation.clayWaterThreshold = threshold;
            } else if (soil == "loamy") {
              calibration.irrigation.loamyWaterThreshold = threshold;
            } else {
              sendMessage(" Invalid soil type. Use: sandy, clay, or loamy");
              return;
            }
            PreferenceManager::saveCalibration();
            sendMessage(" " + soil + " threshold set to " + formatFloat(threshold, 1) + "%");
          } else {
            sendMessage(" Threshold must be between 0 and 100");
          }
        } else {
          sendMessage(" Usage: /threshold [sandy|clay|loamy] [0-100]\nExample: /threshold sandy 50");
        }
      }
      else if (text.startsWith("/manual ")) {
        String state = text.substring(8);
        state.trim();
        bool enable = (state == "on");
        IrrigationController::setManualMode(enable);
        sendMessage(" Manual mode " + String(enable ? "enabled" : "disabled"));
      }
      else if (text.startsWith("/rain ")) {
        float mm = text.substring(6).toFloat();
        if (mm >= 0 && mm <= 100) {
          SensorManager::simulateRainfall(mm);
          sendMessage(" Rainfall simulation: " + formatFloat(mm, 1) + "mm\n Humidity increased, pressure decreased");
        } else {
          sendMessage(" Rainfall must be between 0 and 100 mm");
        }
      }
      else if (text == "/stats") {
        float avgSandy = 0, avgClay = 0, avgLoamy = 0;
        int count = 0;
        for (int i = 0; i < HISTORY_SIZE; i++) {
          if (historicalData.sandyPercent[i] > 0) {
            avgSandy += historicalData.sandyPercent[i];
            avgClay += historicalData.clayPercent[i];
            avgLoamy += historicalData.loamyPercent[i];
            count++;
          }
        }
        if (count > 0) {
          avgSandy /= count;
          avgClay /= count;
          avgLoamy /= count;
        }
        
        String message = " *SYSTEM STATISTICS*\n\n";
        message += " *Average Moisture (last " + String(count) + " readings):*\n";
        message += "   Sandy: " + formatFloat(avgSandy, 1) + "%\n";
        message += "   Clay:  " + formatFloat(avgClay, 1) + "%\n";
        message += "   Loamy: " + formatFloat(avgLoamy, 1) + "%\n\n";
        message += " *Pump Runtime:*\n";
        message += "   Sandy: " + String(pumpStatus.sandyRuntime / 60) + "m " + String(pumpStatus.sandyRuntime % 60) + "s\n";
        message += "   Clay:  " + String(pumpStatus.clayRuntime / 60) + "m " + String(pumpStatus.clayRuntime % 60) + "s\n";
        message += "   Loamy: " + String(pumpStatus.loamyRuntime / 60) + "m " + String(pumpStatus.loamyRuntime % 60) + "s\n\n";
        message += " *Security Events:* " + String(calibration.mpu6050.sendAlerts ? "Alerts enabled" : "Alerts disabled") + "\n";
        message += " *Uptime:* " + String(millis() / 86400000) + "d " + 
                   String((millis() % 86400000) / 3600000) + "h " +
                   String((millis() % 3600000) / 60000) + "m\n";
        if (systemState.wifiConnected) {
          message += " *Signal:* " + String(WiFi.RSSI()) + " dBm\n";
        }
        sendMessage(message);
      }
      else if (text == "/restart") {
        sendMessage(" System restarting...");
        delay(1000);
        ESP.restart();
      }
      else if (text == "/help") {
        String help = " *Available Commands:*\n\n";
        help += "*System:*\n";
        help += "/status - System status\n";
        help += "/sensors - Sensor readings\n";
        help += "/pumps - Pump status\n";
        help += "/stats - Statistics\n\n";
        help += "*Security:*\n";
        help += "/security - Security status\n";
        help += "/security_on - Enable security\n";
        help += "/security_off - Disable security\n\n";
        help += "*Control:*\n";
        help += "/water [soil] [on|off] - Control pumps\n";
        help += "/threshold [soil] [0-100] - Set threshold\n";
        help += "/manual [on|off] - Manual mode\n";
        help += "/rain [mm] - Simulate rain\n\n";
        help += "*Other:*\n";
        help += "/restart - Restart system\n";
        help += "/help - This help";
        sendMessage(help);
      }
      else {
        sendMessage(" Unknown command\nUse /help to see available commands");
      }
      
      pos = t2;
    }
  }
  
public:
  static void initialize() {
    if (!calibration.telegram.enabled) {
      Serial.println(" Telegram bot disabled");
      return;
    }
    
    if (strlen(calibration.telegram.botToken) == 0) {
      Serial.println(" Telegram bot token not configured!");
      Serial.println("   Set token via: TELEGRAM TOKEN <your_token>");
      return;
    }
    
    if (strlen(calibration.telegram.chatId) == 0) {
      Serial.println("⚠️ Telegram chat ID not configured!");
      Serial.println("   Set chat ID via: TELEGRAM CHATID <your_chat_id>");
      Serial.println("\n   To get your chat ID:");
      Serial.println("   1. Open Telegram and search for @userinfobot");
      Serial.println("   2. Send /start to @userinfobot");
      Serial.println("   3. It will show your chat ID");
      return;
    }
    
    if (!systemState.wifiConnected) {
      Serial.println(" Telegram waiting for WiFi...");
      return;
    }
    
    Serial.println("\n Initializing Telegram Bot (Direct API)...");
    Serial.print("   Token: ");
    Serial.println(String(calibration.telegram.botToken).substring(0, 20) + "...");
    Serial.print("   Chat ID: ");
    Serial.println(calibration.telegram.chatId);
    
    // Test connection by sending a message
    String testMsg = " *Secure & Optimized IoT-Based Irrigation System Bot*\n\n Bot initialized and ready!\n\n";
    testMsg += "*Security Status:* " + String(systemState.securityEnabled ? " ENABLED" : " DISABLED") + "\n";
    testMsg += "\nType /help for commands";
    
    if (sendMessage(testMsg)) {
      initialized = true;
      lastUpdateId = 0;
      lastPollTime = 0;
      securityAlertSent = false;
      lastSecurityAlertTime = 0;
      Serial.println(" Telegram bot initialized successfully!");
      Serial.println(" Test message sent to your Telegram");
    } else {
      initialized = false;
      Serial.println(" Failed to initialize Telegram bot!");
      Serial.println("   Check your token and chat ID");
      Serial.println("   Make sure you started a conversation with the bot");
    }
  }
  
  static void update() {
    if (!initialized || !calibration.telegram.enabled) return;
    if (!systemState.wifiConnected) return;
    
    // Poll for messages
    unsigned long currentMillis = millis();
    if (currentMillis - lastPollTime > 1500) {
      lastPollTime = currentMillis;
      pollTelegram();
    }
    
    // Handle security alerts
    if (systemState.securityEnabled && calibration.mpu6050.enabled && calibration.mpu6050.sendAlerts) {
      if (systemState.securityAlert && !securityAlertSent) {
        // Send immediate alert
        String alertMsg = " *SECURITY ALERT!* \n\n";
        alertMsg += " Movement detected on the device!\n";
        alertMsg += " Intensity: " + String(currentData.movementIntensity) + "%\n";
        alertMsg += " Time: " + getDateTime() + "\n";
        alertMsg += " IP: " + systemState.ipAddress + "\n\n";
        alertMsg += "Action taken:\n";
        alertMsg += "• Buzzer activated\n";
        alertMsg += "• Alert logged\n";
        alertMsg += "• You are being notified\n\n";
        alertMsg += "Use /security to check status";
        
        if (sendMessage(alertMsg)) {
          securityAlertSent = true;
          lastSecurityAlertTime = currentMillis;
          Serial.println(" Security alert sent to Telegram");
        }
      }
      
      // Clear alert after delay
      if (securityAlertSent && !systemState.securityAlert) {
        securityAlertSent = false;
        String clearMsg = " *SECURITY ALERT CLEARED*\n\n";
        clearMsg += "No movement detected.\n";
        clearMsg += "System back to normal.";
        sendMessage(clearMsg);
        Serial.println(" Security alert cleared notification sent");
      }
      
      // Re-send alert if still active after cooldown (for continuous movement)
      if (securityAlertSent && systemState.securityAlert && 
          (currentMillis - lastSecurityAlertTime > calibration.mpu6050.tamperAlertDelay)) {
        lastSecurityAlertTime = currentMillis;
        String repeatMsg = "⚠️ *MOVEMENT CONTINUES*\n\n";
        repeatMsg += "Still detecting movement!\n";
        repeatMsg += "Intensity: " + String(currentData.movementIntensity) + "%\n";
        repeatMsg += "Please check the device immediately!";
        sendMessage(repeatMsg);
        Serial.println(" Repeat security alert sent");
      }
    }
  }
  
  static void sendAlert(String message) {
    if (!initialized || !calibration.telegram.enabled) return;
    if (!systemState.securityEnabled) return;
    if (!calibration.mpu6050.sendAlerts) return;
    
    unsigned long currentMillis = millis();
    if (currentMillis - calibration.telegram.lastNotifyTime > calibration.telegram.notifyInterval) {
      calibration.telegram.lastNotifyTime = currentMillis;
      sendMessage("⚠️ *ALERT:* " + message);
    }
  }
  
  static void sendNotification(String message, bool force = false) {
    if (!initialized || !calibration.telegram.enabled) return;
    
    if (force) {
      sendMessage(message);
    } else {
      unsigned long currentMillis = millis();
      if (currentMillis - calibration.telegram.lastNotifyTime > calibration.telegram.notifyInterval) {
        calibration.telegram.lastNotifyTime = currentMillis;
        sendMessage(message);
      }
    }
  }
  
  static void enable() {
    calibration.telegram.enabled = true;
    PreferenceManager::saveCalibration();
    initialized = false;
    initialize();
  }
  
  static void disable() {
    calibration.telegram.enabled = false;
    initialized = false;
    Serial.println(" Telegram bot disabled");
  }
  
  static void setBotToken(String token) {
    token.trim();
    if (token.length() > 0 && token.indexOf(':') > 0) {
      strncpy(calibration.telegram.botToken, token.c_str(), sizeof(calibration.telegram.botToken) - 1);
      PreferenceManager::saveCalibration();
      Serial.println(" Telegram bot token set");
      if (calibration.telegram.enabled) {
        initialized = false;
        initialize();
      }
    } else {
      Serial.println(" Invalid token format. Token should contain ':'");
      Serial.println("   Example: 1234567890:ABCdefGHIjklmNOPqrstUvwxyz");
    }
  }
  
  static void setChatId(String chatId) {
    chatId.trim();
    if (chatId.length() > 0 && isDigit(chatId.charAt(0))) {
      strncpy(calibration.telegram.chatId, chatId.c_str(), sizeof(calibration.telegram.chatId) - 1);
      PreferenceManager::saveCalibration();
      Serial.println(" Telegram chat ID set to: " + String(calibration.telegram.chatId));
      
      if (initialized) {
        String msg = " Chat ID configured!\n\n";
        msg += "Security alerts are " + String(systemState.securityEnabled ? "ENABLED" : "DISABLED") + "\n";
        msg += "Type /help for commands";
        sendMessage(msg);
      }
    } else {
      Serial.println(" Invalid chat ID. Chat ID should be a number (e.g., 123456789)");
      Serial.println("   To get your chat ID:");
      Serial.println("   1. Open Telegram and search for @userinfobot");
      Serial.println("   2. Send /start to @userinfobot");
      Serial.println("   3. It will show your chat ID");
    }
  }
  
  static void test() {
    if (!initialized || !calibration.telegram.enabled) {
      Serial.println(" Telegram not initialized or disabled");
      Serial.println("   Check: TELEGRAM STATUS");
      return;
    }
    
    String testMsg = " *Test Message*\n\n";
    testMsg += "If you received this, Telegram is working correctly!\n\n";
    testMsg += "*System Status:*\n";
    testMsg += "• Security: " + String(systemState.securityEnabled ? "ENABLED" : "DISABLED") + "\n";
    testMsg += "• WiFi: " + String(systemState.wifiConnected ? "Connected" : "Disconnected") + "\n";
    testMsg += "• Uptime: " + String(millis() / 3600000) + " hours";
    
    if (sendMessage(testMsg)) {
      Serial.println(" Test message sent to Telegram");
    } else {
      Serial.println(" Failed to send test message");
    }
  }
  
  static void showStatus() {
    Serial.println("\n Telegram Bot Status:");
    Serial.printf("   Enabled: %s\n", calibration.telegram.enabled ? "YES" : "NO");
    Serial.printf("   Initialized: %s\n", initialized ? "YES" : "NO");
    Serial.printf("   Token: %s\n", strlen(calibration.telegram.botToken) > 0 ? "***SET***" : "NOT SET");
    Serial.printf("   Chat ID: %s\n", strlen(calibration.telegram.chatId) > 0 ? calibration.telegram.chatId : "NOT SET");
    Serial.printf("   WiFi: %s\n", systemState.wifiConnected ? "Connected" : "Disconnected");
    Serial.printf("   Security System: %s\n", systemState.securityEnabled ? "ENABLED" : "DISABLED");
    Serial.printf("   Security Alerts: %s\n", calibration.mpu6050.sendAlerts ? "ENABLED" : "DISABLED");
    
    if (calibration.telegram.enabled && strlen(calibration.telegram.botToken) > 0 && strlen(calibration.telegram.chatId) > 0 && !initialized) {
      Serial.println("\n Bot not initialized. Try: TELEGRAM OFF then TELEGRAM ON");
    }
    
    if (initialized && systemState.securityEnabled) {
      Serial.println("\n Security monitoring ACTIVE");
      Serial.println("   You will receive alerts if movement is detected");
    }
  }
  
  static bool isEnabled() {
    return initialized && calibration.telegram.enabled;
  }
  
  // Method to manually trigger security alert (for testing)
  static void triggerTestAlert() {
    if (!initialized || !calibration.telegram.enabled) {
      Serial.println(" Telegram not initialized");
      return;
    }
    
    String testAlert = " *TEST ALERT*\n\n";
    testAlert += "This is a test security alert.\n";
    testAlert += "If you received this, security alerts are working correctly!";
    sendMessage(testAlert);
    Serial.println(" Test alert sent to Telegram");
  }
};

// Initialize static members
unsigned long TelegramManager::lastUpdateId = 0;
unsigned long TelegramManager::lastPollTime = 0;
bool TelegramManager::initialized = false;
bool TelegramManager::securityAlertSent = false;
unsigned long TelegramManager::lastSecurityAlertTime = 0;


// =============================================================================
// WEB SOCKET MANAGER
// =============================================================================

class WebSocketManager {
private:
  static void processCommand(uint8_t clientNum, String message) {
    StaticJsonDocument<2048> doc;
    DeserializationError error = deserializeJson(doc, message);
    if (error) {
      sendAlert(clientNum, "Invalid JSON format");
      return;
    }
    
    String type = doc["type"] | "";
    
    // ========== DATA RETRIEVAL COMMANDS ==========
    if (type == "get_sensors") {
      sendSensorData(clientNum);
    }
    else if (type == "get_pumps") {
      sendPumpStatus(clientNum);
    }
    else if (type == "get_system") {
      sendSystemStatus(clientNum);
    }
    else if (type == "get_calibration") {
      sendCalibration(clientNum);
    }
    else if (type == "get_settings") {
      sendSettings(clientNum);
    }
    
    // ========== PUMP CONTROL COMMANDS ==========
    else if (type == "set_pump") {
      String pump = doc["pump"] | "";
      bool state = doc["state"] | false;
      IrrigationController::setManualControl(pump, state);
      PreferenceManager::saveCalibration();
      sendPumpStatus(clientNum);
      sendAlert(clientNum, pump + " pump " + (state ? "ON" : "OFF"));
    }
    else if (type == "set_manual_mode") {
      bool enabled = doc["enabled"] | false;
      IrrigationController::setManualMode(enabled);
      PreferenceManager::saveCalibration();
      sendSystemStatus(clientNum);
      sendAlert(clientNum, enabled ? "Manual mode enabled" : "Auto mode enabled");
    }
    else if (type == "emergency_stop") {
      IrrigationController::emergencyShutdown();
      sendAlert(clientNum, "EMERGENCY STOP - All pumps halted!");
      sendPumpStatus(clientNum);
    }
    else if (type == "test_pumps") {
      IrrigationController::testPumps();
      sendAlert(clientNum, "Pump test completed");
    }
    
    // ========== IRRIGATION SETTINGS COMMANDS ==========
    else if (type == "set_pump_threshold") {
      String soil = doc["soil"] | "";
      int threshold = doc["threshold"] | 40;
      if (soil == "sandy") {
        calibration.irrigation.sandyWaterThreshold = threshold;
        sendAlert(clientNum, "Sandy soil threshold set to " + String(threshold) + "%");
      } else if (soil == "clay") {
        calibration.irrigation.clayWaterThreshold = threshold;
        sendAlert(clientNum, "Clay soil threshold set to " + String(threshold) + "%");
      } else if (soil == "loamy") {
        calibration.irrigation.loamyWaterThreshold = threshold;
        sendAlert(clientNum, "Loamy soil threshold set to " + String(threshold) + "%");
      }
      PreferenceManager::saveCalibration();
      sendCalibration(clientNum);
    }
    else if (type == "set_irrigation_settings") {
      if (doc.containsKey("sandy_threshold")) {
        calibration.irrigation.sandyWaterThreshold = doc["sandy_threshold"].as<float>();
      }
      if (doc.containsKey("clay_threshold")) {
        calibration.irrigation.clayWaterThreshold = doc["clay_threshold"].as<float>();
      }
      if (doc.containsKey("loamy_threshold")) {
        calibration.irrigation.loamyWaterThreshold = doc["loamy_threshold"].as<float>();
      }
      if (doc.containsKey("watering_duration")) {
        calibration.irrigation.wateringDuration = doc["watering_duration"].as<int>() * 1000;
      }
      if (doc.containsKey("watering_interval")) {
        calibration.irrigation.minWateringInterval = doc["watering_interval"].as<int>() * 60 * 1000;
      }
      if (doc.containsKey("max_runtime")) {
        calibration.irrigation.maxPumpRuntime = doc["max_runtime"].as<int>() * 1000;
      }
      if (doc.containsKey("check_rain")) {
        calibration.irrigation.checkRainCondition = doc["check_rain"].as<bool>();
      }
      if (doc.containsKey("check_temp")) {
        calibration.irrigation.checkTemperature = doc["check_temp"].as<bool>();
      }
      if (doc.containsKey("temp_high")) {
        calibration.irrigation.tempThresholdHigh = doc["temp_high"].as<float>();
      }
      if (doc.containsKey("temp_low")) {
        calibration.irrigation.tempThresholdLow = doc["temp_low"].as<float>();
      }
      
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "Irrigation settings saved");
      sendSystemStatus(clientNum);
    }
    
    // ========== SOIL CALIBRATION COMMANDS ==========
    else if (type == "calibrate_soil_dry") {
      String soil = doc["soil"] | "";
      SensorManager::autoCalibrateDry(soil);
      PreferenceManager::saveCalibration();
      sendCalibration(clientNum);
      sendAlert(clientNum, "Soil " + soil + " DRY calibration completed");
    }
    else if (type == "calibrate_soil_wet") {
      String soil = doc["soil"] | "";
      SensorManager::autoCalibrateWet(soil);
      PreferenceManager::saveCalibration();
      sendCalibration(clientNum);
      sendAlert(clientNum, "Soil " + soil + " WET calibration completed");
    }
    else if (type == "reset_soil_calibration") {
      String soil = doc["soil"] | "";
      SensorManager::resetSoilCalibration(soil);
      PreferenceManager::saveCalibration();
      sendCalibration(clientNum);
      sendAlert(clientNum, "Soil " + soil + " calibration reset to defaults");
    }
    
    // ========== ENVIRONMENTAL CALIBRATION COMMANDS ==========
    else if (type == "calibrate_temperature") {
      float refValue = doc["value"] | NAN;
      if (!isnan(refValue)) {
        SensorManager::calibrateTemperature(refValue);
        PreferenceManager::saveCalibration();
        sendAlert(clientNum, "Temperature calibrated to " + String(refValue, 1) + "°C");
        sendCalibration(clientNum);
      }
    }
    else if (type == "calibrate_humidity") {
      float refValue = doc["value"] | NAN;
      if (!isnan(refValue)) {
        SensorManager::calibrateHumidity(refValue);
        PreferenceManager::saveCalibration();
        sendAlert(clientNum, "Humidity calibrated to " + String(refValue, 1) + "%");
        sendCalibration(clientNum);
      }
    }
    else if (type == "calibrate_pressure_direct") {
      float refValue = doc["value"] | NAN;
      if (!isnan(refValue)) {
        SensorManager::calibratePressureDirect(refValue);
        PreferenceManager::saveCalibration();
        sendAlert(clientNum, "Pressure calibrated directly to " + String(refValue, 1) + " hPa");
        sendCalibration(clientNum);
      }
    }
    
    // ========== MPU6050 COMMANDS ==========
    else if (type == "calibrate_mpu") {
      SensorManager::getMPU6050().calibrate(5000);
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "MPU6050 calibration completed");
      sendCalibration(clientNum);
    }
    
    // ========== SECURITY COMMANDS ==========
    else if (type == "toggle_security") {
      calibration.mpu6050.enabled = !calibration.mpu6050.enabled;
      systemState.securityEnabled = calibration.mpu6050.enabled;
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "Security " + String(calibration.mpu6050.enabled ? "enabled" : "disabled"));
      sendSystemStatus(clientNum);
    }
    else if (type == "toggle_buzzer") {
      calibration.buzzer.enabled = !calibration.buzzer.enabled;
      systemState.buzzerEnabled = calibration.buzzer.enabled;
      if (calibration.buzzer.enabled) BuzzerManager::enable();
      else BuzzerManager::disable();
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "Buzzer " + String(calibration.buzzer.enabled ? "enabled" : "disabled"));
    }
    
    // ========== GOOGLE SHEETS COMMANDS ==========
    else if (type == "set_google_sheets") {
      bool enabled = doc["enabled"] | false;
      String url = doc["url"] | "";
      int interval = doc["interval"] | 60;
      
      calibration.googleSheets.enabled = enabled;
      if (url.length() > 0) {
        strncpy(calibration.googleSheets.scriptURL, url.c_str(), sizeof(calibration.googleSheets.scriptURL) - 1);
      }
      calibration.googleSheets.logInterval = interval * 1000;
      systemState.googleSheetsEnabled = enabled;
      
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "Google Sheets " + String(enabled ? "enabled" : "disabled"));
      sendSettings(clientNum);
    }
    else if (type == "test_google_sheets") {
      GoogleSheetsManager::testConnection();
      sendAlert(clientNum, "Google Sheets test completed");
    }
    else if (type == "manual_upload") {
      GoogleSheetsManager::logToGoogleSheets();
      sendAlert(clientNum, "Manual upload triggered");
    }
    
    // ========== TELEGRAM COMMANDS ==========
    else if (type == "telegram_enable") {
      TelegramManager::enable();
      sendAlert(clientNum, "Telegram bot enabled");
    }
    else if (type == "telegram_disable") {
      TelegramManager::disable();
      sendAlert(clientNum, "Telegram bot disabled");
    }
    else if (type == "telegram_set_token") {
      String token = doc["token"] | "";
      if (token.length() > 0) {
        TelegramManager::setBotToken(token);
        sendAlert(clientNum, "Telegram token set. Bot restarting...");
      }
    }
    else if (type == "telegram_test") {
      if (TelegramManager::isEnabled() && strlen(calibration.telegram.chatId) > 0) {
        TelegramManager::sendNotification("Test message from web interface", true);
        sendAlert(clientNum, "Test message sent to Telegram");
      } else {
        sendAlert(clientNum, "Telegram not configured properly");
      }
    }
    else if (type == "telegram_settings") {
      JsonObject settings = doc["settings"];
      if (settings.containsKey("notify_alerts")) {
        calibration.telegram.notifyAlerts = settings["notify_alerts"];
      }
      if (settings.containsKey("notify_movement")) {
        calibration.telegram.notifyMovement = settings["notify_movement"];
      }
      if (settings.containsKey("notify_rain")) {
        calibration.telegram.notifyRain = settings["notify_rain"];
      }
      if (settings.containsKey("notify_pump")) {
        calibration.telegram.notifyPumpStatus = settings["notify_pump"];
      }
      if (settings.containsKey("notify_interval")) {
        calibration.telegram.notifyInterval = settings["notify_interval"].as<int>() * 60000;
      }
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "Telegram notification settings saved");
    }
    
    // ========== WIFI COMMANDS ==========
    else if (type == "scan_wifi") {
      WiFiManager::scanNetworks();
      sendAlert(clientNum, "Scanning for WiFi networks...");
    }
    else if (type == "set_wifi") {
      String ssid = doc["ssid"] | "";
      String password = doc["password"] | "";
      if (WiFiManager::setCredentials(ssid.c_str(), password.c_str())) {
        sendAlert(clientNum, "WiFi credentials updated, reconnecting...");
      }
    }
    else if (type == "wifi_reset") {
      WiFiManager::clearCredentials();
      sendAlert(clientNum, "WiFi credentials cleared");
    }
    
    // ========== SYSTEM COMMANDS ==========
    else if (type == "save_calibration") {
      PreferenceManager::saveCalibration();
      sendAlert(clientNum, "All calibration saved to flash");
    }
    else if (type == "reset_calibration") {
      PreferenceManager::deleteCalibration();
      sendAlert(clientNum, "Factory reset performed - restarting...");
      delay(1000);
      ESP.restart();
    }
    else if (type == "restart") {
      sendAlert(clientNum, "System restarting...");
      delay(500);
      ESP.restart();
    }
    else if (type == "simulate_rainfall") {
      float rainfallMM = doc["value"] | 10.0;
      SensorManager::simulateRainfall(rainfallMM);
      sendAlert(clientNum, "Rain simulation: " + String(rainfallMM, 1) + "mm rainfall");
    }
  }
  
  // ========== SEND FUNCTIONS ==========
  
  static void sendSensorData(uint8_t clientNum = 0) {
    StaticJsonDocument<2048> doc;
    doc["type"] = "sensor_data";
    doc["timestamp"] = currentData.timestamp;
    doc["sandyPercent"] = currentData.sandyPercent;
    doc["clayPercent"] = currentData.clayPercent;
    doc["loamyPercent"] = currentData.loamyPercent;
    doc["rawSandy"] = currentData.rawSandyMoisture;
    doc["rawClay"] = currentData.rawClayMoisture;
    doc["rawLoamy"] = currentData.rawLoamyMoisture;
    doc["temperature"] = currentData.temperature;
    doc["humidity"] = currentData.humidity;
    doc["pressure"] = currentData.pressure;
    doc["altitude"] = currentData.altitude;
    doc["dewPoint"] = currentData.dewPoint;
    doc["isRaining"] = currentData.isRaining;
    doc["deviceMoved"] = currentData.deviceMoved;
    doc["movementIntensity"] = currentData.movementIntensity;
    doc["accelerationX"] = currentData.accelerationX;
    doc["accelerationY"] = currentData.accelerationY;
    doc["accelerationZ"] = currentData.accelerationZ;
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendPumpStatus(uint8_t clientNum = 0) {
    StaticJsonDocument<512> doc;
    doc["type"] = "pump_status";
    doc["sandyActive"] = pumpStatus.sandyActive;
    doc["clayActive"] = pumpStatus.clayActive;
    doc["loamyActive"] = pumpStatus.loamyActive;
    doc["sandyManual"] = pumpStatus.sandyManual;
    doc["clayManual"] = pumpStatus.clayManual;
    doc["loamyManual"] = pumpStatus.loamyManual;
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendSystemStatus(uint8_t clientNum = 0) {
    StaticJsonDocument<1024> doc;
    doc["type"] = "system_status";
    doc["wifiConnected"] = systemState.wifiConnected;
    doc["ipAddress"] = systemState.ipAddress;
    doc["uptime"] = millis() / 1000;
    doc["securityAlert"] = systemState.securityAlert;
    doc["manualMode"] = systemState.manualMode;
    doc["irrigationActive"] = systemState.irrigationActive;
    doc["apModeActive"] = systemState.apModeActive;
    doc["googleSheetsEnabled"] = systemState.googleSheetsEnabled;
    doc["waterThresholdSandy"] = calibration.irrigation.sandyWaterThreshold;
    doc["waterThresholdClay"] = calibration.irrigation.clayWaterThreshold;
    doc["waterThresholdLoamy"] = calibration.irrigation.loamyWaterThreshold;
    doc["wateringDuration"] = calibration.irrigation.wateringDuration / 1000;
    doc["wateringInterval"] = calibration.irrigation.minWateringInterval / 1000 / 60;
    doc["maxRuntime"] = calibration.irrigation.maxPumpRuntime / 1000;
    doc["checkRain"] = calibration.irrigation.checkRainCondition;
    doc["checkTemp"] = calibration.irrigation.checkTemperature;
    doc["tempHigh"] = calibration.irrigation.tempThresholdHigh;
    doc["tempLow"] = calibration.irrigation.tempThresholdLow;
    doc["telegramEnabled"] = calibration.telegram.enabled;
    doc["telegramConfigured"] = (strlen(calibration.telegram.botToken) > 0 && strlen(calibration.telegram.chatId) > 0);
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendCalibration(uint8_t clientNum = 0) {
    StaticJsonDocument<2048> doc;
    doc["type"] = "calibration";
    doc["sandy_dry"] = calibration.sandy.rawDry;
    doc["sandy_wet"] = calibration.sandy.rawWet;
    doc["clay_dry"] = calibration.clay.rawDry;
    doc["clay_wet"] = calibration.clay.rawWet;
    doc["loamy_dry"] = calibration.loamy.rawDry;
    doc["loamy_wet"] = calibration.loamy.rawWet;
    doc["water_threshold_sandy"] = calibration.irrigation.sandyWaterThreshold;
    doc["water_threshold_clay"] = calibration.irrigation.clayWaterThreshold;
    doc["water_threshold_loamy"] = calibration.irrigation.loamyWaterThreshold;
    doc["movement_threshold"] = calibration.mpu6050.movementThreshold;
    doc["security_enabled"] = calibration.mpu6050.enabled;
    doc["buzzer_enabled"] = calibration.buzzer.enabled;
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendSettings(uint8_t clientNum = 0) {
    StaticJsonDocument<1024> doc;
    doc["type"] = "settings";
    doc["sensor_interval"] = calibration.system.sensorReadInterval / 1000;
    doc["ntp_server"] = calibration.ntp.server;
    doc["timezone"] = calibration.ntp.timezoneOffset;
    doc["google_enabled"] = calibration.googleSheets.enabled;
    doc["google_url"] = calibration.googleSheets.scriptURL;
    doc["google_interval"] = calibration.googleSheets.logInterval / 1000;
    doc["ntp_enabled"] = calibration.ntp.enabled;
    doc["enable_leds"] = calibration.system.enableLEDs;
    doc["enable_serial"] = calibration.system.enableSerialOutput;
    doc["telegram_enabled"] = calibration.telegram.enabled;
    doc["telegram_notify_alerts"] = calibration.telegram.notifyAlerts;
    doc["telegram_notify_movement"] = calibration.telegram.notifyMovement;
    doc["telegram_notify_rain"] = calibration.telegram.notifyRain;
    doc["telegram_notify_pump"] = calibration.telegram.notifyPumpStatus;
    doc["telegram_interval"] = calibration.telegram.notifyInterval / 60000;
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendAlert(uint8_t clientNum, String message) {
    StaticJsonDocument<256> doc;
    doc["type"] = "alert";
    doc["message"] = message;
    doc["timestamp"] = getTimestamp();
    
    String jsonString;
    serializeJson(doc, jsonString);
    if (clientNum == 0) webSocket.broadcastTXT(jsonString);
    else webSocket.sendTXT(clientNum, jsonString);
  }
  
  static void sendAlert(String message) {
    sendAlert(0, message);
  }
  
public:
  static void initialize() {
    webSocket.begin();
    webSocket.onEvent([](uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
      if (type == WStype_TEXT) {
        String message = String((char*)payload);
        processCommand(num, message);
      }
    });
    Serial.printf("WebSocket server started on port %d\n", WEB_SOCKET_PORT);
  }
  
  static void update() {
    webSocket.loop();
  }
  
  // Public wrapper methods
  static void broadcastSensorData() {
    sendSensorData(0);
  }
  
  static void broadcastPumpStatus() {
    sendPumpStatus(0);
  }
  
  static void broadcastSystemStatus() {
    sendSystemStatus(0);
  }
  
  static void broadcastAlert(String message) {
    sendAlert(message);
  }
};

// =============================================================================
// WEB SERVER MANAGER
// =============================================================================

class WebServerManager {
private:
  static void initializeWebFiles() {
    if (!LittleFS.exists("/index.html")) {
      Serial.println("ERROR: Web files not found in LittleFS!");
      File file = LittleFS.open("/index.html", "w");
      if (file) {
        file.print("<!DOCTYPE html><html><head><title>.::SOIBIS</title></head>");
        file.print("<body><h1>Secured & Optimized IoT-Based Irrigation System</h1>");
        file.print("<p>Web interface requires LittleFS files.</p></body></html>");
        file.close();
      }
    }
  }
  
public:
  static void initialize() {
    if (!calibration.webserver.enabled) {
      Serial.println("Web server disabled");
      return;
    }
    
    if (!LittleFS.begin(true)) {
      Serial.println("Failed to mount LittleFS");
      return;
    }
    
    initializeWebFiles();
    WebSocketManager::initialize();
    
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");
    server.serveStatic("/css/", LittleFS, "/css/");
    server.serveStatic("/js/", LittleFS, "/js/");
    
    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json = "{";
      json += "\"wifi_connected\":" + String(systemState.wifiConnected) + ",";
      json += "\"ip_address\":\"" + systemState.ipAddress + "\",";
      json += "\"uptime\":" + String(millis() / 1000) + "}";
      request->send(200, "application/json", json);
    });
    
    server.on("/api/sensors", HTTP_GET, [](AsyncWebServerRequest *request) {
      String json = "{";
      json += "\"sandy_percent\":" + String(currentData.sandyPercent) + ",";
      json += "\"clay_percent\":" + String(currentData.clayPercent) + ",";
      json += "\"loamy_percent\":" + String(currentData.loamyPercent) + ",";
      json += "\"temperature\":" + String(currentData.temperature) + ",";
      json += "\"humidity\":" + String(currentData.humidity) + "}";
      request->send(200, "application/json", json);
    });
    
    server.begin();
    systemState.webServerRunning = true;
    Serial.printf("Web server started on http://%s\n", systemState.ipAddress.c_str());
  }
};

// =============================================================================
// QUICK START GUIDE
// =============================================================================

void showQuickStartGuide() {
  Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
  Serial.println("║           SECURED & OPTIMIZED IOT-BASED IRRIGATION SYSTEM         ║");
  Serial.println("║                        QUICK START GUIDE                          ║");
  Serial.println("╚══════════════════════════════════════════════════════════════════╝");
  Serial.println("\n FIRST TIME SETUP:");
  Serial.println("   1. Type 'WIFI CONFIG' to set up WiFi connection");
  Serial.println("   2. After WiFi connects, access web interface at displayed IP");
  Serial.println("   3. If no WiFi configured, AP Mode will start automatically");
  Serial.println("   4. Connect to 'SmartIrrigation' WiFi (password: admin123)");
  Serial.println("   5. Open http://192.168.4.1 in your browser");
  Serial.println("\n COMMON COMMANDS:");
  Serial.println("   HELP        - Show all commands");
  Serial.println("   STATUS      - Show system status");
  Serial.println("   SENSORS     - Show current sensor readings");
  Serial.println("   RAIN 10     - Simulate 10mm of rainfall");
  Serial.println("   SAVE        - Save current calibration");
  Serial.println("   WIFI STATUS - Show WiFi status");
  Serial.println("\n CALIBRATION:");
  Serial.println("   CAL SANDY DRY   - Calibrate sandy soil when dry");
  Serial.println("   CAL SANDY WET   - Calibrate sandy soil when wet");
  Serial.println("   CAL PRESS DIRECT [value] - Calibrate pressure with known value");
  Serial.println("   CAL ALT DIRECT [value]   - Calibrate altitude with known value");
  Serial.println("\n PUMP CONTROL:");
  Serial.println("   MANUAL ON       - Enable manual control mode");
  Serial.println("   PUMP SANDY ON   - Turn on sandy soil pump");
  Serial.println("   STOP            - Emergency stop all pumps");
  Serial.println("\n TELEGRAM BOT:");
  Serial.println("   TELEGRAM TOKEN [token] - Set Telegram bot token");
  Serial.println("   TELEGRAM ON             - Enable Telegram bot");
  Serial.println("   TELEGRAM STATUS         - Show Telegram status");
  Serial.println("   Send any message to your bot to set chat ID");
  Serial.println("\n╔══════════════════════════════════════════════════════════════════╗");
  Serial.println("║  Type 'HELP' for complete command list                          ║");
  Serial.println("╚══════════════════════════════════════════════════════════════════╝\n");
}

// =============================================================================
// SERIAL COMMAND PROCESSOR
// =============================================================================

void processSerialCommand(String command) {
  // Store original command for case-sensitive values
  String originalCommand = command;
  originalCommand.trim();
  
  // Create uppercase version for command comparison
  String cmd = originalCommand;
  cmd.toUpperCase();
  
  if (cmd == "HELP") {
    Serial.println("\n==SECURED & OPTIMIZED IOT-BASED IRRIGATION SYSTEM - SERIAL COMMANDS==");
    Serial.println("HELP                    - Show this help message");
    Serial.println("STATUS                  - Show system status");
    Serial.println("SENSORS                 - Show sensor readings");
    Serial.println("PUMPS                   - Show pump status");
    Serial.println("MANUAL [ON/OFF]         - Enable/disable manual mode");
    Serial.println("PUMP [NAME] [ON/OFF]    - Control pump manually");
    Serial.println("STOP                    - Emergency stop all pumps");
    Serial.println("TEST PUMPS              - Test all pumps");
    Serial.println("RAIN [mm]               - Simulate rainfall");
    Serial.println("SOIL DIAG               - Diagnose soil sensors");
    Serial.println("SOIL TEST               - Live test soil sensors");
    Serial.println("CAL SANDY AUTODRY       - Set current reading as DRY (0%)");
    Serial.println("CAL SANDY AUTOWET       - Set current reading as WET (100%)");
    Serial.println("CAL CLAY AUTODRY        - Set current reading as DRY (0%)");
    Serial.println("CAL CLAY AUTOWET        - Set current reading as WET (100%)");
    Serial.println("CAL LOAMY AUTODRY       - Set current reading as DRY (0%)");
    Serial.println("CAL LOAMY AUTOWET       - Set current reading as WET (100%)");
    Serial.println("CAL SANDY SETDRY 3000   - Set DRY value manually");
    Serial.println("CAL SANDY SETWET 800    - Set WET value manually");
    Serial.println("CAL CLAY SETDRY 2800    - Set DRY value manually");
    Serial.println("CAL CLAY SETWET 900     - Set WET value manually");
    Serial.println("CAL LOAMY SETDRY 3200   - Set DRY value manually");
    Serial.println("CAL LOAMY SETWET 750    - Set WET value manually");
    Serial.println("CAL SANDY RESET         - Reset to defaults (3000/1500)");
    Serial.println("CAL CLAY RESET          - Reset to defaults (3000/1500)");
    Serial.println("CAL LOAMY RESET         - Reset to defaults (3000/1500)");
    Serial.println("CAL PRESS DIRECT [val]  - Direct pressure calibration");
    Serial.println("CAL ALT DIRECT [val]    - Direct altitude calibration");
    Serial.println("CAL TEMP [val]          - Temperature calibration");
    Serial.println("CAL HUM [val]           - Humidity calibration");
    Serial.println("MPU STATUS              - Show MPU6050 status");
    Serial.println("MPU CALIBRATE           - Calibrate MPU6050");
    Serial.println("SD LIST                 - List SD card log files");
    Serial.println("SD DELETE               - Delete all SD card logs");
    Serial.println("SAVE                    - Save calibration to flash");
    Serial.println("LOAD                    - Load calibration from flash");
    Serial.println("RESTART                 - Restart the system");
    Serial.println("WIFI CONFIG             - Configure WiFi connection");
    Serial.println("WIFI SCAN               - Scan for WiFi networks");
    Serial.println("WIFI STATUS             - Show WiFi status");
    Serial.println("WIFI ON/OFF             - Enable/disable WiFi");
    Serial.println("WIFI RESET              - Clear saved WiFi credentials");
    Serial.println("GOOGLE ON               - Enable google cloud upload");
    Serial.println("GOOGLE OFF              - Disable google cloud upload");
    Serial.println("GOOGLE URL              - URL script configurations");
    Serial.println("GOOGLE TEST             - Test google cloud upload");
    Serial.println("GOOGLE STATUS           - Check status");
    Serial.println("\n TELEGRAM COMMANDS (CASE SENSITIVE!):");
    Serial.println("   TELEGRAM ON              - Enable Telegram bot");
    Serial.println("   TELEGRAM OFF             - Disable Telegram bot");
    Serial.println("   TELEGRAM TOKEN <token>   - Set bot token (case sensitive!)");
    Serial.println("   TELEGRAM CHATID <id>     - Set chat ID");
    Serial.println("   TELEGRAM STATUS          - Show status");
    Serial.println("   TELEGRAM TEST            - Send test message");
    Serial.println("=================================================\n");
  }
  else if (cmd == "STATUS") {
    Serial.println("\n=== SYSTEM STATUS ===");
    Serial.printf("Uptime: %lu seconds\n", millis() / 1000);
    if (systemState.wifiConnected) {
      Serial.printf("WiFi: Connected (%s) RSSI: %d dBm\n", systemState.ipAddress.c_str(), WiFi.RSSI());
    } else if (systemState.apModeActive) {
      Serial.printf("WiFi: AP Mode (%s)\n", systemState.ipAddress.c_str());
    } else {
      Serial.printf("WiFi: Disconnected\n");
    }
    Serial.printf("Security: %s\n", systemState.securityAlert ? "ALERT" : "Normal");
    Serial.printf("Manual Mode: %s\n", systemState.manualMode ? "ON" : "OFF");
    Serial.printf("Irrigation Active: %s\n", systemState.irrigationActive ? "YES" : "NO");
    Serial.printf("SD Card: %s\n", systemState.sdCardPresent ? "PRESENT" : "MISSING");
    Serial.printf("NTP Synced: %s\n", systemState.ntpSynced ? "YES" : "NO");
    Serial.printf("Telegram: %s\n", calibration.telegram.enabled ? "ENABLED" : "DISABLED");
    Serial.printf("Sensor Interval: %d ms\n", calibration.system.sensorReadInterval);
    Serial.printf("Water Thresholds: S:%.1f%% C:%.1f%% L:%.1f%%\n",
                  calibration.irrigation.sandyWaterThreshold,
                  calibration.irrigation.clayWaterThreshold,
                  calibration.irrigation.loamyWaterThreshold);
    Serial.println("=====================\n");
  }
  else if (cmd == "SENSORS") {
    Serial.println("\n=== SENSOR READINGS ===");
    Serial.printf("Time: %s\n", currentData.timestamp.c_str());
    Serial.printf("Sandy Soil: %.1f%% (raw: %d)\n", currentData.sandyPercent, currentData.rawSandyMoisture);
    Serial.printf("Clay Soil: %.1f%% (raw: %d)\n", currentData.clayPercent, currentData.rawClayMoisture);
    Serial.printf("Loamy Soil: %.1f%% (raw: %d)\n", currentData.loamyPercent, currentData.rawLoamyMoisture);
    Serial.printf("Temperature: %.1f°C\n", currentData.temperature);
    Serial.printf("Humidity: %.1f%%\n", currentData.humidity);
    Serial.printf("Pressure: %.1f hPa\n", currentData.pressure);
    Serial.printf("Altitude: %.1f m\n", currentData.altitude);
    Serial.printf("Raining: %s\n", currentData.isRaining ? "YES" : "NO");
    Serial.printf("Movement: %s (Intensity: %d%%)\n", currentData.deviceMoved ? "YES" : "NO", currentData.movementIntensity);
    Serial.println("======================\n");
  }
  else if (cmd == "PUMPS") {
    Serial.println("\n=== PUMP STATUS ===");
    Serial.printf("Sandy Pump: %s (Manual: %s, Runtime: %d sec)\n",
                  pumpStatus.sandyActive ? "ACTIVE" : "INACTIVE",
                  pumpStatus.sandyManual ? "YES" : "NO",
                  pumpStatus.sandyRuntime);
    Serial.printf("Clay Pump: %s (Manual: %s, Runtime: %d sec)\n",
                  pumpStatus.clayActive ? "ACTIVE" : "INACTIVE",
                  pumpStatus.clayManual ? "YES" : "NO",
                  pumpStatus.clayRuntime);
    Serial.printf("Loamy Pump: %s (Manual: %s, Runtime: %d sec)\n",
                  pumpStatus.loamyActive ? "ACTIVE" : "INACTIVE",
                  pumpStatus.loamyManual ? "YES" : "NO",
                  pumpStatus.loamyRuntime);
    Serial.println("===================\n");
  }
  // Handle TELEGRAM commands - CASE SENSITIVE for token and chatid
  else if (originalCommand.startsWith("TELEGRAM TOKEN ")) {
    String token = originalCommand.substring(15);
    token.trim();
    TelegramManager::setBotToken(token);
  }
  else if (originalCommand.startsWith("TELEGRAM CHATID ")) {
    String chatId = originalCommand.substring(16);
    chatId.trim();
    TelegramManager::setChatId(chatId);
  }
  else if (cmd == "TELEGRAM ON") {
    TelegramManager::enable();
  }
  else if (cmd == "TELEGRAM OFF") {
    TelegramManager::disable();
  }
  else if (cmd == "TELEGRAM STATUS") {
    TelegramManager::showStatus();
  }
  else if (cmd == "TELEGRAM TEST") {
    TelegramManager::test();
  }
  // Handle other commands (these can be uppercase)
  else if (cmd.startsWith("MANUAL ")) {
    String state = originalCommand.substring(7);
    state.toUpperCase();
    if (state == "ON") IrrigationController::setManualMode(true);
    else if (state == "OFF") IrrigationController::setManualMode(false);
  }
  else if (cmd.startsWith("PUMP ")) {
    int firstSpace = cmd.indexOf(' ');
    int secondSpace = cmd.indexOf(' ', firstSpace + 1);
    if (secondSpace != -1) {
      String pump = originalCommand.substring(firstSpace + 1, secondSpace);
      String state = originalCommand.substring(secondSpace + 1);
      state.toUpperCase();
      bool pumpState = (state == "ON");
      pump.toLowerCase();
      IrrigationController::setManualControl(pump, pumpState);
    }
  }
  else if (cmd == "STOP") {
    IrrigationController::emergencyShutdown();
  }
  else if (cmd == "TEST PUMPS") {
    IrrigationController::testPumps();
  }
  else if (cmd.startsWith("RAIN ")) {
    float rainfallMM = originalCommand.substring(5).toFloat();
    if (rainfallMM >= 0 && rainfallMM <= 100) {
      SensorManager::simulateRainfall(rainfallMM);
    }
  }
  else if (cmd == "SOIL DIAG") {
    SensorManager::diagnoseSoilSensors();
  }
  else if (cmd == "SOIL TEST") {
    SensorManager::testSoilSensors();
  }
  else if (cmd == "SOIL CALIBRATE" || cmd == "SOIL CAL") {
    SensorManager::interactiveSoilCalibration();
  }
  // Auto calibration commands
  else if (cmd == "CAL SANDY AUTODRY") {
    SensorManager::autoCalibrateDry("sandy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL SANDY AUTOWET") {
    SensorManager::autoCalibrateWet("sandy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL CLAY AUTODRY") {
    SensorManager::autoCalibrateDry("clay");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL CLAY AUTOWET") {
    SensorManager::autoCalibrateWet("clay");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL LOAMY AUTODRY") {
    SensorManager::autoCalibrateDry("loamy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL LOAMY AUTOWET") {
    SensorManager::autoCalibrateWet("loamy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd.startsWith("CAL SANDY SETDRY ")) {
    int value = originalCommand.substring(17).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilDryValue("sandy", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL SANDY SETWET ")) {
    int value = originalCommand.substring(17).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilWetValue("sandy", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL CLAY SETDRY ")) {
    int value = originalCommand.substring(16).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilDryValue("clay", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL CLAY SETWET ")) {
    int value = originalCommand.substring(16).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilWetValue("clay", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL LOAMY SETDRY ")) {
    int value = originalCommand.substring(17).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilDryValue("loamy", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL LOAMY SETWET ")) {
    int value = originalCommand.substring(17).toInt();
    if (value >= 0 && value <= 4095) {
      SensorManager::setSoilWetValue("loamy", value);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd == "CAL SANDY RESET") {
    SensorManager::resetSoilCalibration("sandy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL CLAY RESET") {
    SensorManager::resetSoilCalibration("clay");
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "CAL LOAMY RESET") {
    SensorManager::resetSoilCalibration("loamy");
    PreferenceManager::saveCalibration();
  }
  else if (cmd.startsWith("CAL PRESS DIRECT ")) {
    float refValue = originalCommand.substring(17).toFloat();
    if (refValue >= 300 && refValue <= 1100) {
      SensorManager::calibratePressureDirect(refValue);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL ALT DIRECT ")) {
    float refValue = originalCommand.substring(15).toFloat();
    if (refValue >= -500 && refValue <= 9000) {
      SensorManager::calibrateAltitudeDirect(refValue);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL TEMP ")) {
    float refValue = originalCommand.substring(8).toFloat();
    if (refValue >= -40 && refValue <= 85) {
      SensorManager::calibrateTemperature(refValue);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd.startsWith("CAL HUM ")) {
    float refValue = originalCommand.substring(7).toFloat();
    if (refValue >= 0 && refValue <= 100) {
      SensorManager::calibrateHumidity(refValue);
      PreferenceManager::saveCalibration();
    }
  }
  else if (cmd == "MPU STATUS") {
    Serial.println("\n=== MPU6050 STATUS ===");
    Serial.println(SensorManager::getMPU6050().getStatus());
    Serial.printf("Movement: %s\n", currentData.deviceMoved ? "YES" : "NO");
    Serial.printf("Intensity: %d%%\n", currentData.movementIntensity);
    Serial.println("=====================\n");
  }
  else if (cmd == "MPU CALIBRATE") {
    SensorManager::getMPU6050().calibrate();
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "SD LIST") {
    SDCardManager::listLogFiles();
  }
  else if (cmd == "SD DELETE") {
    SDCardManager::deleteAllLogs();
  }
  else if (cmd == "SAVE") {
    PreferenceManager::saveCalibration();
  }
  else if (cmd == "LOAD") {
    PreferenceManager::loadCalibration();
  }
  else if (cmd == "RESTART") {
    Serial.println("Restarting system...");
    delay(1000);
    ESP.restart();
  }
  else if (cmd == "WIFI CONFIG") {
    WiFiConfigMenu::startConfiguration();
  }
  else if (cmd == "WIFI SCAN") {
    WiFiConfigMenu::scanNetworks();
  }
  else if (cmd == "WIFI STATUS") {
    WiFiConfigMenu::showWiFiStatus();
  }
  else if (cmd == "WIFI ON") {
    WiFiConfigMenu::enableWiFi();
  }
  else if (cmd == "WIFI OFF") {
    WiFiConfigMenu::disableWiFi();
  }
  else if (cmd == "WIFI RESET") {
    WiFiConfigMenu::resetCredentials();
  }
  else if (cmd == "GOOGLE ON") {
    calibration.googleSheets.enabled = true;
    systemState.googleSheetsEnabled = true;
    PreferenceManager::saveCalibration();
    Serial.println(" Google Sheets enabled");
  }
  else if (cmd == "GOOGLE OFF") {
    calibration.googleSheets.enabled = false;
    systemState.googleSheetsEnabled = false;
    PreferenceManager::saveCalibration();
    Serial.println(" Google Sheets disabled");
  }
  else if (cmd.startsWith("GOOGLE URL ")) {
    String url = originalCommand.substring(11);
    url.trim();
    strncpy(calibration.googleSheets.scriptURL, url.c_str(), sizeof(calibration.googleSheets.scriptURL) - 1);
    calibration.googleSheets.enabled = true;
    systemState.googleSheetsEnabled = true;
    PreferenceManager::saveCalibration();
    Serial.println(" Google Sheets URL set to: " + url);
  }
  else if (cmd == "GOOGLE TEST") {
    GoogleSheetsManager::testConnection();
  }
  else if (cmd == "GOOGLE STATUS") {
    Serial.println("\n Google Sheets Status:");
    Serial.printf("   Enabled: %s\n", systemState.googleSheetsEnabled ? "YES" : "NO");
    Serial.printf("   URL: %s\n", calibration.googleSheets.scriptURL);
    Serial.printf("   Interval: %d seconds\n", calibration.googleSheets.logInterval / 1000);
  }
  // ── ML COMMANDS ────────────────────────────────────────────────────────
  else if (cmd == "ML STATUS") {
    Serial.println("\n=== ML IRRIGATION MODEL STATUS ===");
    Serial.printf("   Model ready    : %s\n", MLIrrigationModel::modelReady ? "YES" : "NO");
    Serial.printf("   ML override    : %s\n", mlOverrideEnabled ? "ENABLED" : "DISABLED");
    Serial.printf("   Manual mode    : %s (ML inactive in manual)\n",
                  systemState.manualMode ? "ON" : "OFF");
    Serial.println("\n   Last prediction:");
    Serial.printf("   Sandy  prob=%.3f  pump=%s\n",
                  mlPrediction.sandyProb, mlPrediction.sandyPump ? "ON" : "OFF");
    Serial.printf("   Clay   prob=%.3f  pump=%s\n",
                  mlPrediction.clayProb,  mlPrediction.clayPump  ? "ON" : "OFF");
    Serial.printf("   Loamy  prob=%.3f  pump=%s\n",
                  mlPrediction.loamyProb, mlPrediction.loamyPump ? "ON" : "OFF");
    Serial.printf("   Valid           : %s\n", mlPrediction.valid ? "YES" : "NO");
    Serial.println("\n   Current effective thresholds (ML-adjusted):");
    Serial.printf("   Sandy : %.1f%%   Clay : %.1f%%   Loamy : %.1f%%\n",
                  calibration.irrigation.sandyWaterThreshold,
                  calibration.irrigation.clayWaterThreshold,
                  calibration.irrigation.loamyWaterThreshold);
    Serial.printf("\n   Tensor arena: %u / %u bytes\n",
                  MLIrrigationModel::interpreter
                    ? MLIrrigationModel::interpreter->arena_used_bytes() : 0,
                  MLIrrigationModel::kTensorArenaSize);
    Serial.println("==================================\n");
  }
  else if (cmd == "ML ON") {
    mlOverrideEnabled = true;
    Serial.println("[ML] ML irrigation override ENABLED");
  }
  else if (cmd == "ML OFF") {
    mlOverrideEnabled = false;
    // Restore base thresholds
    Serial.println("[ML] ML irrigation override DISABLED — base thresholds restored");
  }
  else if (cmd == "ML SCALER") {
    MLIrrigationModel::printScalerHint();
  }
  else if (cmd == "ML PREDICT") {
    // Force a one-shot prediction and print it right now
    MLIrrigationModel::updateRollingBuffers(currentData);
    MLIrrigationModel::MLPrediction p = MLIrrigationModel::predict(currentData);
    Serial.println("\n[ML] ON-DEMAND PREDICTION:");
    Serial.printf("   Sandy  prob=%.4f → %s\n", p.sandyProb, p.sandyPump ? "IRRIGATE" : "HOLD");
    Serial.printf("   Clay   prob=%.4f → %s\n", p.clayProb,  p.clayPump  ? "IRRIGATE" : "HOLD");
    Serial.printf("   Loamy  prob=%.4f → %s\n", p.loamyProb, p.loamyPump ? "IRRIGATE" : "HOLD");
    Serial.printf("   Valid  : %s\n\n", p.valid ? "YES" : "NO — sensor NaN or model not ready");
  }
  // ── END ML COMMANDS ────────────────────────────────────────────────────
  else if (cmd.length() > 0) {
    Serial.println("Unknown command. Type HELP for available commands.");
  }
}
// =============================================================================
// SETUP AND LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n============================================");
  Serial.println("SECURED & OPTIMIZED IOT-BASED IRRIGATION SYSTEM");
  Serial.println("   BY DAUDA OLORUNKEMI ISIAKA - PHD23CSC005  ");
  Serial.println("==============================================\n");
  
  addToSerialBuffer("System starting...");
  
  pinMode(PIN_SANDY_PUMP, OUTPUT);
  pinMode(PIN_CLAY_PUMP, OUTPUT);
  pinMode(PIN_LOAMY_PUMP, OUTPUT);
  pinMode(PIN_RED_LED, OUTPUT);
  pinMode(PIN_YELLOW_LED, OUTPUT);
  pinMode(PIN_GREEN_LED, OUTPUT);
  
  digitalWrite(PIN_SANDY_PUMP, LOW);
  digitalWrite(PIN_CLAY_PUMP, LOW);
  digitalWrite(PIN_LOAMY_PUMP, LOW);
  
  systemState.isCalibrating = false;
  systemState.securityAlert = false;
  systemState.wifiConnected = false;
  systemState.sdCardPresent = false;
  systemState.calibrationLoaded = false;
  systemState.systemEnabled = true;
  systemState.securityEnabled = false;
  systemState.wifiEnabled = false;
  systemState.ntpSynced = false;
  systemState.buzzerEnabled = false;
  systemState.irrigationActive = false;
  systemState.manualMode = false;
  systemState.webServerRunning = false;
  systemState.googleSheetsEnabled = false;
  systemState.sdEnabled = false;
  systemState.ipAddress = "";
  systemState.serialBuffer = "";
  systemState.webSerialEnabled = false;
  systemState.sdCardStatus = "Not initialized";
  systemState.apModeActive = false;
  
  Serial.print("Loading calibration... ");
  if (PreferenceManager::loadCalibration()) {
    systemState.calibrationLoaded = true;
    Serial.println("OK");
  } else {
    Serial.println("Using defaults");
  }
  
  SensorManager::initializeAll();
  IrrigationController::initialize();
  BuzzerManager::initialize();
  // ── TFLite Micro model ──────────────────────────────────────────────────
  Serial.print("Initializing ML irrigation model... ");
  if (MLIrrigationModel::initialize()) {
    Serial.println("OK");
    MLIrrigationModel::printScalerHint();  // prints Colab snippet for verification
  } else {
    Serial.println("FAILED — running without ML inference");
    mlOverrideEnabled = false;
  }
  // ── End TFLite init ─────────────────────────────────────────────────────
  WiFiManager::initialize();
  
  if (calibration.sd.enabled) {
    SDCardManager::initialize();
  }
  
  if (strlen(calibration.wifiCredentials.ssid) == 0) {
    showQuickStartGuide();
    WiFiManager::startAPMode();
  } else {
    Serial.println("\n WiFi credentials found. Attempting to connect...");
    int wifiTimeout = 0;
    while (!systemState.wifiConnected && wifiTimeout < 30 && !systemState.apModeActive) {
      delay(1000);
      WiFiManager::update();
      wifiTimeout++;
      Serial.print(".");
    }
    
    if (systemState.wifiConnected) {
      systemState.ipAddress = WiFi.localIP().toString();
      Serial.println("\n WiFi connected!");
      Serial.printf("   IP Address: %s\n", systemState.ipAddress.c_str());
      if (calibration.ntp.enabled) {
        NTPTimeManager::initialize();
      }
      // Initialize Telegram after WiFi is connected
      if (calibration.telegram.enabled) {
        TelegramManager::initialize();
      }
    } else if (!systemState.apModeActive) {
      Serial.println("\n WiFi connection failed");
      WiFiManager::startAPMode();
    }
  }
  
  if (calibration.webserver.enabled) {
    WebServerManager::initialize();
  }
  
  BuzzerManager::playStartupSequence();
  
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_RED_LED, HIGH);
    delay(200);
    digitalWrite(PIN_YELLOW_LED, HIGH);
    delay(200);
    digitalWrite(PIN_GREEN_LED, HIGH);
    delay(200);
    digitalWrite(PIN_RED_LED, LOW);
    digitalWrite(PIN_YELLOW_LED, LOW);
    digitalWrite(PIN_GREEN_LED, LOW);
    delay(200);
  }
  
  digitalWrite(PIN_GREEN_LED, HIGH);
  
  Serial.println("\n System ready!");
  if (systemState.wifiConnected) {
    Serial.printf("   Web Interface: http://%s\n", systemState.ipAddress.c_str());
  } else if (systemState.apModeActive) {
    Serial.printf("   AP Mode Active - Connect to '%s' WiFi\n", calibration.wifi.apSSID);
    Serial.printf("   Web Interface: http://%s\n", systemState.ipAddress.c_str());
  }
  Serial.println("\n=== SERIAL COMMANDS ===");
  Serial.println("HELP - Show all commands");
  Serial.println("WIFI CONFIG - Configure WiFi");
  Serial.println("WIFI STATUS - Show WiFi status");
  Serial.println("TELEGRAM TOKEN <token> - Set Telegram bot token");
  Serial.println("===============================\n");
}

void loop() {
  static unsigned long lastSensorRead = 0;
  static unsigned long lastWebUpdate = 0;
  static unsigned long lastStatusPrint = 0;
  static unsigned long lastSerialCheck = 0;
  static unsigned long lastSecurityCheck = 0;
  
  unsigned long currentMillis = millis();
  
  WiFiConfigMenu::update();
  WiFiManager::update();
  NTPTimeManager::update();
  WebSocketManager::update();
  BuzzerManager::update();
  SensorManager::updateRainSimulation();
  TelegramManager::update();  // Update Telegram bot
  
  if (currentMillis - lastSensorRead >= calibration.system.sensorReadInterval) {
    lastSensorRead = currentMillis;
    currentData = SensorManager::readAllSensors();

    // ── ML INFERENCE BLOCK ────────────────────────────────────────────────
    // Run only in AUTO mode so manual overrides are never disturbed.
    if (mlOverrideEnabled && !systemState.manualMode) {

      // Update rolling buffers for 1-hour feature window
      MLIrrigationModel::updateRollingBuffers(currentData);

      // Run inference
      mlPrediction = MLIrrigationModel::predict(currentData);

      if (mlPrediction.valid) {
        // ── ML-guided moisture threshold tuning ──────────────────────────
        // Instead of hard-switching pumps (which would bypass the cooldown /
        // safety logic in IrrigationController), we tighten or relax the
        // irrigation thresholds dynamically based on model confidence.
        //
        // Logic:
        //   ML says irrigate (prob ≥ 0.7) AND soil is dry → lower threshold
        //     so IrrigationController triggers sooner.
        //   ML says do NOT irrigate (prob < 0.3) → raise threshold temporarily
        //     so IrrigationController holds off.
        //   In between (0.3–0.7) → use the user-configured threshold unchanged.
        //
        // This keeps all safety guards (cooldown, max runtime, rain check)
        // fully active while letting the ML tune sensitivity.

        const float BASE_SANDY = calibration.irrigation.sandyWaterThreshold;
        const float BASE_CLAY  = calibration.irrigation.clayWaterThreshold;
        const float BASE_LOAMY = calibration.irrigation.loamyWaterThreshold;

        // Sandy
        if (mlPrediction.sandyProb >= 0.70f && currentData.sandyPercent < BASE_SANDY + 10.0f) {
          // ML strongly wants irrigation: nudge threshold up by 8% so
          // IrrigationController triggers even if soil is slightly above base threshold
          calibration.irrigation.sandyWaterThreshold = min(BASE_SANDY + 8.0f, 90.0f);
        } else if (mlPrediction.sandyProb < 0.30f) {
          // ML strongly advises against: lower threshold so pump won't trigger
          calibration.irrigation.sandyWaterThreshold = max(BASE_SANDY - 10.0f, 5.0f);
        } else {
          // Neutral: restore baseline (in case previous cycle adjusted it)
          calibration.irrigation.sandyWaterThreshold = BASE_SANDY;
        }

        // Clay
        if (mlPrediction.clayProb >= 0.70f && currentData.clayPercent < BASE_CLAY + 10.0f) {
          calibration.irrigation.clayWaterThreshold = min(BASE_CLAY + 8.0f, 90.0f);
        } else if (mlPrediction.clayProb < 0.30f) {
          calibration.irrigation.clayWaterThreshold = max(BASE_CLAY - 10.0f, 5.0f);
        } else {
          calibration.irrigation.clayWaterThreshold = BASE_CLAY;
        }

        // Loamy
        if (mlPrediction.loamyProb >= 0.70f && currentData.loamyPercent < BASE_LOAMY + 10.0f) {
          calibration.irrigation.loamyWaterThreshold = min(BASE_LOAMY + 8.0f, 90.0f);
        } else if (mlPrediction.loamyProb < 0.30f) {
          calibration.irrigation.loamyWaterThreshold = max(BASE_LOAMY - 10.0f, 5.0f);
        } else {
          calibration.irrigation.loamyWaterThreshold = BASE_LOAMY;
        }

        // ── Broadcast ML result over WebSocket so the dashboard shows it ──
        {
          StaticJsonDocument<256> mlDoc;
          mlDoc["type"]       = "ml_prediction";
          mlDoc["sandyProb"]  = mlPrediction.sandyProb;
          mlDoc["clayProb"]   = mlPrediction.clayProb;
          mlDoc["loamyProb"]  = mlPrediction.loamyProb;
          mlDoc["sandyML"]    = mlPrediction.sandyPump;
          mlDoc["clayML"]     = mlPrediction.clayPump;
          mlDoc["loamyML"]    = mlPrediction.loamyPump;
          String mlJson;
          serializeJson(mlDoc, mlJson);
          webSocket.broadcastTXT(mlJson);
        }

        // ── Log ML decisions to SD card ───────────────────────────────────
        if (systemState.sdEnabled && systemState.sdCardPresent) {
          char logLine[160];
          snprintf(logLine, sizeof(logLine),
            "[ML] S:%.3f(%d) C:%.3f(%d) L:%.3f(%d) thr S:%.1f C:%.1f L:%.1f",
            mlPrediction.sandyProb, mlPrediction.sandyPump,
            mlPrediction.clayProb,  mlPrediction.clayPump,
            mlPrediction.loamyProb, mlPrediction.loamyPump,
            calibration.irrigation.sandyWaterThreshold,
            calibration.irrigation.clayWaterThreshold,
            calibration.irrigation.loamyWaterThreshold);
          addToSerialBuffer(String(logLine));
        }

        // ── Telegram notification if ML strongly predicts need ────────────
        static unsigned long lastMLNotify = 0;
        if (currentMillis - lastMLNotify > 1800000UL) {  // max once per 30 min
          bool anyMLIrrigate = (mlPrediction.sandyPump ||
                                mlPrediction.clayPump  ||
                                mlPrediction.loamyPump);
          if (anyMLIrrigate && calibration.telegram.enabled && calibration.telegram.notifyPumpStatus) {
            lastMLNotify = currentMillis;
            String mlMsg = " *ML IRRIGATION PREDICTION*\n\n";
            mlMsg += "Sandy:  " + String(mlPrediction.sandyProb * 100.0f, 1) + "% → "
                   + (mlPrediction.sandyPump ? "irrigate" : "hold") + "\n";
            mlMsg += "Clay:   " + String(mlPrediction.clayProb  * 100.0f, 1) + "% → "
                   + (mlPrediction.clayPump  ? "irrigate" : "hold") + "\n";
            mlMsg += "Loamy:  " + String(mlPrediction.loamyProb * 100.0f, 1) + "% → "
                   + (mlPrediction.loamyPump ? "irrigate" : "hold") + "\n";
            TelegramManager::sendNotification(mlMsg, false);
          }
        }
      }  // mlPrediction.valid
    }    // mlOverrideEnabled && !manualMode
    // ── END ML INFERENCE BLOCK ───────────────────────────────────────────

    IrrigationController::update();

    if (systemState.sdEnabled && systemState.sdCardPresent) {
      SDCardManager::logData(currentData, pumpStatus);
    }
    
    // Google Sheets logging
    if (systemState.googleSheetsEnabled && calibration.googleSheets.enabled) {
      GoogleSheetsManager::logToGoogleSheets();
    }
    
    // Store in historical data
    int idx = historicalData.index;
    historicalData.sandyPercent[idx] = currentData.sandyPercent;
    historicalData.clayPercent[idx] = currentData.clayPercent;
    historicalData.loamyPercent[idx] = currentData.loamyPercent;
    historicalData.temperature[idx] = currentData.temperature;
    historicalData.humidity[idx] = currentData.humidity;
    historicalData.pressure[idx] = currentData.pressure;
    historicalData.altitude[idx] = currentData.altitude;
    historicalData.dewPoint[idx] = currentData.dewPoint;
    historicalData.timestamps[idx] = currentData.timestamp;
    historicalData.index = (idx + 1) % HISTORY_SIZE;
    if (historicalData.index == 0) historicalData.isFull = true;
  }
  
  // WebSocket updates
  if (calibration.webserver.enableWebSockets && systemState.webServerRunning) {
    if (currentMillis - lastWebUpdate >= calibration.webserver.updateInterval) {
      lastWebUpdate = currentMillis;
      WebSocketManager::broadcastSensorData();
      WebSocketManager::broadcastPumpStatus();
      WebSocketManager::broadcastSystemStatus();
    }
  }
  
  // Security monitoring
  if (systemState.securityEnabled && calibration.mpu6050.enabled) {
    if (currentMillis - lastSecurityCheck > 100) {
      lastSecurityCheck = currentMillis;
      bool movementDetected = SensorManager::getMPU6050().isMoving();
      currentData.deviceMoved = movementDetected;
      currentData.movementIntensity = SensorManager::getMPU6050().getMovementIntensity();
      
      if (movementDetected && !systemState.securityAlert) {
        systemState.securityAlert = true;
        systemState.lastAlertTime = currentMillis;
        Serial.println("⚠️ SECURITY ALERT: Device movement detected!");
        addToSerialBuffer("SECURITY ALERT: Device movement detected! Intensity: " + String(currentData.movementIntensity) + "%");
        
        // Send Telegram alert
        if (calibration.telegram.enabled && calibration.telegram.notifyMovement) {
          String alertMsg = " *SECURITY ALERT!*\n";
          alertMsg += "Device movement detected!\n";
          alertMsg += "Intensity: " + String(currentData.movementIntensity) + "%\n";
          alertMsg += "Time: " + getDateTime();
          TelegramManager::sendAlert(alertMsg);
        }
        
        if (calibration.mpu6050.enableBuzzer) {
          BuzzerManager::playAlertSequence();
        }
        
        if (calibration.mpu6050.webAlerts && systemState.webServerRunning) {
          WebSocketManager::broadcastAlert("Security Alert: Device movement detected! Intensity: " + String(currentData.movementIntensity) + "%");
        }
      }
      
      if (systemState.securityAlert && (currentMillis - systemState.lastAlertTime > calibration.mpu6050.tamperAlertDelay)) {
        systemState.securityAlert = false;
        Serial.println("Security alert cleared");
        addToSerialBuffer("Security alert cleared");
      }
    }
  }
  
  // Check for rain and send notification
  static bool previousRainState = false;
  if (currentData.isRaining && !previousRainState) {
    if (calibration.telegram.enabled && calibration.telegram.notifyRain) {
      String rainMsg = " *RAIN DETECTED!*\n";
      rainMsg += "Humidity: " + formatFloat(currentData.humidity, 1) + "%\n";
      rainMsg += "Pressure: " + formatFloat(currentData.pressure, 1) + " hPa\n";
      rainMsg += "Irrigation may be delayed";
      TelegramManager::sendNotification(rainMsg);
    }
    previousRainState = true;
  } else if (!currentData.isRaining && previousRainState) {
    previousRainState = false;
  }
  
  // Check for pump activation and send notification
  static bool previousPumpState = false;
  bool currentPumpState = (pumpStatus.sandyActive || pumpStatus.clayActive || pumpStatus.loamyActive);
  if (currentPumpState && !previousPumpState) {
    if (calibration.telegram.enabled && calibration.telegram.notifyPumpStatus) {
      String pumpMsg = " *PUMP ACTIVATED*\n";
      if (pumpStatus.sandyActive) {
        pumpMsg += "Sandy soil pump started\n";
        pumpMsg += "Soil moisture: " + formatFloat(currentData.sandyPercent, 1) + "%\n";
        pumpMsg += "Threshold: " + formatFloat(calibration.irrigation.sandyWaterThreshold, 1) + "%";
      } else if (pumpStatus.clayActive) {
        pumpMsg += "Clay soil pump started\n";
        pumpMsg += "Soil moisture: " + formatFloat(currentData.clayPercent, 1) + "%\n";
        pumpMsg += "Threshold: " + formatFloat(calibration.irrigation.clayWaterThreshold, 1) + "%";
      } else if (pumpStatus.loamyActive) {
        pumpMsg += "Loamy soil pump started\n";
        pumpMsg += "Soil moisture: " + formatFloat(currentData.loamyPercent, 1) + "%\n";
        pumpMsg += "Threshold: " + formatFloat(calibration.irrigation.loamyWaterThreshold, 1) + "%";
      }
      TelegramManager::sendNotification(pumpMsg);
    }
    previousPumpState = true;
  } else if (!currentPumpState && previousPumpState) {
    previousPumpState = false;
  }
  
  // Serial output
  if (calibration.system.enableSerialOutput) {
    if (currentMillis - lastStatusPrint >= calibration.system.statusPrintInterval) {
      lastStatusPrint = currentMillis;
      Serial.println("\n=== SYSTEM STATUS ===");
      Serial.printf("Time: %s\n", getDateTime().c_str());
      Serial.printf("Soil Raw: S:%d C:%d L:%d\n", currentData.rawSandyMoisture, currentData.rawClayMoisture, currentData.rawLoamyMoisture);
      Serial.printf("Soil: S:%.1f%% C:%.1f%% L:%.1f%%\n", currentData.sandyPercent, currentData.clayPercent, currentData.loamyPercent);
      Serial.printf("Env: T:%.1f°C H:%.1f%% P:%.1f hPa\n", currentData.temperature, currentData.humidity, currentData.pressure);
      Serial.printf("Security: %s (Intensity: %d%%)\n", systemState.securityAlert ? "ALERT" : "Normal", currentData.movementIntensity);
      Serial.printf("Google Sheets: %s\n", systemState.googleSheetsEnabled ? "Enabled" : "Disabled");
      Serial.printf("Telegram: %s\n", calibration.telegram.enabled ? "Enabled" : "Disabled");
      Serial.println("=====================\n");
    }
  }
  
  IrrigationController::updateLEDs();
  
  if (currentMillis - lastSerialCheck > 100) {
    lastSerialCheck = currentMillis;
    if (Serial.available()) {
      String command = Serial.readStringUntil('\n');
      command.trim();
      addToSerialBuffer(">>> " + command);
      processSerialCommand(command);
    }
  }
  
  delay(100);
}