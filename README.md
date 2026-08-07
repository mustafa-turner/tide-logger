# Stasiun Pasang Surut ESP32-S3

Proyek PlatformIO ini menggunakan Seeed Studio XIAO ESP32S3 untuk memantau
ketinggian air, tegangan, dan arus. Hasil pengukuran ditampilkan melalui Serial
Monitor dan dikirim ke dashboard Blynk melalui Wi-Fi.

## Fitur

- Membaca tegangan solar cell pada INA3221 channel 1.
- Membaca tegangan baterai pada INA3221 channel 2.
- Membaca tegangan dan arus sistem 5 V pada INA3221 channel 3.
- Memfilter 50 sampel processed-mode A02YYUW dengan median, MAD, dan 10%
  trimmed mean.
- Mengontrol catu A02YYUW melalui load switch BS250 dan PN2222A pada D0.
- Membaca suhu dan kelembapan menggunakan Fermion DFRobot SHT40.
- Menghitung ketinggian air terhadap datum/titik nol pengukuran.
- Menyinkronkan waktu UTC melalui NTP dan menjadwalkan pengukuran pada batas
  absolut 5 menit (`00`, `05`, `10`, dan seterusnya).
- Menggunakan interval relatif 5 menit sebagai fallback apabila waktu UTC belum
  tersedia.
- Mengelola provisioning Wi-Fi, koneksi cloud, dan OTA melalui Blynk.Edgent.
- Membatasi koneksi siklus normal selama 15 detik dan menyediakan jendela OTA
  15 detik setelah upload.
- Menyediakan kontrol V17 untuk menonaktifkan deep sleep dan mempertahankan
  koneksi saat pengujian atau pemeliharaan jarak jauh.

## Perangkat yang dibutuhkan

- Seeed Studio XIAO ESP32S3.
- Modul INA3221 dengan alamat I2C default `0x40`.
- Sensor ultrasonik waterproof A02YYUW.
- Sensor suhu dan kelembapan Fermion DFRobot SHT40.
- Catu daya dan kabel sesuai kebutuhan instalasi.

## Wiring

### INA3221

| INA3221 | XIAO ESP32S3 | Keterangan |
|---|---|---|
| VCC | 3V3 | Catu sensor |
| GND | GND | Ground bersama |
| SDA | D4 / GPIO 5 | Data I2C |
| SCL | D5 / GPIO 6 | Clock I2C |

Alamat I2C program adalah `0x40`. Pastikan konfigurasi pin alamat A0 pada modul
sesuai dengan alamat tersebut.

Koneksi pengukuran channel:

```text
Sumber positif ---- IN+ channel ---- resistor shunt ---- IN- channel ---- beban
Sumber negatif --------------------------------------------------------- beban
       |
       +---- GND INA3221 dan GND XIAO
```

- Channel 1 dipakai untuk membaca tegangan solar cell.
- Channel 2 dipakai untuk membaca tegangan baterai.
- Channel 3 dipakai untuk membaca tegangan dan arus sistem 5 V.
- Arus channel 3 bernilai positif apabila arus mengalir dari `IN+` ke `IN-`.

Jangan melebihi batas tegangan common-mode dan shunt INA3221. XIAO, INA3221,
sumber yang diukur, dan A02YYUW harus memiliki ground bersama.

### A02YYUW

| A02YYUW | XIAO ESP32S3 | Keterangan |
|---|---|---|
| VCC | Output load switch BS250 | Catu sensor 5 V yang dapat diputus |
| GND | GND | Ground bersama |
| TX | D7 | Data UART menuju RX XIAO |
| RX | Tidak disambungkan | Memilih processed value |

Kontrol load switch:

| Sinyal | Koneksi | Fungsi |
|---|---|---|
| D0 XIAO | Input/base driver PN2222A melalui resistor | HIGH = sensor ON |
| GND XIAO | Emitter PN2222A dan ground rangkaian | Ground bersama |
| BS250 source | 5 V | Masukan catu sensor |
| BS250 drain | VCC A02YYUW | Keluaran catu sensor |

