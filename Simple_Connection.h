#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>

// WiFi and Tago.io settings
const char* ssid = "your_ssid";
const char* password = "your_password";
const char* server = "api.tago.io";
const char* token = "your_tagoio_token";

#define BMP280_ADDRESS 0x76
#define AHT20_ADDRESS 0x38

// BMP280 Calibration
uint16_t dig_T1;
int16_t dig_T2, dig_T3;
uint16_t dig_P1;
int16_t dig_P2, dig_P3, dig_P4, dig_P5, dig_P6, dig_P7, dig_P8, dig_P9;

void setup() {
  Serial.begin(115200);
  
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }

  Wire.begin(6, 7); // SDA=GPIO6, SCL=GPIO7
  bmp280_init();
  aht20_init();
}

void loop() {
  float bmp_temperature = read_bmp280_temp();
  float pressure_kpa = read_bmp280_pressure() / 1000.0;
  float humidity, aht_temperature;
  read_aht20(&aht_temperature, &humidity);

  sendToTagoIO(bmp_temperature, pressure_kpa, aht_temperature, humidity);
  
  delay(60000); // Wait 1 minute between readings
}

void sendToTagoIO(float temp_bmp, float pressure, float temp_aht, float humidity) {
  StaticJsonDocument<512> jsonDocument;
  JsonArray data = jsonDocument.to<JsonArray>();

  JsonObject tempBmp = data.createNestedObject();
  tempBmp["variable"] = "temperature_bmp";
  tempBmp["value"] = temp_bmp;
  tempBmp["unit"] = "°C";

  JsonObject press = data.createNestedObject();
  press["variable"] = "pressure";
  press["value"] = pressure;
  press["unit"] = "kPa";

  JsonObject tempAht = data.createNestedObject();
  tempAht["variable"] = "temperature_aht";
  tempAht["value"] = temp_aht;
  tempAht["unit"] = "°C";

  JsonObject humi = data.createNestedObject();
  humi["variable"] = "humidity";
  humi["value"] = humidity;
  humi["unit"] = "%";

  String jsonString;
  serializeJson(jsonDocument, jsonString);

  HTTPClient http;
  http.begin("http://" + String(server) + "/data");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("token", token);

  int httpResponseCode = http.POST(jsonString);

  if (httpResponseCode == 202) {
    Serial.println("Data successfully sent!");
  } else {
    Serial.print("Error: ");
    Serial.println(httpResponseCode);
    Serial.println(http.getString());
  }

  http.end();
}

// -------------------------------------------------
void bmp280_init() {
  writeRegister(BMP280_ADDRESS, 0xF4, 0x3F); // Normal mode, 16x pressure oversampling, 2x temp oversampling
  
  dig_T1 = read16(BMP280_ADDRESS, 0x88);
  dig_T2 = read16s(BMP280_ADDRESS, 0x8A);
  dig_T3 = read16s(BMP280_ADDRESS, 0x8C);
  
  dig_P1 = read16(BMP280_ADDRESS, 0x8E);
  dig_P2 = read16s(BMP280_ADDRESS, 0x90);
  dig_P3 = read16s(BMP280_ADDRESS, 0x92);
  dig_P4 = read16s(BMP280_ADDRESS, 0x94);
  dig_P5 = read16s(BMP280_ADDRESS, 0x96);
  dig_P6 = read16s(BMP280_ADDRESS, 0x98);
  dig_P7 = read16s(BMP280_ADDRESS, 0x9A);
  dig_P8 = read16s(BMP280_ADDRESS, 0x9C);
  dig_P9 = read16s(BMP280_ADDRESS, 0x9E);
}

float read_bmp280_temp() {
  int32_t var1, var2, adc_T;
  adc_T = read24(BMP280_ADDRESS, 0xFA);
  adc_T >>= 4;
  
  var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * 
          ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) * ((int32_t)dig_T3)) >> 14;
  
  return (float)((var1 + var2) / 5120.0);
}

float read_bmp280_pressure() {
  int64_t var1, var2, p;
  int32_t adc_P = read24(BMP280_ADDRESS, 0xF7);
  adc_P >>= 4;
  
  float temperature = read_bmp280_temp();
  int32_t t_fine = (var1 + var2);
  
  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1 * (int64_t)dig_P5) << 17);
  var2 = var2 + (((int64_t)dig_P4) << 35);
  var1 = ((var1 * var1 * (int64_t)dig_P3) >> 8) + ((var1 * (int64_t)dig_P2) << 12);
  var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)dig_P1) >> 33;
  
  if (var1 == 0) {
    return 0;
  }
  
  p = 1048576 - adc_P;
  p = (((p << 31) - var2) * 3125) / var1;
  var1 = (((int64_t)dig_P9) * (p >> 13) * (p >> 13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7) << 4);
  return (float)p / 256.0;
}

// --------------------------------------------------
void aht20_init() {
  Wire.beginTransmission(AHT20_ADDRESS);
  Wire.write(0xBE); 
  Wire.write(0x08);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(20);
}

void read_aht20(float *temp, float *humidity) {
  uint8_t data[6];
  
  Wire.beginTransmission(AHT20_ADDRESS);
  Wire.write(0xAC);
  Wire.write(0x33);
  Wire.write(0x00);
  Wire.endTransmission();
  delay(80);
  
  Wire.requestFrom(AHT20_ADDRESS, 6);
  for(int i=0; i<6; i++) data[i] = Wire.read();

  uint32_t hum = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
  uint32_t tem = ((uint32_t)(data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];
  
  *humidity = (hum * 100.0) / 0x100000;
  *temp = (tem * 200.0 / 0x100000) - 50;
}

// ------------------------------------------------
void writeRegister(uint8_t address, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission();
}

uint16_t read16(uint8_t address, uint8_t reg) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(address, 2);
  return (Wire.read() << 8) | Wire.read();
}

int16_t read16s(uint8_t address, uint8_t reg) {
  return (int16_t)read16(address, reg);
}

uint32_t read24(uint8_t address, uint8_t reg) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(address, 3);
  return ((uint32_t)Wire.read() << 16) | ((uint32_t)Wire.read() << 8) | Wire.read();
}
