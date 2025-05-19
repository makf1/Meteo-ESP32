//////////////////////////////////////////////////////////////////////////////
// Главный файл прошивки для метеостанции

#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include "ui.h"
#include <GyverBME280.h>
#include <SparkFunCCS811.h>

// =================== Конфигурация оборудования ===================
#define I2C_SDA 21         // Пин I2C SDA
#define I2C_SCL 22         // Пин I2C SCL
#define CCS811_WAKE_PIN -1 // Пин пробуждения CCS811 (не используется)

// ================= Глобальные объекты и переменные =================
TFT_eSPI tft = TFT_eSPI(); // Объект дисплея
GyverBME280 bme;           // Датчик BME280 (T/H/P)
CCS811 ccs811(0x5A);       // Датчик CO2/TVOC (адрес 0x5A)

// Таймеры и интервалы опроса
uint32_t sensorUpdateTimer = 0;                // Таймер последнего обновления
const uint16_t SENSOR_UPDATE_INTERVAL = 2000;  // Интервал опроса датчиков (2 сек)

// Буферы для строковых представлений данных
char tempStr[20];   // Температура
char humStr[20];    // Влажность
char presStr[20];   // Давление
char co2Str[20];    // CO2
char tvocStr[20];   // ЛОС (TVOC)

bool sensorsError = false; // Флаг ошибки датчиков

// Объявления объектов LVGL из UI
extern lv_obj_t* ui_Screen1_Arc_Temp;  // Дуга температуры
extern lv_obj_t* ui_Screen1_Arc_Hum;   // Дуга влажности
extern lv_obj_t* ui_Screen1_Arc_Press; // Дуга давления

// ===================== Прототипы функций =====================
void initDisplay();         // Инициализация дисплея и LVGL
void initSensors();         // Инициализация датчиков
void updateSensorData();    // Чтение данных с датчиков
void updateUI();            // Обновление интерфейса
void lvglTick(void*);       // Системный тик LVGL

// ======================= Настройка =======================
void setup() {
  Serial.begin(115200);
  Serial.println("Initializing system...");

  // Инициализация I2C
  Wire.begin(I2C_SDA, I2C_SCL);
  
  // Инициализация периферии
  initDisplay();
  initSensors();

  // Настройка таймера для LVGL (обновление каждые 5 мс)
  const uint32_t lvglTickPeriod = 5;
  esp_timer_create_args_t lvglTimerArgs = {
    .callback = &lvglTick,
    .name = "lvgl_timer"
  };
  esp_timer_handle_t lvglTimer;
  esp_timer_create(&lvglTimerArgs, &lvglTimer);
  esp_timer_start_periodic(lvglTimer, lvglTickPeriod * 1000);

  // Инициализация пользовательского интерфейса
  ui_init();
}

// ==================== Основной цикл ====================
void loop() {
  lv_timer_handler(); // Обработчик событий LVGL

  // Обновление данных по таймеру
  if(millis() - sensorUpdateTimer >= SENSOR_UPDATE_INTERVAL){
    sensorUpdateTimer = millis();
    updateSensorData(); // Получение новых данных
    updateUI();         // Обновление интерфейса
  }
  delay(5); // Небольшая задержка для стабильности
}

// ================ Инициализация дисплея ================
void initDisplay() {
  tft.begin();         // Инициализация TFT
  tft.setRotation(0);  // Ориентация дисплея
  lv_init();           // Инициализация LVGL

  // Настройка буфера LVGL (частичная перерисовка)
  static lv_color_t buf[TFT_HEIGHT * TFT_WIDTH / 10];
  lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
  lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);

  // Регистрация функции отрисовки
  lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;
    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t *)px_map, w * h, true);
    tft.endWrite();
    lv_display_flush_ready(disp);
  });
}

