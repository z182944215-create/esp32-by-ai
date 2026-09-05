#pragma once
#include <Arduino.h>
#include <stdint.h>

// ============================================================
//  副屏监控数据结构
// ============================================================
struct DashboardData {
  int fps;
  int cpu;
  int mem;
  float cpu_temp;
  float cpu_pwr;
  float gpu_temp;
  float gpu_pwr;
  int gpu_usage;
  char mode[16];
  char time[16];

  DashboardData() {
    fps = 0;
    cpu = 0;
    mem = 0;
    cpu_temp = 0.0f;
    cpu_pwr = 0.0f;
    gpu_temp = 0.0f;
    gpu_pwr = 0.0f;
    gpu_usage = -1;
    strncpy(mode, "TURBO", sizeof(mode));
    strncpy(time, "00:00:00", sizeof(time));
  }
};

// ============================================================
//  动态颜色与进度条接口
// ============================================================
uint16_t usage_color(int v);
uint16_t temp_color(float t, float yellow_c, float orange_c, float red_c);
uint16_t pwr_color(float w, float ref);
uint16_t fps_color(int fps);
void draw_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int percent, uint16_t color);

// ============================================================
//  界面显示与数据解析接口
// ============================================================
// 待连接等待界面
void ui_show_waiting();

// 清空整个屏幕
void ui_clear();

// 渲染副屏仪表盘完整内容
void ui_render_dashboard(const DashboardData& data);

// 解析接收到的 JSON 字符串填充数据结构
bool parse_dashboard_json(const String& jsonStr, DashboardData& outData);
