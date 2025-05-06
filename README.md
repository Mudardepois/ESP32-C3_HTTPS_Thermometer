# ESP32-CE_HTTPS_Thermometer
This is a simple thermometer made with a ESP32-C3 supermini, and a aht20+bmp280 module, it can mesure
- Temperature
- Relative umidity
- Atmosferc pressure

![isso](Pictures/Picture3.jpg)

The two compontes are weld together with a simple 3D printed case and are conected by simple solderin wires on then with the flowin wireing diagram

| ESP32-C3 Super Mini Pin | I2C Thermometer Pin | Connection Notes |  
|-------------------------|---------------------|------------------|  
| `3V3`                   | `VCC` (or `VIN`)    | Power (3.3V)     |  
| `GND`                   | `GND`               | Ground           |  
| `GPIO4` (SCL)           | `SCL`               | I2C Clock        |  
| `GPIO5` (SDA)           | `SDA`               | I2C Data         |  