Pastikan gate BS250 memiliki resistor pull-up ke source agar sensor tetap OFF saat
boot atau GPIO belum aktif. Periksa pinout fisik BS250 dan PN2222A yang digunakan
karena urutan kaki dapat berbeda antarprodusen. Firmware menetapkan D0 LOW saat
boot; D0 HIGH mengaktifkan PN2222A, menarik gate BS250 ke bawah, dan menyalakan
A02YYUW. Pada siklus otomatis, firmware menunggu warm-up 1 detik, membuang lima
frame awal, lalu mengumpulkan 50 sampel sebelum kembali mematikan sensor.

UART menggunakan 9600 baud, 8 data bit, tanpa parity, dan 1 stop bit. Program
memvalidasi header `0xFF` dan checksum sebelum memakai data jarak.

Pemilihan keluaran A02YYUW:

- RX sensor floating atau HIGH: processed value, lebih stabil.
- RX sensor ke GND: real-time value, respons lebih cepat.

Processed value disarankan untuk pemantauan pasang surut karena permukaan air
dapat bergerak akibat gelombang. D6 XIAO dicadangkan sebagai TX UART oleh
program, tetapi tidak perlu dihubungkan ke RX sensor ketika memakai mode
processed value.

Akuisisi dibatasi 20 detik agar sensor yang gagal tidak membuat perangkat terus
aktif. Setelah pemeriksaan checksum dan rentang 30–4500 mm, firmware menghitung
median dan Median Absolute Deviation (MAD). Sampel dipertahankan apabila:

```text
|sampel - median| <= max(20 mm, 3 × 1,4826 × MAD)
```

Dari inlier yang tersisa, 10% nilai terendah dan 10% tertinggi dibuang sebelum
rata-rata akhir dihitung. Minimal 30 sampel/inlier diperlukan agar jarak dan
ketinggian air dianggap valid.

### Fermion DFRobot SHT40

| SHT40 | XIAO ESP32S3 | Keterangan |
|---|---|---|
| VCC | 3V3 | Catu sensor |
| GND | GND | Ground bersama |
| SDA | D4 / GPIO 5 | Data I2C, berbagi dengan INA3221 |
| SCL | D5 / GPIO 6 | Clock I2C, berbagi dengan INA3221 |

Alamat I2C default SHT40 adalah `0x44` dan tidak bentrok dengan INA3221 pada
`0x40`. Firmware menggunakan pengukuran presisi tinggi dan memvalidasi CRC
setiap hasil pembacaan.

## Konfigurasi Blynk

1. Buka Blynk Console dan buat Template baru.
2. Buat device dari Template tersebut.
3. Salin Template ID dan Template Name.
4. Buat Datastream berikut pada Template.

