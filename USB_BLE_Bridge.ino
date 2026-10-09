#define USE_NIMBLE // Wajib di-define sebelum BleCombo agar menggunakan memori lebih kecil
#include <NimBLEDevice.h> // Memancing NimBLE untuk aktif

#include <Arduino.h>
#include <Preferences.h>
#include <BleCombo.h>
#include <hid_usage_keyboard.h>
#include <esp_mac.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "hid_host.h"
#include "usb/usb_host.h"

// ============================================================================
// 1. CONFIGURATION
// ============================================================================
#define DEVICE_NAME_1 "USB-BLE Dev 1"
#define DEVICE_NAME_2 "USB-BLE Dev 2"
#define DEVICE_NAME_3 "USB-BLE Dev 3"
#define DEVICE_MANUFACTURER "ESP32-S3"
#define BATTERY_LEVEL 100
#define NUM_DEVICE_SLOTS 3
#define ENABLE_DEVICE_SWITCHING true
#define LED_FEEDBACK_PIN 2 // Ganti -1 jika tidak ada LED di pin 2

// ============================================================================
// 2. KELAS & DEKLARASI
// ============================================================================

class BLEManager {
public:
  BLEManager() : _bleCombo(nullptr) {}
  void begin(uint8_t slot, const char *deviceName);
  bool isConnected();
  void sendKeyboardReport(const uint8_t *keys, uint8_t modifiers);
private:
  BleCombo *_bleCombo;
  void setUniqueMac(uint8_t slot);
};

class NVSUtils {
public:
  static void copyNamespace(const char *src_ns, const char *dst_ns);
  static void loadSlotBonds(uint8_t slot);
  static void saveSlotBonds(uint8_t slot);
};

typedef void (*KeyboardReportCallback)(const uint8_t *data, size_t length);

class USBManager {
public:
  static void begin();
  static void setKeyboardCallback(KeyboardReportCallback cb) { _keyboardCb = cb; }
private:
  static KeyboardReportCallback _keyboardCb;
  static void usb_lib_task(void *arg);
  static void hid_host_task(void *pvParameters);
  static void hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
  static void hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg);
  static void hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg);
};

class Bridge {
public:
  static void begin();
  static void loop();
  static void switchToSlot(uint8_t slot);
private:
  static uint8_t _currentSlot;
  static BLEManager _bleManager;
  static Preferences _preferences;
  static void onKeyboardReport(const uint8_t *data, size_t length);
  static bool checkDeviceSwitchCombo(const uint8_t *keys);
};

// ============================================================================
// 3. VARIABEL STATIK GLOBAL
// ============================================================================
uint8_t Bridge::_currentSlot = 0;
BLEManager Bridge::_bleManager;
Preferences Bridge::_preferences;
KeyboardReportCallback USBManager::_keyboardCb = nullptr;
static QueueHandle_t hid_host_event_queue;

typedef struct {
  hid_host_device_handle_t hid_device_handle;
  hid_host_driver_event_t event;
  void *arg;
} hid_host_event_queue_t;

static const char *hid_proto_name_str[] = {"NONE", "KEYBOARD", "MOUSE"};

// ============================================================================
// 4. MAIN ARDUINO SETUP & LOOP
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║  ESP32-S3 USB to BLE Keyboard Bridge           ║");
  Serial.println("║  Supports keyboard + multi-device              ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");

  if (LED_FEEDBACK_PIN >= 0) {
    pinMode(LED_FEEDBACK_PIN, OUTPUT);
    digitalWrite(LED_FEEDBACK_PIN, LOW);
  }

  Bridge::begin();

  Serial.println("\n╔════════════════════════════════════════════════╗");
  Serial.println("║  READY - Connect USB devices via hub           ║");
  Serial.println("╚════════════════════════════════════════════════╝\n");
}

void loop() {
  Bridge::loop();
  delay(10);
}

// ============================================================================
// 5. IMPLEMENTASI FUNGSI
// ============================================================================

// --- BLEManager ---
void BLEManager::begin(uint8_t slot, const char *deviceName) {
  setUniqueMac(slot);
  Serial.printf("[BLE] Initializing slot %d: '%s'\n", slot + 1, deviceName);
  _bleCombo = new BleCombo(deviceName, DEVICE_MANUFACTURER, BATTERY_LEVEL);
  _bleCombo->begin();
  Serial.printf("[BLE] Advertising as '%s'\n", deviceName);
}

