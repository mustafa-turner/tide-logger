# Stasiun Pasang Surut ESP32-S3

Proyek PlatformIO ini menggunakan Seeed Studio XIAO ESP32S3 untuk memantau
ketinggian air, tegangan, dan arus. Hasil pengukuran ditampilkan melalui Serial
Monitor dan dikirim ke dashboard Blynk melalui Wi-Fi.

## Fitur

- Membaca tegangan solar cell pada INA3221 channel 1.
- Membaca tegangan baterai pada INA3221 channel 2.
- Membaca tegangan dan arus sistem 5 V pada INA3221 channel 3.
- Memfilter seluruh sampel processed-mode A02YYUW selama jendela waktu yang
  dikonfigurasi dengan median, MAD, dan 10%
  trimmed mean.
- Mengontrol catu A02YYUW melalui load switch BS250 dan PN2222A pada D0.
- Membaca suhu dan kelembapan menggunakan Fermion DFRobot SHT40.
- Menghitung ketinggian air terhadap datum/titik nol pengukuran.
- Menyinkronkan waktu UTC melalui NTP dan menjadwalkan pengukuran pada batas
  interval yang dapat dikonfigurasi (default 10 menit).
- Menggunakan interval relatif yang sama sebagai fallback apabila waktu UTC
  belum tersedia.
- Menyimpan maksimal 8.064 record (56 hari pada interval default) pada ring buffer LittleFS sebelum
  mencoba upload ke cloud.
- Mengirim ulang backlog dengan timestamp asli dan menghapus record hanya setelah
  sequence V21 terkonfirmasi dari Blynk.
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
frame awal, lalu mengumpulkan seluruh sampel selama jendela waktu sebelum
kembali mematikan sensor.

UART menggunakan 9600 baud, 8 data bit, tanpa parity, dan 1 stop bit. Program
memvalidasi header `0xFF` dan checksum sebelum memakai data jarak.

Pemilihan keluaran A02YYUW:

- RX sensor floating atau HIGH: processed value, lebih stabil.
- RX sensor ke GND: real-time value, respons lebih cepat.

Processed value disarankan untuk pemantauan pasang surut karena permukaan air
dapat bergerak akibat gelombang. D6 XIAO dicadangkan sebagai TX UART oleh
program, tetapi tidak perlu dihubungkan ke RX sensor ketika memakai mode
processed value.

Akuisisi berlangsung 60 detik secara default agar gelombang terwakili tanpa
membuat perangkat terus aktif. Batas ini dapat diubah dari Terminal V10. Setelah
pemeriksaan checksum dan rentang 30–4500 mm, firmware menghitung
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
| V10 | Device Terminal | String | - | Terminal perintah untuk konfigurasi MQTT, waktu pengukuran, dan antrean offline. |
| V11 | Measurement Quality | Integer | 0/1/2 | Kualitas hasil A02YYUW: 0 INVALID, 1 POOR, atau 2 GOOD. |
| V12 | Samples Acquired | Integer | count | Jumlah seluruh sampel A02YYUW valid yang terkumpul selama duration. Gunakan rentang hingga 4095. |
| V13 | Samples Used | Integer | count | Jumlah sampel yang dipakai dalam trimmed mean setelah filter MAD dan trimming. |
| V14 | MAD Outliers | Integer | count | Jumlah sampel yang ditolak sebagai outlier oleh filter MAD. |
| V15 | Distance MAD | Double | mm | Ukuran kestabilan/sebaran jarak terhadap median; semakin kecil semakin stabil. |
| V16 | Acquisition Duration | Integer | ms | Waktu pengumpulan sampel A02YYUW setelah warm-up hingga selesai atau timeout. |
| V17 | Stay Awake | Integer | 0/1 | `0` memakai deep sleep normal; `1` membuat perangkat tetap terbangun. |
| V18 | Connected SSID | String | - | Nama jaringan Wi-Fi yang sedang digunakan perangkat; password tidak dikirim. |
| V19 | Pending Offline Records | Integer | count | Jumlah record yang masih menunggu konfirmasi upload; `-1` berarti LittleFS gagal. |
| V20 | Dropped Offline Records | Integer | count | Akumulasi record rusak atau record tertua yang dibuang ketika antrean penuh. |
| V21 | Last Uploaded Sequence | Integer | count | Sequence internal untuk memastikan record sudah diterima cloud sebelum dihapus. |

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