// ================ Инициализация датчиков ================
void initSensors() {
  // Инициализация BME280
  if(!bme.begin()) {
    Serial.println("BME280 error!");
    sensorsError = true;
  }

  // Инициализация CCS811
  if(!ccs811.begin()) {
    Serial.println("CCS811 error!");
    sensorsError = true;
  } else {
    ccs811.setDriveMode(1); // Режим измерения каждую секунду
  }
}

// ================ Обновление данных датчиков ================
void updateSensorData() {
  if(sensorsError) return; // Выход при ошибке

  // Чтение данных с BME280
  float temperature = bme.readTemperature();      // Температура (°C)
  float humidity = bme.readHumidity();            // Влажность (%)
  float pressure = bme.readPressure() / 100.0F;   // Давление (гПа)

  // Чтение данных с CCS811
  if(ccs811.dataAvailable()) {
    ccs811.readAlgorithmResults();
    snprintf(co2Str, sizeof(co2Str), "CO2: %d ppm", ccs811.getCO2());
    snprintf(tvocStr, sizeof(tvocStr), "TVOC: %d ppb", ccs811.getTVOC());
  }
  
  // Форматирование строк для отображения
  snprintf(tempStr, sizeof(tempStr), "%.1f C", temperature);
  snprintf(humStr, sizeof(humStr), "%.1f %%", humidity);
  snprintf(presStr, sizeof(presStr), "%.1f hPa", pressure);
}

// ================= Обновление интерфейса =================
void updateUI() {
  // Обновление текстовых меток
  lv_label_set_text(ui_Screen1_Label_Temp, tempStr);
  lv_label_set_text(ui_Screen1_Label_Hum, humStr);
  lv_label_set_text(ui_Screen1_Label_Press, presStr);

  // Обновление дуг с анимацией
  lv_arc_set_value(ui_Screen1_Arc_Temp, (int)bme.readTemperature());     // 0-50°C
  lv_arc_set_value(ui_Screen1_Arc_Hum, (int)bme.readHumidity());         // 0-100%
  lv_arc_set_value(ui_Screen1_Arc_Press, (int)(bme.readPressure()/100)); // 0-1100 гПа

  /* Примечание: Для плавной анимации можно использовать:
     lv_anim_set_values(&a, current_val, new_val);
     lv_anim_start(&a);
  */
}

// ============== Системный тик для LVGL ==============
void lvglTick(void*) {
  lv_tick_inc(5); // Обновление системного времени LVGL
}


//////////////////////////////////////////////////////////////////////////////

// #include <Arduino.h>
// #include <Wire.h>
// #include <TFT_eSPI.h>
// #include <lvgl.h>
// #include "ui.h"
// #include <GyverBME280.h>
// #include <SparkFunCCS811.h>

// // Определения пинов I2C
// #define I2C_SDA 21
// #define I2C_SCL 22
// #define CCS811_WAKE_PIN -1

// // Глобальные объекты
// TFT_eSPI tft = TFT_eSPI();
// GyverBME280 bme;
// CCS811 ccs811(0x5A);

// // Таймеры обновления
// uint32_t sensorUpdateTimer = 0;
// const uint16_t SENSOR_UPDATE_INTERVAL = 2000;

// // Буферы для строк
// char tempStr[20];
// char humStr[20];
// char presStr[20];
// char co2Str[20];
// char tvocStr[20];

// // Флаг ошибки датчиков
// bool sensorsError = false;

// // Прототипы функций
// void initDisplay();
// void initSensors();
// void updateSensorData();
// void updateUI();
// void lvglTick(void*);

// //==================================================
// // Настройка периферии
// //==================================================
// void setup() {
//   Serial.println("setup start");
//   Serial.begin(115200);
//   Wire.begin(I2C_SDA, I2C_SCL);
  
//   initDisplay();
//   initSensors();

