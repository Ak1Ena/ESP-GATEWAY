## โปรเจค BLE_BEACON (ESP32 BLE ↔ WiFi Bridge)

โปรเจคนี้คือเฟิร์มแวร์สำหรับ ESP32 (เช่น M5Stack) ที่ทำหน้าที่เป็นสะพานเชื่อมต่อข้อมูลระหว่างอุปกรณ์ BLE (เช่น มือถือ) กับ Web API ผ่าน WiFi

### คุณสมบัติหลัก
- รองรับการเชื่อมต่อ BLE หลายเครื่องพร้อมกัน (Multi-client)
- รับ-ส่งข้อมูลระหว่างมือถือ (BLE) กับ REST API (HTTP POST)
- มี WebServer สำหรับรับคำสั่งจากฝั่ง Web เพื่อส่งข้อความไปยังมือถือผ่าน BLE
- Debug log ผ่าน Serial Monitor

### สถาปัตยกรรม

```
มือถือ (BLE) ←→ ESP32 ←→ WiFi/REST API
			   ↑
		   WebServer (HTTP)
```

### วิธีใช้งาน
1. แก้ไขไฟล์ `src/main.cpp` ใส่ WiFi SSID, PASSWORD และ URL ของ REST API
2. อัปโหลดโค้ดลงบอร์ด ESP32 (เช่น M5Stack)
3. มือถือเชื่อมต่อ BLE กับ ESP32 และส่งข้อมูล (สามารถตั้ง ID ด้วย `SET_ID:<user_id>`)
4. ข้อมูลที่ส่งมาจะถูก POST ไปยัง REST API ตามที่กำหนด
5. สามารถสั่งให้ ESP32 ส่งข้อความไปยังมือถือแต่ละเครื่องผ่าน HTTP POST `/phone/<user_id>`

### ตัวอย่างการใช้งาน
- มือถือเชื่อม BLE แล้วส่งข้อความ: `SET_ID:user123` (ตั้งชื่อ)
- ส่งข้อมูล: `user123:temperature=25`
- Web API จะได้รับ JSON `{"deviceId":"user123", "data":"temperature=25"}`
- ฝั่ง Web สามารถส่ง POST ไปที่ `http://<esp32_ip>/phone/user123` พร้อม body `{"message":"Hello"}` เพื่อส่งข้อความไปมือถือ

### การตั้งค่า PlatformIO
ไฟล์ `platformio.ini` ตัวอย่าง:
```ini
[env:m5stack-core-esp32]
platform = espressif32
board = m5stack-core-esp32
framework = arduino
lib_deps = h2zero/NimBLE-Arduino @ ^1.4.1
monitor_speed = 115200
```