| Virtual pin | Nama yang disarankan | Tipe | Unit | Keterangan ringkas |
|---|---|---|---|---|
| V0 | Battery Voltage (CH2) | Double | V | Tegangan baterai yang dibaca INA3221 channel 2. |
| V1 | Jarak ke Air | Integer | mm | Jarak A02YYUW hasil filtering dari sensor ke permukaan air. |
| V2 | Ketinggian Air | Double | m | Tinggi referensi V6 dikurangi jarak hasil filtering V1. |
| V3 | System 5V Voltage (CH3) | Double | V | Tegangan jalur sistem 5 V yang dibaca INA3221 channel 3. |
| V4 | Arus Sistem (CH3) | Double | A | Arus beban sistem yang dihitung dari shunt INA3221 channel 3. |
| V5 | Solar Cell Voltage (CH1) | Double | V | Tegangan panel surya yang dibaca INA3221 channel 1. |
| V6 | Tinggi Referensi Sensor | Double | m | Input jarak vertikal sensor terhadap titik nol; disinkronkan dari Blynk dan disimpan ke NVS. |
| V7 | Suhu SHT40 | Double | °C | Suhu lingkungan hasil pengukuran SHT40. |
| V8 | Kelembapan SHT40 | Double | %RH | Kelembapan relatif hasil pengukuran SHT40. |
| V9 | A02YYUW Power | Integer | 0/1 | Status dan kontrol manual load switch A02YYUW selama perangkat terbangun. |
| V10 | Dicadangkan untuk Terminal | String | - | Belum digunakan; disiapkan untuk terminal/konsol Blynk mendatang. |
| V11 | Measurement Quality | Integer | 0/1/2 | Kualitas hasil A02YYUW: 0 INVALID, 1 POOR, atau 2 GOOD. |
| V12 | Samples Acquired | Integer | count | Jumlah sampel A02YYUW valid yang berhasil dikumpulkan, maksimal 50. |
| V13 | Samples Used | Integer | count | Jumlah sampel yang dipakai dalam trimmed mean setelah filter MAD dan trimming. |
| V14 | MAD Outliers | Integer | count | Jumlah sampel yang ditolak sebagai outlier oleh filter MAD. |
| V15 | Distance MAD | Double | mm | Ukuran kestabilan/sebaran jarak terhadap median; semakin kecil semakin stabil. |
| V16 | Acquisition Duration | Integer | ms | Waktu pengumpulan sampel A02YYUW setelah warm-up hingga selesai atau timeout. |
| V17 | Stay Awake | Integer | 0/1 | `0` memakai deep sleep normal; `1` membuat perangkat tetap terbangun. |
| V18 | Connected SSID | String | - | Nama jaringan Wi-Fi yang sedang digunakan perangkat; password tidak dikirim. |

Atur rentang minimum dan maksimum setiap Datastream sesuai kondisi instalasi.
Tambahkan widget Gauge, Value Display, atau Chart pada dashboard dan hubungkan
widget ke virtual pin yang sesuai.

Untuk V6, gunakan rentang `0.03` sampai `20.0` m dan aktifkan penyimpanan nilai
terakhir pada Blynk. Tambahkan widget Numeric Input atau Slider. Setiap perubahan
V6 langsung memperbarui tinggi referensi yang dipakai perangkat. Ketika perangkat
tersambung kembali, nilai terakhir V6 disinkronkan dari Blynk Cloud. Firmware
juga menyimpan nilai valid V6 ke NVS agar perhitungan setelah bangun dari deep
sleep tidak kembali ke nilai default.

Untuk V17, buat Datastream Integer dengan rentang `0` sampai `1` dan hubungkan
ke widget Switch. Nilai `0` mengaktifkan siklus deep sleep normal, sedangkan
nilai `1` menonaktifkan deep sleep. Pilihan ini disimpan ke NVS dan disinkronkan
dari Blynk ketika perangkat tersambung kembali.

Untuk V18, buat Datastream String dan hubungkan ke widget Label atau Value
Display. Firmware memperbarui V18 setiap kali berhasil tersambung ke Blynk agar
dashboard menunjukkan SSID aktif. Hanya nama SSID yang dikirim; password dan
BSSID tidak dikirim.

Arti V11:

| Nilai | Status | Kriteria ringkas |
|---|---|---|
| 0 | INVALID | Kurang dari 30 sampel atau inlier; V1/V2 tidak diperbarui |
| 1 | POOR | Hasil dapat dihitung, tetapi target/kualitas GOOD tidak terpenuhi |
| 2 | GOOD | 50 sampel, minimal 40 inlier, MAD maksimal 30 mm, dan frame valid minimal 90% |

V12 adalah sampel valid secara protokol/rentang yang berhasil dikumpulkan. V13
adalah jumlah sampel yang benar-benar masuk trimmed mean. V14 hanya menghitung
outlier yang ditolak MAD; sampel yang di-trim tidak dimasukkan sebagai outlier.
Saat V11 bernilai `INVALID`, V15 dikirim sebagai `0` sebagai nilai penanda karena
MAD tidak dapat dihitung; baca V15 bersama V11 karena MAD valid juga bisa bernilai
0 jika seluruh sampel identik.