Saat V17 **Stay Awake** bernilai `1`, kegagalan menyambung ke Wi-Fi secara terus
menerus selama dua menit akan membuka access point provisioning Blynk.
Sambungkan ponsel ke access point bernama `Blynk <nama-template>-XXXX`, lalu
lakukan konfigurasi ulang melalui aplikasi Blynk seperti saat pemasangan
pertama. Konfigurasi lama tetap tersimpan sampai pengganti dikirim melalui
portal. Fallback ini hanya dipicu ketika ESP32 tidak tersambung ke Wi-Fi. Jika
Wi-Fi tersambung tetapi internet atau Blynk Cloud sedang offline, konfigurasi
Wi-Fi tidak dihapus. Selama portal fallback aktif, firmware tetap menjalankan
siklus pengukuran pada interval yang dikonfigurasi dan menyimpan setiap record
ke antrean offline LittleFS. Portal tetap dapat digunakan oleh teknisi pada saat
yang sama.

Ketika V17 **Stay Awake** aktif dan koneksi tersedia, firmware menguras antrean
offline terus-menerus satu record per pass tanpa menunggu slot pengukuran
berikutnya. Setiap record tetap menunggu ACK Blynk dan/atau PUBACK MQTT sesuai
mode delivery sebelum record berikutnya dimulai, dengan jeda satu detik antar
pass. Saat slot ukur tiba, replay diprioritaskan lebih rendah: firmware memulai
jendela A02YYUW, menyelesaikan INA3221/SHT40, dan menyimpan record lebih dahulu;
tidak ada publish record offline selama akuisisi berlangsung.

Saat V17 bernilai `0`, dua wake pertama yang gagal tersambung ke Wi-Fi tetap
memakai perilaku hemat daya: perangkat menyimpan record yang belum terkirim,
deep sleep, lalu mencoba lagi pada wake berikutnya. Pada kegagalan Wi-Fi ketiga
secara berturut-turut, perangkat membuka access point provisioning Blynk dan
tetap terjaga agar dapat dikonfigurasi di lokasi baru. Counter disimpan di NVS
agar tidak hilang selama deep sleep. Setiap koneksi Wi-Fi yang berhasil me-reset
counter, meskipun internet atau Blynk Cloud sedang offline. Konfigurasi lama
tidak dihapus ketika AP otomatis dibuka. Setiap 30 detik firmware memindai SSID
lama tanpa mematikan access point; ketika SSID itu muncul kembali, firmware
mencoba password tersimpan selama maksimal 15 detik. Koneksi yang berhasil
menutup fallback tanpa input portal, me-reset counter, lalu melanjutkan koneksi
Blynk/MQTT dan replay antrean. Kegagalan percobaan tetap berada di portal dan
tidak berpindah-pindah ke state koneksi Edgent. Setelah konfigurasi baru berhasil
atau jaringan lama pulih, perangkat kembali ke jadwal ukur/deep sleep normal.

Provisioning pertama dan reset manual dengan tombol BOOT tetap menahan perangkat
dalam portal sampai konfigurasi selesai. Kegagalan internet saja tidak dihitung
sebagai kegagalan Wi-Fi dan tidak membuka portal otomatis.

Untuk V18, buat Datastream String dan hubungkan ke widget Label atau Value
Display. Firmware memperbarui V18 setiap kali berhasil tersambung ke Blynk agar
dashboard menunjukkan SSID aktif. Hanya nama SSID yang dikirim; password dan
BSSID tidak dikirim.

