#include <Arduino.h>
#include "Event.h"

// Khởi tạo các Handle toàn cục cho FreeRTOS
EventGroupHandle_t systemEventGroup = NULL;

QueueHandle_t xGPSMailbox = NULL;
QueueHandle_t xBMEMailbox = NULL;
QueueHandle_t xCANMailbox = NULL;

SemaphoreHandle_t xSerialMutex = NULL;
SemaphoreHandle_t xSPIMutex = NULL;

// ========== Cấu hình SPI chung cho hệ thống ==========
#define SPI_SCK_PIN  12
#define SPI_MISO_PIN 13
#define SPI_MOSI_PIN 11

// Cấu hình các chân Chip Select (CS) của các thiết bị ngoại vi
#define CAN_CS_PIN   39
#define SD_CS_PIN    10
#define TFT_CS_PIN   8

// Khai báo các Task thành phần của Project
#include "BME_Sensor.h"
#include "MicroSD_Storage.h"
#include "GPS_Module.h"
#include "Wifi_Connection.h"
#include "Firebase_Cloud.h"
#include "ScreenTFT.h"
#include "CAN_Control.h"
#include "Debug_Terminal.h"

#include <Wire.h>
#include <SPI.h>

// =================================================================
// HỆ THỐNG IN TERMINAL AN TOÀN TRONG ĐA NHIỆM (MUTEX PROTECTED)
// =================================================================
void SerialPrintlnSafe(String message) {
    if (xSerialMutex != NULL) {
        if (xSemaphoreTake(xSerialMutex, (TickType_t)10) == pdTRUE) {
            Serial.println(message);
            xSemaphoreGive(xSerialMutex);
        }
    }
}

void SerialPrintlnSafe(const char* message) { SerialPrintlnSafe(String(message)); }
void SerialPrintlnSafe(int value)          { SerialPrintlnSafe(String(value)); }
void SerialPrintlnSafe(float value)        { SerialPrintlnSafe(String(value, 2)); }
void SerialPrintlnSafe(double value)       { SerialPrintlnSafe(String(value, 2)); }
void SerialPrintlnSafe(long value)         { SerialPrintlnSafe(String(value)); }

void setup()
{
    // 1. Khởi tạo cổng giao tiếp Serial với Máy tính
    Serial.begin(115200);
    delay(500); 

    // 2. Khởi tạo các tài nguyên FreeRTOS (Event Group, Mailbox, Mutex)
    systemEventGroup = xEventGroupCreate();
    xGPSMailbox = xQueueCreate(1, sizeof(GPSData_t));
    xBMEMailbox = xQueueCreate(1, sizeof(BMEData_t));
    xCANMailbox = xQueueCreate(1, sizeof(CANData_t));

    GPSData_t emptyGPS = {};
    if (xGPSMailbox != NULL) { xQueueOverwrite(xGPSMailbox, &emptyGPS); }
    
    BMEData_t emptyBME = {};
    if (xBMEMailbox != NULL) { xQueueOverwrite(xBMEMailbox, &emptyBME); }
    
    CANData_t emptyCAN = {};
    if (xCANMailbox != NULL) { xQueueOverwrite(xCANMailbox, &emptyCAN); }

    xSerialMutex = xSemaphoreCreateMutex();
    xSPIMutex = xSemaphoreCreateMutex(); 

    SerialPrintlnSafe("--- SYSTEM INITIALIZING ---");

    // 3. VÔ HIỆU HÓA TẤT CẢ CÁC CHIP TRÊN BUS SPI (Tránh xung đột phần cứng ban đầu)
    pinMode(SD_CS_PIN, OUTPUT);
    pinMode(TFT_CS_PIN, OUTPUT);
    digitalWrite(SD_CS_PIN, HIGH);
    digitalWrite(TFT_CS_PIN, HIGH);

    // 4. KHỞI TẠO ĐƯỜNG SPI CHUNG VÀ PHẦN CỨNG MÀN HÌNH TFT (ĐỒNG BỘ TUẦN TỰ)
    if(xSemaphoreTake(xSPIMutex, portMAX_DELAY) == pdTRUE) {
        // Khởi chạy đường cao tốc SPI toàn hệ thống
        SPI.begin(SPI_SCK_PIN, SPI_MISO_PIN, SPI_MOSI_PIN);
        
        
        tft.begin();
        
        // BÍ MẬT NẰM Ở ĐÂY: Khởi tạo bộ nhớ DMA để chống sập StoreProhibited
        //tft.initDMA(); 
        
        tft.setRotation(1); // Xoay màn hình ngang
        drawStaticUI();     // Vẽ khung đồ họa Lopaka tĩnh
        
        xSemaphoreGive(xSPIMutex);
    }
    SerialPrintlnSafe("[SPI] Shared SPI Highway & TFT Hardware Initialized.");

    // =================================================================
    // PHÂN CHIA NHIỆM VỤ LÊN CÁC LÕI (CORE ALLOCATION) - TỐI ƯU HIỆU NĂNG
    // =================================================================

    // ----------------- LÕI 1 (CORE 1): THỜI GIAN THỰC & ĐỒ HỌA -----------------
    xTaskCreatePinnedToCore(CAN_Task, "CAN_Task", 8192, NULL, 3, NULL, 1);
    xTaskCreatePinnedToCore(GPS_Task, "GPS_Task", 8192, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(ScreenTFT_Task, "ScreenTFT_Task", 16384, NULL, 2, NULL, 1);
    xTaskCreatePinnedToCore(BME_Task, "BME_Task", 8192, NULL, 1, NULL, 1);
    xTaskCreatePinnedToCore(DebugTerminal_Task, "DebugTerminal_Task", 4096, NULL, 1, NULL, 1);

    // ----------------- LÕI 0 (CORE 0): MẠNG TRUYỀN THÔNG & LƯU TRỮ -----------------
    xTaskCreatePinnedToCore(Wifi_Task, "Wifi_Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(Firebase_Task, "Firebase_Task", 8192, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(MicroSD_Task, "MicroSD_Task", 8192, NULL, 1, NULL, 0);

    SerialPrintlnSafe("[LOG] All multi-threading tasks created successfully.");
}

void loop() {
    // Để trống hoàn toàn. FreeRTOS tự quản lý.
}