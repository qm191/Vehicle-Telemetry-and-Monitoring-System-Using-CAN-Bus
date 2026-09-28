#pragma once
#include <Arduino.h>
#include <HardwareSerial.h>
#include <TinyGPS++.h>
#include <time.h>
#include "Event.h"

// ========== Cấu hình chân UART cho GPS NEO-M8N ==========
#define GPS_TX_PIN 17  // Chân TX của ESP32-S3 (Nối vào chân RX của module GPS)
#define GPS_RX_PIN 18  // Chân RX của ESP32-S3 (Nối vào chân TX của module GPS)

// Khởi tạo đối tượng giải mã chuỗi NMEA từ vệ tinh
TinyGPSPlus gps;

// Kế thừa luồng Hardware Serial 1 (Đã chạy thành công ở code test độc lập)
HardwareSerial GPS_Serial(1);

// Hàm phụ trợ: Chuyển đổi Ngày/Giờ vệ tinh UTC thành Unix Timestamp (Số giây tính từ năm 1970)
unsigned long long convertToUnixTimestamp(TinyGPSDate &d, TinyGPSTime &t) {
    if (!d.isValid() || !t.isValid()) return 0;
    
    struct tm timeinfo;
    timeinfo.tm_year = d.year() - 1900; // Năm kể từ 1900
    timeinfo.tm_mon = d.month() - 1;    // Tháng từ 0 - 11 (Cần trừ 1)
    timeinfo.tm_mday = d.day();
    timeinfo.tm_hour = t.hour();
    timeinfo.tm_min = t.minute();
    timeinfo.tm_sec = t.second();
    
    return (unsigned long long)mktime(&timeinfo);
}

void GPS_Task(void *parameter) {
    SerialPrintlnSafe("[GPS] Task Started using Pin 17 (TX) & 18 (RX) on UART1");

    // Khởi động cổng Serial1 phần cứng kết nối với GPS với tốc độ Baud chuẩn 9600
    GPS_Serial.begin(9600, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

    GPSData_t gpsData = {}; // Khởi tạo rỗng cấu trúc dữ liệu GPS
    unsigned long lastCheckTime = millis();

    for (;;) {
        // 1. Đọc liên tục từng ký tự (Byte) thô gửi về từ mạch GPS qua cổng Serial
        while (GPS_Serial.available() > 0) {
            char c = GPS_Serial.read();
            
            // Đẩy ký tự vào bộ lọc giải mã của TinyGPS++
            if (gps.encode(c)) {
                
                // Kiểm tra xem bộ lọc đã xác định được vị trí (Fix thành công vệ tinh) chưa
                if (gps.location.isValid()) {
                    gpsData.currentLat   = gps.location.lat();
                    gpsData.currentLon   = gps.location.lng();
                    gpsData.currentSpeed = gps.speed.kmph();
                    gpsData.currentSat   = gps.satellites.value();
                    gpsData.isGpsFixed   = true;
                    
                    // Chuyển đổi thời gian vệ tinh sang dạng Số Nguyên Lớn cho Firebase_Task
                    gpsData.ts = convertToUnixTimestamp(gps.date, gps.time);
                } else {
                    // KẾ THỪA TỐI ƯU: Nếu mất tín hiệu (xe vào hầm), báo false 
                    // nhưng KHÔNG gán tọa độ về 0.0 để giữ lại vị trí cuối cùng tránh lỗi nhảy map trên Firebase
                    gpsData.isGpsFixed   = false;
                    gpsData.currentSat   = gps.satellites.value(); 
                }

                // Ghi đè dữ liệu mới nhất vào Mailbox hệ thống cho Firebase_Task lấy đi
                if (xGPSMailbox != NULL) {
                    xQueueOverwrite(xGPSMailbox, &gpsData);
                }
            }
        }

        // 2. LỌC LOGIC THÔNG MINH: Kiểm tra trạng thái kết nối phần cứng định kỳ mỗi 5 giây
        if (millis() - lastCheckTime > 5000) {
            lastCheckTime = millis();
            
            // Nếu số ký tự thô nhận được từ lúc khởi động mạch đến giờ < 10 ký tự
            if (gps.charsProcessed() < 10) {
                // TRƯỜNG HỢP 1: Đứt dây phần cứng hoặc sai chân TX/RX
                SerialPrintlnSafe("[GPS - CẢNH BÁO] LỖI PHẦN CỨNG: Không có tín hiệu thô từ module. Hãy kiểm tra lại dây nối hoặc vị trí chân cắm!");
            } 
            else {
                // TRƯỜNG HỢP 2: Dây nối hoàn hảo, chip ESP32 đang đọc được dữ liệu thô của GPS
                if (!gps.location.isValid()) {
                    // Đang ở trong nhà, chưa bám được vệ tinh để lấy tọa độ 3D Fix
                    SerialPrintlnSafe("[GPS - TRẠNG THÁI] Kết nối dây OK! Đang dò tìm vệ tinh (Đợi đèn LED module chớp)... Số vệ tinh thấy: " + String(gps.satellites.value()));
                } else {
                    // Đã định vị thành công, hoạt động hoàn hảo
                    SerialPrintlnSafe("[GPS - TRẠNG THÁI] Hoạt động TỐT! Đã định vị thành công với " + String(gps.satellites.value()) + " vệ tinh.");
                }
            }
        }

        // Tần suất quét luồng dữ liệu đệm Serial cực nhanh (50ms) để tránh tràn bộ đệm UART
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}