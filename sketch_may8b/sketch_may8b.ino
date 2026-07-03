#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
EnergyMonitor emon1;

const int pinACS = A2;     
const int pinZMPT = A1;    
const int pinLED = 13;      
const int pinBuzzer = 3;   

const float VOLT_CAL = 550.7; 
const float CURRENT_CAL = 5.33; 

unsigned long previousMillisBuzzer = 0;
const long beepInterval = 500; 
bool isBeeping = false;

float energyWh = 0.0;
unsigned long previousTimeEnergy = 0;

bool showScreen1 = true; 
unsigned long previousScreenMillis = 0;
const long screenInterval = 3000; 

void setup() {
  Serial.begin(9600);
  
  Wire.begin();
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

  if (currentRMS < 0.065) {
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
    lcd.print("PF :"); lcd.print(pf,2);
    lcd.setCursor(9, 1);
    lcd.print("VA:"); lcd.print(powerVA, 0); 
  } 
  else {

    lcd.setCursor(0, 0);
    lcd.print("W :"); lcd.print(powerWatt, 1); 

    lcd.setCursor(0, 1);
    lcd.print("Wh : "); lcd.print(energyWh, 2); 
  }

  if (currentRMS >= 3.0) {
    digitalWrite(pinBuzzer, HIGH);
    digitalWrite(pinLED, HIGH);
  } 
  else {
    digitalWrite(pinBuzzer, LOW);
    digitalWrite(pinLED, LOW);
    isBeeping = false;
  }
}