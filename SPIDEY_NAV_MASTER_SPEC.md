# 🏍️ Spidey-Nav: Master Architecture & Brainstorming Specification
> **Tanggal Pembuatan:** 31 Oktober 2026  
> **Status:** Finalized Blueprint (Mode Brainstorming Menuju Deadline Besok)  
> **Target Hardware:** ESP32-C3 Super Mini + Layar SPI TFT ILI9341 2.4" (320×240) + Baterai 550mAh + Modul Mini UPS 5V 1A  
> **Target Software:** Flutter Native APK (Android) & Firmware C++ (TFT_eSPI / TJpgDec)

---

## 1. Ringkasan Eksekutif & Filosofi Sistem
Proyek **Spidey-Nav** dirancang sebagai sistem navigasi kokpit sepeda motor mandiri (*Turn-by-Turn Motorcycle Head-Up Display*) kelas produksi. 

Sistem ini memisahkan beban kerja secara cerdas:
1. **Ponsel Pengendara (Android APK Native):** Mengurus komputasi berat, GPS satelit berkepresisian tinggi, perutean OSRM, koneksi Mapbox, serta *rendering* grafis kanvas 320×240.
2. **ESP32-C3 di Kokpit:** Berfungsi sebagai *Thin-Client Display Terminal* yang ringan, hemat daya, tangguh terhadap guncangan dan perubahan pasokan listrik motor, serta dikendalikan oleh 4 tombol fisik ergonomis.
3. **Zero-Laptop di Jalan Raya:** Seluruh sistem berjalan mandiri via aplikasi APK terpasang di ponsel dan hotspot internal tanpa memerlukan PC/localhost sama sekali.

---

## 2. Arsitektur Daya & Sistem Kelistrikan (Power Architecture)

### 2.1. Komponen Daya
* **Sumber Daya Primer:** USB Port / Kontak Kelistrikan Aki Motor (5V DC).
* **Sumber Daya Sekunder:** Baterai Lithium Rechargeable 550mAh (3.7V nominal).
* **Power Management Unit (PMU):** Modul Mini UPS 5V 1A (Auto-charging + Seamless Cutover).

### 2.2. Skenario Idling Stop System (ISS) & Anti-Restart
```
[Arus Aki Motor 5V] ───► [Modul Mini UPS 5V 1A] ───► [ESP32-C3 + LCD 2.4"]
                               ▲   │
                               │   ▼
                     [Baterai 550mAh (Cadangan)]
```
* **Kondisi Normal (Mesin Hidup):** Arus dari motor memberi daya ke layar dan ESP32, sekaligus mengisi baterai 550mAh secara bertahap.
* **Kondisi Lampu Merah / ISS Aktif (Mesin Mati Seketika):** 
  * Sumber arus dari aki terputus seketika.
  * Modul UPS melakukan **seamless cutover** (<10 mikrodetik) ke baterai 550mAh.
  * **Hasil:** ESP32 tidak restart, koneksi Wi-Fi tidak drop, koordinat GPS dan rute jalan tetap berjalan mulus tanpa freeze.
* **Estimasi Ketahanan Baterai:** Arus kerja rata-rata (ESP32 Wi-Fi RX + LCD Backlight) = **130–150 mA**. Baterai 550mAh mampu menyalakan layar secara mandiri selama **3.5 hingga 4 jam nonstop**.

---

## 3. Fungsionalitas & Ergonomi 4 Tombol Fisik (Cockpit Tactile Switches)

Pengendara motor menggunakan sarung tangan tebal dan tidak boleh terganggu fokus pandangannya. Sistem 4 tombol fisik dipetakan dengan logika standar otomotif (*BMW Motorrad Navigator / Garmin Zūmo*):

```
       ┌────────────────────────┐
       │   ILI9341 TFT 2.4"     │
       │       320 x 240        │
       └────────────────────────┘
  [BTN 1]    [BTN 2]    [BTN 3]    [BTN 4]
 (ZOOM IN) (ZOOM OUT)   (MODE)    (ACTION)
```

