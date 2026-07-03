#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
EnergyMonitor emon1;

const int pinACS = A2;     
const int pinZMPT = A1;    
const int pinLED = 13;      
const int pinBuzzer = 3;   

const float VOLT_CAL = 328.9; 
const float CURRENT_CAL = 5.23; 

unsigned long previousMillisBuzzer = 0;
const long beepInterval = 500; 
bool isBeeping = false;

float energyWh = 0.0;
unsigned long previousTimeEnergy = 0;

bool showScreen1 = true; 
unsigned long previousScreenMillis = 0;
const long screenInterval = 3000; 

unsigned long previousLcdReset = 0;
const long lcdResetInterval = 30000; 

float getFrequency(int pin) {
  int prev = analogRead(pin) >= 512;
  unsigned long prevCross = 0;
  unsigned long periodSum = 0;
  int periods = 0;

  for (int i = 0; i < 200; i++) {
    int cur = analogRead(pin) >= 512;
    if (prev == 0 && cur == 1) {
      unsigned long now = micros();
      if (prevCross != 0) {
        periodSum += now - prevCross;
        periods++;
      }
      prevCross = now;
    }
    prev = cur;
    delayMicroseconds(500);
  }

  if (periods > 0) {
    return 1000000.0 / (periodSum / periods);
  }
  return 0;
}

void resetLcd() {
  Wire.end();
  delay(10);
  Wire.begin();
  Wire.setWireTimeout(3000, true);
  lcd.init();
  lcd.backlight();
  lcd.clear();
}

void setup() {
  Serial.begin(9600);
  
  Wire.begin();
  // Set timeout I2C agar tidak loop infinite jika modul hang
  Wire.setWireTimeout(3000, true); 

  lcd.init();
  lcd.backlight();

  pinMode(pinLED, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);

  emon1.voltage(pinZMPT, VOLT_CAL, -0.9); 
  emon1.current(pinACS, CURRENT_CAL); 

  lcd.setCursor(0, 0);
  lcd.print("VA-Meter");
  lcd.setCursor(0, 1);
  lcd.print("Loading...      ");
  delay(2000);
  
  // Hapus layar satu kali saja di setup
  lcd.setCursor(0, 0); lcd.print("                ");
  lcd.setCursor(0, 1); lcd.print("                ");

  previousTimeEnergy = millis();
}

void loop() {
  emon1.calcVI(20, 2000);

  float voltageAC = emon1.Vrms;
  float currentRMS = emon1.Irms;
  float powerWatt = emon1.realPower;     
  float powerVA = emon1.apparentPower;   
  float pf = emon1.powerFactor;          
  float freq = (voltageAC >= 3.0) ? getFrequency(pinZMPT) : 0;  

  if (voltageAC < 3.0) {
    voltageAC = 0.0;
  }

  if (currentRMS < 0.08) {
    currentRMS = 0.0;
    powerWatt = 0.0;
    powerVA = 0.0;
    pf = 0.0;
  }

  if (pf < 0.0) pf = 0.0;
  if (pf > 1.0) pf = 1.0;

  unsigned long currentTime = millis();
  unsigned long deltaTime = currentTime - previousTimeEnergy;
  previousTimeEnergy = currentTime;
  
  energyWh += (powerWatt * (deltaTime / 3600000.0));

  if (Wire.getWireTimeoutFlag() || (currentTime - previousLcdReset >= lcdResetInterval)) {
    Wire.clearWireTimeoutFlag();
    resetLcd();
    previousLcdReset = currentTime;
  }

  // --- LOGIK GANTI LAYAR ---
  if (currentTime - previousScreenMillis >= screenInterval) {
    previousScreenMillis = currentTime;
    showScreen1 = !showScreen1; 
    
    lcd.setCursor(0, 0); lcd.print("                ");
    lcd.setCursor(0, 1); lcd.print("                ");
  }

  // --- TAMPILAN LCD DENGAN PADDING SPASI ---
  if (showScreen1) {
    lcd.setCursor(0, 0);
    lcd.print("V :"); lcd.print(voltageAC, 1); lcd.print(" ");
    lcd.setCursor(9, 0);
    lcd.print("A:"); lcd.print(currentRMS, 2); lcd.print(" ");

    lcd.setCursor(0, 1);
    lcd.print("PF :"); lcd.print(pf, 2); lcd.print(" ");
    lcd.setCursor(9, 1);
    lcd.print("Hz:"); lcd.print(freq, 0); lcd.print(" ");
  } 
  else {
    lcd.setCursor(0, 0);
    lcd.print("W :"); lcd.print(powerWatt, 1); lcd.print("     ");

    lcd.setCursor(0, 1);
    lcd.print("Wh : "); lcd.print(energyWh, 2); lcd.print("     ");
  }

  // --- LOGIK BUZZER & LED ---
  if (currentRMS >= 2.5) {
    digitalWrite(pinBuzzer, HIGH);
    digitalWrite(pinLED, HIGH);
  } 
  else if (currentRMS >= 2.0) {
    if (currentTime - previousMillisBuzzer >= beepInterval) {
      previousMillisBuzzer = currentTime;
      isBeeping = !isBeeping;
      digitalWrite(pinBuzzer, isBeeping);
      digitalWrite(pinLED, isBeeping);
    }
  } 
  else {
    digitalWrite(pinBuzzer, LOW);
    digitalWrite(pinLED, LOW);
    isBeeping = false;
  }

  Serial.print("V:"); Serial.print(voltageAC, 1);
  Serial.print(" A:"); Serial.print(currentRMS, 2);
  Serial.print(" W:"); Serial.print(powerWatt, 1);
  Serial.print(" VA:"); Serial.print(powerVA, 0);
  Serial.print(" PF:"); Serial.print(pf, 2);
  Serial.print(" Hz:"); Serial.print(freq, 0);
  Serial.print(" Wh:"); Serial.println(energyWh, 2);
  delay(100);
}
