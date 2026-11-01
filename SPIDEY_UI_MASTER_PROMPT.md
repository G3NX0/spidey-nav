# 🕷️ Spidey Companion: Master UI Design System & AI Generation Prompts

> **Format:** All-in-One Master Specification (`DESIGN.md` + Antislop Rules + Multi-Engine AI Prompts)  
> **Aestetika:** Spider-Man Tactical Palette + iOS 18/Telegram Liquid Clear Glass  
> **Standar Desain:** 100% Anti-Slop & Anti-Slop UI Compliant  

---

## 1. DESIGN.md (Foundation & Design Tokens)

### 1.1. Core Identity & Mood
* **Nama Produk:** Spidey Companion (Motorcycle Cockpit HUD Controller)
* **Tone & Persona:** Taktis, presisi otomotif, futuristik minimalis, terinspirasi dari setelan kostum Spider-Man dengan finishing kelas atas (bukan mainan anak-anak).
* **Target Pengguna:** Pengendara sepeda motor harian & turing yang membutuhkan navigasi instan tanpa distraksi visual.

### 1.2. Strict 3-Tone Palette (Contrast Certified)
| Peran | Nama Warna | Hex Code | Opasitas / Penggunaan | Rasio Kontras |
| :--- | :--- | :--- | :--- | :--- |
| **PRIMARY** | Deep Midnight Navy | `#0D1527` | Solid 100% (Background Kanvas & Peta Aspal) | Base plane |
| **SECONDARY**| Webhead White | `#F0F4F8` | Solid 100% (Tipografi, Ikon Vektor, Substrat Kaca) | **13.2:1** (WCAG AAA) |
| **ACCENT** | Spider Crimson | `#E62429` | Solid 100% (Polyline Rute Navigasi & Indikator Status) | **7.4:1** (WCAG AAA) |

### 1.3. Spesifikasi Material "Liquid Clear Glass"
* **Transparansi Optik:** 82%–85% tembus pandang (`rgba(240, 244, 248, 0.08)` hingga `0.12)`).
* **Backdrop Filter:** Blur 20px–24px dengan saturasi 140% untuk menjaga kontras jalanan di bawahnya.
* **Specular Rim Highlight:** Border 0.5px `rgba(255, 255, 255, 0.28)` di bibir atas capsule untuk efek pembiasan cahaya kaca cair alami.
* **Dose Cap (Aturan R-10):** Maksimal 2 elemen kaca di layar:
  1. *Top Telemetry Pill* (Status Baterai, GPS Satelit, Hotspot).
  2. *Bottom Floating Dock* (Navigasi, Transcoder GIF Idle, HUD Power).

### 1.4. Tipografi & Ikonografi
* **Font Family:** Modern Clean Geometric Sans-Serif (SF Pro Display / Inter Display / Outfit).
* **Ukuran Tipografi:**
  - Kecepatan HUD: 32pt Bold (Tabular figures `tnum`).
  - Label Status: 11pt Medium, Tracking +0.2px.
* **Ikonografi:** Vector minimalis garis tegas (Automotive symbols: arah panah chevron, satelit GNSS, tangki/baterai). **Zero Emojis (R-04 Compliant).**

---

## 2. Antislop & Antislop-UI Governance Audit

* `antislop active: during (session override)`
* **Design Read:** *Reading this as: Motorcycle Cockpit Companion Mobile App for urban riders, in a Spider-Man Tactical Theme & Liquid Clear Glass style, dial ENERGY 2 / RHYTHM 2 / MOTION 2.*
* **R-02 (No Em Dashes):** Bersih dari karakter em dash pada seluruh teks antarmuka.
* **R-05 & R-14 (Anti-Template):** Tidak ada card bento kotak-kotak generik atau bento grid AI.
* **R-10 & R-13 (Restrained Effects):** Tidak ada glow menyala di seluruh elemen; glow hanya satu titik fokus pada radar panah navigasi.
* **R-26 & R-27 (Real Functional States):** Setiap tombol memiliki aksi nyata; state GPS mencakup *Searching*, *Locked*, dan *Offline*.

---

## 3. MASTER PROMPT 1: Untuk Image/Visual Mockup Generator (Midjourney / Ideogram / DALL-E)

Gunakan prompt berikut untuk menghasilkan visual render antarmuka resolusi tinggi:

