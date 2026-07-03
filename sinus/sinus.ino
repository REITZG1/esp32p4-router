/*
 * Analisis Gelombang Sinus AC (PLN 50Hz) dengan ZMPT101B
 * Output diarahkan ke Serial Plotter
 */

const int sensorPin = A1;  // Pin analog yang terhubung ke modul ZMPT101B
int sensorValue = 0;
int centeredValue = 0;

// Nilai tengah referensi (Biasanya 512 untuk Arduino 5V 10-bit ADC)
// Jika gelombang tidak pas di tengah 0, Anda bisa menyesuaikan angka ini sedikit (misal 510-515)
const int offset = 512; 

void setup() {
  // Menggunakan baud rate tinggi agar pengiriman data lebih cepat
  // dan bentuk gelombang di Serial Plotter terlihat halus
  Serial.begin(115200);
}

void loop() {
  // 1. Membaca nilai dari sensor (rentang 0 - 1023)
  sensorValue = analogRead(sensorPin);

  // 2. Menghapus offset DC agar titik tengah gelombang berada di 0
  centeredValue = sensorValue - offset;

  // 3. Mengirimkan data ke Serial Plotter
  Serial.println(centeredValue);

  // 4. Jeda untuk sampling rate
  // Listrik PLN di Indonesia berfrekuensi 50Hz (1 siklus = 20 milidetik).
  // Jeda 500 mikrodetik (0.5 ms) akan menghasilkan sekitar 40 sampel per siklus gelombang.
  delayMicroseconds(1000); 
}