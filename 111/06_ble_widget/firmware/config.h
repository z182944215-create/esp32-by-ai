#pragma once
#include <Arduino.h>

// ============================================================
//  硬件引脚定义 (ST7789 屏幕 SPI 引脚)
// ============================================================
#define PIN_CS    4
#define PIN_DC   16
#define PIN_RST  17
#define PIN_MOSI 23
#define PIN_SCK  18

// 屏幕分辨率与显示尺寸
#define SCREEN_WIDTH  320
#define SCREEN_HEIGHT 240

#include "soc/gpio_struct.h"

// 极速 GPIO 寄存器宏（直接操作 ESP32 寄存器加速 SPI 模拟时钟与数据）
#define W1TS_REG (GPIO.out_w1ts)
#define W1TC_REG (GPIO.out_w1tc)
#define SCK_BIT  (1u << PIN_SCK)
#define MOSI_BIT (1u << PIN_MOSI)

// 功耗与显示基准常量 (用于动态颜色与进度条比例换算)
#define CPU_PWR_REF_W  160.0f
#define GPU_PWR_REF_W  150.0f

// ============================================================
//  低功耗蓝牙 (BLE) 参数配置
// ============================================================
#define BLE_DEVICE_NAME     "ESP32-Dashboard"
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// 常用 16-bit RGB565 颜色定义
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_CYAN        0x07FF
#define COLOR_YELLOW      0xFFE0
#define COLOR_ORANGE      0xFDA0
#define COLOR_DARK_GRAY   0x18E3
#define COLOR_DIVIDER     0x31A6
