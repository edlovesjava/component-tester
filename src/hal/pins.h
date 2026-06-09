#pragma once

// I2C (ADS1115)
#define PIN_I2C_SDA  21
#define PIN_I2C_SCL  22

// TFT SPI (VSPI)
#define PIN_TFT_SCLK 18
#define PIN_TFT_MOSI 23
#define PIN_TFT_MISO 19
#define PIN_TFT_CS    5
#define PIN_TFT_DC   17
#define PIN_TFT_RST  16

// Test pins — 680 Ω drive
#define PIN_T1_LO   4
#define PIN_T2_LO  14
#define PIN_T3_LO  26

// Test pins — 470 kΩ sense
#define PIN_T1_HI  13
#define PIN_T2_HI  27
#define PIN_T3_HI  25

// PWM outputs → RC filter
#define PIN_PWM_BASE      32
#define PIN_PWM_COLLECTOR 33

// ADS1115 I2C address
#define ADS1115_ADDR 0x48