Untuk V10, buat Datastream **String** dengan panjang maksimum `255`, lalu
hubungkan widget **Terminal** ke V10. Aktifkan input, auto-scroll, dan penambahan
baris baru pada widget. Jangan aktifkan **Sync with latest server value** untuk
V10 karena perintah lama tidak boleh dijalankan kembali setelah reconnect.
Gunakan perintah berikut:

```text
help
mqtt show
mqtt on
mqtt off
mqtt set 192.168.1.50
mqtt apply
mqtt reset
measure show
measure interval 600
measure duration 60
measure reset
offline show
offline clear
```

`mqtt set` hanya menerima satu alamat IPv4 unicast dan menyimpannya ke NVS;
password MQTT tidak diperlukan. Perubahan berlaku pada wake/restart berikutnya.
`mqtt apply` me-restart perangkat setelah satu detik agar nilai tersimpan segera
dipakai. `mqtt reset` menghapus override dan mengembalikan `MQTT_HOST` dari
`include/device_config.h` pada restart berikutnya. Jika deep sleep aktif, buka
Terminal ketika perangkat sedang online; untuk konfigurasi lebih nyaman,
aktifkan V17 **Stay Awake** sementara lalu kembalikan ke `0` setelah selesai.

`mqtt on` mengaktifkan delivery **Blynk + MQTT**, sedangkan `mqtt off`
mengaktifkan **Blynk only**. Pilihan berlaku langsung, disimpan di NVS, dan
dipertahankan setelah restart/deep sleep. Saat MQTT dimatikan, koneksi MQTT
diputus dan antrean direkonsiliasi terhadap ACK Blynk; record yang sudah mendapat
ACK Blynk tidak lagi ditahan hanya karena belum mendapat PUBACK MQTT. `mqtt show`
menampilkan mode aktif bersama alamat broker.

`measure interval <seconds>` mengatur jarak antar-siklus pengukuran dari `60`
sampai `86400` detik. `measure duration <seconds>` mengatur jendela maksimum
pengumpulan A02YYUW dari `5` sampai `120` detik. Interval harus selalu minimal
30 detik lebih panjang daripada duration agar ada waktu untuk pengukuran lain,
koneksi, dan upload. Nilai disimpan ke NVS dan berlaku untuk siklus berikutnya;
`measure reset` mengembalikan interval `600` detik dan duration `60` detik.

`offline show` menampilkan jumlah record yang menunggu dan counter record yang
pernah dibuang. `offline clear` menghapus seluruh record yang saat itu tersimpan
di antrean LittleFS. Sequence tetap monoton setelah penghapusan agar ACK lama
yang terlambat tidak dapat menghapus record baru.

Duration menentukan lamanya jendela pengumpulan setelah warm-up. Firmware
membuang lima frame awal, lalu menyimpan semua frame valid sampai duration
berakhir. Seluruh kumpulan tersebut masuk perhitungan median, MAD, penolakan
outlier, dan trimmed mean. Duration yang lebih panjang biasanya menaikkan V12
**Samples Acquired**. Jika hasil akhirnya kurang dari 30 sampel/inlier, kualitas
dinyatakan `INVALID` dan V1/V2 tidak diperbarui.

Gunakan rentang `0` sampai `4095` untuk V12, V13, dan V14 agar counter dari
jendela pengumpulan panjang tidak terpotong di Blynk. Untuk V19, gunakan rentang
`-1` sampai `8064`. Untuk V20 dan V21, gunakan rentang
`0` sampai `2147483647`. Aktifkan **Sync with latest server value** pada V21.
V21 wajib tersedia karena firmware menyinkronkannya kembali sebagai acknowledgement
setelah setiap upload. V19 dan V20 dapat ditampilkan menggunakan Value Display;
V21 boleh disembunyikan dari dashboard pengguna.

