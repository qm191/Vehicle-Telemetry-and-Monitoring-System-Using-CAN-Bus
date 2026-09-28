#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <INA226_WE.h>
#include "icon.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <mcp2515.h>

/* ================= CẤU HÌNH CHÂN (ESP32-S3) ================= */
#define TFT_MISO 13
#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   15
#define TFT_RST  14

#define I2C_SDA 8
#define I2C_SCL 9
#define INA226_ADDR 0x50

#define TRIG_PIN   43
#define ECHO_PIN   44
#define SPEED_PIN  41
#define BUZZER_PIN 19  
#define RAIN_PIN   20   

#define POT_PIN    18 

#define L298N_ENB  42 
#define L298N_IN4  1   
#define L298N_IN3  2   

#define BTN_BRAKE_PIN 17  
#define BTN_FWD_PIN   6  
#define BTN_REV_PIN   16  
#define BTN_STOP_PIN  7  

/* ================= THÔNG SỐ XE ================= */
#define WHEEL_DIAMETER 0.66  
#define SLOTS          20     
#define INTERVAL_MS    1000   

// --- [FIX 1] THÊM ĐỊNH NGHĨA LỌC NHIỄU ---
#define MIN_PULSE_INTERVAL_US 500 

/* ================= DISPLAY CONFIG ================= */
#define SPD_X 85
#define SPD_Y 144
#define SPD_R 70
#define RPM_X 247
#define RPM_Y 155
#define RPM_R 55

#define CAN_MISO_PIN 37
#define CAN_MOSI_PIN 35
#define CAN_SCK_PIN  36
#define CAN_CS_PIN   39
#define CAN_INT_PIN  47

SPIClass SPI_CAN(HSPI);
MCP2515 mcp2515(CAN_CS_PIN, 10000000, &SPI_CAN);
/* ================= TOÀN CỤC ================= */
Adafruit_ILI9341 tft(TFT_CS, TFT_DC, TFT_RST);
INA226_WE ina226(INA226_ADDR);

// --- [FIX 2] KHAI BÁO BIẾN MUTEX CHO NGẮT ---
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

struct SpeedData { float rpm; float kmh; };
QueueHandle_t qDistance;
QueueHandle_t qSpeedData;
QueueHandle_t qReverseState;


uint16_t lastSpeed = 0;
uint16_t lastRPM = 0;
int lastBattery = -1; 
bool rain = false;
bool lastRain = true; 
volatile uint32_t pulse_count = 0;
volatile unsigned long last_interrupt_time = 0;

// --- BIẾN LỌC PIN (SMOOTH BATTERY) ---
float avgVoltage = 0.0;
bool isFirstRead = true;

// --- BIẾN ĐỒNG BỘ CHO CAN BUS ---
volatile uint16_t sync_speed = 0;
volatile uint16_t sync_rpm = 0;
volatile float sync_voltage = 0.0;

enum MotorState {
  STATE_STOPPED, 
  STATE_FORWARD, 
  STATE_REVERSE  
};

MotorState currentGear = STATE_STOPPED; 
bool isBraking = false;                 
MotorState lastGearDisplay = STATE_STOPPED;
bool lastBrakeDisplay = false;

/* ================= HÀM NGẮT (ISR) ================= */
void IRAM_ATTR isrSpeedSensor() {
    unsigned long interrupt_time = micros(); // Lấy thời gian hiện tại (us)
    
    // Nếu thời gian giữa xung hiện tại và xung trước lớn hơn ngưỡng cho phép
    // thì mới tính là 1 xung hợp lệ. (Debounce)
    if (interrupt_time - last_interrupt_time > MIN_PULSE_INTERVAL_US) {
        pulse_count++;
        last_interrupt_time = interrupt_time;
    }
}

/* ================= HÀM ĐIỀU KHIỂN ĐỘNG CƠ ================= */
void setMotor(int speed, bool direction, bool stop) {
    if (stop) {
        digitalWrite(L298N_IN3, LOW);
        digitalWrite(L298N_IN4, LOW);
        analogWrite(L298N_ENB, 0);
    } else {
        analogWrite(L298N_ENB, speed); 
        if (direction == true) { // Tiến
            digitalWrite(L298N_IN3, HIGH);
            digitalWrite(L298N_IN4, LOW);
        } else { // Lùi
            digitalWrite(L298N_IN3, LOW);
            digitalWrite(L298N_IN4, HIGH);
        }
    }
}

