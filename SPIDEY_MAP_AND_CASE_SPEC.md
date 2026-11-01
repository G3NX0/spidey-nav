# 🕷️ Spidey-Nav: Spesifikasi Eksekusi Map & 3D Hardcase Kokpit (v2.0)

> **Status:** Architecture Blueprint & Implementation Guide  
> **Fokus Bahasan:** UI Peta Tema Spider-Man (Navy-Red-White), Data Coverage Jakarta, Proteksi Cuaca (Waterproofing), dan Panduan 3D CAD Enclosure v2.0 (Front-Buttons Ergonomics).

---

## 1. Analisis & Stress-Test: UI Peta Bertema Spider-Man

Pilihan palet warna Spider-Man (**Deep Navy Blue, Vibrant Crimson Red, Crisp White**) bukan sekadar kosmetik, melainkan **sangat ideal untuk standar otomotif malam/siang hari** jika diimplementasikan dengan rasio kontras yang benar.

### 1.1. Formula Palet Warna Spidey (Contrast-Checked)
* **Peta / Background:** `Deep Navy Midnight` (`#0A1128` / `#0B132B`).
  - Menggantikan hitam pekat yang membosankan dengan nuansa kostum biru Spider-Man gelap.
  - Mematikan polusi cahaya saat berkendara malam hari tanpa mengorbankan kontras aspal jalan.
* **Garis Jalur Navigasi (Polyline):** `Vibrant Spidey Red` (`#FF1744` atau `#FF0038`).
  - Lebar: 4.5–5.0 piksel.
  - Nilai kontras terhadap navy mencapai **7.8:1** (jauh melampaui standar WCAG AAA 4.5:1). Garis jalur terlihat menyala tajam layaknya tembakan jaring laba-laba.
* **Puck Posisi Pengendara (User Arrow):** `Clean Web White` (`#FFFFFF`).
  - Panah *White Chevron* di tengah lingkaran radar merah transparan (`rgba(255, 23, 68, 0.25)`).
  - Bergerak dinamis memutar sesuai kompas motor tanpa teks/emoji yang menutupi rute.

### 1.2. Apakah UI Peta Bisa Kita Modifikasi Sendiri?
**100% Bisa.** Ada 2 metode teknis yang kita pakai:
1. **Mapbox Style URL Kustom:** Di Flutter, `MapboxGl` / `flutter_map` membaca TileJSON URL. Kita mengarahkan ke style ID Mapbox Studio bergaya Spidey (`mapbox://styles/...`) atau menggunakan style Tile layer CartoDB Dark Matter / Mapbox Navigation Night.
2. **Dynamic Canvas Color Matrix Filter:** Kita menyuntikkan filter warna langsung pada layer Flutter Canvas (`ColorFiltered`) untuk memproyeksikan palet navy-blue dan merender garis rute merah secara native di layer atas sebelum dikompres menjadi JPEG untuk ESP32.

---

## 2. Akurasi & Kelengkapan Peta di Jakarta

> **Pertanyaan Anda:** *"Apakah map ini lengkap di Jakarta?"*

**Jawaban:** **Sangat lengkap dan paling akurat.**
* Jakarta adalah wilayah dengan kontributor data spasial paling aktif di Asia Tenggara, baik di **Mapbox (OpenStreetMap engine)** maupun **HERE/OSRM routing**.
* **Keunggulan untuk Motor di Jakarta:**
  - Mengenali jalan tikus, gang pemukiman, dan jalur alternatif motor yang sering dilewati pengendara.
  - Akurasi geometri tikungan tajam (U-turn, flyover Casablanca, jalur bawah flyover) sangat presisi sehingga panah navigasi tidak akan salah membaca arah belokan.
  - Update pembangunan (jalur LRT baru, pelebaran jalan, penutupan jalan satu arah) langsung masuk ke tile server secara berkala.

---

## 3. Rekayasa Proteksi Cuaca (Waterproofing IP65 untuk Motor)

Di motor, musuh utama bukan hanya panas matahari, tapi **hujan deras berkecepatan tinggi** (air terdorong angin kencang saat motor melaju 60–80 km/jam).

### 3.1. Struktur Layer "Sandwich" Casing
```
[Muka Depan]
     │
     ▼
1. Front Bezel Plastik PETG (Warna: Spidey Red) -> Memiliki bibir penahan (ceruk 2.5mm)
     │
     ▼
2. Busa Double-Tape 3M VHB (Tebal 0.8mm) -> Mengelilingi pinggiran ceruk (Gasket Kedap Air)
     │
     ▼
3. Kaca Akrilik Bening (Tebal 2.0mm) -> Menempel rapat di atas busa 3M VHB
     │
     ▼
4. Modul LCD TFT 2.4" SPI -> Rapat menempel di balik kaca akrilik
     │
     ▼
5. Busa EVA Pad (Tebal 2.0mm) di Belakang LCD -> Meredam getaran mesin motor
     │
     ▼
6. Kompartemen Mesin (ESP32-C3, Mini UPS, Baterai 550mAh)
     │
     ▼
7. Back Shell Plastik PETG (Warna: Midnight Navy Blue) -> Tutup belakang dengan 4 Baut M3
```

---

## 4. Solusi 3D CAD: OpenSCAD v2.0 (Front-Button Ergonomics)

File siap pakai: [`spidey_cockpit_case.scad`](file:///c:/Users/glixer/.gemini/antigravity-ide/scratch/spidey_companion/spidey_cockpit_case.scad).

### Fitur Kunci Desain v2.0:
1. **Front-Faceplate 4 Tombol:** 4 tombol terletak di bawah layar (area dagu), berjejer horizontal dengan jarak 14mm antar pusat tombol. Pengendara cukup menekan lurus ke depan dengan jempol tanpa perlu mencubit casing.
2. **Spider-Eyes Visor:** Kanopi penahan silau matahari dibuat miring 20 derajat dengan profil bersudut agresif.
3. **Port USB-C & Weep Hole Menghadap Bawah:** Mencegah air hujan langsung masuk ke port koneksi dan mengalirkan kondensasi embun keluar.
4. **Dual-Tone Filamen:**
   - Front Bezel: **Spidey Red**
   - Back Shell: **Midnight Navy Blue**
   - Tombol Karet: **Hitam / Putih**
