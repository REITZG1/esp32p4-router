#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
EnergyMonitor emon1;

const int pinZMPT = A1;
const int pinACS  = A2;
const float VOLT_CAL = 310.0;
const float CURR_CAL = 5.0;
const float PHASE_SHIFT = 1.7;

float freq = 50.0;
float energyWh = 0.0;
unsigned long prevTime = 0;
unsigned long prevScreen = 0;
bool screen1 = true;

void setup() {
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("VA Meter Plus");
  lcd.setCursor(0, 1); lcd.print("Loading...");
  delay(1500);

  emon1.voltage(pinZMPT, VOLT_CAL, PHASE_SHIFT);
  emon1.current(pinACS, CURR_CAL);
  prevTime = millis();
}

void loop() {
  emon1.calcVI(20, 2000);

  float V = emon1.Vrms;
  float I = emon1.Irms;
  float W = emon1.realPower;
  float VA = emon1.apparentPower;
  float PF = emon1.powerFactor;

  if (V < 3.0) V = 0;
  if (I < 0.05) { I = 0; W = 0; VA = 0; PF = 0; }
  if (PF < 0) PF = 0;
  if (PF > 1) PF = 1;

  freq = emon1.frequency;

  unsigned long now = millis();
  energyWh += W * ((now - prevTime) / 3600000.0);
  prevTime = now;

  if (now - prevScreen >= 3000) {
    prevScreen = now;
    screen1 = !screen1;
    lcd.clear();
  }

  if (screen1) {
    lcd.setCursor(0, 0);
    lcd.print("V:"); lcd.print(V, 1);
    lcd.setCursor(8, 0);
    lcd.print("A:"); lcd.print(I, 2);
    lcd.setCursor(0, 1);
    lcd.print("VA:"); lcd.print(VA, 0);
    lcd.setCursor(9, 1);
    lcd.print(freq, 0); lcd.print("Hz");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("W:"); lcd.print(W, 1);
    lcd.setCursor(8, 0);
    lcd.print("PF:"); lcd.print(PF, 2);
    lcd.setCursor(0, 1);
    lcd.print("Wh:"); lcd.print(energyWh, 2);
  }

  delay(100);
}