| Tombol Fisik | Aksi Tekan Cepat (*Single Click*) | Aksi Tekan Tahan (*Long Press 1.5s*) | Indikasi Suara/Visual |
| :--- | :--- | :--- | :--- |
| **Tombol 1 (Atas/Kiri)** | **Zoom In Peta (+0.5x)**<br>Mendekat untuk melihat detail gang / bundaran | **Max Zoom (18.5x)**<br>Fokus ekstrem persimpangan terdekat | Animasi zoom smooth di HUD |
| **Tombol 2 (Bawah/Tengah)** | **Zoom Out Peta (-0.5x)**<br>Melihat rute makro di depan | **Reset Zoom Default (16.5x / 17.2x)**<br>Kembali ke standar kecepatan motor | Indikator zoom pill ter-update |
| **Tombol 3 (Mode)** | **Toggle State Tampilan:**<br>Navigasi Peta $\leftrightarrow$ Standby Clock/Speedo | **Toggle Tema Peta:**<br>Streets v12 (Siang) $\leftrightarrow$ Nav Night (Malam) | Transisi layar tanpa lag |
| **Tombol 4 (Action)** | **Re-Center Posisi Motor:**<br>Memusatkan kamera jika sempat tergeser | **Standby / Backlight Toggle:**<br>Mematikan lampu layar untuk hemat daya baterai | Backlight on/off via pin BLK |

---

## 4. Dual-State UI/UX Architecture pada Layar Kokpit

Layar tidak boleh dibiarkan menampilkan gambar peta kosong/statis jika navigasi belum diaktifkan oleh pengguna di ponsel. ESP32 mengoperasikan 2 state tampilan utama:

### 4.1. STATE 1: Cockpit Standby & Idle Dashboard (Rute Belum Aktif)
Tampilan ini muncul saat ESP32 menyala dan tersambung ke HP, namun pengguna belum memulai perjalanan.
```
+-----------------------------------------------------------------------------------+
|  14:25  •  Selasa, 31 Okt                       [ Bat: 98% • Hotspot Connected ]  |
|                                                                                   |
|                         ┌─────────────────────────────┐                           |
|                         │     SPIDEY COCKPIT HUD      │                           |
|                         │     [ Pulsing Red Halo ]    │                           |
|                         └─────────────────────────────┘                           |
|                                                                                   |
|               0                  KM/H                 Standby Mode                |
|          [ SPEED ]                                Menunggu Rute dari HP...        |
|                                                                                   |
|  [ Tekan Tombol 3 untuk cek rute ]            [ GPS Satelit: 8 Terkunci ]         |
+-----------------------------------------------------------------------------------+
```
* **Sinkronisasi Waktu Instan:** Begitu aplikasi HP terhubung, ponsel mengirim paket data UNIX timestamp. Jam dan tanggal di layar motor langsung presisi 100% mengikuti HP tanpa butuh modul jam RTC fisik tambahan.
* **Idle Animation:** Visual pulsing neon emblem Spidey atau jarum digital speedometer minimalis yang estetik dan modern.

### 4.2. STATE 2: Active Navigation HUD (Rute Sedang Berjalan)
Tampilan ini otomatis aktif saat tombol "Mulai Navigasi" ditekan di aplikasi ponsel.
```
+-----------------------------------------------------------------------------------+
|  [ ↰ 180m ]  Jl. Bojong                              [ 45 KM/H ]   14:28 ETA      |
|  ════════════════════════════════════════════════════════════════════════════════ |
|                                                                                   |
|            [ PETA MAPBOX STREETS V12 (HIGH-CONTRAST ROAD & HALO TEXT) ]           |
|                                                                                   |
|                  📍 PERSIMPANGAN: [ ↰ Jl. Bojong ] (Milestone Badge)              |
|                                                                                   |
|                            🏍️ (TOP-DOWN SUPERBIKE PUCK)                          |
|                                                                                   |
|  ════════════════════════════════════════════════════════════════════════════════ |
|  [ 📍 JALAN AKTIF: Jl. Raya Cilimus ]                      [ 175° • Zoom 17.2x ]  |
+-----------------------------------------------------------------------------------+
```
* **Dual-Casing Route Line:** Rute biru bergaris tepi navy tua (`#003875`), terbaca kontras tinggi di bawah terik matahari.
* **Turn Milestone Marker:** Badge melayang tepat di titik belokan.
* **Current Active Street Pill:** Label jalan aktif di bagian bawah tengah layar agar pengendara tahu lokasinya sekilas dalam waktu < 0.2 detik (*glance time safe*).

