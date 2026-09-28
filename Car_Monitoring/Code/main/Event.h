#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/event_groups.h"

//------------------ KHAI BÁO EVENT_GROUPS ------------------
#define WIFI_CONNECTED_BIT (1 << 0) // Cờ báo kết nối WiFi thành công

//------------------ KHAI BÁO STRUCT DATA (MAILBOX) ---------------

// 1. Dữ liệu Định vị (Giữ nguyên form cũ rất chuẩn của bạn)
typedef struct {
    double currentLat;    
    double currentLon;    
    float currentSpeed;   
    int currentSat;
    unsigned long long ts;
    bool isGpsFixed;
} GPSData_t;

// 2. Dữ liệu Môi trường (Cảm biến BME)
typedef struct {
    float temperature;    // Nhiệt độ (°C)
    float humidity;       // Độ ẩm (%)
    float pressure;       // Áp suất khí quyển (hPa)
} BMEData_t;

// 3. Dữ liệu Xe (CAN Bus)
typedef struct {
    uint32_t messageID;   // ID của gói tin CAN (VD: 0x7DF, 0x153...)
    uint8_t data[8];      // Mảng chứa 8 byte dữ liệu thô
    uint8_t dataLength;   // Độ dài dữ liệu (DLC)
    
    // Các thông số đã giải mã (Ví dụ OBD2 chuẩn)
    float vehicleSpeed;   // Tốc độ xe (km/h)
    float engineRPM; 
    float batteryVoltage;     // Vòng tua máy (vòng/phút)   // Nhiệt độ nước làm mát (°C)
} CANData_t;

//------------------ HANDLES HỆ THỐNG --------------------------------
extern EventGroupHandle_t systemEventGroup;

// Các trạm trung chuyển dữ liệu (Mailbox)
extern QueueHandle_t xGPSMailbox;
extern QueueHandle_t xBMEMailbox;
extern QueueHandle_t xCANMailbox;

// Khóa bảo vệ tài nguyên phần cứng
extern SemaphoreHandle_t xSPIMutex;    // Đã thay thế xI2CMutex thành xSPIMutex
extern SemaphoreHandle_t xSerialMutex; // Khóa cổng In/Out màn hình Serial

//------------------ HÀM DÙNG CHUNG -------------------------
void SerialPrintlnSafe(String message);      // Cho chuỗi String
void SerialPrintlnSafe(const char* message); // Cho chuỗi ký tự "abc"
void SerialPrintlnSafe(int value);           // Cho số nguyên
void SerialPrintlnSafe(float value);         // Cho số thực
void SerialPrintlnSafe(double value);        // Cho số thực độ chính xác cao
void SerialPrintlnSafe(long value);          // Cho số nguyên lớn