bool BLEManager::isConnected() { return (_bleCombo != nullptr && _bleCombo->isConnected()); }

void BLEManager::sendKeyboardReport(const uint8_t *keys, uint8_t modifiers) {
  if (!isConnected()) return;
  KeyReport report;
  report.modifiers = modifiers;
  report.reserved = 0;
  memcpy(report.keys, keys, 6);
  _bleCombo->sendReport(&report);
}

void BLEManager::setUniqueMac(uint8_t slot) {
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  mac[5] = (mac[5] & 0xF0) | (slot & 0x0F);
  
  // Fitur ubah MAC Address dimatikan sementara agar tidak crash di beberapa board ESP32
  // esp_err_t err = esp_base_mac_addr_set(mac);
  // if (err != ESP_OK) {
  //   Serial.printf("[BLE] Failed to set MAC: %s\n", esp_err_to_name(err));
  // } else {
  //   Serial.printf("[BLE] Set MAC: %02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  // }
}

// --- NVSUtils ---
void NVSUtils::copyNamespace(const char *src_ns, const char *dst_ns) {
  nvs_handle_t h_src, h_dst;
  esp_err_t err = nvs_open(src_ns, NVS_READONLY, &h_src);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      if (nvs_open(dst_ns, NVS_READWRITE, &h_dst) == ESP_OK) {
        nvs_erase_all(h_dst);
        nvs_commit(h_dst);
        nvs_close(h_dst);
      }
    }
    return;
  }
  if (nvs_open(dst_ns, NVS_READWRITE, &h_dst) != ESP_OK) {
    nvs_close(h_src);
    return;
  }
  
  nvs_erase_all(h_dst);
  
  // Format iterator NVS khusus untuk ESP32 Core 2.0.x
  nvs_iterator_t it = nvs_entry_find("nvs", src_ns, NVS_TYPE_ANY);
  while (it != NULL) {
    nvs_entry_info_t info;
    nvs_entry_info(it, &info);
    switch (info.type) {
      case NVS_TYPE_U8: { uint8_t v; nvs_get_u8(h_src, info.key, &v); nvs_set_u8(h_dst, info.key, v); break; }
      case NVS_TYPE_I8: { int8_t v; nvs_get_i8(h_src, info.key, &v); nvs_set_i8(h_dst, info.key, v); break; }
      case NVS_TYPE_U16: { uint16_t v; nvs_get_u16(h_src, info.key, &v); nvs_set_u16(h_dst, info.key, v); break; }
      case NVS_TYPE_I16: { int16_t v; nvs_get_i16(h_src, info.key, &v); nvs_set_i16(h_dst, info.key, v); break; }
      case NVS_TYPE_U32: { uint32_t v; nvs_get_u32(h_src, info.key, &v); nvs_set_u32(h_dst, info.key, v); break; }
      case NVS_TYPE_I32: { int32_t v; nvs_get_i32(h_src, info.key, &v); nvs_set_i32(h_dst, info.key, v); break; }
      case NVS_TYPE_U64: { uint64_t v; nvs_get_u64(h_src, info.key, &v); nvs_set_u64(h_dst, info.key, v); break; }
      case NVS_TYPE_I64: { int64_t v; nvs_get_i64(h_src, info.key, &v); nvs_set_i64(h_dst, info.key, v); break; }
      case NVS_TYPE_STR: {
        size_t len;
        if (nvs_get_str(h_src, info.key, NULL, &len) == ESP_OK) {
          char *v = (char *)malloc(len);
          if (v) { nvs_get_str(h_src, info.key, v, &len); nvs_set_str(h_dst, info.key, v); free(v); }
        }
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t len;
        if (nvs_get_blob(h_src, info.key, NULL, &len) == ESP_OK) {
          void *v = malloc(len);
          if (v) { nvs_get_blob(h_src, info.key, v, &len); nvs_set_blob(h_dst, info.key, v, len); free(v); }
        }
        break;
      }
    }
    it = nvs_entry_next(it);
  }
  nvs_release_iterator(it);
  nvs_commit(h_dst);
  nvs_close(h_src);
  nvs_close(h_dst);
}