---

## 5. Mobile Native Pipeline: Migrasi dari Localhost ke Android APK

```
+-----------------------------------------------------------------------------------+
|                     PIPELINE PRODUKSI: ANDROID NATIVE ENGINE                      |
+-----------------------------------------------------------------------------------+
| [Flutter Project (spidey_companion)]                                              |
|      │                                                                            |
|      ├─ Menggunakan plugin 'geolocator' (Akses Satelit GPS Hardware HP Asli)      |
|      ├─ Menggunakan 'flutter_map' + Deadband Limiter (Hemat Kuota Mapbox 90%)     |
|      ├─ Menggunakan HTTP Client Keep-Alive / TCP Socket Streamer                 |
|      │                                                                            |
|      ▼ Kompilasi Native (Build Release)                                           |
| flutter build apk --release --split-per-abi                                       |
|      │                                                                            |
|      ▼ Output File Mandiri                                                        |
| app-arm64-v8a-release.apk (Ukuran ~18-22 MB)                                     |
|      │                                                                            |
|      ▼ Instalasi ke Ponsel Pengendara                                             |
| Tinggal pasang via File Manager / WhatsApp / ZArchiver di Android                 |
+-----------------------------------------------------------------------------------+
```

### Keuntungan Mutlak Dibanding Localhost Laptop:
1. **Zero Setup di Jalan:** Tidak perlu bawa laptop, tidak perlu jalankan terminal, tidak perlu buka Chrome.
2. **True Satellite GPS:** Aplikasi native langsung mengunci sensor GNSS ponsel (GPS, GLONASS, Galileo) dengan akurasi hingga $\pm 2$ meter, bukan IP browser yang meleset ratusan meter.
3. **Background Persistence:** Aplikasi tetap dapat mengirim data rute meskipun layar ponsel dikunci atau disimpan di kantong jaket/holder stang.

---

## 6. Protokol Komunikasi & Hotspot Otomatis

1. **Jaringan:** Pengendara mengaktifkan Personal Hotspot di ponsel:
   * **SSID:** `Spidey-Nav`
   * **Password:** `spidey1234`
2. **Auto-Handshake:** Begitu ESP32 dinyalakan, modul Wi-Fi ESP32-C3 langsung mendeteksi hotspot HP dan terhubung dalam waktu < 3 detik.
3. **mDNS Auto-Discovery:** Ponsel mengenali ESP32 secara otomatis di jaringan lokal via nama hostname `spidey-hud.local` (tanpa repot mengetik IP manual setiap ganti koneksi).
4. **Data Payload (Kompak & Cepat):**
   * *Status Packet (JSON pendek tiap 1 detik saat idle):* Jam, status koneksi, cuaca/baterai.
   * *Map Frame (JPEG Quality 68, ukuran ~7-9 KB tiap interval update):* Langsung di-render ke framebuffer ILI9341.

---

## 7. Rencana Aksi Eksekusi Besok (Execution Checklist)

- [ ] **Modul 1 (Android APK Build):** Generate release APK dari Flutter, install dan verifikasi GPS satelit di HP Android.
- [ ] **Modul 2 (ESP32 Firmware C++):** Menulis kode modular Arduino/PlatformIO (DisplayDriver, JpegRenderer, NetworkService, ButtonHandler).
- [ ] **Modul 3 (Wiring & Hardware Colok):** Menghubungkan pin SPI ILI9341, modul UPS 5V, baterai 550mAh, dan 4 tombol fisik ke GPIO ESP32-C3.
- [ ] **Modul 4 (Testing Akhir di Motor):** Pengujian simulasi lampu merah (tes cutover baterai) dan uji navigasi rute jalan raya.