/* ================= GUI FUNCTIONS ================= */
void drawDynamicBattery(int percent) {
  int x = 277; int y = 8; int w = 36; int h = 24;
  uint16_t color = (percent > 60) ? ILI9341_GREEN : ((percent > 20) ? ILI9341_YELLOW : ILI9341_RED);
  tft.drawRect(x, y, w - 4, h, ILI9341_WHITE); 
  tft.fillRect(x + w - 4, y + 6, 4, 12, ILI9341_WHITE); 
  int barW = map(percent, 0, 100, 0, w - 8);
  tft.fillRect(x + 2, y + 2, w - 8, h - 4, ILI9341_BLACK);
  if (barW > 0) tft.fillRect(x + 2, y + 2, barW, h - 4, color);
}

void drawGearIndicator() {
    int x = 155; 
    int y = 200; 
    int w = 30; 
    int h = 30;

    if (currentGear != lastGearDisplay || isBraking != lastBrakeDisplay) {
        tft.fillRect(x, y, w, h, ILI9341_BLACK); 
        tft.drawRect(x, y, w, h, ILI9341_WHITE); 
        tft.setTextSize(3);
        tft.setCursor(x + 7, y + 5);
        if (isBraking) {
            tft.setTextColor(ILI9341_RED); tft.print("B"); 
        } else {
            switch (currentGear) {
                case STATE_FORWARD:
                    tft.setTextColor(ILI9341_CYAN); tft.print("D"); break;
                case STATE_REVERSE:
                    tft.setTextColor(ILI9341_YELLOW); tft.print("R"); break;
                default:
                    tft.setTextColor(ILI9341_GREEN); tft.print("N"); break;
            }
        }
        lastGearDisplay = currentGear;
        lastBrakeDisplay = isBraking;
    }
}

void updateGUI_Rain() {
  rain = (digitalRead(RAIN_PIN) == LOW);
  if (rain != lastRain) {
    tft.drawBitmap(16, 7, image_weather_cloud_rain_bits, 52, 48, ILI9341_BLACK); 
    tft.drawBitmap(16, 7, image_weather_cloud_rain_bits, 52, 48, rain ? ILI9341_CYAN : 0x73AE); 
    lastRain = rain;
  }
}

void drawGaugeBase() {
  tft.fillScreen(ILI9341_BLACK);
  // SPEED
  tft.drawEllipse(SPD_X, SPD_Y, 75, 75, 0xF206);
  tft.drawEllipse(SPD_X, SPD_Y, 80, 80, 0x3A96);
  tft.setTextColor(ILI9341_RED); tft.setTextSize(2); 
  tft.setCursor(62, 160); tft.print("Km/h");
  const char *spdTxt[] = {"0","20","40","60","80","100","120","140","160","180","200"};
  int spdPos[][2] = {{23,164},{17,141},{20,116},{35,95},{54,82},{77,76},{106,82},{122,98},{134,116},{138,141},{133,166}};
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
  for (int i = 0; i <= 10; i++) {
    tft.setCursor(spdPos[i][0], spdPos[i][1]); tft.print(spdTxt[i]);
  }
  // RPM
  tft.drawEllipse(RPM_X, RPM_Y, 65, 65, 0xF206);
  tft.drawEllipse(RPM_X, RPM_Y, 70, 70, 0x3A96);
  tft.setTextColor(0xFC00); tft.setTextSize(2);
  tft.setCursor(231, 166); tft.print("RPM");
  const char *rpmTxt[] = {"0","1","2","3","4","5","6","7","8","9","10"};
  int rpmPos[][2] = {{191,173},{187,150},{191,131},{202,112},{221,99},{242,94},{266,99},{281,112},{293,131},{297,150},{292,173}};
  tft.setTextColor(ILI9341_WHITE); tft.setTextSize(1);
  for (int i = 0; i <= 10; i++) {
    tft.setCursor(rpmPos[i][0], rpmPos[i][1]); tft.print(rpmTxt[i]);
  }
  // ICONS
  tft.drawBitmap(16, 7, image_weather_cloud_rain_bits, 52, 48, 0x73AE);
  tft.drawBitmap(163, 4, image_operation_warning_bits, 32, 32, 0x73AE);
  // GEAR
  lastGearDisplay = (MotorState)-1; 
  drawGearIndicator();
}

