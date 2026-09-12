// Pin map for the Elecrow CrowPanel 2.1" HMI ESP32 Rotary Display (DHE03921D).
// Source: Elecrow RotaryScreen_2_1.ino — see docs/crowpanel-2.1-rotary.md
#pragma once

#define LCD_W 480
#define LCD_H 480

// ST7701 init over 9-bit software SPI
#define LCD_SPI_CS  16
#define LCD_SPI_SCK 2
#define LCD_SPI_SDA 1

// 16-bit RGB parallel bus
#define LCD_DE    40
#define LCD_VSYNC 7
#define LCD_HSYNC 15
#define LCD_PCLK  41
#define LCD_R0 46
#define LCD_R1 3
#define LCD_R2 8
#define LCD_R3 18
#define LCD_R4 17
#define LCD_G0 14
#define LCD_G1 13
#define LCD_G2 12
#define LCD_G3 11
#define LCD_G4 10
#define LCD_G5 9
#define LCD_B0 5
#define LCD_B1 45
#define LCD_B2 48
#define LCD_B3 47
#define LCD_B4 21

#define LCD_BL 6       // backlight, LEDC PWM
#define LED_PIN 43     // ambient "breathing" LED, LEDC PWM

// I2C: CST8xx touch (0x15) + PCF8574 expander (0x21)
#define I2C_SDA 38
#define I2C_SCL 39
#define PCF8574_ADDR 0x21
#define TOUCH_ADDR   0x15

// PCF8574 bits
#define PCF_TP_RST   0
#define PCF_TP_INT   2
#define PCF_LCD_PWR  3
#define PCF_LCD_RST  4
#define PCF_ENC_BTN  5   // active low, needs pull-up (write 1)

// Rotary encoder
#define ENC_A 42
#define ENC_B 4
