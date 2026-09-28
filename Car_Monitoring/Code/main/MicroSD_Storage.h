#pragma once
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include "Event.h"

bool isSD_OK = false;

void MicroSD_Task(void *parameter) {
    SerialPrintlnSafe("[SD] Task Started");

    // 1. Khởi tạo SD Card (Sử dụng trực tiếp SD_CS_PIN từ main.cpp)
    if (xSemaphoreTake(xSPIMutex, portMAX_DELAY) == pdTRUE) {
        
        // Không cần SPI.begin() ở đây nữa vì main.cpp đã làm rồi
        if (!SD.begin(SD_CS_PIN)) {
            SerialPrintlnSafe("[SD] LỖI: Không nhận dạng được thẻ Micro SD!");
            isSD_OK = false;
        } else {
            SerialPrintlnSafe("[SD] Khởi tạo thẻ nhớ THÀNH CÔNG!");
            isSD_OK = true;

            if (!SD.exists("/datalog.csv")) {
                File file = SD.open("/datalog.csv", FILE_WRITE);
                if (file) {
                    file.println("Timestamp,Latitude,Longitude,GPS_Speed,Satellites,GPS_Fixed,Temperature,Humidity,Pressure,CAN_Speed,RPM,batteryVoltage");
                    file.close();
                }
            }
        }
        xSemaphoreGive(xSPIMutex); // Trả khóa SPI
    }

    GPSData_t currentGPS;
    BMEData_t currentBME;
    CANData_t currentCAN;

    // 2. Vòng lặp chính
    for (;;) {
        if (isSD_OK) {
            if (xGPSMailbox != NULL) xQueuePeek(xGPSMailbox, &currentGPS, 0);
            if (xBMEMailbox != NULL) xQueuePeek(xBMEMailbox, &currentBME, 0);
            if (xCANMailbox != NULL) xQueuePeek(xCANMailbox, &currentCAN, 0);

            // Mượn khóa SPI để ghi thẻ
            if (xSemaphoreTake(xSPIMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                File file = SD.open("/datalog.csv", FILE_APPEND);
                if (file) {
                    // 1. Timestamp (Thời gian)
        file.print(currentGPS.ts);              file.print(",");
        
        // 2. Latitude (Vĩ độ - lấy 6 chữ số thập phân)
        file.print(currentGPS.currentLat, 6);   file.print(",");
        
        // 3. Longitude (Kinh độ - lấy 6 chữ số thập phân)
        file.print(currentGPS.currentLon, 6);   file.print(",");
        
        
        
        // 5. Satellites (Số lượng vệ tinh)
        file.print(currentGPS.currentSat);      file.print(","); // *Lưu ý: Nếu struct đặt tên là .sats thì sửa thành currentGPS.sats
        
       
        
        // 7. Temperature (Nhiệt độ BME - lấy 1 chữ số thập phân)
        file.print(currentBME.temperature, 1);  file.print(",");
        
        // 8. Humidity (Độ ẩm BME - lấy 1 chữ số thập phân)
        file.print(currentBME.humidity, 1);     file.print(",");
        
        // 9. Pressure (Áp suất BME - lấy số nguyên tròn)
        file.print(currentBME.pressure, 0);     file.print(",");
        
        // 10. CAN_Speed (Tốc độ từ CAN Bus - lấy số nguyên tròn)
        file.print(currentCAN.vehicleSpeed, 0); file.print(",");
        
        // 11. RPM (Vòng tua máy từ CAN Bus - lấy số nguyên tròn)
        file.print(currentCAN.engineRPM, 0);    file.print(",");
        
        // 12. batteryVoltage (Điện áp pin từ CAN Bus - cột cuối cùng không thêm dấu phẩy)
        file.print(currentCAN.batteryVoltage, 1);
                    file.println();
                    file.close(); 
                }
                xSemaphoreGive(xSPIMutex); // Ghi xong phải trả khóa ngay
            }
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}