void updateGUI_Speed(uint16_t spd) {
    if (spd != lastSpeed) { 
        float oldRad = map(lastSpeed, 0, 200, -210, 30) * DEG_TO_RAD;
        tft.drawLine(SPD_X, SPD_Y, SPD_X + cos(oldRad)*(SPD_R-10), SPD_Y + sin(oldRad)*(SPD_R-10), ILI9341_BLACK);
        float newRad = map(spd, 0, 200, -210, 30) * DEG_TO_RAD;
        tft.drawLine(SPD_X, SPD_Y, SPD_X + cos(newRad)*(SPD_R-10), SPD_Y + sin(newRad)*(SPD_R-10), ILI9341_WHITE);

        tft.fillRect(53, 182, 64, 20, ILI9341_BLACK); 
        
        tft.setTextColor(ILI9341_WHITE); 
        tft.setTextSize(2);
        tft.setCursor(68, 185); 
        tft.print(spd);

        lastSpeed = spd;
    }
}
void updateGUI_RPM(uint16_t rpm) {
  if (rpm != lastRPM) {
    // 1. Dùng chung dải map (ví dụ 0-8500)
    float oldRad = map(lastRPM, 0, 8500, -210, 30) * DEG_TO_RAD;
    float newRad = map(rpm, 0, 8500, -210, 30) * DEG_TO_RAD;

    // 2. Xóa kim cũ bằng màu nền (Black)
    tft.drawLine(RPM_X, RPM_Y, RPM_X + cos(oldRad)*(RPM_R-10), RPM_Y + sin(oldRad)*(RPM_R-10), ILI9341_BLACK);

    // 3. VẼ LẠI KHUNG HOẶC VẠCH TẠI ĐÂY (Nếu kim vừa xóa đè lên khung)
    // Ví dụ: drawTicks(); 

    // 4. Vẽ kim mới
    tft.drawLine(RPM_X, RPM_Y, RPM_X + cos(newRad)*(RPM_R-10), RPM_Y + sin(newRad)*(RPM_R-10), ILI9341_WHITE);

    // 5. Cập nhật số điện tử (Dùng fillRect để xóa số cũ là đúng)
    tft.fillRect(222, 184, 55, 18, ILI9341_BLACK);
    tft.setCursor(226, 185);
    tft.print(rpm);

    lastRPM = rpm;
  }
}
void updateGUI_Battery() {
  float rawVoltage = ina226.getBusVoltage_V(); 
  if (rawVoltage < 1.0) rawVoltage = 0; 
  if (isFirstRead || abs(rawVoltage - avgVoltage) > 3.0) {
    avgVoltage = rawVoltage;
    isFirstRead = false;
  } else {
    avgVoltage = (avgVoltage * 0.90) + (rawVoltage * 0.10);
  }
  sync_voltage = avgVoltage;
  long volInMilli = (long)(avgVoltage * 1000); 
  int percent = map(volInMilli, 12800, 16800, 0, 100);
  percent = constrain(percent, 0, 100);

  Serial.println("rawVoltage: "); Serial.print(rawVoltage, 2);  Serial.print("V");
  Serial.println("avgVoltage: "); Serial.print(avgVoltage, 2);  Serial.print("V");
  Serial.println("percent: ");    Serial.print(percent);        Serial.print("%");

  if (percent != lastBattery || lastBattery == -1) {
    tft.setTextColor(ILI9341_WHITE, ILI9341_BLACK); 
    tft.setTextSize(3);
    tft.setCursor(200, 10);
    if (percent < 100) tft.print(" "); 
    if (percent < 10) tft.print(" ");
    tft.print(percent); 
    tft.print("%");
    drawDynamicBattery(percent);
    lastBattery = percent;
  }
}

