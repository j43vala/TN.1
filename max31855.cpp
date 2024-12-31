#include "MAX31855.h"

// Define SPI settings for MAX31855
SPISettings max31855SPISettings;

// Constructor to initialize the CS pin and SPI clock speed
MAX31855::MAX31855(int8_t csPin, uint32_t spiClock)
    : _csPin(csPin), _spiClock(spiClock) {
    pinMode(_csPin, OUTPUT);
    digitalWrite(_csPin, HIGH);
    max31855SPISettings = SPISettings(_spiClock, MSBFIRST, SPI_MODE0);
}

// Initialize the MAX31855
bool MAX31855::begin() {
    SPI.begin();
    return true; // Assuming the MAX31855 does not need specific start-up verification
}

// Read temperature in Celsius
double MAX31855::readCelsius() {
    uint32_t rawData = readRawData();

    // Check if there's an error
    if (rawData & 0x00010000) {
        return NAN; // Return NaN if any error is detected
    }

    // Extract the temperature value (bits 18 to 31)
    int16_t value = (rawData >> 18) & 0x3FFF;
    // Check if temperature is negative
    if (value & 0x2000) {
        value |= 0xC000; // Extend the sign bit for negative temperatures
    }

    return value * 0.25; // MAX31855 resolution is 0.25°C per bit
}

// Read error status
uint8_t MAX31855::readError() {
    uint32_t rawData = readRawData();
    return (rawData & 0x7); // Error bits are the 3 least significant bits
}

// Read raw data from the sensor
uint32_t MAX31855::readRawData() {
    uint32_t data = 0;

    // Begin SPI transaction with the correct settings for MAX31855
    SPI.beginTransaction(max31855SPISettings);
    digitalWrite(_csPin, LOW);
    delayMicroseconds(1); // Small delay for CS setup

    // Read 4 bytes of data
    data |= SPI.transfer(0) << 24;
    data |= SPI.transfer(0) << 16;
    data |= SPI.transfer(0) << 8;
    data |= SPI.transfer(0);

    digitalWrite(_csPin, HIGH);
    SPI.endTransaction();

    return data;
}