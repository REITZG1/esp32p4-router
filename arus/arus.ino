#include <Wire.h>
#include <LiquidCrystal_I2C.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // Alamat I2C umum: 0x27 atau 0x3F

const int pinArus = A2;

// Faktor Kalibrasi (Silakan sesuaikan nilai ini saat pengujian)
const float kalibrasiArus = 0.300;     // Sensitivitas ACS712 5A (~185mV/A)

// Variabel untuk timer dan rata-rata
unsigned long previousMillis = 0;
const long interval = 500; // Update LCD dan Serial setiap 500ms
float totalArus = 0;
int jumlahData = 0;

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0);
  lcd.print("Monitor Energi");
  delay(2000);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Monitor Energi"); // Tampilkan judul kembali setelah clear
}

void loop() {
  float maxI = 0;
  
  // 1. Ambil nilai puncak (peak) dalam 1 siklus gelombang AC (20ms)
  for (int i = 0; i < 100; i++) {
    int sampleI = analogRead(pinArus) - 512;
    if (abs(sampleI) > maxI) maxI = abs(sampleI);
    delayMicroseconds(200);
  }

  // 2. Hitung nilai RMS SESAAT
  float arusRMS_sesaat = (maxI / 1024.0) * 5.0 * 0.9 / kalibrasiArus;
  
  // 3. Akumulasikan data untuk dirata-rata
  totalArus += arusRMS_sesaat;
  jumlahData++;

  // 4. Cek apakah sudah 500ms
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; // Reset timer

    if (jumlahData > 0) {
      // Hitung rata-rata selama 500ms terakhir
      float rataRataArus = totalArus / jumlahData;

      // Filter noise kecil (diaplikasikan pada hasil akhir rata-rata)
      if (rataRataArus < 0.15) rataRataArus = 0.0; 

      // 5. Tampilkan ke LCD 16x2
      lcd.setCursor(0, 1);
      lcd.print("Arus : ");
      lcd.print(rataRataArus, 2); // Tampilkan 2 angka di belakang koma
      lcd.print(" A   ");         // Spasi ekstra untuk menimpa sisa karakter lama

      // 6. Tampilkan ke Serial Monitor
      Serial.println(rataRataArus);
    }

    // 7. Kosongkan kembali keranjang total dan jumlah data untuk 500ms berikutnya
    totalArus = 0;
    jumlahData = 0;
  }
}