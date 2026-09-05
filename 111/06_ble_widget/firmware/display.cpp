#include "display.h"
#include "config.h"
#include "dseg_font.h"

// 极速 GPIO 模拟 SPI 输出单字节
inline void spi_write_inline(uint8_t data) {
  W1TC_REG = SCK_BIT; if (data & 0x80) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x40) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x20) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x10) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x08) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x04) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x02) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
  W1TC_REG = SCK_BIT; if (data & 0x01) W1TS_REG = MOSI_BIT; else W1TC_REG = MOSI_BIT; W1TS_REG = SCK_BIT;
}

void spi_write(uint8_t data) {
  spi_write_inline(data);
}

void write_cmd(uint8_t cmd) {
  digitalWrite(PIN_DC, LOW);
  digitalWrite(PIN_CS, LOW);
  spi_write_inline(cmd);
  digitalWrite(PIN_CS, HIGH);
}

void write_data(uint8_t data) {
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  spi_write_inline(data);
  digitalWrite(PIN_CS, HIGH);
}

void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1) {
  write_cmd(0x2A);
  write_data(x0 >> 8);
  write_data(x0 & 0xFF);
  write_data(x1 >> 8);
  write_data(x1 & 0xFF);

  write_cmd(0x2B);
  write_data(y0 >> 8);
  write_data(y0 & 0xFF);
  write_data(y1 >> 8);
  write_data(y1 & 0xFF);

  write_cmd(0x2C);
}

void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (w == 0 || h == 0) return;
  set_window(x, y, x + w - 1, y + h - 1);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;
  uint32_t count = (uint32_t)w * h;
  for (uint32_t i = 0; i < count; i++) {
    spi_write_inline(hi);
    spi_write_inline(lo);
  }
  digitalWrite(PIN_CS, HIGH);
}

void draw_char(uint16_t x, uint16_t y, const uint32_t glyph[], uint16_t color) {
  set_window(x, y, x + DSEG_CELL_W - 1, y + DSEG_CELL_H - 1);
  digitalWrite(PIN_DC, HIGH);
  digitalWrite(PIN_CS, LOW);
  uint8_t hi = color >> 8;
  uint8_t lo = color & 0xFF;
  for (int row = 0; row < DSEG_CELL_H; row++) {
    uint32_t line = glyph[row];
    for (int col = 0; col < DSEG_CELL_W; col++) {
      if (line & (0x80000000u >> col)) {
        spi_write_inline(hi);
        spi_write_inline(lo);
      } else {
        spi_write_inline(0x00);
        spi_write_inline(0x00);
      }
    }
  }
  digitalWrite(PIN_CS, HIGH);
}

void draw_text(uint16_t x, uint16_t y, const char* str, uint16_t color) {
  uint16_t cur_x = x;
  while (*str) {
    uint8_t adv = 0;
    const uint32_t* glyph = dseg_glyph(*str++, &adv);
    if (glyph) {
      draw_char(cur_x, y, glyph, color);
    } else {
      fill_rect(cur_x, y, adv, DSEG_CELL_H, COLOR_BLACK);
    }
    cur_x += adv;
  }
}

void display_init() {
  pinMode(PIN_CS, OUTPUT);
  pinMode(PIN_DC, OUTPUT);
  pinMode(PIN_RST, OUTPUT);
  pinMode(PIN_MOSI, OUTPUT);
  pinMode(PIN_SCK, OUTPUT);

  // 硬件复位屏幕
  digitalWrite(PIN_RST, HIGH); delay(50);
  digitalWrite(PIN_RST, LOW);  delay(100);
  digitalWrite(PIN_RST, HIGH); delay(150);

  // 180度横屏配置 (0xE8)
  write_cmd(0x01); delay(120);
  write_cmd(0x11); delay(120);
  write_cmd(0x36); write_data(0xE8);
  write_cmd(0x3A); write_data(0x55);
  write_cmd(0x29); delay(50);

  // 初始化清屏
  fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
}
