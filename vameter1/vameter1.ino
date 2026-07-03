#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const uint8_t PIN_ZMPT = A1;
const uint8_t PIN_ACS = A2;
const uint8_t PIN_BUZZER = 13;

const uint16_t SAMPLE_COUNT = 1000;
const uint16_t SAMPLE_DELAY_US = 100;

const float ADC_REFERENCE = 5.0;
const float ADC_MAX_VALUE = 1023.0;
const float RMS_FACTOR = 0.70710678;

float voltageCalibration = 325.0;
float currentSensitivity = 0.185;
float currentCalibration = 1.0;

const float VOLTAGE_NOISE_THRESHOLD = 10.0;
const float CURRENT_NOISE_THRESHOLD = 0.05;
const float CURRENT_ALARM_THRESHOLD = 1.0;

float voltageRms = 0.0;
float currentRms = 0.0;
float apparentPower = 0.0;
float currentNoiseRms = 0.0;

float measureCurrentRms(uint16_t samples) {
  double sum = 0.0;
  double sumSquare = 0.0;

  for (uint16_t i = 0; i < samples; i++) {
    int raw = analogRead(PIN_ACS);

    sum += raw;
    sumSquare += (double)raw * raw;

    delayMicroseconds(SAMPLE_DELAY_US);
  }

  double mean = sum / samples;
  double variance = (sumSquare / samples) - (mean * mean);

  if (variance < 0.0) {
    variance = 0.0;
  }

  float adcRms = sqrt(variance);
  float sensorRmsVoltage =
      (adcRms / ADC_MAX_VALUE) * ADC_REFERENCE;

  return (sensorRmsVoltage / currentSensitivity)
         * currentCalibration;
}

void calibrateCurrentNoise() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Remove load");
  lcd.setCursor(0, 1);
  lcd.print("Calibrating...");

  delay(1000);

  float totalNoise = 0.0;
  const uint8_t calibrationCycles = 10;

  for (uint8_t i = 0; i < calibrationCycles; i++) {
    totalNoise += measureCurrentRms(500);
  }

  currentNoiseRms = totalNoise / calibrationCycles;

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Calibration OK");
  lcd.setCursor(0, 1);
  lcd.print("Noise:");
  lcd.print(currentNoiseRms, 3);
  lcd.print("A");

  delay(1500);
  lcd.clear();
}

void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(PIN_BUZZER, OUTPUT);
  digitalWrite(PIN_BUZZER, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("VA Meter");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");

  delay(1500);

  calibrateCurrentNoise();
}

void readSensors() {
  int voltageMin = 1023;
  int voltageMax = 0;

  double currentSum = 0.0;
  double currentSumSquare = 0.0;

  for (uint16_t i = 0; i < SAMPLE_COUNT; i++) {
    int voltageRaw = analogRead(PIN_ZMPT);
    int currentRaw = analogRead(PIN_ACS);

    if (voltageRaw < voltageMin) {
      voltageMin = voltageRaw;
    }

    if (voltageRaw > voltageMax) {
      voltageMax = voltageRaw;
    }

    currentSum += currentRaw;
    currentSumSquare +=
        (double)currentRaw * currentRaw;

    delayMicroseconds(SAMPLE_DELAY_US);
  }

  float voltagePeakAdc =
      (voltageMax - voltageMin) / 2.0;

  float voltagePeakSensor =
      (voltagePeakAdc / ADC_MAX_VALUE)
      * ADC_REFERENCE;

  voltageRms =
      voltagePeakSensor
      * RMS_FACTOR
      * voltageCalibration;

  double currentMean =
      currentSum / SAMPLE_COUNT;

  double currentVariance =
      (currentSumSquare / SAMPLE_COUNT)
      - (currentMean * currentMean);

  if (currentVariance < 0.0) {
    currentVariance = 0.0;
  }

  float currentAdcRms =
      sqrt(currentVariance);

  float currentSensorRmsVoltage =
      (currentAdcRms / ADC_MAX_VALUE)
      * ADC_REFERENCE;

  float measuredCurrentRms =
      (currentSensorRmsVoltage /
       currentSensitivity)
      * currentCalibration;

  float correctedCurrentSquared =
      (measuredCurrentRms * measuredCurrentRms)
      -
      (currentNoiseRms * currentNoiseRms);

  if (correctedCurrentSquared > 0.0) {
    currentRms = sqrt(correctedCurrentSquared);
  } else {
    currentRms = 0.0;
  }

  if (voltageRms < VOLTAGE_NOISE_THRESHOLD) {
    voltageRms = 0.0;
  }

  if (currentRms < CURRENT_NOISE_THRESHOLD) {
    currentRms = 0.0;
  }

  apparentPower = voltageRms * currentRms;
}

void updateBuzzer() {
  if (currentRms > CURRENT_ALARM_THRESHOLD) {
    digitalWrite(PIN_BUZZER, HIGH);
  } else {
    digitalWrite(PIN_BUZZER, LOW);
  }
}

void updateDisplay() {
  lcd.setCursor(0, 0);
  lcd.print("V:");
  lcd.print(voltageRms, 1);
  lcd.print(" A:");
  lcd.print(currentRms, 2);
  lcd.print("   ");

  lcd.setCursor(0, 1);
  lcd.print("VA:");
  lcd.print(apparentPower, 1);
  lcd.print("          ");
}

void loop() {
  readSensors();
  updateBuzzer();
  updateDisplay();

  delay(300);
}