#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui.h"
#include <GyverBME280.h>
#include <SparkFunCCS811.h>

//=============== НАСТРОЙКИ ===============
#define I2C_SDA 21                 // Пин SDA I2C
#define I2C_SCL 22                 // Пин SCL I2C
#define LVGL_TICK_PERIOD 2         // Период обновления LVGL (мс)
#define SENSOR_UPDATE_MS 1000      // Интервал опроса датчиков
#define DISP_BUF_HEIGHT 25         // Высота буфера дисплея
// #define DEBUG                   // Отладка

//=============== ДОБАВЛЕННЫЕ НАСТРОЙКИ ===============
#define LED_PIN 2                  
#define CO2_ALARM_THRESHOLD 1500   // Порог для срабатывания сигнализации (на перспективу)

//=============== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===============
TFT_eSPI tft;
GyverBME280 bme;
CCS811 ccs811(0x5A);
lv_chart_series_t *co2Series = NULL;
lv_display_t *lv_disp = nullptr;   // Указатель на дисплей LVGL

// Состояния приложения
enum AppState { MAIN_SCREEN, CO2_SCREEN };
volatile AppState currentState = MAIN_SCREEN;

volatile bool sensorsInitialized = false;
volatile bool ui_ready = false;    // Флаг готовности UI

// Текущие показания
float currentTemp = 0;
float currentHum = 0;
float currentPress = 0;
uint16_t currentCO2 = 400;
uint16_t currentTVOC = 0;

//=============== НАСТРОЙКИ LVGL ===============
static lv_color_t lv_buf1[TFT_WIDTH * DISP_BUF_HEIGHT];
static lv_color_t lv_buf2[TFT_WIDTH * DISP_BUF_HEIGHT];

//=============== ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ ===============
bool initDisplay() {
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    if (!lv_is_initialized()) {
        lv_init();
        lv_disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
        if (!lv_disp) return false;
        
        lv_display_set_buffers(lv_disp, lv_buf1, lv_buf2, 
                             sizeof(lv_buf1), 
                             LV_DISPLAY_RENDER_MODE_PARTIAL);
        
        lv_display_set_flush_cb(lv_disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
            tft.startWrite();
            tft.setAddrWindow(area->x1, area->y1, 
                            area->x2 - area->x1 + 1, 
                            area->y2 - area->y1 + 1);
            tft.pushColors((uint16_t*)px_map, 
                          (area->x2 - area->x1 + 1) * 
                          (area->y2 - area->y1 + 1), 
                          true);
            tft.endWrite();
            lv_display_flush_ready(disp);
        });
    }
    return true;
}

//=============== НАСТРОЙКА УСТРОЙСТВА ===============
void setup() {
    Serial.begin(115200);
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(100000);
    pinMode(LED_PIN, OUTPUT);        

    // Инициализация дисплея
    if (!initDisplay()) {
        Serial.println("[ERROR] Дисплей не инициализирован!");
        while(1);
    }

    ui_init();

    // Инициализация датчиков в отдельной задаче
    xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            bool sensorsOK = true;
        
            if (!bme.begin() || !ccs811.begin()) {
                Serial.println("[ERROR] Датчики не найдены!");
                sensorsOK = false;
            } else {
                ccs811.setDriveMode(1);
                bme.setFilter(3);
            }
            
            if (sensorsOK) {
                sensorsInitialized = true;
                while (!ccs811.dataAvailable()) {
                    vTaskDelay(100 / portTICK_PERIOD_MS);
                }
                updateSensorData();
                ui_ready = true; // Разрешаем загрузку UI
            }
            vTaskDelete(NULL);
        },
        "SensorsInit",
        4096,
        NULL,
        0,
        NULL,
        CONFIG_ARDUINO_RUNNING_CORE
    );
}

