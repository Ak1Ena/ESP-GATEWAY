#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NimBLEDevice.h>

// --- WiFi Config ---
const char* ssid = "";
const char* password = "";

// --- REST API Config ---
const char* serverURL = "http://xxx.xxx.xxx.xxx:3000/api/data";

// --- BLE Config ---
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-5678-90ab-cdef-1234567890ab"

// Enable/Disable debug logging
#define SERIAL_DEBUG 1

// --- Global Variables ---
NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pCharacteristic = nullptr;

// Mapping: conn_handle -> deviceId (user_id)
std::map<uint16_t, String> clientMap;

// ===== HTTP POST Function =====
bool sendToWeb(const char* deviceId, const char* data) {
  if (WiFi.status() != WL_CONNECTED) {
    #if SERIAL_DEBUG
    Serial.println("ERROR: WiFi not connected");
    #endif
    return false;
  }

  HTTPClient http;
  http.begin(serverURL);
  http.addHeader("Content-Type", "application/json");
  http.setTimeout(10000); // เพิ่ม timeout เป็น 10 วินาที

  String payload = "{\"deviceId\":\"";
  payload += deviceId;
  payload += "\",\"data\":\"";
  payload += data;
  payload += "\"}";

  #if SERIAL_DEBUG
  Serial.print("POST to: ");
  Serial.println(serverURL);
  Serial.print("Payload: ");
  Serial.println(payload);
  #endif

  int code = http.POST(payload);
  bool success = (code >= 200 && code < 300);

  #if SERIAL_DEBUG
  if (code > 0) {
    Serial.printf("HTTP Response: %d\n", code);
    String response = http.getString();
    if (response.length() > 0) {
      Serial.print("Response body: ");
      Serial.println(response);
    }
  } else {
    // HTTP Error codes
    Serial.printf("HTTP ERROR: %d - ", code);
    switch(code) {
      case -1:  Serial.println("Connection failed"); break;
      case -2:  Serial.println("Send header failed"); break;
      case -3:  Serial.println("Send payload failed"); break;
      case -4:  Serial.println("Not connected"); break;
      case -5:  Serial.println("Connection lost"); break;
      case -6:  Serial.println("No stream"); break;
      case -7:  Serial.println("No HTTP server"); break;
      case -8:  Serial.println("Too less RAM"); break;
      case -9:  Serial.println("Encoding error"); break;
      case -10: Serial.println("Stream write error"); break;
      case -11: Serial.println("Read timeout"); break;
      default:  Serial.println("Unknown error"); break;
    }
  }
  #endif

  http.end();
  return success;
}

// ===== BLE Server Callbacks =====
class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        uint16_t connId = desc->conn_handle;
        #if SERIAL_DEBUG
        Serial.printf("Connected: conn_id=%d\n", connId);
        #endif
        clientMap[connId] = "unknown";
        pServer->startAdvertising(); // รองรับหลาย connection
    }

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        uint16_t connId = desc->conn_handle;
        #if SERIAL_DEBUG
        Serial.printf("Disconnected: conn_id=%d\n", connId);
        #endif
        clientMap.erase(connId);
        pServer->startAdvertising();
    }
};

// ===== BLE Characteristic Callbacks =====
class MyCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic *pCharacteristic, ble_gap_conn_desc *desc) override {
        std::string value = pCharacteristic->getValue();
        uint16_t connId = desc->conn_handle;

        if (value.empty()) return;

        String msg(value.c_str());
        #if SERIAL_DEBUG
        Serial.printf("Recv from conn_id=%d: %s\n", connId, msg.c_str());
        #endif

        // รูปแบบที่ 1: SET_ID:user123
        if (msg.startsWith("SET_ID:")) {
            String deviceId = msg.substring(7);
            deviceId.trim();
            clientMap[connId] = deviceId;
            #if SERIAL_DEBUG
            Serial.printf("Set ID: conn_id=%d -> deviceId=%s\n", connId, deviceId.c_str());
            #endif
            
            // ส่งกลับว่า SET สำเร็จ
            String response = "OK:ID_SET";
            pCharacteristic->setValue(response.c_str());
            pCharacteristic->notify(connId);
            return;
        }

        // รูปแบบที่ 2: deviceId:data หรือแค่ data
        String deviceId = clientMap[connId]; // ใช้ ID ที่ SET ไว้
        String data = msg;

        // ถ้ามี : แปลว่าส่งมาพร้อม deviceId
        int sep = msg.indexOf(':');
        if (sep > 0) {
            deviceId = msg.substring(0, sep);
            data = msg.substring(sep + 1);
        }

        // ส่งไปที่ REST API
        bool success = sendToWeb(deviceId.c_str(), data.c_str());

        // ส่งผลลัพธ์กลับไปที่มือถือ
        String response = success ? "OK:" : "ERROR:";
        response += data;
        pCharacteristic->setValue(response.c_str());
        pCharacteristic->notify(connId);
    }
};

void setup() {
    Serial.begin(115200);
    #if SERIAL_DEBUG
    Serial.println("\n=== ESP32 NimBLE Multi-Client Bridge ===");
    #endif

    // ===== WiFi Setup =====
    #if SERIAL_DEBUG
    Serial.print("Connecting to WiFi");
    #endif
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        #if SERIAL_DEBUG
        Serial.print(".");
        #endif
        attempts++;
    }

    #if SERIAL_DEBUG
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi OK");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed - BLE only mode");
    }
    #endif

    // ===== BLE Setup =====
    #if SERIAL_DEBUG
    Serial.println("Init BLE...");
    #endif

    NimBLEDevice::init("ESP32_BLE_Server");
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new MyServerCallbacks());

    NimBLEService *pService = pServer->createService(SERVICE_UUID);
    pCharacteristic = pService->createCharacteristic(
        CHARACTERISTIC_UUID,
        NIMBLE_PROPERTY::READ |
        NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::NOTIFY
    );
    pCharacteristic->setCallbacks(new MyCallbacks());

    pService->start();

    NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->addServiceUUID(SERVICE_UUID);
    pAdvertising->setScanResponse(true);
    pAdvertising->setMinPreferred(0x06);
    pAdvertising->setMaxPreferred(0x12);
    pAdvertising->start();

    #if SERIAL_DEBUG
    Serial.println("BLE Ready - Multi-client supported");
    Serial.printf("Max connections: %d\n", CONFIG_BT_NIMBLE_MAX_CONNECTIONS);
    #endif
}

void loop() {
    // ตรวจสอบ WiFi ทุก 30 วินาที
    static unsigned long lastCheck = 0;
    if (millis() - lastCheck > 30000) {
        if (WiFi.status() != WL_CONNECTED) {
            #if SERIAL_DEBUG
            Serial.println("WiFi reconnecting...");
            #endif
            WiFi.reconnect();
        }
        lastCheck = millis();
    }

    delay(100);
}