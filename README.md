# 🕷️ Spidey-Nav: Retro Motorcycle IoT Navigation Cockpit & Companion

Spidey-Nav adalah sistem navigasi head-up display (HUD) sepeda motor berbasis **ESP32-C3 Super Mini** dengan display **TFT 2.4" SPI (ILI9341)** yang terhubung secara wireless (Wi-Fi TCP Socket) ke aplikasi mobile pendamping (**Flutter Android Companion**).

---

## 📱 Arsitektur Sistem

```text
┌─────────────────────────────────┐           Wi-Fi TCP Socket          ┌──────────────────────────────────┐
│   Spidey Companion (Android)    │  ─────────────────────────────────> │  ESP32-C3 Super Mini + 2.4" TFT  │
│  - Mapbox Night Navigation Tile │          (192.168.4.1:8080)         │  - ILI9341 320x240 SPI           │
│  - OSRM Turn-by-Turn Engine     │                                     │  - Partial Framebuffer (160x120) │
│  - Liquid Clear Glass UI HUD    │                                     │  - 3 Tactile Navigation Buttons  │
└─────────────────────────────────┘                                     └──────────────────────────────────┘
```

---

## 🚀 Fitur Utama

1. **Mapbox Navigation Night Style:**
   - Tile map resolusi tinggi dengan skema warna gelap malam kontras tinggi (`navigation-night-v1`).
   - Fallback otomatis ke OpenStreetMap jika koneksi seluler lambat.
2. **Liquid Clear Glass HUD:**
   - Antarmuka futuristik bertema Spider-Man Tactical (Midnight Navy `#0D1527`, Webhead White `#F0F4F8`, Spider Crimson `#E62429`).
   - Blur dinamis 22px dengan saturasi 140% dan specular rim highlights.
3. **In-App Connection Manager:**
   - Dialog pengaturan IP dan Port ESP32 langsung dari layar HP tanpa perlu restart aplikasi.
   - Indikator status koneksi real-time (Online, Connecting, Standalone Offline).
   - Mode navigasi mandiri (*standalone*): Peta dan rute tetap berfungsi 100% di HP meskipun modul hardware ESP32 belum dinyalakan.
4. **Hardware Cockpit Case 3D Printable:**
   - Desain OpenSCAD parametrik untuk stang motor (clamp 22.2 mm / 28.6 mm) dengan sunshade hood 12 mm dan saluran drainase air hujan.

---

## 📂 Struktur Repositori

```text
spidey-nav/
├── lib/                     # Flutter Android Source Code
│   └── main.dart            # Mapbox integration, OSRM routing, Liquid Glass HUD
├── android/                 # Android Native Manifest & Build Configuration
├── firmware_esp32/          # PlatformIO ESP32-C3 Firmware
│   ├── platformio.ini       # Build flags & TFT_eSPI driver definitions
│   ├── include/config.h     # Pinout, SPI speed, buffer sizes
│   └── src/main.cpp         # TCP Stream Server & Display Rendering Engine
├── spidey_cockpit_case.scad # 3D Printable Enclosure (OpenSCAD)
├── SPIDEY_NAV_MASTER_SPEC.md# Spesifikasi lengkap hardware & protokol
└── pubspec.yaml             # Flutter Dependencies
```

---

## 🛠️ Cara Menjalankan

### 1. Mobile Companion (Android)
```bash
# Debug mode
puro flutter run

# Build Release APK
puro flutter build apk --release
```
File APK siap instalasi tersimpan di `build/app/outputs/flutter-apk/app-release.apk`.

### 2. ESP32-C3 Firmware
```bash
cd firmware_esp32
pio run --target upload
```

---

## ⚖️ Lisensi
MIT License. Dibuat untuk para pengendara motor perkotaan yang menginginkan navigasi minimalis, futuristik, dan tahan cuaca.