void NVSUtils::loadSlotBonds(uint8_t slot) {
  char slot_ns[16];
  snprintf(slot_ns, sizeof(slot_ns), "ble_bond_%d", slot);
  Serial.printf("[System] Loading BLE bonds for slot %d from '%s'...\n", slot + 1, slot_ns);
  copyNamespace(slot_ns, "nimble_bond");
}

void NVSUtils::saveSlotBonds(uint8_t slot) {
  char slot_ns[16];
  snprintf(slot_ns, sizeof(slot_ns), "ble_bond_%d", slot);
  Serial.printf("[System] Saving BLE bonds for slot %d to '%s'...\n", slot + 1, slot_ns);
  copyNamespace("nimble_bond", slot_ns);
}

// --- USBManager ---
void USBManager::begin() {
  Serial.println("[USB] Installing USB Host library...");
  BaseType_t task_created = xTaskCreatePinnedToCore(usb_lib_task, "usb_events", 4096, xTaskGetCurrentTaskHandle(), 2, NULL, 0);
  assert(task_created == pdTRUE);
  ulTaskNotifyTake(false, 1000);
  
  Serial.println("[USB] Installing HID driver...");
  const hid_host_driver_config_t hid_host_driver_config = {
      .create_background_task = true, .task_priority = 5, .stack_size = 4096,
      .core_id = 0, .callback = hid_host_device_callback, .callback_arg = NULL};
  ESP_ERROR_CHECK(hid_host_install(&hid_host_driver_config));
  
  task_created = xTaskCreate(&hid_host_task, "hid_task", 4096, NULL, 2, NULL);
  assert(task_created == pdTRUE);
  Serial.println("[USB] HID driver ready");
}
void USBManager::usb_lib_task(void *arg) {
  const usb_host_config_t host_config = { .skip_phy_setup = false, .intr_flags = ESP_INTR_FLAG_LEVEL1 };
  ESP_ERROR_CHECK(usb_host_install(&host_config));
  xTaskNotifyGive((TaskHandle_t)arg);
  while (true) {
    uint32_t event_flags;
    usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
    if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) { usb_host_device_free_all(); }
  }
}
void USBManager::hid_host_task(void *pvParameters) {
  hid_host_event_queue_t evt_queue;
  hid_host_event_queue = xQueueCreate(10, sizeof(hid_host_event_queue_t));
  while (true) {
    if (xQueueReceive(hid_host_event_queue, &evt_queue, pdMS_TO_TICKS(50))) {
      hid_host_device_event(evt_queue.hid_device_handle, evt_queue.event, evt_queue.arg);
    }
  }
}
void USBManager::hid_host_device_callback(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
  const hid_host_event_queue_t evt_queue = { .hid_device_handle = hid_device_handle, .event = event, .arg = arg };
  xQueueSend(hid_host_event_queue, &evt_queue, 0);
}
void USBManager::hid_host_device_event(hid_host_device_handle_t hid_device_handle, const hid_host_driver_event_t event, void *arg) {
  hid_host_dev_params_t dev_params;
  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) return;
  const hid_host_device_config_t dev_config = { .callback = hid_host_interface_callback, .callback_arg = NULL };
  switch (event) {
  case HID_HOST_DRIVER_EVENT_CONNECTED:
    Serial.printf("[USB] %s connected!\n", hid_proto_name_str[dev_params.proto]);
    if (dev_params.proto == HID_PROTOCOL_NONE) break;
    if (hid_host_device_open(hid_device_handle, &dev_config) != ESP_OK) break;
    if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class) {
      hid_class_request_set_protocol(hid_device_handle, HID_REPORT_PROTOCOL_BOOT);
      if (HID_PROTOCOL_KEYBOARD == dev_params.proto) hid_class_request_set_idle(hid_device_handle, 0, 0);
    }
    hid_host_device_start(hid_device_handle);
    break;
  default: break;
  }
}
void USBManager::hid_host_interface_callback(hid_host_device_handle_t hid_device_handle, const hid_host_interface_event_t event, void *arg) {
  uint8_t data[64] = {0}; size_t data_length = 0; hid_host_dev_params_t dev_params;
  if (hid_host_device_get_params(hid_device_handle, &dev_params) != ESP_OK) return;
  switch (event) {
  case HID_HOST_INTERFACE_EVENT_INPUT_REPORT:
    if (hid_host_device_get_raw_input_report_data(hid_device_handle, data, 64, &data_length) == ESP_OK) {
      if (HID_SUBCLASS_BOOT_INTERFACE == dev_params.sub_class && HID_PROTOCOL_KEYBOARD == dev_params.proto) {
        if (_keyboardCb) _keyboardCb(data, data_length);
      }
    }
    break;
  case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
    Serial.printf("[USB] %s disconnected\n", hid_proto_name_str[dev_params.proto]);
    hid_host_device_close(hid_device_handle);
    break;
  default: break;
  }
}

