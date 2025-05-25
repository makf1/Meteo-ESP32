# Метеостанция(meteo-ESP32-TFT_eSPI) - LVLG-version !!!

**Проект домашней метеостанции, который считывает данные о температуре, влажности и CO2 с датчиков BME280 и CCS811; отображает данные с помощью LVGL на круглом дисплее. Данные с датчиков обновляются в режиме реального времени с помощью графического пользовательского интерфейса (ГПИ), работающего во встроенной системе.**

## Экраны
На данный момент (24.05) имеется 3 экрана:
- Стартовый 
- Основной
- Экран графика CO2

*Переход между основным и экраном графика CO2 производится плавным свайпом, НО пока наблюдаются фризы при переходе.*

## Фото работы дисплея
<img src="img/main.jpg" width="750" height="750"> <img src="img/co2_chart.jpg" width="750" height="750">

## Железо
- ESP-WROM-32 (ESP32 Devkit v1);
- BME280;
- CCS811;
- TFT 1.28inch Round GC9A01

## Зависимости
- [GyverBME280](https://github.com/GyverLibs/GyverBME280.git) //v1.5.3
- [Sparkfun_CCS811](https://github.com/sparkfun/SparkFun_CCS811_Arduino_Library.git) //v2.0.3
- [FastBot2](https://github.com/GyverLibs/FastBot2.git) 
- [TFT_eSPI](https://github.com/Bodmer/TFT_eSPI.git) //v2.5.43
- [LVGL](https://github.com/lvgl/lvgl.git) //v9.1.0

## Распиновка
| GC9A01 Pin | ESP32 Pin | Description |
| --- | --- | --- |
| VCC | 3.3V | Power supply |
| GND | GND | Ground |
| CS | GPIO 15 | Chip Select |
| RST | GPIO 4 | Reset |
| DC (RS) | GPIO 2 | Data/Command |
| SDI (MOSI) | GPIO 23 | SPI Master Out Slave In |
| SCK(CLK) | GPIO 18 | SPI Clock |

## Версии
- v1.0
