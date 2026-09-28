#pragma once
#include <Arduino.h>
#include <FirebaseESP32.h>
#include "Event.h"

// Thông tin cấu hình Firebase cấp từ Server
#define FIREBASE_HOST "smart-16ec8-default-rtdb.asia-southeast1.firebasedatabase.app"
#define FIREBASE_AUTH "PYUewbEEBlsEjVOC9r5vwnIL0uc9kogGlKWmwFT3"

// Khai báo các đối tượng cấu hình của thư viện Firebase
FirebaseData fbdo_send;
FirebaseAuth auth;
FirebaseConfig config;

void Firebase_Task(void *parameter) {
    SerialPrintlnSafe("[Firebase] Task Started - Waiting for WiFi...");

    // BLOCKING: Chờ cho đến khi Wifi_Task kết nối thành công và bật cờ WIFI_CONNECTED_BIT
    xEventGroupWaitBits(systemEventGroup, WIFI_CONNECTED_BIT, pdFALSE, pdTRUE, portMAX_DELAY);
    
    SerialPrintlnSafe("[Firebase] WiFi OK! Initializing connection to Host...");
    
    // Nạp cấu hình bảo mật thông tin
    config.host = FIREBASE_HOST;
    config.signer.tokens.legacy_token = FIREBASE_AUTH;
    
    // Kích hoạt kết nối và thiết lập tự động kết nối lại khi mất mạng
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // Tạo các biến cục bộ hứng dữ liệu từ Mailbox
    GPSData_t currentGPS;
    BMEData_t currentBME;
    CANData_t currentCAN;

    // Biến quản lý mốc thời gian đẩy dữ liệu lịch sử
    unsigned long lastHistoryPush = 0;

    for (;;) {
        unsigned long now = millis();
        EventBits_t bits = xEventGroupGetBits(systemEventGroup);
        
        // Điều kiện an toàn: Mạng Wifi phải thông suốt và Core Firebase đã sẵn sàng sàng nhận lệnh
        if (((bits & WIFI_CONNECTED_BIT) != 0) && Firebase.ready()) {
            
            // Đọc lướt (Peek) lấy bản sao dữ liệu mới nhất từ các Mailbox hệ thống
            if (xGPSMailbox != NULL) xQueuePeek(xGPSMailbox, &currentGPS, 0);
            if (xBMEMailbox != NULL) xQueuePeek(xBMEMailbox, &currentBME, 0);
            if (xCANMailbox != NULL) xQueuePeek(xCANMailbox, &currentCAN, 0);

            // =========================================================
            // 1. ĐẨY DỮ LIỆU REAL-TIME (Cập nhật liên tục mỗi 1 giây)
            // =========================================================
            FirebaseJson jsonRealtime;
            
            // Đóng gói thông số định vị vệ tinh GPS
            jsonRealtime.add("Lat", currentGPS.currentLat);
            jsonRealtime.add("Lon", currentGPS.currentLon);
            jsonRealtime.add("Satellites", currentGPS.currentSat);
            jsonRealtime.add("GPS_fixed", currentGPS.isGpsFixed);
            
            // Đóng gói thông số cảm biến môi trường BME280 (Làm tròn 1 chữ số thập phân)
            jsonRealtime.add("Temperature", (float)((int)(currentBME.temperature * 10)) / 10.0);
            jsonRealtime.add("Humidity", (float)((int)(currentBME.humidity * 10)) / 10.0);
            jsonRealtime.add("Pressure", currentBME.pressure);
            
            // Đóng gói thông số mạng xe CAN Bus nhận từ bộ giả lập của bạn
            jsonRealtime.add("Speed", currentCAN.vehicleSpeed);
            jsonRealtime.add("RPM", currentCAN.engineRPM);
            jsonRealtime.add("Energy", currentCAN.batteryVoltage);

            // Ghi đè trực tiếp lên Node Realtime của Firebase
            if (Firebase.updateNode(fbdo_send, "/VehicleData/Realtime", jsonRealtime)) {
                // Bạn có thể mở comment dòng dưới nếu muốn kiểm tra log xác nhận thành công
                // SerialPrintlnSafe("[Firebase] Update Realtime successfully.");
            } else {
                // Nếu lỗi (Sai Rules, mạng nghẽn), in thẳng nguyên nhân ra Terminal để xử lý
                SerialPrintlnSafe("[Firebase] LỖI Cập Nhật Realtime: " + fbdo_send.errorReason());
            }

           
        } else {
            // Cảnh báo động ra Terminal nếu tác vụ Firebase bị ngắt do lỗi mất kết nối WiFi nửa chừng
            if ((bits & WIFI_CONNECTED_BIT) == 0) {
                SerialPrintlnSafe("[Firebase] Warning: Suspended connection. Waiting for WiFi recovery...");
            }
        }

        // Nhường CPU cho các Task khác xử lý dữ liệu phần cứng
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}