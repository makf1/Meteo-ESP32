//////////////////////////////////////////////////////////////////////////////
// Метеостанция с оптимизацией памяти

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui.h"
#include <GyverBME280.h>
#include <SparkFunCCS811.h>

//=============== НАСТРОЙКИ ===============
#define I2C_SDA 21
#define I2C_SCL 22
#define CO2_HISTORY_SIZE 30      // Уменьшено с 60 до 30
#define LVGL_TICK_PERIOD 5
#define DEBUG_LEVEL 1            // Уменьшен уровень отладки
#define I2C_TIMEOUT 1000
#define DISP_BUF_HEIGHT 20       // Высота буфера дисплея

//=============== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===============
TFT_eSPI tft;
GyverBME280 bme;
CCS811 ccs811(0x5A);
bool ccs811Initialized = false;

enum AppState { BOOT, MAIN_SCREEN, CO2_SCREEN };
volatile AppState currentState = BOOT;

struct {
    uint16_t data[CO2_HISTORY_SIZE] = {400}; // Используем uint16_t вместо int
    uint8_t index = 0;
    bool updated = false;
} co2History;

float currentTemp = 0;
float currentHum = 0;
float currentPres = 0;
uint16_t currentCO2 = 400;      // Используем uint16_t

char tempStr[10], humStr[10], presStr[15], co2Str[15];
lv_chart_series_t *series = NULL;

//=============== ОСНОВНОЙ ЦИКЛ ===============
void loop() { // Убедитесь, что функция объявлена в глобальной области
    static uint32_t lastUpdate = 0;
    
    lv_timer_handler();
    lvglTick();
    handleSerialInput();

    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();
        if (ccs811Initialized) updateSensorData();
        
        if (currentState == MAIN_SCREEN) updateMainUI();
        else if (currentState == CO2_SCREEN) updateCO2Screen();
    }
    
    safeDelay(2);
}

//=============== ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ ===============
bool initDisplay() {
    debugPrint(2, "Инициализация дисплея...");
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
    if (!lv_is_initialized()) {
        lv_init();
        static lv_color_t buf1[TFT_WIDTH * DISP_BUF_HEIGHT]; // Уменьшенный буфер
        static lv_color_t buf2[TFT_WIDTH * DISP_BUF_HEIGHT];
        lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
        lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
        
        lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
            tft.startWrite();
            tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
            tft.pushColors((uint16_t *)px_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
            tft.endWrite();
            lv_display_flush_ready(disp);
        });
    }
    return true;
}

//=============== НАСТРОЙКА УСТРОЙСТВА ===============
void setup() {
    Serial.begin(115200);
    debugPrint(1, "Инициализация системы...");

    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    Wire.setTimeout(I2C_TIMEOUT);
    
    if (!initDisplay()) {
        debugPrint(3, "Ошибка дисплея!");
        while(1) safeDelay(1000);
    }
    
    ccs811Initialized = initSensors();
    if (!ccs811Initialized) {
        debugPrint(3, "Ошибка датчиков!");
        lv_label_set_text(ui_Screen1_Label_CO2, "SENSOR ERROR");
    }

    ui_init();
    
    // Инициализация графика
    lv_chart_set_range(ui_Screen2_Chart_CO2, LV_CHART_AXIS_PRIMARY_Y, 400, 2000);
    lv_chart_set_point_count(ui_Screen2_Chart_CO2, CO2_HISTORY_SIZE);
    series = lv_chart_add_series(ui_Screen2_Chart_CO2, 
                                lv_palette_main(LV_PALETTE_RED), 
                                LV_CHART_AXIS_PRIMARY_Y);
    
    lv_screen_load(ui_Screen1);
    currentState = MAIN_SCREEN;
    debugPrint(1, "Система готова");
}

bool initSensors() {
    debugPrint(2, "Инициализация датчиков...");
    bool status = true;
    
    if (!bme.begin()) {
        debugPrint(3, "Ошибка BME280");
        status = false;
    }
    
    if (!ccs811.begin()) {
        debugPrint(3, "CCS811 не обнаружен!");
        status = false;
    } else {
        ccs811.setDriveMode(1);
        safeDelay(1000);
        debugPrint(2, "CCS811 OK");
    }
    
    return status;
}