void drawWarningUI(float distance) {
  // Vẽ icon và chữ WARNING (Chỉ nên vẽ 1 lần hoặc vẽ cố định)
  tft.drawBitmap(72, 10, image_big_operation_warning_bits, 176, 176, 0xFE00); 
  tft.setTextColor(0xF206); tft.setTextSize(3); 
  tft.setCursor(95, 190); tft.println("WARNING");
  tft.fillRect(150, 225, 100, 20, ILI9341_BLACK); 

  // 2. Vẽ giá trị mới
  tft.setTextSize(2); 
  tft.setTextColor(ILI9341_WHITE); 
  tft.setCursor(80, 225);
  tft.print("DIST: "); 
  tft.print(distance, 2); 
  tft.print(" m");
}

/* ================= TASK: XỬ LÝ NÚT NHẤN & ĐIỀU KHIỂN MOTOR (ĐÃ SỬA) ================= */
void taskControl(void *pv) {
  pinMode(BTN_BRAKE_PIN, INPUT_PULLUP);
  pinMode(BTN_FWD_PIN,   INPUT_PULLUP);
  pinMode(BTN_REV_PIN,   INPUT_PULLUP);
  pinMode(BTN_STOP_PIN,  INPUT_PULLUP);
  pinMode(POT_PIN, INPUT);
  pinMode(L298N_IN3, OUTPUT);
  pinMode(L298N_IN4, OUTPUT);
  pinMode(L298N_ENB, OUTPUT);

  setMotor(0, true, true);
  Serial.println("Motor Control Ready");
  bool throttleSafe = true; 

  while(1) {
    int adcValue = analogRead(POT_PIN); 
    int targetSpeed = map(adcValue, 0, 4095, 0, 255);
    
    if (targetSpeed < 15) targetSpeed = 0; 

    if (digitalRead(BTN_BRAKE_PIN) == LOW) isBraking = true;
    else isBraking = false;

    bool safeToShift = (targetSpeed < 30) || isBraking;
    if (safeToShift) {
        if (digitalRead(BTN_STOP_PIN) == LOW) currentGear = STATE_STOPPED;
        else if (digitalRead(BTN_FWD_PIN) == LOW) currentGear = STATE_FORWARD;
        else if (digitalRead(BTN_REV_PIN) == LOW) currentGear = STATE_REVERSE;
        throttleSafe = true; 
    } 

    if (targetSpeed > 30 && !throttleSafe) {
        targetSpeed = 0; 
    } else {
        throttleSafe = true; 
    }

    if (isBraking) {
        digitalWrite(L298N_IN3, HIGH);
        digitalWrite(L298N_IN4, HIGH);
        analogWrite(L298N_ENB, 255); 
    } else {
        switch (currentGear) {
            case STATE_FORWARD: 
                setMotor(targetSpeed, true, false); 
                break;
                
            case STATE_REVERSE: { // <--- THÊM DẤU NGOẶC NHỌN MỞ Ở ĐÂY
                int revSpeed = 0;
                if (targetSpeed > 0) {
                    // Map ga để đảm bảo đủ moment khởi động (70) và giới hạn tốc độ lùi (150)
                    revSpeed = map(targetSpeed, 0, 255, 70, 150);
                }
                setMotor(revSpeed, false, false);
                break;
            } // <--- THÊM DẤU NGOẶC NHỌN ĐÓNG Ở ĐÂY
                
            case STATE_STOPPED: 
            default: 
                setMotor(0, true, true); 
                break;
        }
    }
    vTaskDelay(pdMS_TO_TICKS(50)); 
  }
}

