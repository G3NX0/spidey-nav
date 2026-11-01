// =========================================================================
// SPIDEY-NAV COCKPIT ENCLOSURE v2.0 (SPIDER-MAN EDITION)
// Ergonomi Khusus Motor: 4 Tombol Front-Faceplate, Akrilik 2mm, Visor Spidey
// =========================================================================

$fn = 60;

// Dimensi Komponen Hardware (mm)
lcd_w = 78.0;      // Lebar PCB Layar TFT 2.4"
lcd_h = 43.0;      // Tinggi PCB Layar TFT 2.4"
view_w = 49.0;     // Lebar Jendela Tampilan Peta Aktif
view_h = 37.0;     // Tinggi Jendela Tampilan Peta Aktif

wall_th = 2.5;     // Ketebalan Dinding Plastik PETG
btn_area_h = 20.0; // Area Dagu (Chin) Bawah untuk 4 Tombol Fisik
case_depth = 26.0; // Ruang Kompartemen Baterai 550mAh + UPS 5V 1A + ESP32-C3
total_h = lcd_h + btn_area_h; // Tinggi Total Casing Depan

// -------------------------------------------------------------------------
// 1. FRONT BEZEL (Warna Rekomendasi Filament: SPIDEY RED)
// -------------------------------------------------------------------------
module front_bezel_v2() {
    difference() {
        union() {
            // Body Casing Depan
            cube([lcd_w + (wall_th * 2), total_h + (wall_th * 2), 6], center = true);
            
            // Sun Visor Bentuk Bersudut Khas Spidey (Anti-Silau Siang Hari)
            translate([0, (total_h/2) + wall_th - 1, 6])
                rotate([20, 0, 0])
                cube([lcd_w + (wall_th * 2), 3, 14], center = true);
        }
        
        // Jendela Tampilan Peta LCD (Posisi di bagian atas)
        translate([0, (btn_area_h/2), 0])
            cube([view_w, view_h, 20], center = true);
            
        // Ceruk / Dudukan Kaca Akrilik 2mm + Busa 3M VHB (Kedalaman 2.5mm)
        translate([0, (btn_area_h/2), 1.5])
            cube([view_w + 5, view_h + 5, 3], center = true);

        // 4 Lubang Tombol Fisik di Dagu Depan (Btn 1, 2, 3, 4)
        // Tombol 12x12mm Waterproof Tactile Switch (Diameter lubang 7mm untuk cap karet)
        btn_y = -(total_h/2) + (btn_area_h/2);
        for (i = [-3, -1, 1, 3]) {
            translate([i * 14, btn_y, 0])
                cylinder(r = 3.8, h = 20, center = true);
        }
            
        // 4 Lubang Baut Sudut M3 untuk mengunci ke Back Shell
        for (x = [-(lcd_w/2), (lcd_w/2)]) {
            for (y = [-(total_h/2), (total_h/2)]) {
                translate([x, y, 0])
                    cylinder(r = 1.6, h = 30, center = true);
            }
        }
    }
}

// -------------------------------------------------------------------------
// 2. BACK SHELL (Warna Rekomendasi Filament: MIDNIGHT NAVY BLUE)
// -------------------------------------------------------------------------
module back_shell_v2() {
    difference() {
        // Kotak Kompartemen Belakang
        cube([lcd_w + (wall_th * 2), total_h + (wall_th * 2), case_depth], center = true);
        
        // Ruang Komponen Internal (Rongga Mesin)
        translate([0, 0, wall_th])
            cube([lcd_w, total_h, case_depth], center = true);
            
        // 4 Lubang Baut M3
        for (x = [-(lcd_w/2), (lcd_w/2)]) {
            for (y = [-(total_h/2), (total_h/2)]) {
                translate([x, y, 0])
                    cylinder(r = 1.5, h = case_depth + 10, center = true);
            }
        }
        
        // Dudukan Baut M5 / Ballhead Stang Motor di Belakang Casing
        translate([0, 0, -(case_depth/2)])
            cylinder(r = 2.6, h = 10, center = true);

        // Weep Hole Anti-Embun (Diameter 1.2mm menghadap ke bawah)
        translate([0, -(total_h/2) - wall_th, -(case_depth/2) + 4])
            rotate([90, 0, 0]) cylinder(r = 0.8, h = 10, center = true);
            
        // Lubang Port Kabel USB-C (Sisi Bawah Menghadap Bawah)
        translate([20, -(total_h/2) - wall_th, -(case_depth/2) + 6])
            rotate([90, 0, 0]) cube([9, 4, 10], center = true);
    }
}

// Preview Tampilan Berdampingan
translate([0, 0, 20]) front_bezel_v2();
translate([0, 0, -20]) back_shell_v2();
