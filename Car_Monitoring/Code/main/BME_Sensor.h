#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include "Event.h"

// ========== Cấu hình chân I2C cho BME280 ==========
#define BME_SDA_PIN 2
#define BME_SCL_PIN 1

// Khởi tạo đối tượng BME280
Adafruit_BME280 bme;

// Trạng thái sống của cảm biến
bool isBME_OK = false;

void BME_Task(void *parameter) {
    SerialPrintlnSafe("[BME] Task Started");

    // 1. Khởi tạo Bus I2C cục bộ dành riêng cho BME280
    Wire.begin(BME_SDA_PIN, BME_SCL_PIN);

    // 2. Kích hoạt cảm biến BME280
    // Lưu ý: Địa chỉ I2C phổ biến của BME280 là 0x76. Nếu module của bạn xài 0x77 thì đổi lại nhé.
    if (!bme.begin(0x76, &Wire)) {
        SerialPrintlnSafe("[BME] LỖI: Không tìm thấy BME280, vui lòng kiểm tra dây nối!");
        isBME_OK = false;
    } else {
        SerialPrintlnSafe("[BME] Khởi tạo thành công!");
        isBME_OK = true;
    }

    // 3. Vòng lặp chính của Task
    for (;;) {
        if (isBME_OK) {
            // Đọc dữ liệu môi trường từ cảm biến
            float t = bme.readTemperature();
            float h = bme.readHumidity();
            float p = bme.readPressure() / 100.0F; // Chia 100 để đổi từ Pa sang hPa

            // Kiểm tra xem dữ liệu có bị lỗi NaN (Not a Number) do đứt dây đột ngột không
            if (isnan(t) || isnan(h) || isnan(p)) {
                SerialPrintlnSafe("[BME] Cảnh báo: Lỗi đọc dữ liệu đột ngột!");
            } else {
                // Đóng gói dữ liệu vào Struct
                BMEData_t bmeData;
                bmeData.temperature = t;
                bmeData.humidity = h;
                bmeData.pressure = p;

                // Ghi đè vào Mailbox để các Task khác (Màn hình, SD, Firebase) lấy xài
                if (xBMEMailbox != NULL) {
                    xQueueOverwrite(xBMEMailbox, &bmeData);
                }

                // In log ra Serial để bạn theo dõi (Có thể comment lại nếu thấy rác màn hình)
                // SerialPrintlnSafe("[BME] Nhiệt độ: " + String(t, 1) + "C | Ẩm: " + String(h, 1) + "% | Áp suất: " + String(p, 1) + "hPa");
            }
        }

        // Nhiệt độ và độ ẩm môi trường thay đổi rất chậm, nên delay 2 giây (2000ms) là mức lý tưởng,
        // vừa tiết kiệm CPU vừa tránh bắt cảm biến làm việc quá sức gây nóng sai số.
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}