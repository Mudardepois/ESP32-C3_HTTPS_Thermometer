#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <WebServer.h>
#include <EEPROM.h>

// Estrutura para armazenar credenciais WiFi na EEPROM
struct WiFiConfig {
  char ssid[32];
  char password[64];
};

// Configurações padrão do AP
const char* apSSID = "thermometer config";
const char* apPassword = "thermometer config";

// Configurações Tago.io
const char* server = "api.tago.io";
const char* token = "";

// Endereços I2C
#define BMP280_ADDRESS 0x76
#define AHT20_ADDRESS 0x38

// Variáveis globais
WiFiConfig wifiConfig;
WebServer serverWeb(80);
bool wifiConfigured = false;

// Variáveis de calibração BMP280
uint16_t dig_T1;
int16_t dig_T2, dig_T3;

void setup() {
  Serial.begin(115200);
  
  // Inicializar EEPROM
  EEPROM.begin(sizeof(WiFiConfig));
  
  // Tentar ler configuração WiFi salva
  EEPROM.get(0, wifiConfig);
  
  // Verificar se há credenciais salvas
  if (strlen(wifiConfig.ssid) > 0) {
    Serial.println("Tentando conectar ao WiFi salvo...");
    Serial.print("SSID: ");
    Serial.println(wifiConfig.ssid);
    
    WiFi.begin(wifiConfig.ssid, wifiConfig.password);
    
    int tentativas = 0;
    while (WiFi.status() != WL_CONNECTED && tentativas < 20) {
      delay(500);
      Serial.print(".");
      tentativas++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      wifiConfigured = true;
      Serial.println("\nConectado ao WiFi!");
      Serial.print("Endereço IP: ");
      Serial.println(WiFi.localIP());
      
      // Inicializar sensores
      Wire.begin(6, 7); // SDA=GPIO6, SCL=GPIO7
      bmp280_init();
      aht20_init();
      return;
    }
  }
  
  // Se não conseguiu conectar, iniciar modo AP
  startAPMode();
}

void loop() {
  if (wifiConfigured) {
    // Rotina normal de leitura dos sensores
    float temperatura_bmp = read_bmp280_temp();
    float pressao_kpa = read_bmp280_pressure() / 1000.0;
    float umidade, temp_aht;
    read_aht20(&temp_aht, &umidade);

    // Enviar para Tago.io
    sendToTagoIO(temperatura_bmp, pressao_kpa, temp_aht, umidade);
    
    delay(60000); // Enviar a cada minuto
  } else {
    // Manter servidor web ativo para configuração
    serverWeb.handleClient();
    delay(1);
  }
}

void startAPMode() {
  Serial.println("\nIniciando modo de configuração AP...");
  Serial.print("Conecte-se ao WiFi: ");
  Serial.println(apSSID);
  Serial.print("Senha: ");
  Serial.println(apPassword);
  Serial.println("Acesse http://192.168.4.1 para configurar");
  
  WiFi.softAP(apSSID, apPassword);
  
  serverWeb.on("/", HTTP_GET, []() {
    String html = "<html><body>";
    html += "<h1>Configuração WiFi</h1>";
    html += "<form method='POST' action='/save'>";
    html += "SSID: <input type='text' name='ssid'><br>";
    html += "Senha: <input type='password' name='password'><br>";
    html += "<input type='submit' value='Salvar'>";
    html += "</form></body></html>";
    serverWeb.send(200, "text/html", html);
  });
  
  serverWeb.on("/save", HTTP_POST, []() {
    String newSSID = serverWeb.arg("ssid");
    String newPass = serverWeb.arg("password");
    
    if (newSSID.length() > 0) {
      // Salvar novas credenciais
      strncpy(wifiConfig.ssid, newSSID.c_str(), sizeof(wifiConfig.ssid));
      strncpy(wifiConfig.password, newPass.c_str(), sizeof(wifiConfig.password));
      EEPROM.put(0, wifiConfig);
      EEPROM.commit();
      
      serverWeb.send(200, "text/html", "<h1>Configuração salva!</h1><p>O dispositivo será reiniciado.</p>");
      delay(1000);
      ESP.restart();
    } else {
      serverWeb.send(400, "text/html", "<h1>Erro</h1><p>SSID não pode estar vazio</p>");
    }
  });
  
  serverWeb.begin();
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
    Serial.println("Dados enviados com sucesso!");
  } else {
    Serial.print("Erro no envio: ");
    Serial.println(httpResponseCode);
    Serial.println(http.getString());
  }

  http.end();
}

// Funções dos sensores (mantidas do código original)
// ------------------------------------------------------------------
void bmp280_init() {
  writeRegister(BMP280_ADDRESS, 0xF4, 0x3F);
  dig_T1 = read16(BMP280_ADDRESS, 0x88);
  dig_T2 = read16s(BMP280_ADDRESS, 0x8A);
  dig_T3 = read16s(BMP280_ADDRESS, 0x8C);
}

float read_bmp280_temp() {
  int32_t var1, var2, adc_T;
  adc_T = read24(BMP280_ADDRESS, 0xFA);
  adc_T >>= 4;
  
  var1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * ((int32_t)dig_T2)) >> 11;
  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) * ((adc_T >> 4) - ((int32_t)dig_T1)) >> 12) * ((int32_t)dig_T3))) >> 14;
  
  return (float)((var1 + var2) / 5120.0);
}

float read_bmp280_pressure() {
  // Implementar cálculo de pressão conforme necessário
  return 0; // Placeholder
}

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
  return (Wire.read() << 16) | (Wire.read() << 8) | Wire.read();
}