Arti V11:

| Nilai | Status | Kriteria ringkas |
|---|---|---|
| 0 | INVALID | Kurang dari 30 sampel atau inlier; V1/V2 tidak diperbarui |
| 1 | POOR | Hasil dapat dihitung, tetapi kriteria kestabilan/inlier GOOD tidak terpenuhi |
| 2 | GOOD | Buffer tidak penuh, minimal 80% sampel menjadi inlier, MAD maksimal 30 mm, dan frame valid minimal 90% |

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

Urutan normal setiap interval pengukuran (default sepuluh menit):

1. Bangun dari timer deep sleep.
2. Tetapkan D0 HIGH dan tunggu A02YYUW stabil selama 1 detik.
3. Buang 5 frame awal, kumpulkan seluruh sampel selama duration (default 60 detik), lalu lakukan filtering.
4. Tetapkan D0 LOW dan tahan pin LOW selama deep sleep.
5. Baca SHT40 serta seluruh pengukuran INA3221 yang digunakan firmware.
6. Simpan record bertimestamp ke ring buffer LittleFS sebelum mencoba internet.
7. Jalankan Blynk.Edgent dengan batas koneksi total 15 detik.
8. Setelah Wi-Fi tersambung, mulai sinkronisasi waktu UTC melalui NTP tanpa
   menghentikan proses Edgent.
9. Setelah tersambung ke Blynk, kirim data setelah callback sync V6 dan
   V17 diterima.
   Jika Blynk tidak mengirim nilai tersimpan, gunakan tinggi NVS setelah fallback
   3 detik, tanpa melewati deadline koneksi/upload 15 detik.
10. Sinkronkan V21 dan hapus record hanya jika sequence yang diterima sama dengan
    sequence yang baru dikirim.
11. Tetap online selama 15 detik sebagai jendela penerimaan Blynk.Air OTA.
12. Jika waktu UTC valid, deep sleep sampai batas interval absolut berikutnya.
    Jika waktu belum valid, gunakan sisa interval relatif yang dikonfigurasi.

Jika V17 bernilai `1`, langkah deep sleep dilewati. Perangkat tetap melayani
Blynk.Edgent dan memulai pengukuran baru pada batas absolut interval yang
dikonfigurasi tanpa restart. Jika waktu UTC belum tersedia, mode ini kembali
memakai interval relatif. Ubah V17 menjadi `0` untuk memulihkan deep sleep
setelah siklus aktif selesai.

### Waktu NTP dan jadwal absolut

Firmware sejak versi `1.2.0` menggunakan UTC agar jadwal tidak dipengaruhi zona waktu
atau perubahan konfigurasi lokal. Server yang dicoba adalah `pool.ntp.org`,
`time.google.com`, dan `time.cloudflare.com`. Permintaan NTP berjalan setelah
Wi-Fi tersambung dan tidak menambah proses blocking baru ke siklus Edgent.

Setelah waktu valid, awal siklus diarahkan ke timestamp yang habis dibagi interval
yang dikonfigurasi. Pada default 600 detik contohnya adalah `00:00`, `00:10`,
`00:20`, dan seterusnya. RTC internal ESP32
mempertahankan acuan waktu selama deep sleep, sedangkan koneksi berikutnya
memulai sinkronisasi NTP kembali untuk mengoreksi drift.

Jika NTP belum tersedia, perangkat tetap mengukur dan tidur memakai interval
relatif sehingga kegagalan server waktu tidak menghentikan stasiun. Setelah mati
daya penuh tanpa koneksi internet, timestamp absolut tidak dapat dipastikan
sampai NTP berhasil kembali.

### Buffer offline bertimestamp

