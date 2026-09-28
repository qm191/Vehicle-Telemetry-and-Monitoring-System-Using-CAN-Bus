#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "Event.h"

// Thông tin mạng WiFi
const char* ssid = "wifi206ne";
const char* password = "206206206";

unsigned long lastWiFiCheck = 0;

void Wifi_Task(void *parameter) {
    // Tắt tính năng tự động lưu cấu hình để bảo vệ tuổi thọ bộ nhớ Flash
    WiFi.setAutoReconnect(false);
    WiFi.persistent(false);  
    
    SerialPrintlnSafe("[WiFi] Task Started");
    
    for (;;) {
        unsigned long now = millis();
        
        // Chu kỳ kiểm tra và xử lý kết nối mỗi 5 giây
        if (now - lastWiFiCheck > 5000) {
            lastWiFiCheck = now;
            
            // Nếu mất kết nối hoặc chưa kết nối
            if (WiFi.status() != WL_CONNECTED) {
                SerialPrintlnSafe("[WiFi] Connecting...");
                
                // Hạ cờ sự kiện để báo cho các Task khác (như Firebase) dừng gửi dữ liệu
                xEventGroupClearBits(systemEventGroup, WIFI_CONNECTED_BIT);
                
                WiFi.mode(WIFI_STA);
                vTaskDelay(pdMS_TO_TICKS(100));
                
                // Bắt đầu quá trình kết nối
                WiFi.begin(ssid, password);
                
                int retry = 0;
                // Chờ kết nối tối đa 10 giây (20 vòng * 500ms)
                while (WiFi.status() != WL_CONNECTED && retry < 20) {
                    vTaskDelay(pdMS_TO_TICKS(500));
                    retry++;
                }
                
                // Đánh giá kết quả kết nối
                if (WiFi.status() == WL_CONNECTED) {
                    SerialPrintlnSafe("[WiFi] Connected!");
                    SerialPrintlnSafe("[WiFi] IP: " + WiFi.localIP().toString());
                    SerialPrintlnSafe("[WiFi] Signal: " + String(WiFi.RSSI()) + " dBm");
                    
                    // Kéo cờ sự kiện lên để đánh thức các Task cần mạng
                    xEventGroupSetBits(systemEventGroup, WIFI_CONNECTED_BIT);
                } else {
                    SerialPrintlnSafe("[WiFi] Connection failed. Retrying in 5s...");
                    WiFi.disconnect(true, false); // Dọn dẹp để thử lại ở chu kỳ sau
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            } else {
                // Đảm bảo cờ luôn được giữ nếu mạng vẫn đang hoạt động ổn định
                xEventGroupSetBits(systemEventGroup, WIFI_CONNECTED_BIT);
                
                // Log nhịp đập mỗi 30 giây (giúp bạn theo dõi mạng không bị treo)
                if (now % 30000 < 5000) {  
                    // Có thể bỏ comment dòng dưới nếu muốn xem log liên tục
                    // SerialPrintlnSafe("[WiFi] Still connected - RSSI: " + String(WiFi.RSSI()));
                }
            }
        }
        
        // Ngủ 1 giây cuối mỗi vòng lặp để nhường CPU cho CAN_Task và Screen_Task
        vTaskDelay(pdMS_TO_TICKS(1000)); 
    }
}