/* ================= TASK HIỂN THỊ ================= */
void taskDisplay(void *pv) {
    SpeedData spdData = {0, 0};
    float dist = 0;
    bool isWarningActive = false; 
    drawGaugeBase(); 

    while(1) {
        xQueueReceive(qDistance, &dist, 0);
        if (dist > 0.01 && dist < 0.20) { 
            if (!isWarningActive) { tft.fillScreen(ILI9341_BLACK); isWarningActive = true; }
            drawWarningUI(dist); 
        } 
        else {
            if (isWarningActive) {
                tft.fillScreen(ILI9341_BLACK); drawGaugeBase(); 
                lastSpeed = -1; lastRPM = -1; lastBattery = -1; lastRain = !rain;
                isWarningActive = false;
            }
            drawGearIndicator();
            if (xQueueReceive(qSpeedData, &spdData, 0) == pdTRUE) {
                updateGUI_Speed((uint16_t)spdData.kmh);
                updateGUI_RPM((uint16_t)spdData.rpm);
            }
            updateGUI_Battery();
            rain = (digitalRead(RAIN_PIN) == LOW);
            if(rain != lastRain) { updateGUI_Rain(); }
            if(dist > 0.01 && dist < 1.0) tft.drawBitmap(163, 4, image_operation_warning_bits, 32, 32, ILI9341_RED);
            else tft.drawBitmap(163, 4, image_operation_warning_bits, 32, 32, 0x73AE);
        }
        vTaskDelay(pdMS_TO_TICKS(50)); 
    }
}

/* ================= TASK: ULTRASONIC ================= */
void taskUltrasonic(void *pv) {
  pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT); digitalWrite(BUZZER_PIN, LOW);
  float lastValidDistance = 0.0;

  while (1) {
    // --- 1. Đọc cảm biến ---
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    
    long duration = pulseIn(ECHO_PIN, HIGH, 30000); 
    // Tính khoảng cách ra mét (m)
    float dist = (duration > 0) ? (duration * 0.000343) / 2.0 : lastValidDistance;
    
    // Lọc nhiễu cơ bản
    if(dist > 4.0) dist = 4.0; 
    if(dist < 0.02 && dist > 0) dist = 0.0;
    if(duration > 0) lastValidDistance = dist;
    
    xQueueOverwrite(qDistance, &dist);

    // --- 2. Xử lý logic còi ---
    bool isReversing = (currentGear == STATE_REVERSE && !isBraking);
    int beepInterval = 0; // Thời gian nghỉ giữa các tiếng bíp (ms)

    // LOGIC ƯU TIÊN: Kiểm tra từ nguy hiểm nhất đến ít nguy hiểm nhất
    
    // Mức 1: Gần sát vật thể (Ví dụ < 0.2m) -> Kêu rất nhanh
    if (dist > 0.0 && dist < 0.2) {
        beepInterval = 50; 
    }
    // Mức 2: Khoảng cách cảnh báo (< 0.5m) -> Kêu vừa phải
    else if (dist >= 0.2 && dist < 0.5) {
        beepInterval = 400;
    }
    // Mức 3: Chỉ đang lùi (và đường thoáng) -> Kêu chậm
    else if (isReversing) {
        beepInterval = 800;
    }
    // Mức 4: Không lùi và không có vật cản gần -> Im lặng
    else {
        beepInterval = 0;
    }

    // --- 3. Điều khiển Buzzer ---
    if (beepInterval > 0) {
        digitalWrite(BUZZER_PIN, HIGH);
        vTaskDelay(pdMS_TO_TICKS(50)); // Thời gian kêu (Beep) cố định ngắn gọn
        digitalWrite(BUZZER_PIN, LOW);
        vTaskDelay(pdMS_TO_TICKS(beepInterval)); // Thời gian nghỉ quyết định tốc độ nhanh/chậm
    } else {
        // Không cần kêu, delay một chút để hệ thống không bị quá tải khi lặp lại
        vTaskDelay(pdMS_TO_TICKS(100));
    }
  }
}