void updateSensorData() {
    currentTemp = bme.readTemperature();
    currentHum = bme.readHumidity();
    currentPres = bme.readPressure() / 133.322F;

    if (ccs811.dataAvailable()) {
        ccs811.setEnvironmentalData(currentHum, currentTemp);
        
        uint8_t retry = 3;
        while (retry-- && !ccs811.readAlgorithmResults()) {
            debugPrint(2, "Повтор...");
            safeDelay(200);
        }
        if (!retry) {
            debugPrint(3, "Ошибка CCS811");
            return;
        }
        currentCO2 = ccs811.getCO2();
        
        co2History.data[co2History.index] = currentCO2;
        co2History.index = (co2History.index + 1) % CO2_HISTORY_SIZE;
        co2History.updated = true;
    }
    
    snprintf(tempStr, sizeof(tempStr), "%d°C", (int)round(currentTemp));
    snprintf(humStr, sizeof(humStr), "%d%%", (int)round(currentHum));
    snprintf(presStr, sizeof(presStr), "%.1fmmHg", currentPres);
    snprintf(co2Str, sizeof(co2Str), "%dppm", currentCO2);
}

void updateMainUI() {
    lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
    lv_label_set_text(ui_Screen1_Label_Hum, humStr);
    lv_label_set_text(ui_Screen1_Label_Press, presStr);
    lv_label_set_text(ui_Screen1_Label_CO2, co2Str);

    lv_arc_set_value(ui_Screen1_Arc_Temp, (int)round(currentTemp));
    lv_arc_set_value(ui_Screen1_Arc_Hum, (int)round(currentHum));
    lv_arc_set_value(ui_Screen1_Arc_Press, (int)currentPres);
    lv_arc_set_value(ui_Screen1_Arc_CO2, currentCO2);
}

void updateCO2Screen() {
    if (!co2History.updated || !series) return;

    // Обновление всех точек графика
    for (int i = 0; i < CO2_HISTORY_SIZE; i++) {
        int idx = (co2History.index + i) % CO2_HISTORY_SIZE;
        lv_chart_set_next_value(ui_Screen2_Chart_CO2, series, co2History.data[idx]);
    }
    
    lv_label_set_text(ui_Screen2_Label_CO2, co2Str);
    co2History.updated = false;
}

void debugPrint(int level, const String &message) {
#if DEBUG_LEVEL >= 1
    if (level <= DEBUG_LEVEL) {
        Serial.printf("[%lu] %s\n", millis(), message.c_str());
    }
#endif
}

void safeDelay(uint32_t ms) {
    uint32_t start = millis();
    while (millis() - start < ms) delay(1);
}

void lvglTick() {
    static uint32_t lastTick = 0;
    if (millis() - lastTick >= LVGL_TICK_PERIOD) {
        lv_tick_inc(LVGL_TICK_PERIOD);
        lastTick = millis();
    }
}

