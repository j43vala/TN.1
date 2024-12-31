#ifndef MAX31855_H
#define MAX31855_H

#include <Arduino.h>
#include <SPI.h>

class MAX31855 {
public:
    MAX31855(int8_t csPin, uint32_t spiClock = 5000000); // Constructor with optional SPI clock speed
    bool begin();                // Initialize the sensor
    double readCelsius();        // Read temperature in Celsius
    uint8_t readError();         // Check for sensor faults

private:
    int8_t _csPin;               // Chip select pin for the sensor
    uint32_t _spiClock;          // SPI clock speed
    uint32_t readRawData();      // Read raw data from the sensor
};
#endif