/* ================= TASK: SPEED MEASURE ================= */
void taskSpeed(void *pv) {
  SpeedData currentSpeed;
  TickType_t xLastWakeTime = xTaskGetTickCount();
  const TickType_t xFrequency = pdMS_TO_TICKS(INTERVAL_MS);

  while (1) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    portENTER_CRITICAL_ISR(&timerMux); 
      uint32_t current_pulses = pulse_count;
      pulse_count = 0; 
    portEXIT_CRITICAL_ISR(&timerMux); 
    
    // --- [FIX LOGIC RPM] ---
    float multiplier = 1.8; // Mặc định là 1.5
    if (currentGear == STATE_REVERSE) {
        multiplier = 1.0;   // Nếu đang lùi thì không nhân 1.5
    }

    float rpm = multiplier * (float(current_pulses) / SLOTS) * (60000.0 / INTERVAL_MS);
    float kmh = rpm * 3.14159 * WHEEL_DIAMETER * 0.06; 

    currentSpeed.rpm = rpm; 
    currentSpeed.kmh = kmh;
    xQueueOverwrite(qSpeedData, &currentSpeed);
    // Đẩy vào biến đồng bộ cho mạng CAN
    sync_rpm = (uint16_t)rpm;
    sync_speed = (uint16_t)kmh;
  }
}
void taskCANTx(void *pv) {
    // Khởi tạo SPI riêng cho CAN
    pinMode(CAN_CS_PIN, OUTPUT);
    digitalWrite(CAN_CS_PIN, HIGH);
    SPI_CAN.begin(CAN_SCK_PIN, CAN_MISO_PIN, CAN_MOSI_PIN, CAN_CS_PIN);
    
    mcp2515.reset();
    mcp2515.setBitrate(CAN_500KBPS, MCP_8MHZ); // Tùy chỉnh thạch anh trên module (8MHz hoặc 16MHz)
    mcp2515.setNormalMode();

    Serial.println("[CAN TX] Ready to send Simulation Data...");
    struct can_frame frame;
    frame.can_dlc = 2; // Payload 2 bytes cho mỗi gói

    while(1) {
        // 1. Gửi SPEED (ID: 0x100)
        frame.can_id = 0x100;
        frame.data[0] = (sync_speed >> 8) & 0xFF;
        frame.data[1] = sync_speed & 0xFF;
        mcp2515.sendMessage(&frame);
        vTaskDelay(pdMS_TO_TICKS(10));

        // 2. Gửi RPM (ID: 0x101)
        frame.can_id = 0x101;
        frame.data[0] = (sync_rpm >> 8) & 0xFF;
        frame.data[1] = sync_rpm & 0xFF;
        mcp2515.sendMessage(&frame);
        vTaskDelay(pdMS_TO_TICKS(10));

        // 3. Gửi VOLTAGE/ENERGY (ID: 0x102) - Nhân 100 để gửi dạng số nguyên (VD: 12.34V -> 1234)
        uint16_t intVoltage = (uint16_t)(sync_voltage * 100.0);
        frame.can_id = 0x102;
        frame.data[0] = (intVoltage >> 8) & 0xFF;
        frame.data[1] = intVoltage & 0xFF;
        mcp2515.sendMessage(&frame);

        vTaskDelay(pdMS_TO_TICKS(80)); // Đợi 80ms => Tổng chu kỳ ~100ms/lần gửi
    }
}
/* ================= SETUP ================= */
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("System Booting...");
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(SPEED_PIN, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(SPEED_PIN), isrSpeedSensor, FALLING);

  qDistance  = xQueueCreate(1, sizeof(float));
  qSpeedData = xQueueCreate(1, sizeof(SpeedData));
  SPI.begin(TFT_SCLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.begin(); tft.setRotation(1);
  Wire.begin(I2C_SDA, I2C_SCL);
  ina226.init(); ina226.setAverage(INA226_AVERAGE_16); 
  drawGaugeBase();

  xTaskCreate(taskUltrasonic, "Ultrasonic", 2048, NULL, 2, NULL);
  xTaskCreate(taskSpeed,      "Speed",      4096, NULL, 3, NULL);
  xTaskCreate(taskDisplay,    "Display",    6000, NULL, 1, NULL); 
  xTaskCreate(taskControl,    "Control",    4096, NULL, 4, NULL); 
  xTaskCreatePinnedToCore(taskCANTx, "CANTx", 4096, NULL, 2, NULL, 1);
}

void loop() { vTaskDelete(NULL); }