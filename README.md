# 🚀 ESP32-S3 USB to BLE Keyboard / Barcode Scanner Bridge

Proyek ini mengubah perangkat USB kabel biasa (seperti Keyboard atau Barcode Scanner) menjadi perangkat **Bluetooth Low Energy (BLE)** nirkabel. Proyek ini menggunakan **ESP32-S3** yang memiliki fitur Native USB Host.

Fitur Unggulan:
*   **🔌 Plug & Play:** Sambungkan Keyboard/Scanner USB ke ESP32-S3, dan ia akan memancarkan sinyal Bluetooth.
*   **📱 Multi-Device KVM (3 Slot):** Bisa tersambung ke 3 perangkat (HP/PC) yang berbeda. Pindah perangkat cukup dengan menekan kombinasi tombol di keyboard (`Scroll Lock` + `1`, `2`, atau `3`).
*   **⚡ Hyperspeed Scanner Support:** Dilengkapi *delay handler* khusus untuk Barcode Scanner mode *hyperspeed*, sehingga hasil scan resi beruntun tidak akan saling tumpuk atau hilang.
*   **🔋 Hemat Daya:** Kecepatan CPU dioptimalkan untuk penggunaan dengan Powerbank.

---

## 🛠️ Persyaratan Perangkat Keras (Hardware)
1.  **Modul ESP32-S3** (Wajib versi S3 karena butuh fitur USB OTG/Host).
2.  **Kabel USB OTG** atau konektor USB Type-A Female.
3.  Barcode Scanner atau Keyboard USB.
4.  Powerbank atau sumber daya 5V.

---

## ⚙️ Persyaratan Perangkat Lunak (Wajib Diikuti!)
Karena perbedaan arsitektur memori pada Bluetooth ESP32, **Anda WAJIB menggunakan versi persis seperti di bawah ini** agar tidak terjadi *Crash / Bootloop*:

1.  **Arduino IDE** (Disarankan versi 2.x).
2.  **ESP32 Core Version `2.0.17`**:
    *   Buka Arduino IDE > Boards Manager > Cari `esp32` by Espressif.
    *   Pilih versi **2.0.17** lalu Install (Jangan gunakan versi 3.x karena struktur memori NVS-nya berbeda).
3.  **Library NimBLE-Arduino Version `1.4.1`**:
    *   Buka Library Manager > Cari `NimBLE-Arduino` by h2zero.
    *   Pilih versi **1.4.1** lalu Install.
4.  **Library ESP32_USB_Host_HID**:
    *   Download dari [github.com/esp32beans/ESP32_USB_Host_HID](https://github.com/esp32beans/ESP32_USB_Host_HID).
5.  **Library ESP32_BLE_Combo (Versi Modifikasi)**:
    *   Gunakan file `ESP32_BLE_HID_Combo.zip` yang ada di *repository* ini. Library ini sudah dimodifikasi untuk menyelesaikan masalah *Double Inclusion*, *Null Pointer Crash*, dan kompatibilitas murni dengan NimBLE.

---

## 🚀 Cara Instalasi & Flash ke ESP32-S3

1.  **Install semua software dan library** sesuai dengan versi yang disebutkan di atas. Khusus untuk file `ESP32_BLE_HID_Combo.zip`, install via menu `Sketch` -> `Include Library` -> `Add .ZIP Library...` di Arduino IDE.
2.  Buka file `USB_BLE_Bridge/USB_BLE_Bridge.ino`.
3.  **Pengaturan Menu Tools (Sangat Penting!):**
    *   **Board:** `ESP32S3 Dev Module`
    *   **USB Mode:** `Hardware CDC and JTAG` *(Wajib agar pin D+/D- bisa dipakai USB Host)*
    *   **PSRAM:** `OPI PSRAM` (Atau sesuaikan dengan spesifikasi board Anda)
    *   **Partition Scheme:** `Huge APP (3MB No OTA/1MB SPIFFS)` *(Wajib karena library Bluetooth sangat besar)*
    *   **CPU Frequency:** `240MHz` *(Disarankan `160MHz` untuk menghemat baterai Powerbank)*
4.  Sambungkan ESP32-S3 ke PC, klik **Upload**.
5.  Jika berhasil, Serial Monitor (Baudrate 115200) akan menampilkan `[BLE] Advertising as 'USB-BLE Dev 1'`.
6.  Buka Bluetooth di HP/PC Anda, cari perangkat, dan *pairing*!

---

## 🧠 Penjelasan Cara Kerja Kode

Struktur kode ini dibuat termodular menjadi satu file `.ino` yang kuat:

*   **`USBManager`**: Menangani komunikasi tingkat rendah dengan USB perangkat keras (Native USB Host). Membaca setiap ketikan tombol dari keyboard/scanner kabel.
*   **`BLEManager`**: Mengelola sinyal Bluetooth menggunakan **NimBLE** (versi ringan dari Bluedroid bawaan ESP32 yang menghemat RAM dan Flash).
*   **`NVSUtils`**: Ini adalah kunci dari fitur *Multi-Device*. Ia mengelabui stack NimBLE dengan cara mem- *backup* dan menimpa ruang memori NVS (`nimble_bond`) saat kita berpindah slot. Ini memungkinkan ESP32-S3 mengingat 3 HP/PC yang berbeda tanpa perlu *pairing* ulang layaknya keyboard Logitech premium.
*   **Delay Hyperspeed (`delay(12)`)**: Terletak di fungsi `onKeyboardReport`. Barcode scanner menembakkan puluhan karakter dalam milidetik. Bluetooth BLE memiliki batasan *bandwidth*. Delay 12ms ini bertindak sebagai rem agar antrean karakter Bluetooth tidak tabrakan/hilang (Loss Data).

## 🎮 Cara Berpindah Perangkat (Device Switching)
Secara *default*, ESP32-S3 menyala di **Slot 1**. 
Untuk berpindah ke perangkat 2 atau 3, gunakan keyboard yang tersambung ke USB ESP32, lalu tekan bersamaan:
*   `Scroll Lock` + `1` (Pindah ke Slot 1)
*   `Scroll Lock` + `2` (Pindah ke Slot 2)
*   `Scroll Lock` + `3` (Pindah ke Slot 3)

Saat berpindah, memori *pairing* akan otomatis disimpan, ESP32 akan *restart* dalam hitungan milidetik, mengganti nama Bluetooth, dan langsung terhubung ke perangkat baru.

---
*Dibuat untuk memudahkan operasional logistik, kasir, dan produktivitas.*
