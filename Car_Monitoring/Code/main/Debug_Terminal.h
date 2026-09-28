#pragma once
#include <Arduino.h>
#include "Event.h"

void DebugTerminal_Task(void *parameter) {
    SerialPrintlnSafe("[DEBUG] Terminal Task Started");

    // Khởi tạo các struct cục bộ để chứa bản sao dữ liệu
    GPSData_t gps;
    BMEData_t bme;
    CANData_t can;

    for (;;) {
        // Đọc lướt qua (Peek) dữ liệu mới nhất từ 3 Mailbox trạm trung chuyển
        if (xGPSMailbox != NULL) xQueuePeek(xGPSMailbox, &gps, 0);
        if (xBMEMailbox != NULL) xQueuePeek(xBMEMailbox, &bme, 0);
        if (xCANMailbox != NULL) xQueuePeek(xCANMailbox, &can, 0);

        // Gom tất cả thông tin vào một chuỗi duy nhất để in ra một lần (Tránh nghẽn Serial)
        String output = "\n============= SYSTEM MONITOR (REALTIME) =============\n";
        
        // 1. Hiển thị thông số GPS
        output += "[GPS]  - Trạng thái: " + String(gps.isGpsFixed ? "FIXED (OK)" : "NO FIX (Searching...)") + "\n";
        output += "       - Tọa độ    : Lat: " + String(gps.currentLat, 6) + " / Lon: " + String(gps.currentLon, 6) + "\n";
        output += "       - Tốc độ    : " + String(gps.currentSpeed, 1) + " km/h | Số vệ tinh: " + String(gps.currentSat) + "\n";
        output += "       - Timestamp : " + String(gps.ts) + "\n";
        output += "-----------------------------------------------------\n";

        // 2. Hiển thị thông số môi trường BME280
        output += "[BME]  - Nhiệt độ  : " + String(bme.temperature, 1) + " °C\n";
        output += "       - Độ ẩm     : " + String(bme.humidity, 1) + " %\n";
        output += "       - Áp suất   : " + String(bme.pressure, 1) + " hPa\n";
        output += "-----------------------------------------------------\n";

        // 3. Hiển thị thông số mạng xe CAN Bus
        output += "[CAN]  - Vận tốc xe : " + String(can.vehicleSpeed, 1) + " km/h\n";
        output += "       - Vòng tua   : " + String(can.engineRPM, 0) + " RPM\n";
        output += "       - Nhiệt nước : " + String(can.batteryVoltage, 1) + " °C\n";
        output += "=====================================================";

        // Đẩy chuỗi dữ liệu ra Serial một cách an toàn qua Mutex
        SerialPrintlnSafe(output);

        // Định kỳ quét và in ra Terminal mỗi 1 giây (1000ms)
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}