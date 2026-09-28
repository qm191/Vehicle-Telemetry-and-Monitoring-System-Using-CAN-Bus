#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <mcp2515.h>
#include "Event.h"

// ========== Cấu hình cụm chân SPI Độc Lập cho module CAN BUS ==========
#define CAN_MISO_PIN 37
#define CAN_MOSI_PIN 35
#define CAN_SCK_PIN  36
#define CAN_CS_PIN   39
#define CAN_INT_PIN  47 

SPIClass SPI_CAN(HSPI);
MCP2515 receiverMcp(CAN_CS_PIN, 10000000, &SPI_CAN);

void CAN_Task(void *parameter) {
    SerialPrintlnSafe("[CAN] Task Started - Waiting for Simulation Data");

    // 1. Cấu hình chân CS và Kích hoạt bus SPI độc lập
    pinMode(CAN_CS_PIN, OUTPUT);
    digitalWrite(CAN_CS_PIN, HIGH);
    SPI_CAN.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);

    // 2. Khởi tạo module MCP2515
    receiverMcp.reset();
    receiverMcp.setBitrate(CAN_500KBPS, MCP_8MHZ); // *Đảm bảo tham số này TRÙNG KHỚP với mạch TX*
    receiverMcp.setNormalMode();

    CANData_t localCANData = {};
    struct can_frame frame;

    for (;;) {
        // Kiểm tra lỗi mạng CAN nội bộ (Tránh treo CPU khi mất dây)
        if (receiverMcp.checkError() != MCP2515::ERROR_OK) {
            vTaskDelay(pdMS_TO_TICKS(500)); 
            receiverMcp.reset();
            receiverMcp.setBitrate(CAN_500KBPS, MCP_8MHZ);
            receiverMcp.setNormalMode();
            continue; 
        }

        bool hasNewData = false;
        int messageCount = 0; // BIẾN ĐẾM BẢO VỆ CHỐNG TREO CPU

        // 3. Đọc bộ đệm (Chỉ đọc tối đa 10 khung mỗi vòng lặp tránh bị chiếm dụng Core)
        while ((receiverMcp.readMessage(&frame) == MCP2515::ERROR_OK) && (messageCount < 10)) {
            hasNewData = true;
            messageCount++;

            // GIẢI MÃ DỮ LIỆU TỪ NODE SIMULATION
            switch (frame.can_id) {
                case 0x100: { // TỐC ĐỘ (Speed)
                    uint16_t speedRaw = (frame.data[0] << 8) | frame.data[1];
                    localCANData.vehicleSpeed = (float)speedRaw;
                    break;
                }
                case 0x101: { // VÒNG TUA MÁY (RPM)
                    uint16_t rpmRaw = (frame.data[0] << 8) | frame.data[1];
                    localCANData.engineRPM = (float)rpmRaw;
                    break;
                }
                case 0x102: { // ĐIỆN ÁP / NĂNG LƯỢNG (Voltage INA226)
                    uint16_t volRaw = (frame.data[0] << 8) | frame.data[1];
                    // Chia lại cho 100.0 để ra số Float chuẩn (VD: 1234 -> 12.34V)
                    localCANData.batteryVoltage = (float)volRaw / 100.0; 
                    break;
                }
            }
        }

        // 4. Nếu có dữ liệu mới, đóng gói và gửi vào Mailbox cho Main UI vẽ lên màn hình
        if (hasNewData && xCANMailbox != NULL) {
            xQueueOverwrite(xCANMailbox, &localCANData);
        }

        // Delay ngắn gọn để nhường luồng (Tick rate = 20ms)
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}