Untuk pengujian load switch, buat Datastream V9 bertipe Integer dengan rentang
`0` sampai `1`, lalu hubungkan ke widget Switch. Pada siklus normal, load switch
dikendalikan otomatis: ON saat akuisisi dan OFF setelah selesai. Karena Wi-Fi
baru dinyalakan setelah pengukuran, kondisi ON otomatis tidak terlihat secara
live di dashboard; saat tersambung, V9 akan menunjukkan kondisi fisik OFF.
Kontrol manual hanya diterima setelah akuisisi otomatis selesai dan selama
perangkat masih berada dalam jendela aktif sebelum deep sleep.

Pengujian yang sama dapat dilakukan dari Serial Monitor dengan line ending
Newline atau Both NL & CR:

```text
sensor on
sensor status
sensor off
```

Perintah tersebut menggunakan konsol bawaan Blynk.Edgent. Setelah `sensor on`,
tunggu pesan `A02YYUW warm-up selesai` dan pastikan data jarak mulai muncul.

Isi identitas Template pada `include/secrets.h`:

```cpp
#define BLYNK_TEMPLATE_ID "TMPLxxxxxxxx"
#define BLYNK_TEMPLATE_NAME "Stasiun Pasang Surut"
```

SSID, password, dan Auth Token tidak ditulis di firmware. Tambahkan perangkat
melalui aplikasi Blynk dan ikuti provisioning Blynk.Edgent. Untuk menghapus
konfigurasi dan kembali ke mode provisioning, tahan tombol BOOT (GPIO0) sekitar
10 detik. Firmware tidak menggunakan LED indikator.

## Siklus operasi dan OTA

Urutan normal setiap lima menit:

1. Bangun dari timer deep sleep.
2. Tetapkan D0 HIGH dan tunggu A02YYUW stabil selama 1 detik.
3. Buang 5 frame awal, kumpulkan maksimal 50 sampel, lalu lakukan filtering.
4. Tetapkan D0 LOW dan tahan pin LOW selama deep sleep.
5. Baca SHT40 serta seluruh pengukuran INA3221 yang digunakan firmware.
6. Jalankan Blynk.Edgent dengan batas koneksi total 15 detik.
7. Setelah Wi-Fi tersambung, mulai sinkronisasi waktu UTC melalui NTP tanpa
   menghentikan proses Edgent.
8. Setelah tersambung ke Blynk, kirim data segera setelah callback sync V6 dan
   V17 diterima.
   Jika Blynk tidak mengirim nilai tersimpan, gunakan tinggi NVS setelah fallback
   3 detik, tanpa melewati deadline koneksi/upload 15 detik.
9. Tetap online selama 15 detik sebagai jendela penerimaan Blynk.Air OTA.
10. Jika waktu UTC valid, deep sleep sampai batas absolut lima menit berikutnya.
    Jika waktu belum valid, gunakan sisa interval relatif lima menit.

Jika V17 bernilai `1`, langkah deep sleep dilewati. Perangkat tetap melayani
Blynk.Edgent dan memulai pengukuran baru pada batas absolut 5 menit tanpa
restart. Jika waktu UTC belum tersedia, mode ini kembali memakai interval
relatif. Ubah V17 menjadi `0` untuk memulihkan deep sleep setelah siklus aktif
selesai.

### Waktu NTP dan jadwal absolut

Firmware versi `1.2.0` menggunakan UTC agar jadwal tidak dipengaruhi zona waktu
atau perubahan konfigurasi lokal. Server yang dicoba adalah `pool.ntp.org`,
`time.google.com`, dan `time.cloudflare.com`. Permintaan NTP berjalan setelah
Wi-Fi tersambung dan tidak menambah proses blocking baru ke siklus Edgent.

Setelah waktu valid, awal siklus diarahkan ke timestamp yang habis dibagi 300
detik. Contohnya: `00:00`, `00:05`, `00:10`, dan seterusnya. RTC internal ESP32
mempertahankan acuan waktu selama deep sleep, sedangkan koneksi berikutnya
memulai sinkronisasi NTP kembali untuk mengoreksi drift.

