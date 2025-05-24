////////////////////////////////////////////////////////////////////

// Метеостанция с оптимизацией памяти (без сенсорного ввода)

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
#define CO2_HISTORY_SIZE 10        // Размер истории CO2
#define LVGL_TICK_PERIOD 5         // Период обновления LVGL (мс)
#define DEBUG_LEVEL 1              // Уровень отладки (1-3)
#define I2C_TIMEOUT 1000           // Таймаут I2C
#define DISP_BUF_HEIGHT 24         // Высота буфера дисплея
#define LED 2                      // Пин встроенного светодиода

//=============== ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===============
TFT_eSPI tft;                      // Объект дисплея
GyverBME280 bme;                   // Датчик BME280
CCS811 ccs811(0x5A);              // Датчик CCS811 (адрес 0x5A)
bool ccs811Initialized = false;    // Флаг инициализации CCS811

// Состояния приложения
enum AppState { BOOT, MAIN_SCREEN, CO2_SCREEN };
volatile AppState currentState = BOOT;

// Структура для хранения истории CO2
struct {
    uint16_t data[CO2_HISTORY_SIZE] = {400}; // Данные CO2
    uint8_t index = 0;                       // Текущий индекс
    bool updated = false;                    // Флаг обновления
} co2History;

// Текущие показания датчиков
float currentTemp = 0;
float currentHum = 0;
float currentPres = 0;
uint16_t currentCO2 = 400;

// Строковые буферы для отображения
char tempStr[10], humStr[10], presStr[15], co2Str[15], co2Str1[15];
lv_chart_series_t *series = NULL;           // Серия данных графика

// Флаги и таймеры
bool sensorsReady = false;                  // Флаг готовности датчиков
uint32_t bootStartTime = 0;                 // Время начала загрузки
const uint32_t BOOT_TIMEOUT = 5000;         // Таймаут инициализации (5 сек)

//=============== ПРОТОТИПЫ ФУНКЦИЙ ===============
bool initDisplay();                         // Инициализация дисплея
bool initSensors();                         // Инициализация датчиков
void updateSensorData();                    // Обновление данных с датчиков
void updateMainUI();                        // Обновление главного экрана
void updateCO2Screen();                     // Обновление экрана CO2
void debugPrint(int level, 
                const String &message);     // Логирование
void safeDelay(uint32_t ms);                // Безопасная задержка
void lvglTick();                            // Обновление таймера LVGL
void handleSerialInput();                   // Обработка команд с порта

//=============== ИНИЦИАЛИЗАЦИЯ ДИСПЛЕЯ ===============
bool initDisplay() {
    debugPrint(2, "Инициализация дисплея...");
    tft.begin();                            // Запуск дисплея
    tft.setRotation(0);                     // Ориентация
    tft.fillScreen(TFT_BLACK);              // Очистка экрана
    
    if (!lv_is_initialized()) {             // Инициализация LVGL
        lv_init();
        // Настройка буферов (уменьшенный размер для экономии памяти)
        static lv_color_t buf1[TFT_WIDTH * DISP_BUF_HEIGHT];
        static lv_color_t buf2[TFT_WIDTH * DISP_BUF_HEIGHT];
        lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
        lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_PARTIAL);
        // lv_display_set_buffers(disp, buf1, buf2, sizeof(buf1), LV_DISPLAY_RENDER_MODE_DIRECT);
        
        // Настройка вывода на дисплей
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

    pinMode(LED, OUTPUT);
    digitalWrite(LED,LOW); 

    Wire.begin(I2C_SDA, I2C_SCL);          // Инициализация I2C
    Wire.setClock(100000);                 // Частота 100 кГц
    Wire.setTimeout(I2C_TIMEOUT);          // Таймаут I2C
    
    if (!initDisplay()) {                  // Инициализация дисплея
        debugPrint(3, "Ошибка дисплея!");
        while(1) safeDelay(1000);
    }

    ui_init();                             // Инициализация интерфейса
    lv_screen_load_anim(ui_Screen3, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); // Загрузка экрана загрузки
    // lv_spinner_set_anim_params(ui_Screen3_Spinner1, 1000, 360);
    bootStartTime = millis();

    // Фоновая инициализация датчиков
    xTaskCreatePinnedToCore(
        [](void *pvParameters) {
            ccs811Initialized = initSensors();
            sensorsReady = true;
            vTaskDelete(NULL);
        },
        "SensorsInit",
        4096,
        NULL,
        1,
        NULL,
        CONFIG_ARDUINO_RUNNING_CORE
    );
}

//=============== ОСНОВНОЙ ЦИКЛ ===============
void loop() {
    static uint32_t lastUpdate = 0;
    
    lv_timer_handler();                   // Обработка LVGL
    lvglTick();                           // Обновление времени LVGL
    handleSerialInput();                  // Проверка команд с порта

    // Обработка состояния загрузки
    if(!sensorsReady) {
        if(millis() - bootStartTime > BOOT_TIMEOUT) {
            debugPrint(3, "Таймаут инициализации датчиков!");
            // lv_label_set_text(ui_Screen3_Label1, "Ошибка инициализации!");
            lv_label_set_text(ui_Screen3_Label1, "Error init!");
            sensorsReady = true;          // Принудительный переход
        }
        return;
    }

    // Обновление данных каждые 2 секунды
    if (millis() - lastUpdate >= 2000) {
        lastUpdate = millis();
        if(ccs811Initialized) updateSensorData(); // Чтение датчиков
        
        // Обновление текущего экрана
        if (currentState == MAIN_SCREEN) updateMainUI();
        else if (currentState == CO2_SCREEN) updateCO2Screen();
    }
    
    safeDelay(2);                         // Небольшая задержка
}