Firmware menyimpan setiap hasil pengukuran sebagai record biner 66 byte dengan
checksum. Schema record v2 memadatkan counter sampel yang lebih besar ke layout
yang sama dan firmware tetap dapat membaca record v1 yang sudah mengantre.
Ring buffer berkapasitas 8.064 record, setara 56 hari pada
interval default sepuluh menit, dan menggunakan sekitar 520 KiB dari partisi filesystem
1,5 MiB. Dua salinan metadata dipakai bergantian agar metadata sebelumnya tetap
tersedia jika daya terputus saat penulisan.

Saat Blynk tersambung, record paling lama dikirim menggunakan
`Blynk.beginGroup(timestamp)` sehingga V0–V16 yang relevan dan V21 mempunyai
timestamp pengukuran yang sama. Firmware kemudian meminta kembali V21. Record
hanya dikeluarkan dari LittleFS jika sequence yang diterima sama dengan record
yang sedang menunggu ACK. Timeout atau koneksi terputus membuat record tetap
tersimpan untuk siklus berikutnya.

Untuk memberi ruang terhadap batas datapoint harian Blynk, firmware mengirim satu
record pada satu slot dan maksimal dua record pada slot UTC berikutnya. Rata-rata
maksimum adalah 1,5 record per siklus, sehingga backlog berkurang bertahap ketika
koneksi pulih. Ketika kapasitas penuh, record tertua dibuang dan penghitung V20
bertambah. Batas per-siklus ini berlaku pada operasi deep sleep normal; V17
**Stay Awake** memakai replay satu-record berkelanjutan seperti dijelaskan di
atas.

Record yang dibuat setelah UTC tersedia mempertahankan timestamp asli. Record
yang dibuat setelah cold boot tanpa NTP tetap disimpan, tetapi ketika diunggah
akan memakai waktu server karena waktu absolut aslinya tidak dapat dipastikan
tanpa RTC eksternal.

Pengujian buffer offline yang disarankan:

1. Buat V19, V20, dan V21 sesuai tabel, kemudian upload firmware melalui USB.
2. Jalankan satu siklus online dan pastikan log ACK V21 muncul serta V19 kembali
   ke `0`.
3. Matikan access point selama minimal tiga siklus. Record harus tetap bertambah
   di LittleFS walaupun dashboard tidak dapat diperbarui saat offline.
4. Hidupkan access point dan pastikan V19 menampilkan backlog, lalu berkurang
   bertahap pada siklus berikutnya.
5. Periksa chart V1/V2: record replay yang memiliki UTC valid harus muncul pada
   waktu pengukurannya, bukan pada waktu Wi-Fi tersambung kembali.
6. Ulangi dengan reset perangkat ketika offline untuk memastikan antrean tetap
   tersedia setelah boot.

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
A02YYUW warm-up selesai; buang 5 frame lalu kumpulkan semua sampel selama 60 detik.
A02YYUW: 100 sampel terkumpul.
...
A02YYUW: 600 sampel terkumpul.
A02YYUW OFF: D0/GPIO 1 LOW.
Hasil filter A02YYUW:
  acquired=50, used=44, outlier=2, checksum_error=0, range_error=0
  median=1250.0 mm, MAD=4.0 mm, limit=20.0 mm
  filtered=1251 mm, quality=2, duration=5100 ms