//=============== ОСНОВНОЙ ЦИКЛ ===============
void loop() {
    digitalWrite(LED_PIN, LOW);
    static uint32_t lastSensorUpdate = 0;
    
    lv_timer_handler();
    lvglTick();

    // Загрузка основного экрана после инициализации
    if (ui_ready) {
        lv_screen_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_IN, 100, 2000, true);
        currentState = MAIN_SCREEN;
        ui_ready = false;
    }

    // Обновление данных
    if (sensorsInitialized && (millis() - lastSensorUpdate >= SENSOR_UPDATE_MS)) {
        lastSensorUpdate = millis();
        updateSensorData();
        updateUI();
    }
    
    handleSerialInput();
    delay(LVGL_TICK_PERIOD);
}

//=============== ОБНОВЛЕНИЕ ДАННЫХ С ДАТЧИКОВ ===============
void updateSensorData() {
    if (!sensorsInitialized) return;
    
    if (ccs811.dataAvailable()) {
        currentTemp = bme.readTemperature();
        currentHum = bme.readHumidity();
        currentPress = bme.readPressure() * 0.00750061683f;     // Перевод давления в мм.рт.ст
        ccs811.readAlgorithmResults();
        ccs811.setEnvironmentalData(currentHum, currentTemp);
        currentCO2 = ccs811.getCO2();
        currentTVOC = ccs811.getTVOC();

        #ifdef DEBUG
           Serial.printf("[DEBUG] Temp: %.1f°C", currentTemp); 
           Serial.printf(" Hum: %.1f%%", currentHum);  
           Serial.printf(" Press: %.1fmmHg", currentPress);  
           Serial.printf(" CO2: %dppm", currentCO2);  
           Serial.printf(" TVOC: %dppb\n", currentTVOC);  
        #endif 
    }
}

//=============== ОБНОВЛЕНИЕ ИНТЕРФЕЙСА ===============
void updateUI() {
    if (currentState == MAIN_SCREEN) {
        lv_label_set_text_fmt(ui_Screen1_Label_CO2, "%d", currentCO2);
        lv_arc_set_value(ui_Screen1_Arc_CO2, currentCO2);

        lv_label_set_text_fmt(ui_Screen1_Label_Hum, "%d", (int)round(currentHum));
        lv_arc_set_value(ui_Screen1_Arc_Hum, (int)round(currentHum));

        lv_label_set_text_fmt(ui_Screen1_Label_Temp, "%.1f", currentTemp);
        lv_arc_set_value(ui_Screen1_Arc_Temp, currentTemp);

        lv_label_set_text_fmt(ui_Screen1_Label_TVOC, "%d", currentTVOC);
        lv_label_set_text_fmt(ui_Screen1_Label_Press, "%d", (int)round(currentPress));

    } else {
        if (!co2Series) {
            co2Series = lv_chart_add_series(ui_Screen2_Chart_CO2, 
                lv_color_hex(0xa9a9a9), LV_CHART_AXIS_PRIMARY_Y);
            lv_chart_set_point_count(ui_Screen2_Chart_CO2, 10);
            lv_chart_set_update_mode(ui_Screen2_Chart_CO2, LV_CHART_UPDATE_MODE_SHIFT);
        }
        
        lv_label_set_text_fmt(ui_Screen2_Label_CO2, "%d", currentCO2);
        
        // Ограничение для графика CO2
        if (currentCO2 >= 2250) currentCO2 = 2250;
        lv_chart_set_next_value(ui_Screen2_Chart_CO2, co2Series, currentCO2);
    }
}

//=============== ВСПОМОГАТЕЛЬНАЯ ФУНКЦИИ ===============
void lvglTick() {
    static uint32_t lastTick = 0;
    if (millis() - lastTick >= LVGL_TICK_PERIOD) {
        lv_tick_inc(LVGL_TICK_PERIOD);
        lastTick = millis();
    }
}

//=============== ОБРАБОТКА КОМАНД ===============
void handleSerialInput() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input.equalsIgnoreCase("graph")) {
            currentState = CO2_SCREEN;
            lv_screen_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
        } else if (input.equalsIgnoreCase("main")) {
            currentState = MAIN_SCREEN;
            lv_screen_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
        }
    }
}