//=============== ИНИЦИАЛИЗАЦИЯ ДАТЧИКОВ ===============
bool initSensors() {
    debugPrint(2, "Инициализация датчиков...");
    bool status = true;
    
    if (!bme.begin()) {                   // Проверка BME280
        debugPrint(3, "Ошибка BME280");
        status = false;
    }
    
    if (!ccs811.begin()) {                // Проверка CCS811
        debugPrint(3, "CCS811 не обнаружен!");
        status = false;
    } else {
        ccs811.setDriveMode(1);           // Режим измерения
        // safeDelay(1000);                  // Задержка для стабилизации
        debugPrint(2, "CCS811 OK");
    }

    // Переход на главный экран при успехе
    if(status) {
        // lv_screen_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_IN, 1000, 0, false);
        lv_screen_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_FADE_IN, 100, 3000, true);
        currentState = MAIN_SCREEN;
    }
    
    return status;
}

//=============== ОБНОВЛЕНИЕ ДАННЫХ ДАТЧИКОВ ===============
void updateSensorData() {
    currentTemp = bme.readTemperature();  // Температура
    currentHum = bme.readHumidity();      // Влажность
    currentPres = bme.readPressure() / 133.322F; // Давление (в mmHg)

    if (ccs811.dataAvailable()) {         // Чтение CO2
        ccs811.setEnvironmentalData(currentHum, currentTemp); // Коррекция данных
        
        // Повторные попытки чтения (3 раза)
        uint8_t retry = 3;
        while (retry-- && !ccs811.readAlgorithmResults()) {
            debugPrint(2, "Повтор...");
            safeDelay(200);
        }
        if (!retry) {
            debugPrint(3, "Ошибка CCS811");
            return;
        }
        currentCO2 = ccs811.getCO2();     // Получение CO2
        
        // Обновление истории
        co2History.data[co2History.index] = currentCO2;
        co2History.index = (co2History.index + 1) % CO2_HISTORY_SIZE;
        co2History.updated = true;
    }
    
    // Форматирование строк для отображения
    snprintf(tempStr, sizeof(tempStr), "%d°C", (int)round(currentTemp));
    snprintf(humStr, sizeof(humStr), "%d%%", (int)round(currentHum));
    snprintf(presStr, sizeof(presStr), "%.1fmmHg", currentPres);
    snprintf(co2Str, sizeof(co2Str), "%dppm", currentCO2);
    snprintf(co2Str1, sizeof(co2Str1), "%d", currentCO2);
}

//=============== ОБНОВЛЕНИЕ ГЛАВНОГО ЭКРАНА ===============
void updateMainUI() {
    // Установка текста меток
    lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
    lv_label_set_text(ui_Screen1_Label_Hum, humStr);
    lv_label_set_text(ui_Screen1_Label_Press, presStr);
    lv_label_set_text(ui_Screen1_Label_CO2, co2Str);

    // Обновление значений дуг (индикаторов)
    lv_arc_set_value(ui_Screen1_Arc_Temp, (int)round(currentTemp));
    lv_arc_set_value(ui_Screen1_Arc_Hum, (int)round(currentHum));
    lv_arc_set_value(ui_Screen1_Arc_Press, (int)currentPres);
    lv_arc_set_value(ui_Screen1_Arc_CO2, currentCO2);
}

//=============== ОБНОВЛЕНИЕ ЭКРАНА CO2 ===============
void updateCO2Screen() {
    if (!co2History.updated) return;
    
    // Инициализация серии при первом запуске
    if(!series) {
        series = lv_chart_add_series(ui_Screen2_Chart_CO2, lv_color_hex(0xC0C0C0), LV_CHART_AXIS_PRIMARY_Y);
    }
    
    for(int i=0; i<CO2_HISTORY_SIZE; i++) {
        lv_chart_set_next_value(ui_Screen2_Chart_CO2, series, co2History.data[i]);
    }
    
    lv_label_set_text(ui_Screen2_Label_CO2, co2Str1);
    co2History.updated = false;
}

//=============== ВСПОМОГАТЕЛЬНЫЕ ФУНКЦИИ ===============
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
        lv_tick_inc(LVGL_TICK_PERIOD);    // Обновление системного времени LVGL
        lastTick = millis();
    }
}

//=============== ОБРАБОТКА КОМАНД С ПОСЛЕДОВАТЕЛЬНОГО ПОРТА ===============
void handleSerialInput() {
    if (Serial.available()) {
        String input = Serial.readStringUntil('\n');
        input.trim();
        
        if (input.equalsIgnoreCase("screen2")) { // Переход на экран CO2
            currentState = CO2_SCREEN;
            // lv_screen_load(ui_Screen2);
            lv_screen_load_anim(ui_Screen2, LV_SCR_LOAD_ANIM_MOVE_LEFT, 100, 0, false);
            lv_chart_refresh(ui_Screen2_Chart_CO2); // Принудительное обновление графика
        } else if (input.equalsIgnoreCase("screen1")) { // Возврат на главный экран
            currentState = MAIN_SCREEN;
            // lv_screen_load(ui_Screen1);
            lv_screen_load_anim(ui_Screen1, LV_SCR_LOAD_ANIM_MOVE_RIGHT, 100, 0, false);
        }
    }
}

