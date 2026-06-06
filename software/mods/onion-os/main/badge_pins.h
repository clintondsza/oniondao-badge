#pragma once

#define PIN_PWR         18
#define PIN_SE_EN        8

#define PIN_SCL          9
#define PIN_SDA         10

#define PIN_EPD_MOSI    17
#define PIN_EPD_SCK     11
#define PIN_EPD_CS      12
#define PIN_EPD_DC      13
#define PIN_EPD_RST     14
#define PIN_EPD_BUSY    21

#define PIN_BTN_IRQ      1

// Optional LiPo sense input. The current board pinout does not route BAT/VSYS
// to an ESP32 ADC pin, so leave disabled unless a hardware divider is added.
#ifndef PIN_BATTERY_ADC
#define PIN_BATTERY_ADC -1
#endif
