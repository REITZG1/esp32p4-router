#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <math.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

const int pinACS = 35;      // Sensor Arus (ACS712)
const int pinZMPT = 34;     // Sensor Tegangan (ZMPT101B)

const float VREF = 3.3;           
const int ADC_MAX = 4095; 
const int NUM_SAMPLES = 256;      

// Kalibrasi - NILAI AWAL, HARUS DIKALIBRASI ULANG!
// Rumus konversi dari Nano: nilai_esp32 = value_nano × (1023/4095)
float VOLT_CAL = 132.5;     // 530 × 0.25 ≈ 132.5 (mulai dari sini, lalu sesuaikan)
float CURRENT_CAL = 1.33;   

const float VOLTAGE_OFFSET = 2048.0;
const float CURRENT_OFFSET = 2048.0;

float energyWh = 0.0;
bool showScreen1 = true;

unsigned long previousMillisBuzzer = 0;
unsigned long previousTimeEnergy = 0;
unsigned long previousScreenMillis = 0;
const long screenInterval = 3000;

int readADCFiltered(int pin, int samples = 4) {
  long sum = 0;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(pin);
    delayMicroseconds(50);  // Jeda singkat agar ADC stabil
  }
  return sum / samples;
}

void calculatePower(float *voltage, float *current, float *realPower, float *apparentPower, float *pf) {
  float sumV2 = 0, sumI2 = 0, sumVI = 0;
  
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int vRaw = readADCFiltered(pinZMPT, 1);
    int iRaw = readADCFiltered(pinACS, 1);
    
    // Konversi ke nilai AC murni (hapus offset DC, normalisasi ke volt)
    // Rumus: (raw - offset) × (Vref / (ADC_MAX/2))
    float vSample = ((float)vRaw - VOLTAGE_OFFSET) * (VREF / (ADC_MAX / 2.0));
    float iSample = ((float)iRaw - CURRENT_OFFSET) * (VREF / (ADC_MAX / 2.0));
    
    // Akumulasi untuk perhitungan RMS dan Power
    sumV2 += vSample * vSample;      // Σv²
    sumI2 += iSample * iSample;      // Σi²
    sumVI += vSample * iSample;      // Σ(v×i)
  }
  
  // Hitung RMS dari sampel (Root Mean Square)
  float vRMS_raw = sqrt(sumV2 / NUM_SAMPLES);
  float iRMS_raw = sqrt(sumI2 / NUM_SAMPLES);
  
  // Hitung Real Power (rata-rata dari perkalian instan v×i)
  float realPower_raw = sumVI / NUM_SAMPLES;
  
  // Aplikasikan faktor kalibrasi (disesuaikan dengan rasio trafo/CT & divider)
  *voltage = vRMS_raw * VOLT_CAL;
  *current = iRMS_raw * CURRENT_CAL;
  *realPower = realPower_raw * VOLT_CAL * CURRENT_CAL;
  
  // Apparent Power = Vrms × Irms (setelah kalibrasi)
  *apparentPower = (*voltage) * (*current);
  
  // Power Factor = Real Power / Apparent Power
  if (*apparentPower > 0.5) {  // Hindari pembagian dengan nilai sangat kecil
    *pf = *realPower / *apparentPower;
    // Clamp PF ke range [0.0 - 1.0]
    if (*pf < 0.0) *pf = 0.0;
    if (*pf > 1.0) *pf = 1.0;
  } else {
    *pf = 0.0;
  }
}

void setup() {
  Serial.begin(96000);  

  Wire.begin(21, 22);
  
  // Inisialisasi LCD
  lcd.init();
  lcd.backlight();
  
  pinMode(pinLED, OUTPUT);
  pinMode(pinBuzzer, OUTPUT);
  
  // Konfigurasi ADC ESP32
  #ifdef ESP32
    analogReadResolution(12);           
    analogSetAttenuation(ADC_11db);   
    analogSetPinAttenuation(pinACS, ADC_11db);
    analogSetPinAttenuation(pinZMPT, ADC_11db);
  #endif
  
  lcd.setCursor(0, 0);
  lcd.print("VA-Meter");
  lcd.setCursor(0, 1);
  lcd.print("Loading...");
  delay(2000);
  lcd.clear();
  
  previousTimeEnergy = millis();
  
  Serial.println("ESP32 VA-Meter Ready (No EmonLib)");
}

void loop() {
  float voltageAC, currentRMS, powerWatt, powerVA, pf;
  
  // Hitung semua parameter daya
  calculatePower(&voltageAC, &currentRMS, &powerWatt, &powerVA, &pf);
  
  // Filter noise: anggap 0 jika arus sangat kecil (sesuai kode asli)
  if (currentRMS < 0.065) {
    currentRMS = 0.0;
    powerWatt = 0.0;
    powerVA = 0.0;
    pf = 0.0;
  }
  
  // Clamp Power Factor ke [0.0 - 1.0]
  if (pf < 0.0) pf = 0.0;
  if (pf > 1.0) pf = 1.0;
  
  // Akumulasi Energi (Wh)
  unsigned long currentTime = millis();
  unsigned long deltaTime = currentTime - previousTimeEnergy;
  previousTimeEnergy = currentTime;
  energyWh += (powerWatt * (deltaTime / 3600000.0));  // Wh = Watt × jam
  
  // Ganti layar tiap 3 detik (toggle display)
  /*if (currentTime - previousScreenMillis >= screenInterval) {
    previousScreenMillis = currentTime;
    showScreen1 = !showScreen1;
    lcd.clear();
  }*/

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
  
  // Debug via Serial (opsional - bisa di-comment jika tidak dipakai)
  // Serial.printf("V:%.1fV A:%.3fA W:%.1fW VA:%.0fVA PF:%.2f Wh:%.2f\n", 
  //               voltageAC, currentRMS, powerWatt, powerVA, pf, energyWh);


  Serial.print (voltageAC);
  delay(1000); 
}