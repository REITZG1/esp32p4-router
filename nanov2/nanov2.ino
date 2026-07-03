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

bool showScreen1 = true; 
unsigned long previousScreenMillis = 0;
const long screenInterval = 3000; 

void setup(){
 Serial.begin(9600);
  Wire.begin();

  lcd.init();
  lcd.backlight();

  emon1.voltage(pinZMPT, VOLT_CAL, -0.9);
}

void loop(){
  emon1.calcVI(20, 2000);
  float volt = emon1.Vrms;

    if (showScreen1) {
    lcd.setCursor(0, 0);
    lcd.print("V :"); lcd.print(volt, 1);
    }

    Serial.println(volt);
    delay(100);
}