#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EmonLib.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);
EnergyMonitor emon1;

// Pin mapping
const int pinZMPT   = A1;
const int pinACS    = A2;
const int pinLED    = 13;
const int pinBuzzer = 3;

// Calibration - sesuaikan dengan sensor
const float VOLT_CAL     = 310.0;
const float CURR_CAL     = 5.0;
const float PHASE_SHIFT  = 1.7;

// Threshold noise
const float V_THRESHOLD  = 3.0;
const float I_THRESHOLD  = 0.05;

// Buzzer zones
const float I_ALARM_ON   = 3.0;
const float I_ALARM_BLINK = 2.5;

// Display
bool showScreen1 = true;
unsigned long prevScreenMs = 0;
const long SCREEN_INTERVAL = 3000;

// Energy
float energyWh = 0.0;
unsigned long prevEnergyMs = 0;

// Frequency
unsigned long prevCrossUs = 0;
float frequency = 50.0;
int prevV = 0;

// Buzzer
unsigned long prevBuzzerMs = 0;
bool buzzerState = false;

// -- Frequency via zero-crossing --
float getFrequency(int pin) {
  int v = analogRead(pin);
  if (prevV < 512 && v >= 512) {
    if (prevCrossUs != 0) {
      unsigned long period = micros() - prevCrossUs;
      if (period > 0) frequency = 1000000.0 / period;
    }
    prevCrossUs = micros();
  }
  prevV = v;
  return frequency;
}

void setup() {
  Serial.begin(9600);

  Wire.begin();
  Wire.setWireTimeout(3000, true);

  lcd.init();
  lcd.backlight();

  pinMode(pinLED, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);

  emon1.voltage(pinZMPT, VOLT_CAL, PHASE_SHIFT);
  emon1.current(pinACS, CURR_CAL);

  lcd.setCursor(0, 0); lcd.print("   VA Meter");
  lcd.setCursor(0, 1); lcd.print("   Loading...");
  delay(2000);
  lcd.clear();

  prevEnergyMs = millis();
}

void loop() {
  // -- Baca sensor via EmonLib (20 crossings, 2000ms timeout) --
  emon1.calcVI(20, 2000);

  float V  = emon1.Vrms;
  float I  = emon1.Irms;
  float W  = emon1.realPower;
  float VA = emon1.apparentPower;
  float PF = emon1.powerFactor;

  // -- Threshold noise --
  if (V < V_THRESHOLD) V = 0;
  if (I < I_THRESHOLD) { I = 0; W = 0; VA = 0; PF = 0; }
  if (PF < 0.0) PF = 0.0;
  if (PF > 1.0) PF = 1.0;

  // -- Frekuensi (zero-crossing terpisah) --
  float Hz = getFrequency(pinZMPT);

  // -- Energi --
  unsigned long now = millis();
  energyWh += W * ((now - prevEnergyMs) / 3600000.0);
  prevEnergyMs = now;

  // -- I2C timeout recovery --
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    Wire.begin();
    lcd.init();
  }

  // -- Ganti layar --
  if (now - prevScreenMs >= SCREEN_INTERVAL) {
    prevScreenMs = now;
    showScreen1 = !showScreen1;
    lcd.clear();
  }

  // -- Tampilkan LCD --
  if (showScreen1) {
    lcd.setCursor(0, 0);
    lcd.print("V:"); lcd.print(V, 1);
    lcd.setCursor(8, 0);
    lcd.print("A:"); lcd.print(I, 2);

    lcd.setCursor(0, 1);
    lcd.print("VA:"); lcd.print(VA, 0);
    lcd.setCursor(9, 1);
    lcd.print(Hz, 0); lcd.print("Hz");
  } else {
    lcd.setCursor(0, 0);
    lcd.print("W:"); lcd.print(W, 1);
    lcd.setCursor(8, 0);
    lcd.print("PF:"); lcd.print(PF, 2);

    lcd.setCursor(0, 1);
    lcd.print("Wh:"); lcd.print(energyWh, 2);
  }

  // -- Buzzer & LED (3 zona) --
  if (I >= I_ALARM_ON) {
    digitalWrite(pinBuzzer, HIGH);
    digitalWrite(pinLED, HIGH);
  } else if (I >= I_ALARM_BLINK) {
    if (now - prevBuzzerMs >= 500) {
      prevBuzzerMs = now;
      buzzerState = !buzzerState;
      digitalWrite(pinBuzzer, buzzerState);
      digitalWrite(pinLED, buzzerState);
    }
  } else {
    digitalWrite(pinBuzzer, LOW);
    digitalWrite(pinLED, LOW);
    buzzerState = false;
  }

  Serial.print("V:"); Serial.print(V);
  Serial.print(" I:"); Serial.print(I);
  Serial.print(" W:"); Serial.print(W);
  Serial.print(" VA:"); Serial.print(VA);
  Serial.print(" PF:"); Serial.print(PF);
  Serial.print(" Hz:"); Serial.print(Hz);
  Serial.print(" Wh:"); Serial.println(energyWh);

  delay(100);
}