Jika NTP belum tersedia, perangkat tetap mengukur dan tidur memakai interval
relatif sehingga kegagalan server waktu tidak menghentikan stasiun. Setelah mati
daya penuh tanpa koneksi internet, timestamp absolut tidak dapat dipastikan
sampai NTP berhasil kembali. Penyimpanan data offline bertimestamp belum termasuk
dalam versi ini dan akan menjadi tahap terpisah.

Provisioning pertama merupakan pengecualian: perangkat tetap terjaga dan sensor
tetap OFF sampai konfigurasi Edgent berhasil. Timeout koneksi hanya berlaku pada
siklus normal yang sudah mempunyai konfigurasi tersimpan.

Untuk OTA jarak jauh, buat shipment Blynk.Air seperti biasa. Perangkat akan
terlihat online selama jendela OTA pada setiap siklus. Begitu perintah OTA dan
URL firmware diterima, firmware mengabaikan seluruh deadline deep sleep, mematikan
A02YYUW, dan tetap aktif sampai proses download/write selesai serta ESP32 restart.
Jika koneksi gagal sebelum data terkirim, perangkat tidak mencoba tanpa batas;
ia kembali tidur dan mencoba lagi pada siklus berikutnya. Ketika V17 bernilai
`1`, batas yang berakhir dengan deep sleep dinonaktifkan agar Edgent terus
mencoba tersambung.

Tombol BOOT hanya dapat mereset provisioning ketika perangkat sedang bangun.
Firmware menunda deep sleep apabila tombol sedang ditekan agar penekanan sekitar
10 detik dapat diselesaikan. Rangkaian load switch tetap harus memakai pulldown
base PN2222A agar sensor OFF secara hardware ketika ESP32 reset atau tidur.

## Kalibrasi

### Tinggi referensi sensor air

Nilai awal dapat diubah melalui `DEFAULT_SENSOR_HEIGHT_MM` pada `src/main.cpp`.
Nilai ini adalah jarak vertikal dalam milimeter dari muka sensor A02YYUW ke
datum/titik nol stasiun:

```cpp
constexpr uint16_t DEFAULT_SENSOR_HEIGHT_MM = 3000;
```

Rumus yang digunakan:

```text
ketinggian air = tinggi referensi sensor - jarak sensor ke permukaan air
```

Contoh: sensor berada 3000 mm di atas datum dan jarak ke air 1250 mm, sehingga
ketinggian air adalah 1750 mm atau 1,750 m.

Setelah Blynk terhubung, nilai input V6 dalam meter menggantikan nilai awal
tersebut. Rentang yang diterima firmware adalah 0,03–20,00 m. Nilai di luar
rentang ditolak dan dilaporkan melalui Serial Monitor.

Pastikan sensor dipasang tegak lurus menghadap air, berada di luar zona buta
sekitar 30 mm, dan jarak permukaan masih dalam rentang program 30–4500 mm.

### Resistor shunt sistem channel 3

Nilai bawaan program adalah `0.1 ohm`, biasanya ditandai `R100` pada modul:

```cpp
constexpr float CH3_SHUNT_RESISTANCE_OHM = 0.1f;
```

Jika modul memakai resistor berbeda, ganti nilai tersebut. Untuk kalibrasi lebih
lanjut, bandingkan arus hasil pembacaan dengan multimeter referensi lalu koreksi
nilai resistansi efektif.

## Build, upload, dan monitor

1. Instal Visual Studio Code dan ekstensi PlatformIO IDE.
2. Buka folder proyek ini melalui PlatformIO.
3. Isi Template ID dan Template Name pada `include/secrets.h`.
4. Sambungkan XIAO ESP32S3 melalui USB.
5. Jalankan **PlatformIO: Build** untuk mengompilasi.
6. Jalankan **PlatformIO: Upload** untuk mengunggah firmware.
7. Buka **PlatformIO: Serial Monitor** pada 115200 baud.
8. Buka aplikasi Blynk, tambahkan perangkat baru, lalu ikuti provisioning
   Blynk.Edgent untuk memilih jaringan Wi-Fi.

