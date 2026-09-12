#include "board.h"
#include <Wire.h>
#include "board_pins.h"

// Elecrow's example swaps the red/blue RGB565 fields for this ST7701 wiring. Doing the swap by
// feeding the B pins into the R slots costs nothing per frame. Set to 0 if colours come out wrong.
#ifndef LCD_SWAP_RB
#define LCD_SWAP_RB 1
#endif

#if LCD_SWAP_RB
#define P_R0 LCD_B0
#define P_R1 LCD_B1
#define P_R2 LCD_B2
#define P_R3 LCD_B3
#define P_R4 LCD_B4
#define P_B0 LCD_R0
#define P_B1 LCD_R1
#define P_B2 LCD_R2
#define P_B3 LCD_R3
#define P_B4 LCD_R4
#else
#define P_R0 LCD_R0
#define P_R1 LCD_R1
#define P_R2 LCD_R2
#define P_R3 LCD_R3
#define P_R4 LCD_R4
#define P_B0 LCD_B0
#define P_B1 LCD_B1
#define P_B2 LCD_B2
#define P_B3 LCD_B3
#define P_B4 LCD_B4
#endif

static Arduino_DataBus *initBus = new Arduino_SWSPI(
    GFX_NOT_DEFINED /* DC: 9-bit SPI */, LCD_SPI_CS, LCD_SPI_SCK, LCD_SPI_SDA, GFX_NOT_DEFINED);

static Arduino_ESP32RGBPanel *rgbPanel = new Arduino_ESP32RGBPanel(
    LCD_DE, LCD_VSYNC, LCD_HSYNC, LCD_PCLK,
    P_R0, P_R1, P_R2, P_R3, P_R4,
    LCD_G0, LCD_G1, LCD_G2, LCD_G3, LCD_G4, LCD_G5,
    P_B0, P_B1, P_B2, P_B3, P_B4,
    1 /* hsync polarity */, 10 /* hfp */, 4 /* hpw */, 20 /* hbp */,
    1 /* vsync polarity */, 10 /* vfp */, 4 /* vpw */, 20 /* vbp */,
    0 /* pclk active neg */, 12000000 /* pclk */, false /* big endian */,
    0 /* de idle high */, 0 /* pclk idle high */,
    LCD_W * 20 /* bounce buffer px */);

Arduino_RGB_Display *gfx = new Arduino_RGB_Display(
    LCD_W, LCD_H, rgbPanel, 0 /* rotation */, true /* auto flush */,
    initBus, GFX_NOT_DEFINED /* RST via PCF8574 */,
    st7701_type5_init_operations, sizeof(st7701_type5_init_operations));

// PCF8574: quasi-bidirectional, so inputs are "written high". Keep a shadow of the outputs.
static uint8_t pcfShadow = 0xFF;

static void pcfWrite(uint8_t bit, bool high) {
  if (high) pcfShadow |= (1 << bit); else pcfShadow &= ~(1 << bit);
  Wire.beginTransmission(PCF8574_ADDR);
  Wire.write(pcfShadow);
  Wire.endTransmission();
}

static uint8_t pcfRead() {
  if (Wire.requestFrom((uint8_t)PCF8574_ADDR, (uint8_t)1) != 1) return 0xFF;
  return Wire.read();
}

bool boardButtonPressed() { return !(pcfRead() & (1 << PCF_ENC_BTN)); }

void boardBacklight(uint8_t pct) { ledcWrite(LCD_BL, (uint32_t)pct * 255 / 100); }
void boardLed(uint8_t pct)       { ledcWrite(LED_PIN, (uint32_t)pct * 255 / 100); }

void boardInit() {
  Wire.begin(I2C_SDA, I2C_SCL, 400000);
  ledcAttach(LCD_BL, 1000, 8);
  ledcAttach(LED_PIN, 1000, 8);
  boardBacklight(0);
  boardLed(0);

  // Power/reset sequence from Elecrow's reference sketch.
  pcfWrite(PCF_ENC_BTN, true);       // input, pulled up
  pcfWrite(PCF_LCD_PWR, true);
  pcfWrite(PCF_LCD_RST, true);  delay(20);
  pcfWrite(PCF_LCD_RST, false); delay(20);
  pcfWrite(PCF_LCD_RST, true);  delay(120);
  pcfWrite(PCF_TP_RST, true);   delay(10);
  pcfWrite(PCF_TP_RST, false);  delay(10);
  pcfWrite(PCF_TP_RST, true);   delay(50);
  pcfWrite(PCF_TP_INT, true);

  if (!gfx->begin()) Serial.println("[board] gfx->begin() failed");
  gfx->fillScreen(0x0000);
  boardBacklight(80);
}