// --- Bridge ---
void Bridge::begin() {
  _preferences.begin("usb-ble", true);
  _currentSlot = _preferences.getUChar("slot", 0);
  if (_currentSlot >= NUM_DEVICE_SLOTS) _currentSlot = 0;
  _preferences.end();
  
  Serial.printf("[Config] Starting on device slot %d\n", _currentSlot + 1);
  NVSUtils::loadSlotBonds(_currentSlot);
  
  const char *deviceNames[NUM_DEVICE_SLOTS] = {DEVICE_NAME_1, DEVICE_NAME_2, DEVICE_NAME_3};
  _bleManager.begin(_currentSlot, deviceNames[_currentSlot]);
  
  USBManager::setKeyboardCallback(onKeyboardReport);
  USBManager::begin();
}
void Bridge::loop() {
  static unsigned long lastStatus = 0;
  if (millis() - lastStatus > 5000) {
    lastStatus = millis();
    bool connected = _bleManager.isConnected();
    Serial.printf("[Status] Slot %d | BLE: %s\n", _currentSlot + 1, connected ? "CONNECTED" : "waiting for pairing...");
  }
}
void Bridge::switchToSlot(uint8_t slot) {
  if (slot >= NUM_DEVICE_SLOTS) return;
  if (slot == _currentSlot) {
    if (LED_FEEDBACK_PIN >= 0) {
      for (int i = 0; i <= slot; i++) { digitalWrite(LED_FEEDBACK_PIN, HIGH); delay(150); digitalWrite(LED_FEEDBACK_PIN, LOW); delay(150); }
    }
    return;
  }
  Serial.printf("[BLE] Switching from slot %d to slot %d\n", _currentSlot + 1, slot + 1);
  NVSUtils::saveSlotBonds(_currentSlot);
  _preferences.begin("usb-ble", false);
  _preferences.putUChar("slot", slot);
  _preferences.end();
  if (LED_FEEDBACK_PIN >= 0) {
    for (int i = 0; i <= slot; i++) { digitalWrite(LED_FEEDBACK_PIN, HIGH); delay(150); digitalWrite(LED_FEEDBACK_PIN, LOW); delay(150); }
  }
  usb_host_device_free_all();
  Serial.println("[System] Restarting to apply new slot settings...");
  delay(500);
  ESP.restart();
}

void Bridge::onKeyboardReport(const uint8_t *data, size_t length) {
  if (length < sizeof(hid_keyboard_input_report_boot_t)) return;
  hid_keyboard_input_report_boot_t *kb_report = (hid_keyboard_input_report_boot_t *)data;
  
  if (checkDeviceSwitchCombo(kb_report->key)) return;
  
  // Teruskan input dari Barcode/Keyboard USB ke Bluetooth
  _bleManager.sendKeyboardReport(kb_report->key, kb_report->modifier.val);

  // DELAY PENTING UNTUK BARCODE SCANNER MODE HYPERSPEED!
  // Mencegah karakter menimpa satu sama lain saat proses transmisi BLE.
  // Anda bisa naikkan ke 15 atau 20 jika masih ada teks resi yang nyangkut.
  delay(12);
}

bool Bridge::checkDeviceSwitchCombo(const uint8_t *keys) {
  if (!ENABLE_DEVICE_SWITCHING) return false;
  bool hasScrollLock = false; uint8_t numberKey = 0;
  for (int i = 0; i < 6; i++) {
    if (keys[i] == HID_KEY_SCROLL_LOCK) hasScrollLock = true;
    if (keys[i] >= HID_KEY_1 && keys[i] <= HID_KEY_3) numberKey = keys[i] - HID_KEY_1 + 1;
  }
  if (hasScrollLock && numberKey > 0 && numberKey <= NUM_DEVICE_SLOTS) {
    Serial.printf("[Switch] Scroll Lock + %d detected\n", numberKey);
    switchToSlot(numberKey - 1);
    return true;
  }
  return false;
}