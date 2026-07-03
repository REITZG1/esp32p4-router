#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

// I2C LCD
LiquidCrystal_I2C lcd(0x27, 16, 2);

EnergyMonitor emon1;

// Pin Mapping untuk ESP32
// Gunakan pin ADC1 (32, 33, 34, 35, 36, 39) agar tidak konflik dengan Wi-Fi/BT
const int pinACS = 34;    // Sensor Arus (ACS712)
const int pinZMPT = 35;   // Sensor Tegangan (ZMPT101B)
const int pinLED = 2;     // LED (Builtin di banyak board ESP32)
const int pinBuzzer = 15; // Buzzer (Hindari pin RX/TX & Strapping)

// Konstanta Kalibrasi (Perlu penyesuaian ulang untuk ESP32)
const float VOLT_CAL = 530;
const float CURRENT_CAL = 5.33;

unsigned long previousMillisBuzzer = 0;
const long beepInterval = 500;
bool isBeeping = false;
  // Logika Buzzer & LED
  if (currentRMS >= 3.0) {
    
    isBeeping = false;
  }
float energyWh = 0.0;
unsigned long previousTimeEnergy = 0;
bool showScreen1 = true;
unsigned long previousScreenMillis = 0;
const long screenInterval = 3000;

void setup() {
  Serial.begin(115200); // Baudrate standar ESP32
  
  // Inisialisasi I2C pada pin default ESP32 (SDA=21, SCL=22)
  Wire.begin(21, 22);
  
  lcd.init();
  lcd.backlight();

  // Pastikan ADC menggunakan resolusi 12-bit (default ESP32)
  #ifdef ESP32
    analogReadResolution(12);
  #endif

  emon1.voltage(pinZMPT, VOLT_CAL, -0.9);
  emon1.current(pinACS, CURRENT_CAL);

  lcd.setCursor(0, 0);
  lcd.print("VA-Meter");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
  delay(2000);
  lcd.clear();

  previousTimeEnergy = millis();
}

void loop() {
  emon1.calcVI(20, 2000);

  float voltageAC = emon1.Vrms;
  float currentRMS = emon1.Irms;
  float powerWatt = emon1.realPower;
  float powerVA = emon1.apparentPower;
  float pf = emon1.powerFactor;

  // Filter noise saat tidak ada beban
  if (currentRMS < 0.065) {
    currentRMS = 0.0;
    powerWatt = 0.0;
    powerVA = 0.0;
    pf = 0.0;
  }

  // Clamp Power Factor
  if (pf < 0.0) pf = 0.0;
  if (pf > 1.0) pf = 1.0;

  // Akumulasi Energi (Wh)
  unsigned long currentTime = millis();
  unsigned long deltaTime = currentTime - previousTimeEnergy;
  previousTimeEnergy = currentTime;
  energyWh += (powerWatt * (deltaTime / 3600000.0));

  // Ganti layar tiap 3 detik
  if (currentTime - previousScreenMillis >= screenInterval) {
    previousScreenMillis = currentTime;
    showScreen1 = !showScreen1;
    lcd.clear();
  }

  if (showScreen1) {
    lcd.setCursor(0, 0);
    lcd.print("V :"); lcd.print(voltageAC, 1);
    lcd.setCursor(9, 0);
    lcd.print("A:"); lcd.print(currentRMS, 3);
    lcd.setCursor(0, 1);
    lcd.print("PF :"); lcd.print(pf, 2);
    lcd.setCursor(9, 1);
    lcd.print("VA:"); lcd.print(powerVA, 0);
  } /*else {
    lcd.setCursor(0, 0);
    lcd.print("W :"); lcd.print(powerWatt, 1);
    lcd.setCursor(0, 1);
    lcd.print("Wh : "); lcd.print(energyWh, 2);
  }*/
}