#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <NimBLEDevice.h>
#include <WebServer.h>

// --- WiFi Config ---
const char* ssid = "YOUR_SSID";
const char* password = "YOUR_PASSWORD";

// --- Server Config ---
WebServer server(80);

// --- REST API Config ---
const char* serverURL = "http://YOUR_PC_IP:3000/api/data";

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

// ===== Send message to a connected phone via BLE =====
void send_to_phone(String phoneId, String message) {
    for (auto const& pair : clientMap) {
        if (pair.second == phoneId) {
            uint16_t connId = pair.first;
            pCharacteristic->setValue(message.c_str());
            pCharacteristic->notify(connId);
            #if SERIAL_DEBUG
            Serial.printf("Sent to %s (conn=%d): %s\n", phoneId.c_str(), connId, message.c_str());
            #endif
            break;
        }
    }
}

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
    http.setTimeout(10000); // 10 sec timeout

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
        Serial.printf("HTTP ERROR: %d\n", code);
    }
    #endif

    http.end();
    return success;
}

// ===== BLE Server Callbacks =====
class MyServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        uint16_t connId = desc->conn_handle;
        clientMap[connId] = "unknown";
        pServer->startAdvertising(); // รองรับหลาย connection
        #if SERIAL_DEBUG
        Serial.printf("Connected: conn_id=%d\n", connId);
        #endif
    }

    void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
        uint16_t connId = desc->conn_handle;
        clientMap.erase(connId);
        pServer->startAdvertising();
        #if SERIAL_DEBUG
        Serial.printf("Disconnected: conn_id=%d\n", connId);
        #endif
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

        // SET_ID:user123
        if (msg.startsWith("SET_ID:")) {
            String deviceId = msg.substring(7);
            deviceId.trim();
            clientMap[connId] = deviceId;
            String response = "OK:ID_SET";
            pCharacteristic->setValue(response.c_str());
            pCharacteristic->notify(connId);
            return;
        }

        // deviceId:data หรือแค่ data
        String deviceId = clientMap[connId];
        String data = msg;
        int sep = msg.indexOf(':');
        if (sep > 0) {
            deviceId = msg.substring(0, sep);
            data = msg.substring(sep + 1);
        }

        // ส่งไป REST API
        bool success = sendToWeb(deviceId.c_str(), data.c_str());

        // ส่งผลลัพธ์กลับมือถือ
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

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi OK");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed - BLE only mode");
    }

    // ===== WebServer Setup =====
    server.on("/", []() {
        server.send(200, "text/plain", "ESP32 BLE Bridge is running");
    });

    server.on("^/phone/.*", HTTP_POST, []() {
        String path = server.uri();         // /phone/user123
        String phoneId = path.substring(7); // ตัด "/phone/"
        if (!server.hasArg("plain")) {
            server.send(400, "application/json", "{\"error\":\"body required\"}");
            return;
        }
        String body = server.arg("plain");

        // ดึงค่า message แบบง่าย ๆ
        int idx = body.indexOf("\"message\"");
        if (idx == -1) {
            server.send(400, "application/json", "{\"error\":\"message required\"}");
            return;
        }
        int start = body.indexOf("\"", idx + 9);
        if (start == -1) {
            server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
            return;
        }
        start += 1;
        int end = body.indexOf("\"", start);
        if (end == -1) {
            server.send(400, "application/json", "{\"error\":\"invalid JSON\"}");
            return;
        }
        String message = body.substring(start, end);

        // ส่งไปมือถือ
        send_to_phone(phoneId, message);

        server.send(200, "application/json",
                    "{\"status\":\"ok\",\"to\":\"" + phoneId + "\",\"message\":\"" + message + "\"}");
    });

    server.begin();

    // ===== BLE Setup =====
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
    server.handleClient(); // ตรวจสอบ WebServer request

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
    delay(10);
}
