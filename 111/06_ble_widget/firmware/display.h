#pragma once
#include <Arduino.h>
#include <stdint.h>

// ============================================================
//  屏幕驱动与基本图元绘制接口
// ============================================================

// 屏幕初始化（GPIO配置、硬件复位、180度横屏配置、清屏）
void display_init();

// 底层 SPI 写入
void spi_write(uint8_t data);
void write_cmd(uint8_t cmd);
void write_data(uint8_t data);

// 设置显示绘制窗口 [x0, y0] 到 [x1, y1]
void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);

// 填充单色矩形区域
void fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);

// 绘制单字符字模 (19x28)
void draw_char(uint16_t x, uint16_t y, const uint32_t glyph[], uint16_t color);

// 绘制字符串（自动查字模与空格处理）
void draw_text(uint16_t x, uint16_t y, const char* str, uint16_t color);
