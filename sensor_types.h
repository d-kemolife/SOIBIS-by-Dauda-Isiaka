#pragma once
struct SensorData {
  int rawSandyMoisture;
  int rawClayMoisture;
  int rawLoamyMoisture;
  float rawTemperature;
  float rawHumidity;
  float rawPressure;
  float rawAltitude;
  
  int sandyMoisture;
  int clayMoisture;
  int loamyMoisture;
  float temperature;
  float humidity;
  float pressure;
  float altitude;
  float dewPoint;
  bool isRaining;
  bool deviceMoved;
  float accelerationX, accelerationY, accelerationZ;
  float gyroX, gyroY, gyroZ;
  int movementIntensity;
  String timestamp;
  String dateTime;
  
  float sandyPercent;
  float clayPercent;
  float loamyPercent;
};