void handleSerialInput() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input.equalsIgnoreCase("screen2")) {
            currentState = CO2_SCREEN;
            lv_screen_load(ui_Screen2);
            lv_chart_refresh(ui_Screen2_Chart_CO2); // Принудительное обновление
        } else if (input.equalsIgnoreCase("screen1")) {
            currentState = MAIN_SCREEN;
            lv_screen_load(ui_Screen1);
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// Метеостанция с исправлениями для CO2 и графика

// #include <Arduino.h>
// #include <Wire.h>
// #include <TFT_eSPI.h>
// #include <lvgl.h>
// #include "ui.h"
// #include <GyverBME280.h>
// #include <SparkFunCCS811.h>

// //=============== НАСТРОЙКИ ===============
// #define I2C_SDA 21
// #define I2C_SCL 22
// #define CO2_HISTORY_SIZE 60    // Размер буфера истории
// #define LVGL_TICK_PERIOD 5     // Период обновления LVGL (мс)
// #define DEBUG_LEVEL 3          // Уровень отладки (0-3)

// //=============== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===============
// TFT_eSPI tft;
// GyverBME280 bme;
// CCS811 ccs811(0x5A);

// enum AppState { BOOT, MAIN_SCREEN, CO2_SCREEN };
// volatile AppState currentState = BOOT;

// struct {
//     lv_coord_t data[CO2_HISTORY_SIZE] = {400};
//     uint8_t index = 0;
//     bool updated = false;
// } co2History;

// float currentTemp = 0;
// float currentHum = 0;
// float currentPres = 0;
// int currentCO2 = 400;          // Текущее значение CO2

// char tempStr[10], humStr[10], presStr[15], co2Str[15];

// //=============== ПРОТОТИПЫ ФУНКЦИЙ ===============
// void debugPrint(int level, const String &message);
// bool initDisplay();
// bool initSensors();
// void updateSensorData();
// void updateMainUI();
// void updateCO2Screen();
// void handleSerialInput();
// void lvglTick();
// void safeDelay(uint32_t ms);

// //=============== НАСТРОЙКА УСТРОЙСТВА ===============
// void setup() {
//     Serial.begin(115200);
//     debugPrint(1, "Инициализация системы...");

//     Wire.begin(I2C_SDA, I2C_SCL);
//     Wire.setClock(100000);
    
//     if (!initDisplay()) {
//         debugPrint(3, "Ошибка дисплея!");
//         while (1) safeDelay(1000);
//     }
    
//     if (!initSensors()) {
//         debugPrint(3, "Ошибка датчиков!");
//         lv_label_set_text(ui_Screen1_Label_CO2, "SENSOR ERROR");
//     }

//     ui_init();
//     lv_screen_load(ui_Screen1);
//     currentState = MAIN_SCREEN;
//     debugPrint(1, "Система готова");
// }

// //=============== ОСНОВНОЙ ЦИКЛ ===============
// void loop() {
//     static uint32_t lastUpdate = 0;
    
//     lv_timer_handler();
//     lvglTick();
//     handleSerialInput();

//     if (millis() - lastUpdate >= 2000) {
//         lastUpdate = millis();
//         updateSensorData();
        
//         if (currentState == MAIN_SCREEN) updateMainUI();
//         else if (currentState == CO2_SCREEN) updateCO2Screen();
//     }
    
//     safeDelay(2);
// }

// //=============== ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ ===============
// bool initDisplay() {
//     debugPrint(2, "Инициализация дисплея...");
//     tft.begin();
//     tft.setRotation(0);
//     tft.fillScreen(TFT_BLACK);
    
//     if (!lv_is_initialized()) {
//         lv_init();
//         static lv_color_t buf[TFT_HEIGHT * 40];
//         lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
//         lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
        
//         lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
//             tft.startWrite();
//             tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
//             tft.pushColors((uint16_t *)px_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
//             tft.endWrite();
//             lv_display_flush_ready(disp);
//         });
//     }
//     return true;
// }

// //=============== ИНИЦИАЛИЗАЦИЯ ДАТЧИКОВ ===============
// bool initSensors() {
//     debugPrint(2, "Инициализация датчиков...");
//     bool status = true;
    
//     if (!bme.begin()) {
//         debugPrint(3, "Ошибка BME280");
//         status = false;
//     }
    
//     if (!ccs811.begin()) {
//         debugPrint(3, "Ошибка CCS811");
//         status = false;
//     } else {
//         ccs811.setDriveMode(1);
//     }
    
//     return status;
// }

// //=============== ОБНОВЛЕНИЕ ДАННЫХ ===============
// void updateSensorData() {
//     // Чтение BME280
//     currentTemp = bme.readTemperature();
//     currentHum = bme.readHumidity();
//     currentPres = bme.readPressure() / 133.322F;

//     // Чтение CCS811 с обработкой ошибок
//     if (ccs811.dataAvailable()) {
//         if (ccs811.readAlgorithmResults()) {
//             currentCO2 = ccs811.getCO2();
//             debugPrint(1, "CO2: " + String(currentCO2) + " ppm");
            
//             // Обновление истории
//             co2History.data[co2History.index] = currentCO2;
//             co2History.index = (co2History.index + 1) % CO2_HISTORY_SIZE;
//             co2History.updated = true;
//         } else {
//             debugPrint(3, "Ошибка чтения CCS811");
//         }
//     }
    
//     // Форматирование строк
//     snprintf(tempStr, sizeof(tempStr), "%d°C", (int)round(currentTemp));
//     snprintf(humStr, sizeof(humStr), "%d%%", (int)round(currentHum));
//     snprintf(presStr, sizeof(presStr), "%.1fmmHg", currentPres);
//     snprintf(co2Str, sizeof(co2Str), "%dppm", currentCO2);
// }

// //=============== ОБНОВЛЕНИЕ ИНТЕРФЕЙСА ===============
// void updateMainUI() {
//     lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
//     lv_label_set_text(ui_Screen1_Label_Hum, humStr);
//     lv_label_set_text(ui_Screen1_Label_Press, presStr);
//     lv_label_set_text(ui_Screen1_Label_CO2, co2Str);

//     lv_arc_set_value(ui_Screen1_Arc_Temp, (int)round(currentTemp));
//     lv_arc_set_value(ui_Screen1_Arc_Hum, (int)round(currentHum));
//     lv_arc_set_value(ui_Screen1_Arc_Press, (int)currentPres);
//     lv_arc_set_value(ui_Screen1_Arc_CO2, currentCO2);
// }

// void updateCO2Screen() {
//     if (!co2History.updated) return;
    
//     lv_chart_series_t *series = lv_chart_get_series_next(ui_Screen2_Chart_CO2, NULL);
//     for (int i = 0; i < CO2_HISTORY_SIZE; i++) {
//         int idx = (co2History.index + i) % CO2_HISTORY_SIZE;
//         series->y_points[i] = co2History.data[idx];
//     }
//     lv_chart_refresh(ui_Screen2_Chart_CO2);
//     lv_label_set_text(ui_Screen2_Label_CO2, co2Str);
//     co2History.updated = false;
// }

// //=============== ДОПОЛНИТЕЛЬНЫЕ ФУНКЦИИ ===============
// void debugPrint(int level, const String &message) {
// #if DEBUG_LEVEL >= 1
//     if (level <= DEBUG_LEVEL) {
//         Serial.printf("[%lu] %s\n", millis(), message.c_str());
//     }
// #endif
// }

// void safeDelay(uint32_t ms) {
//     uint32_t start = millis();
//     while (millis() - start < ms) {
//         delay(1);
//     }
// }

// void lvglTick() {
//     static uint32_t lastTick = 0;
//     if (millis() - lastTick >= LVGL_TICK_PERIOD) {
//         lv_tick_inc(LVGL_TICK_PERIOD);
//         lastTick = millis();
//     }
// }

// void handleSerialInput() {
//     if (Serial.available()) {
//         String input = Serial.readStringUntil('\n');
//         input.trim();
        
//         if (input.equalsIgnoreCase("screen2")) {
//             currentState = CO2_SCREEN;
//             lv_screen_load(ui_Screen2);
//         } else if (input.equalsIgnoreCase("screen1")) {
//             currentState = MAIN_SCREEN;
//             lv_screen_load(ui_Screen1);
//         }
//     }
// }

//////////////////////////////////////////////////////////////////////////////
// Метеостанция с расширенной диагностикой и защитой от сбоев

// #include <Arduino.h>
// #include <Wire.h>
// #include <TFT_eSPI.h>
// #include <lvgl.h>
// #include "ui.h"
// #include <GyverBME280.h>
// #include <SparkFunCCS811.h>

// //=============== КОНФИГУРАЦИЯ ===============
// #define I2C_SDA 21
// #define I2C_SCL 22
// #define CO2_HISTORY_SIZE 60
// #define LVGL_TICK_PERIOD 5     // 5 ms
// #define SENSOR_TIMEOUT 10000   // 10 sec
// #define DEBUG_LEVEL 3          // 0-3 (0 - выключено)

// //=============== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===============
// TFT_eSPI tft;
// GyverBME280 bme;
// CCS811 ccs811(0x5A);

// // Состояния приложения
// enum AppState { BOOT, MAIN_SCREEN, CO2_SCREEN };
// volatile AppState currentState = BOOT;

// // Данные датчиков
// float currentTemp = 0;
// float currentHum = 0;
// float currentPres = 0;

// // История CO2
// struct {
//     lv_coord_t data[CO2_HISTORY_SIZE] = {400};
//     uint8_t index = 0;
//     bool updated = false;
// } co2History;

// // Таймеры
// uint32_t initStartTime = 0;

// //=============== ПРОТОТИПЫ ФУНКЦИЙ ===============
// void debugPrint(int level, const String &message);
// bool initDisplay();
// bool initSensors();
// void updateSensorData();
// void updateMainUI();
// void updateCO2Screen();
// void handleSerialInput();
// void lvglTick();
// void safeDelay(uint32_t ms);

// // Буферы для строковых данных
// char tempStr[20];
// char humStr[20];
// char presStr[20];
// char co2Str[20];

// //=============== НАСТРОЙКА УСТРОЙСТВА ===============
// void setup() {
//     Serial.begin(115200);
//     debugPrint(1, "Starting setup...");

//     // Инициализация I2C
//     Wire.begin(I2C_SDA, I2C_SCL);
//     Wire.setClock(100000);
    
//     // Инициализация дисплея
//     if (!initDisplay()) {
//         debugPrint(3, "Display initialization failed!");
//         while (1) safeDelay(1000);
//     }
    
//     // Инициализация датчиков
//     if (!initSensors()) {
//         debugPrint(3, "Sensor initialization failed!");
//         lv_label_set_text(ui_Screen1_Label_CO2, "SENSOR ERROR");
//     }

//     // Настройка таймера
//     initStartTime = millis();
//     debugPrint(2, "System timer initialized");

//     // Загрузка интерфейса
//     ui_init();
//     lv_screen_load(ui_Screen1);
//     currentState = MAIN_SCREEN;
//     debugPrint(1, "UI initialized successfully");
// }

// //=============== ОСНОВНОЙ ЦИКЛ ===============
// void loop() {
//     static uint32_t lastUpdate = 0;
    
//     lv_timer_handler();
//     lvglTick();
//     handleSerialInput();

//     if (millis() - lastUpdate >= 2000) {
//         debugPrint(2, "Starting sensor update...");
//         lastUpdate = millis();
//         updateSensorData();
        
//         if (currentState == MAIN_SCREEN) {
//             updateMainUI();
//         } else if (currentState == CO2_SCREEN) {
//             updateCO2Screen();
//         }
//         debugPrint(2, "Update completed");
//     }
    
//     safeDelay(2);
// }

// //=============== ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ ===============
// bool initDisplay() {
//     debugPrint(2, "Initializing display...");
//     tft.begin();
//     tft.setRotation(0);
//     tft.fillScreen(TFT_BLACK);
    
//     if (!lv_is_initialized()) {
//         debugPrint(2, "Starting LVGL init...");
//         lv_init();
        
//         static lv_color_t buf[TFT_HEIGHT * 40];
//         if (!buf) {
//             debugPrint(3, "LVGL buffer allocation failed!");
//             return false;
//         }
        
//         lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
//         lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
        
//         lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
//             debugPrint(3, "Flushing display...");
//             tft.startWrite();
//             tft.setAddrWindow(area->x1, area->y1, area->x2 - area->x1 + 1, area->y2 - area->y1 + 1);
//             tft.pushColors((uint16_t *)px_map, (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1), true);
//             tft.endWrite();
//             lv_display_flush_ready(disp);
//         });
//     }
//     debugPrint(1, "Display initialized OK");
//     return true;
// }

// //=============== ИНИЦИАЛИЗАЦИЯ ДАТЧИКОВ ===============
// bool initSensors() {
//     debugPrint(2, "Initializing sensors...");
//     bool status = true;
    
//     if (!bme.begin()) {
//         debugPrint(3, "BME280 init failed!");
//         status = false;
//     } else {
//         debugPrint(2, "BME280 OK");
//     }
    
//     if (!ccs811.begin()) {
//         debugPrint(3, "CCS811 init failed!");
//         status = false;
//     } else {
//         ccs811.setDriveMode(1);
//         debugPrint(2, "CCS811 OK");
//     }
    
//     return status;
// }

// //=============== ОБНОВЛЕНИЕ ДАННЫХ ===============
// void updateSensorData() {
//     debugPrint(3, "Reading BME280 data...");
//     currentTemp = bme.readTemperature();
//     currentHum = bme.readHumidity();
//     currentPres = bme.readPressure() / 133.322F;

//     debugPrint(3, "Reading CCS811 data...");
//     if (ccs811.dataAvailable()) {
//         if (!ccs811.readAlgorithmResults()) {
//             debugPrint(3, "CCS811 read error!");
//             return;
//         }
        
//         co2History.data[co2History.index] = ccs811.getCO2();
//         co2History.index = (co2History.index + 1) % CO2_HISTORY_SIZE;
//         co2History.updated = true;
//     }
    
//     // Форматирование строк
//     snprintf(tempStr, sizeof(tempStr), "%d°C", (int)round(currentTemp));
//     snprintf(humStr, sizeof(humStr), "%d%%", (int)round(currentHum));
//     snprintf(presStr, sizeof(presStr), "%.1fmmHg", currentPres);
//     snprintf(co2Str, sizeof(co2Str), "%dppm", ccs811.getCO2());
// }

// //=============== ОБНОВЛЕНИЕ ГЛАВНОГО ЭКРАНА ===============
// void updateMainUI() {
//     debugPrint(3, "Updating main UI...");
    
//     lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
//     lv_label_set_text(ui_Screen1_Label_Hum, humStr);
//     lv_label_set_text(ui_Screen1_Label_Press, presStr);
//     lv_label_set_text(ui_Screen1_Label_CO2, co2Str);

//     lv_arc_set_value(ui_Screen1_Arc_Temp, (int)round(currentTemp));
//     lv_arc_set_value(ui_Screen1_Arc_Hum, (int)round(currentHum));
//     lv_arc_set_value(ui_Screen1_Arc_Press, (int)currentPres);
//     lv_arc_set_value(ui_Screen1_Arc_CO2, ccs811.getCO2());
// }

// //=============== ОБНОВЛЕНИЕ ЭКРАНА CO2 ===============
// void updateCO2Screen() {
//     if (!co2History.updated) return;
//     debugPrint(3, "Updating CO2 screen...");
    
//     lv_chart_series_t *series = lv_chart_get_series_next(ui_Screen2_Chart_CO2, NULL);
//     memcpy(series->y_points, co2History.data, sizeof(co2History.data));
//     lv_chart_refresh(ui_Screen2_Chart_CO2);
//     lv_label_set_text(ui_Screen2_Label_CO2, co2Str);
//     co2History.updated = false;
// }

// //=============== ОБРАБОТКА СЕРИЙНОГО ВВОДА ===============
// void handleSerialInput() {
//     if (Serial.available()) {
//         String input = Serial.readStringUntil('\n');
//         input.trim();
//         debugPrint(1, "Received command: " + input);
        
//         if (input.equalsIgnoreCase("screen2")) {
//             currentState = CO2_SCREEN;
//             lv_screen_load(ui_Screen2);
//         } else if (input.equalsIgnoreCase("screen1")) {
//             currentState = MAIN_SCREEN;
//             lv_screen_load(ui_Screen1);
//         }
//     }
// }

// //=============== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ===============
// void debugPrint(int level, const String &message) {
// #if DEBUG_LEVEL >= 1
//     if (level <= DEBUG_LEVEL) {
//         Serial.printf("[%lu] %s\n", millis(), message.c_str());
//     }
// #endif
// }

// void safeDelay(uint32_t ms) {
//     uint32_t start = millis();
//     while (millis() - start < ms) {
//         vTaskDelay(pdMS_TO_TICKS(1));
//     }
// }

// void lvglTick() {
//     static uint32_t lastTick = 0;
//     if (millis() - lastTick >= LVGL_TICK_PERIOD) {
//         lv_tick_inc(LVGL_TICK_PERIOD);
//         lastTick = millis();
//     }
// }

//////////////////////////////////////////////////////////////////////////////

// // Главный файл прошивки для метеостанции

// #include <Arduino.h>
// #include <Wire.h>
// #include <TFT_eSPI.h>
// #include <lvgl.h>
// #include "ui.h"
// #include <GyverBME280.h>
// #include <SparkFunCCS811.h>

// // =================== Конфигурация оборудования ===================
// #define I2C_SDA 21         // Пин I2C SDA
// #define I2C_SCL 22         // Пин I2C SCL
// // #define CCS811_WAKE_PIN -1 // Пин пробуждения CCS811 (не используется)

// // ================= Глобальные объекты и переменные =================
// TFT_eSPI tft = TFT_eSPI(); // Объект дисплея
// GyverBME280 bme;           // Датчик BME280 (T/H/P)
// CCS811 ccs811(0x5A);       // Датчик CO2/TVOC (адрес 0x5A)

// // Таймеры и интервалы опроса
// uint32_t sensorUpdateTimer = 0;                // Таймер последнего обновления
// const uint16_t SENSOR_UPDATE_INTERVAL = 2000;  // Интервал опроса датчиков (2 сек)

// // Буферы для строковых представлений данных
// char tempStr[20];   // Температура
// char humStr[20];    // Влажность
// char presStr[20];   // Давление
// char co2Str[20];    // CO2
// char tvocStr[20];   // ЛОС (TVOC)

// bool sensorsError = false; // Флаг ошибки датчиков

// // Объявления объектов LVGL из UI
// extern lv_obj_t* ui_Screen1_Arc_Temp;  // Дуга температуры
// extern lv_obj_t* ui_Screen1_Arc_Hum;   // Дуга влажности
// extern lv_obj_t* ui_Screen1_Arc_Press; // Дуга давления
// extern lv_obj_t* ui_Screen1_Arc_CO2; // Дуга давления

// // ===================== Прототипы функций =====================
// void initDisplay();         // Инициализация дисплея и LVGL
// void initSensors();         // Инициализация датчиков
// void updateSensorData();    // Чтение данных с датчиков
// void updateUI();            // Обновление интерфейса
// void lvglTick(void*);       // Системный тик LVGL

// // ======================= Настройка =======================
// void setup() {
//   Serial.begin(115200);
//   Serial.println("Initializing system...");

//   // Инициализация I2C
//   Wire.begin(I2C_SDA, I2C_SCL);
  
//   // Инициализация периферии
//   initDisplay();
//   initSensors();

//   // Настройка таймера для LVGL (обновление каждые 5 мс)
//   const uint32_t lvglTickPeriod = 3;
//   esp_timer_create_args_t lvglTimerArgs = {
//     .callback = &lvglTick,
//     .name = "lvgl_timer"
//   };
//   esp_timer_handle_t lvglTimer;
//   esp_timer_create(&lvglTimerArgs, &lvglTimer);
//   esp_timer_start_periodic(lvglTimer, lvglTickPeriod * 1000);

//   // Инициализация пользовательского интерфейса
//   ui_init();
// }

// // ==================== Основной цикл ====================
// void loop() {
//   lv_timer_handler(); // Обработчик событий LVGL

//   // Обновление данных по таймеру
//   if(millis() - sensorUpdateTimer >= SENSOR_UPDATE_INTERVAL){
//     sensorUpdateTimer = millis();
//     updateSensorData(); // Получение новых данных
//     updateUI();         // Обновление интерфейса
//   }
//   delay(2); // Небольшая задержка для стабильности
// }

// // ================ Инициализация дисплея ================
// void initDisplay() {
//   tft.begin();         // Инициализация TFT
//   tft.setRotation(0);  // Ориентация дисплея
//   lv_init();           // Инициализация LVGL

//   // Настройка буфера LVGL (частичная перерисовка)
//   static lv_color_t buf[TFT_HEIGHT * TFT_WIDTH / 10];
//   lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
//   lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

//   // Регистрация функции отрисовки
//   lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
//     uint32_t w = area->x2 - area->x1 + 1;
//     uint32_t h = area->y2 - area->y1 + 1;
//     tft.startWrite();
//     tft.setAddrWindow(area->x1, area->y1, w, h);
//     tft.pushColors((uint16_t *)px_map, w * h, true);
//     tft.endWrite();
//     lv_display_flush_ready(disp);
//   });
// }

// // ================ Инициализация датчиков ================
// void initSensors() {
//   // Инициализация BME280
//   Serial.println("Initializing sensors...");
//   if(!bme.begin()) {
//     Serial.println("BME280 error!");
//     sensorsError = true;
//   }

//   // Инициализация CCS811
//   if(!ccs811.begin()) {
//     Serial.println("CCS811 error!");
//     sensorsError = true;
//   } else {
//     ccs811.setDriveMode(1); // Режим измерения каждую секунду
//     // ccs811.setEnv
//   }
//   Serial.println("Sensors OK!");
// }

// // ================ Обновление данных датчиков ================
// void updateSensorData() {
//   if(sensorsError) return; // Выход при ошибке

//   // Чтение данных с BME280
//   float temperature = bme.readTemperature();      // Температура (°C)
//   float humidity = bme.readHumidity();            // Влажность (%)
//   float pressure = bme.readPressure() / 133.322;   // Давление (мм.рт.ст)
//   float altitude = pressureToAltitude(bme.readPressure()/100.0F);   // Высота (м)

//   // Вывод данных в монитор порта
//   Serial.print("Temperature = "); Serial.print(temperature); Serial.print(" °C | Humidity = "); Serial.print(humidity); Serial.print(" % | Pressure = "); Serial.print(pressure); 
//   Serial.print(" mmHg | Altitude = "); Serial.print(altitude); Serial.println(" m ");

//   // Чтение данных с CCS811
//   if(ccs811.dataAvailable()) {
//     ccs811.readAlgorithmResults();
//     snprintf(co2Str, sizeof(co2Str), "%d ppm", ccs811.getCO2());
//     snprintf(tvocStr, sizeof(tvocStr), "%d ppb", ccs811.getTVOC());
//     Serial.print("CO2 = "); Serial.print(ccs811.getCO2()); Serial.print(" ppm| tVOC = "); Serial.println(ccs811.getTVOC());
//   }
  
//   // Форматирование строк для отображения
//   snprintf(tempStr, sizeof(tempStr), "%.1f C", temperature);
//   snprintf(humStr, sizeof(humStr), "%u %%", humidity);
//   snprintf(presStr, sizeof(presStr), "%.1f mmHg", pressure);
// }

// // ================= Обновление интерфейса =================
// void updateUI() {
//   // Обновление текстовых меток
//   lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
//   lv_label_set_text(ui_Screen1_Label_Hum, humStr);
//   lv_label_set_text(ui_Screen1_Label_Press, presStr);
//   lv_label_set_text(ui_Screen1_Label_CO2, co2Str);

//   // Обновление дуг с анимацией
//   lv_arc_set_value(ui_Screen1_Arc_Temp, (int)bme.readTemperature());     // 0-50°C
//   lv_arc_set_value(ui_Screen1_Arc_Hum, (int)bme.readHumidity());         // 0-100%
//   lv_arc_set_value(ui_Screen1_Arc_Press, (int)(bme.readPressure()/100)); // 0-1100 гПа
//   lv_arc_set_value(ui_Screen1_Arc_CO2, (int)(ccs811.getCO2())); // 
// }

// // ============== Системный тик для LVGL ==============
// void lvglTick(void*) {
//   lv_tick_inc(3); // Обновление системного времени LVGL
// }