Solar CH1: 18.400 V
Baterai CH2: 4.012 V
Sistem CH3: 5.016 V, 0.350 A
SHT40: 28.50 C, 76.20 %RH
Record #125 disimpan; antrean offline 4/8064.
Sinkronisasi waktu UTC melalui NTP dimulai.
Waktu UTC tersedia: 2026-08-07 12:04:27 UTC.
Record #122 dikirim dengan timestamp asli; menunggu ACK V21.
ACK record #122 diterima; record dihapus dari antrean.
Upload siklus selesai (batas replay per siklus tercapai); 1 record dikonfirmasi, antrean tersisa 3; jendela OTA 15000 ms.
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
  arah sensor, bidang pantul, dan apakah duration yang dikonfigurasi cukup untuk menghasilkan
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
- **ACK V21 selalu timeout:** buat Datastream Integer V21, aktifkan penyimpanan
  nilai terakhir/**Sync with latest server value**, lalu pastikan rentangnya dapat
  menerima sequence positif. Record tidak dihapus selama ACK belum cocok.
- **V19 bernilai -1:** LittleFS gagal dimount atau ditulis. Jangan mengandalkan
  backup offline sampai masalah partisi/filesystem diperbaiki.
- **V20 terus bertambah:** antrean penuh atau ditemukan record dengan checksum
  rusak. Periksa lama gangguan internet dan kesehatan flash.
- **Perangkat tidak masuk deep sleep:** pastikan switch V17 bernilai `0`.
- **OTA belum mulai:** shipment Blynk.Air dapat menunggu hingga perangkat bangun
  pada siklus berikutnya. Pastikan perangkat sempat online dan jangan memutus
  catu daya setelah log menunjukkan bahwa OTA dimulai.

## Struktur penting proyek

```text
include/*.h.example  Template konfigurasi/secret yang aman dilacak
include/secrets.h    Konfigurasi Blynk/MQTT lokal yang diabaikan Git
src/OfflineQueue.* Ring buffer LittleFS, checksum record, dan metadata ganda
src/main.cpp       Sensor, Blynk, OTA, NTP, antrean, dan deep sleep
platformio.ini     Konfigurasi board dan library PlatformIO
```

## Output MQTT untuk RTK Dashboard

Firmware dapat memakai Blynk atau broker MQTT lokal sebagai tujuan delivery.
Blynk Edgent tetap mengelola provisioning Wi-Fi, Blynk.Air OTA, konfigurasi V6,
dan V17 Stay Awake. MQTT memakai koneksi Wi-Fi yang sudah dibuat Edgent; SSID
dan password Wi-Fi tidak ditambahkan ke source code.

### Konfigurasi perangkat

1. Salin `include/device_config.h.example` menjadi
   `include/device_config.h` dan `include/secrets.h.example` menjadi
   `include/secrets.h`. Kedua file lokal diabaikan Git.
2. Tentukan alamat LAN host yang menjalankan RTK Dashboard, misalnya dengan
   `hostname -I` pada Raspberry Pi/server.
3. Isi alamat IP/DNS host tersebut pada `MQTT_HOST`. Jangan memakai `0.0.0.0`
   sebagai target dari ESP32; nilai itu hanya alamat bind pada server.
4. Beri host dashboard DHCP reservation/alamat statis, atau pakai nama DNS
   lokal yang stabil. Pada branch `dev`, `mqtt.advertisedHost` mengatur alamat
   yang ditampilkan dashboard; `mqtt.host` tetap alamat bind server, bukan
   alamat tujuan perangkat.
5. Jangan menambahkan username/password MQTT. Broker bawaan dashboard menerima
   koneksi tanpa autentikasi.
6. Batasi TCP 1883 pada LAN/VPN tepercaya. MQTT ini tanpa autentikasi dan tanpa
   enkripsi, sehingga siapa pun yang dapat menjangkau port tersebut dapat
   mengirim telemetri palsu. Jangan mengekspos port 1883 ke internet.
7. Build, upload, lalu monitor dengan PlatformIO:

   ```bash
   pio run
   pio run --target upload
   pio device monitor
   ```

`TELEMETRY_BACKEND` menerima `BLYNK_ONLY`, `MQTT_ONLY`, atau `BOTH`.
`BLYNK_ONLY` memakai ACK sequence V21, sedangkan `MQTT_ONLY` memakai PUBACK QoS
1. `BOTH` menyimpan cursor ACK Blynk dan MQTT secara terpisah pada metadata
queue LittleFS yang redundant. Kedua tujuan dapat terus maju secara independen,
tetapi record baru dihapus setelah kedua cursor melewati sequence tersebut. Jika
perangkat reset atau deep sleep setelah satu ACK, tujuan yang sudah selesai
melanjutkan dari cursor-nya dan tujuan yang tertinggal tetap mengejar backlog.
Nilai ini menjadi default build; perintah Terminal `mqtt on`/`mqtt off` dapat
menggantinya dengan mode `BOTH`/`BLYNK_ONLY` yang persisten saat runtime.

Dual delivery tidak memakai dua filesystem dan tidak menggandakan record.
Queue tetap berisi maksimum 8.064 record x 66 byte, dua header queue 32 byte,
dan dua header cursor 24 byte: maksimum sekitar 532.336 byte. Metadata cursor
disimpan dalam file kecil terpisah pada LittleFS yang sama, sehingga file queue
lama tetap kompatibel dan ukuran per record tidak berubah. Jika salah satu
tujuan offline terus-menerus,
tujuan yang sehat tetap menerima data baru sementara backlog dipertahankan
untuk tujuan yang gagal. Pada interval default sepuluh menit, queue mencapai
kapasitas sekitar 56 hari sebelum kebijakan drop-oldest berlaku.

Contoh konfigurasi dashboard:

```yaml
mqtt:
  host: 0.0.0.0
  port: 1883
```

Jalankan dashboard tanpa secret MQTT:

```bash
python3 server.py --config config.yaml
```

### Topic dan kontrak payload

MQTT 3.1.1 dipublish dengan QoS 1 dan `retain=false` ke:

```text
telemetry/tide_sensor/<device-id>
```

Device ID disanitasi menjadi huruf kecil, angka, dan dash. Client
ID unik berbentuk `tide-<device-id>-<6-digit-chip-suffix>`. Contoh payload:

```json
{
  "schema": "tide-logger-v1",
  "device_id": "tide-station-01",
  "device_type": "tide_sensor",
  "measured_at_ms": 1786406400000,
  "sequence": 125,
  "water_level_m": 1.749,
  "distance_to_water_mm": 1251,
  "sensor_height_m": 3.000,
  "battery_voltage_v": 4.012,
  "solar_voltage_v": 18.400,
  "system_voltage_v": 5.016,
  "system_current_a": 0.350,
  "temperature_c": 28.50,
  "humidity_percent": 76.20,
  "measurement_quality": 2,
  "samples_acquired": 50,
  "samples_used": 44,
  "mad_outliers": 2,
  "distance_mad_mm": 4.00,
  "acquisition_duration_ms": 5100,
  "pending_records": 3,
  "dropped_records": 0,
  "firmware_version": "1.4.0",
  "wifi_rssi_dbm": -61,
  "uptime_sec": 38
}
```

`measured_at_ms` selalu timestamp UTC asli milik record backlog. Field itu
dihilangkan jika `RECORD_TIME_VALID` tidak tersedia; dashboard lalu memakai
waktu terima. Field sensor opsional juga dihilangkan ketika validity flag-nya
tidak aktif atau float-nya tidak finite—firmware tidak menggantinya dengan nol.
Quality, jumlah sampel, outlier, dan durasi tetap dikirim saat hasil jarak tidak
valid. Queue schema v1 tidak memiliki flag khusus MAD; karena itu
`distance_mad_mm` hanya dikirim jika tahap perhitungan MAD tercapai (minimal 30
sampel). `sensor_height_m` direkonstruksi dari distance + water level yang
tersimpan, sehingga replay mempertahankan datum record tersebut.

Payload dibatasi buffer 1024 byte dengan pemeriksaan compile-time dan runtime.
Firmware tidak mengirim username/password MQTT. Kredensial Wi-Fi, token Blynk,
dan payload secret tidak pernah ditulis ke JSON atau log Serial.

Implementasi dashboard saat ini menerima object JSON pada topic apa pun. Topic
`telemetry/tide_sensor/<device-id>` karena itu diterima, dan `device_id` di
payload dipakai sebagai identitas utama. Field `water_level_m` juga membuat
dashboard mengenali `device_type` sebagai `tide_sensor`; raw payload disimpan
ke SQLite. Branch `dev` memakai `measured_at_ms` sebagai waktu sample database
jika field itu tersedia; jika tidak, dashboard memakai waktu terima.

Broker mengirim PUBACK QoS 1 hanya setelah state dashboard diperbarui dan
logger selesai memproses payload. Ini cocok dengan aturan queue firmware:
record baru dihapus setelah packet ID PUBACK yang sama diterima.

### Lifecycle, log, dan troubleshooting

Per siklus, firmware mencoba koneksi/upload MQTT selama maksimum 15 detik dan
memakai reconnect backoff tanpa busy-loop. Maksimum replay tetap satu atau dua
record (sesuai slot UTC), oldest-first, ketika deep sleep aktif. Pada mode V17
**Stay Awake**, setiap pass tetap oldest-first dan maksimal satu record, tetapi
pass berikutnya dimulai setelah ACK dan jeda satu detik sampai antrean kosong
atau slot pengukuran berikutnya tiba. Deep sleep ditahan selama menunggu PUBACK,
lalu dilanjutkan setelah ACK atau deadline; pada timeout record tetap di
LittleFS. MQTT diputus sebelum Wi-Fi dimatikan.

Contoh log normal (tanpa password/token):

```text
MQTT siap: host=192.168.1.50 port=1883 device=tide-station-01 topic=telemetry/tide_sensor/tide-station-01 client=tide-tide-station-01-a1b2c3 qos=1 retain=false TLS=off auth=none.
MQTT PUBLISH: topic=telemetry/tide_sensor/tide-station-01 packet=7 sequence=125 bytes=612.
MQTT PUBACK: packet=7 sequence=125 result=match.
ACK MQTT sequence=125 tersimpan; menunggu tujuan lain.
Semua tujuan ACK sequence=125; record dihapus, sisa=2.
```

Jika koneksi gagal, periksa hal berikut:

- IP salah atau ESP32 dan dashboard berada pada subnet/VLAN yang tidak saling
  route.
- `MQTT_HOST` memakai alamat VPN/interface yang tidak dapat dicapai ESP32;
  ganti dengan alamat LAN atau DNS lokal yang dapat diroute.
- Firewall memblokir TCP 1883.
- Firmware lama masih mencoba kredensial. Dashboard memang mengabaikannya,
  tetapi konfigurasi tide-logger yang benar tidak mengirim kredensial MQTT.
- Beberapa perangkat memakai `TIDE_DEVICE_ID` yang sama. Dashboard akan
  menggabungkan payload mereka ke satu device meskipun client ID chip berbeda.
- TLS diaktifkan terhadap broker plaintext. Firmware ini sengaja menolak
  `MQTT_USE_TLS != 0` saat compile.
- Dashboard offline atau PUBACK terlambat melewati deadline. Record tetap
  queued dan dicoba lagi pada wake berikutnya dengan sequence dan timestamp
  asli.

Pengujian unauthenticated CONNECT, dashboard-offline, delayed PUBACK, dan
transisi deep sleep memerlukan broker serta board fisik. Host test di
`test/test_telemetry`
memeriksa nama field JSON, validity/omission, timestamp replay, sanitasi ID, dan
packet ID PUBACK yang cocok.

### Struktur MQTT

```text
include/device_config.h.example  Konfigurasi site/device tanpa secret
include/secrets.h.example        Placeholder secret yang aman dilacak
src/TelemetryPayload.*           Sanitasi ID dan serialisasi JSON bounded
src/TelemetryDelivery.h          Transisi sequence/packet ID/PUBACK
src/TelemetryDeliveryState.h     Bit ACK durable per tujuan
src/MqttTelemetry.*              Koneksi, backoff, publish QoS 1, callback ACK
src/main.cpp                     Orkestrasi Blynk/MQTT/queue/deep sleep
```