//   // Таймер для LVGL
//   const uint32_t lvglTickPeriod = 5; // Изменено на uint32_t
//   esp_timer_create_args_t lvglTimerArgs = {
//     .callback = &lvglTick,
//     .name = "lvgl_timer"
//   };
//   esp_timer_handle_t lvglTimer;
//   esp_timer_create(&lvglTimerArgs, &lvglTimer);
//   esp_timer_start_periodic(lvglTimer, lvglTickPeriod * 1000);

//   ui_init();
// }

// //==================================================
// // Основной цикл
// //==================================================
// void loop() {
//   lv_timer_handler();
  
//   if(millis() - sensorUpdateTimer >= SENSOR_UPDATE_INTERVAL){
//     sensorUpdateTimer = millis();
//     updateSensorData();
//     updateUI();
//   }
//   delay(5);
// }

// //==================================================
// // Инициализация дисплея (LVGL 9.1.0)
// //==================================================
// void initDisplay() {
//   tft.begin();
//   tft.setRotation(0);
//   Serial.println("Display init");

//   lv_init();
//   Serial.println("lv init");

//   // Инициализация буфера
//   static lv_color_t buf[TFT_HEIGHT * TFT_WIDTH / 10];
//   lv_display_t *disp = lv_display_create(TFT_WIDTH, TFT_HEIGHT);
//   lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
//   Serial.println("buffer init");

//   // Регистрация драйвера
//   lv_display_set_flush_cb(disp, [](lv_display_t *disp, const lv_area_t *area, uint8_t *px_map) {
//     uint32_t w = area->x2 - area->x1 + 1;
//     uint32_t h = area->y2 - area->y1 + 1;
//     tft.startWrite();
//     tft.setAddrWindow(area->x1, area->y1, w, h);
//     tft.pushColors((uint16_t *)px_map, w * h, true);
//     tft.endWrite();
//     lv_display_flush_ready(disp);
//   });
//   Serial.println("reg driver");
// }

// //==================================================
// // Инициализация датчиков
// //==================================================
// void initSensors() {
//   if(!bme.begin()) {
//     Serial.println("BME280 error!");
//     sensorsError = true;
//   }

//   if(!ccs811.begin()) {
//     Serial.println("CCS811 error!");
//     sensorsError = true;
//   } else {
//     ccs811.setDriveMode(1);
//   }
// }

// //==================================================
// // Обновление данных с датчиков
// //==================================================
// void updateSensorData() {
//   Serial.println("update sensor");
//   if(sensorsError) return;

//   // Исправленные методы BME280
//   float temperature = bme.readTemperature();
//   float humidity = bme.readHumidity();
//   float pressure = bme.readPressure() / 133.322;

//   if(ccs811.dataAvailable()) {
//     ccs811.readAlgorithmResults();
//     snprintf(co2Str, sizeof(co2Str), "CO2: %d ppm", ccs811.getCO2());
//     snprintf(tvocStr, sizeof(tvocStr), "TVOC: %d ppb", ccs811.getTVOC());
//   }
  
//   snprintf(tempStr, sizeof(tempStr), "%.1f C", temperature);
//   snprintf(humStr, sizeof(humStr), "%.1f %%", humidity);
//   snprintf(presStr, sizeof(presStr), "%.1f mmHg", pressure);
// }

// //==================================================
// // Обновление интерфейса
// //==================================================
// void updateUI() {
//   Serial.println("update ui")
//   // Проверьте фактические имена в ui.h!
//   lv_obj_t *tempLabel = ui_Screen1_Label_Temp;
//   lv_obj_t *humLabel = ui_Screen1_Label_Hum;
//   lv_obj_t *pressLabel = ui_Screen1_Label_Press;

//   lv_label_set_text(tempLabel, tempStr);
//   lv_label_set_text(humLabel, humStr);
//   lv_label_set_text(pressLabel, presStr);
// }

// //==================================================
// // Системный тик LVGL
// //==================================================
// void lvglTick(void*) {
//   Serial.println("lvgl tick");
//   lv_tick_inc(5);
// }