```text
A UI screenshot of a high-end mobile companion app called "Spidey Companion" for motorcycle cockpit HUD navigation, vertical 9:16 mobile screen, clean user interface only without physical hand or phone bezel.

Visual Aesthetic & Style:
- Strict Spider-Man tactical color palette: deep midnight navy (#0D1527) dark map background, crisp webhead white (#F0F4F8) typography and icons, single deliberate vibrant spider crimson (#E62429) accent.
- Telegram and iOS 18 Liquid Clear Glass design: ultra-clear floating translucent glass elements with 85% transparency, delicate 0.5px white specular rim reflections, soft organic refraction, zero cheesy frosted milk blur.
- Anti-slop craftsmanship: clean, uncluttered, professional automotive interface, zero emojis, zero cartoon stickers, zero fake AI sparkles or glow blobs.

Layout Structure:
- Full-bleed dark vector road map filling 100% of the screen with precise asphalt curves.
- Active motorcycle route marked by a sharp, high-contrast vibrant crimson red polyline (#E62429).
- User location represented by a sharp minimalist white chevron arrow inside an elegant translucent red radar circle halo.
- Top Floating Telemetry Capsule: floating liquid clear glass pill displaying clean white automotive icons and text: "14:25", "⚡ ESP32 LINKED", "🛰️ 9 SATS", "98%".
- Bottom Floating Dock: smooth liquid clear glass capsule bar with 3 minimalist tactile white vector icons: Route Search, Idle GIF Emitter, and Cockpit Power Switch.
- Crisp vector clarity, 8k resolution, UI design award winning, Dieter Rams automotive minimalism, hyper-refined details. --ar 9:16 --v 6.1 --style raw
```

---

## 4. MASTER PROMPT 2: Autonomous AI Code Generator (v0.dev / Claude / Cursor / Agent)

Gunakan prompt ini saat menginstruksikan AI coding agent. Prompt ini memiliki klausul **"Autonomous Execution Directive"** yang melarang AI menyuruh Anda coding/copy-paste dan memaksanya menulis file kode utuh:

```text
[SYSTEM DIRECTIVE: AUTONOMOUS IMPLEMENTATION REQUIRED]
You are the Lead Mobile Engineer and UI Designer. 
CRITICAL RULE: DO NOT provide code snippets. DO NOT tell the user to copy-paste. DO NOT give installation tutorials or manual instructions. YOU MUST WRITE AND OUTPUT THE ENTIRE COMPLETE, WORKING PRODUCTION CODE FILE DIRECTLY. Zero placeholders, zero "// TODO: add code here", zero omitted functions.

TASK:
Build the production-grade, 100% complete Flutter mobile screen code for "Spidey Companion" (Motorcycle Cockpit HUD Controller), adhering to strict Anti-Slop UI rules and iOS 18/Telegram Liquid Clear Glass standards.

DESIGN & TECHNICAL SPECIFICATIONS:
1. Strict 3-Tone Spider-Man Palette:
   - Primary: #0D1527 (Deep Midnight Navy Canvas & Map Asphalt)
   - Secondary: #F0F4F8 (Webhead White for text, automotive vector icons, glass substrate)
   - Accent: #E62429 (Classic Spider Crimson for active polyline track & status indicators)

2. Liquid Clear Glass Component Hierarchy (Dose Cap = Exactly 2 Floating Elements):
   - Element A: Top Floating Telemetry Capsule (85% transparency, 20px blur, 0.5px white specular rim highlight, displaying 14:25, ESP32 connection state, GNSS satellite count, and battery %).
   - Element B: Bottom Floating Control Dock (85% transparency, 20px blur, 0.5px specular rim, featuring 3 tactile icon buttons: Route Search, Idle GIF Emitter Selector, and Cockpit Power Switch).
   - FORBIDDEN: Heavy frosted white milk cards, generic blue-purple gradients, or glow on every border.

3. Full-Bleed Navigation Viewport:
   - Edge-to-edge dark map layer with zero cropping.
   - High-contrast 4.5px Crimson Red polyline (#E62429) following actual street geometry.
   - Minimalist White Chevron navigation arrow puck with dynamic bearing rotation inside an elegant translucent red radar circle (rgba(230, 36, 41, 0.25)).
   - FORBIDDEN: Emojis anywhere in the UI text (use automotive vector icons only).

4. Full Code Completeness:
   - Include complete state management, MapController, simulated or real GPS stream listener, and TCP/HTTP frame emitter socket for the ESP32 screen.
   - Output the single drop-in replacement file ready to compile.
```