Perintah terminal alternatif:

```shell
pio run
pio run --target upload
pio device monitor --baud 115200
```

Contoh output:

```text
A02YYUW ON: D0/GPIO 1 HIGH, warm-up 1000 ms.
A02YYUW warm-up selesai; buang 5 frame lalu ambil 50 sampel processed-mode.
A02YYUW: 10/50 sampel terkumpul.
...
A02YYUW: 50/50 sampel terkumpul.
A02YYUW OFF: D0/GPIO 1 LOW.
Hasil filter A02YYUW:
  acquired=50, used=44, outlier=2, checksum_error=0, range_error=0
  median=1250.0 mm, MAD=4.0 mm, limit=20.0 mm
  filtered=1251 mm, quality=2, duration=5100 ms
Solar CH1: 18.400 V
Baterai CH2: 4.012 V
Sistem CH3: 5.016 V, 0.350 A
SHT40: 28.50 C, 76.20 %RH
Sinkronisasi waktu UTC melalui NTP dimulai.
Waktu UTC tersedia: 2026-08-07 12:04:27 UTC.
Data siklus terkirim; membuka jendela OTA 15000 ms.
Siklus selesai dalam 38000 ms; deep sleep 18000 ms; bangun pada 2026-08-07 12:05:00 UTC.
```

## Pemecahan masalah

- **INA3221 tidak ditemukan:** periksa SDA, SCL, ground, catu daya, dan alamat
  I2C `0x40`.
- **Arus negatif:** tukar arah koneksi `IN+` dan `IN-`, atau pertahankan jika
  arah arus balik memang ingin ditampilkan.
- **Nilai arus tidak akurat:** periksa nilai resistor shunt dan kalibrasi
  `CH3_SHUNT_RESISTANCE_OHM`.
- **A02YYUW tidak mengirim data:** periksa bahwa TX sensor masuk ke D7, baud
  9600, ground bersama, objek berada dalam rentang sensor, D0 berstatus HIGH,
  serta tegangan 5 V benar-benar muncul pada output load switch.
- **Quality selalu INVALID:** periksa V12/V13, checksum/range error pada Serial,
  arah sensor, bidang pantul, dan apakah 20 detik cukup untuk menghasilkan
  minimal 30 sampel processed-mode.
- **Load switch terbalik atau selalu aktif:** periksa pinout BS250/PN2222A,
  resistor base PN2222A, resistor pull-up gate BS250, dan kesamaan ground.
- **SHT40 tidak terbaca:** periksa catu 3,3 V, ground, SDA, SCL, dan pastikan
  alamat modul adalah `0x44`.
- **Ketinggian air negatif:** jarak sensor lebih besar daripada tinggi datum;
  periksa input Blynk V6, `DEFAULT_SENSOR_HEIGHT_MM`, dan definisi titik nol.
- **Blynk offline:** periksa jaringan hasil provisioning, Template ID, akses
  internet, dan status device di Blynk Console. Tahan BOOT sekitar 10 detik untuk
  mengulang provisioning.
- **NTP belum memberikan waktu:** pastikan DNS dan UDP port 123 tidak diblokir
  jaringan. Firmware tetap berjalan dengan jadwal relatif dan mencoba NTP lagi
  setelah boot/deep sleep berikutnya.
- **Perangkat tidak masuk deep sleep:** pastikan switch V17 bernilai `0`.
- **OTA belum mulai:** shipment Blynk.Air dapat menunggu hingga perangkat bangun
  pada siklus berikutnya. Pastikan perangkat sempat online dan jangan memutus
  catu daya setelah log menunjukkan bahwa OTA dimulai.

## Struktur penting proyek

```text
include/secrets.h  Template ID dan Template Name Blynk
src/main.cpp       Program utama
platformio.ini     Konfigurasi board dan library PlatformIO
```
