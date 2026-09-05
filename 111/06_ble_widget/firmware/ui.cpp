#include "ui.h"
#include "display.h"
#include "config.h"
#include <ArduinoJson.h>

// ============================================================
//  动态变色逻辑
// ============================================================
uint16_t usage_color(int v) {
  return v > 85 ? COLOR_RED : (v > 60 ? COLOR_YELLOW : COLOR_GREEN);
}

uint16_t temp_color(float t, float yellow_c, float orange_c, float red_c) {
  return t >= red_c ? COLOR_RED : (t >= orange_c ? COLOR_ORANGE :
         (t >= yellow_c ? COLOR_YELLOW : COLOR_CYAN));
}

uint16_t pwr_color(float w, float ref) {
  float r = w / ref;
  return r >= 0.75f ? COLOR_RED : (r >= 0.35f ? COLOR_ORANGE : COLOR_CYAN);
}

uint16_t fps_color(int fps) {
  return fps >= 120 ? COLOR_GREEN : (fps >= 60 ? COLOR_CYAN : (fps >= 30 ? COLOR_YELLOW : COLOR_RED));
}

void draw_bar(uint16_t x, uint16_t y, uint16_t w, uint16_t h, int percent, uint16_t color) {
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  uint16_t fill_w = (w * percent) / 100;
  if (fill_w > 0) fill_rect(x, y, fill_w, h, color);
  if (w - fill_w > 0) fill_rect(x + fill_w, y, w - fill_w, h, COLOR_DARK_GRAY);
}

void ui_show_waiting() {
  fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
  draw_text(16, 100, "BLE WAITING...", COLOR_CYAN);
}

void ui_clear() {
  fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
}

bool parse_dashboard_json(const String& jsonStr, DashboardData& outData) {
#if ARDUINOJSON_VERSION_MAJOR >= 7
  JsonDocument doc;
#else
  DynamicJsonDocument doc(512);
#endif
  DeserializationError error = deserializeJson(doc, jsonStr);
  if (error) return false;

  outData.fps = doc["fps"] | 0;
  outData.cpu = doc["cpu"] | 0;
  outData.mem = doc["mem"] | 0;
  outData.cpu_temp = doc["cpu_temp"] | 0.0f;
  outData.cpu_pwr = doc["cpu_pwr"] | 0.0f;
  outData.gpu_temp = doc["gpu_temp"] | 0.0f;
  outData.gpu_pwr = doc["gpu_pwr"] | 0.0f;
  outData.gpu_usage = doc["gpu_usage"] | -1;

  const char* m = doc["mode"] | "TURBO";
  strncpy(outData.mode, m, sizeof(outData.mode) - 1);
  outData.mode[sizeof(outData.mode) - 1] = '\0';

  const char* t = doc["time"] | "00:00:00";
  strncpy(outData.time, t, sizeof(outData.time) - 1);
  outData.time[sizeof(outData.time) - 1] = '\0';

  return true;
}

void ui_render_dashboard(const DashboardData& data) {
  // 1. 顶部 Header (y = 6 ~ 34)
  if (data.fps > 0) {
    // 游戏模式: 左侧时钟 (HH:MM)，右侧高亮 FPS
    char t_short[6];
    strncpy(t_short, data.time, 5);
    t_short[5] = '\0';
    draw_text(8, 6, t_short, COLOR_CYAN);

    char fps_str[10];
    snprintf(fps_str, sizeof(fps_str), "%3d FPS", data.fps > 999 ? 999 : data.fps);
    draw_text(178, 6, fps_str, fps_color(data.fps));
  } else {
    // 桌面模式: 左侧完整时钟 (HH:MM:SS)，右侧性能模式
    draw_text(8, 6, data.time, COLOR_CYAN);

    char mode_str[8];
    snprintf(mode_str, sizeof(mode_str), "%-5s", data.mode);
    uint16_t mode_color = strcmp(data.mode, "TURBO") == 0 ? COLOR_RED :
                          (strcmp(data.mode, "FULL") == 0 ? COLOR_ORANGE : COLOR_GREEN);
    draw_text(216, 6, mode_str, mode_color);
  }
  fill_rect(8, 38, 304, 2, COLOR_DIVIDER); // 科技蓝细分割线

  // 2. CPU 区域 (y = 44 ~ 90)
  draw_text(8, 44, "CPU", COLOR_WHITE);
  char cpu_str[8];
  snprintf(cpu_str, sizeof(cpu_str), "%2d%%", data.cpu > 99 ? 99 : (data.cpu < 0 ? 0 : data.cpu));
  draw_text(74, 44, cpu_str, usage_color(data.cpu));

  char cpu_tstr[8];
  float valid_cpu_t = (data.cpu_temp > 0.0f && data.cpu_temp < 125.0f) ? data.cpu_temp : 0.0f;
  // 采用 %2.0fC 格式（如 45C，共3字符*19px=57px），与右侧 218px 列对齐保持整齐
  snprintf(cpu_tstr, sizeof(cpu_tstr), "%2.0fC", valid_cpu_t > 99 ? 99 : valid_cpu_t);
  draw_text(146, 44, cpu_tstr, temp_color(valid_cpu_t, 70, 78, 88));

  char cpu_wstr[8];
  float valid_cpu_w = (data.cpu_pwr >= 0.0f && data.cpu_pwr <= 300.0f) ? data.cpu_pwr : 0.0f;
  snprintf(cpu_wstr, sizeof(cpu_wstr), "%3.0fW", valid_cpu_w > 999 ? 999 : valid_cpu_w);
  draw_text(218, 44, cpu_wstr, pwr_color(valid_cpu_w, CPU_PWR_REF_W));
  draw_bar(8, 76, 304, 12, data.cpu, usage_color(data.cpu));

  // 3. GPU 区域 (y = 96 ~ 142)
  draw_text(8, 96, "GPU", COLOR_WHITE);
  if (data.gpu_usage >= 0) {
    char gpu_str[8];
    snprintf(gpu_str, sizeof(gpu_str), "%2d%%", data.gpu_usage > 99 ? 99 : data.gpu_usage);
    draw_text(74, 96, gpu_str, usage_color(data.gpu_usage));
  } else {
    fill_rect(74, 96, 57, 28, COLOR_BLACK);
  }

  char gpu_tstr[8];
  float valid_gpu_t = (data.gpu_temp > 0.0f && data.gpu_temp < 125.0f) ? data.gpu_temp : 0.0f;
  snprintf(gpu_tstr, sizeof(gpu_tstr), "%2.0fC", valid_gpu_t > 99 ? 99 : valid_gpu_t);
  draw_text(146, 96, gpu_tstr, temp_color(valid_gpu_t, 60, 75, 85));

  char gpu_wstr[8];
  float valid_gpu_w = (data.gpu_pwr >= 0.0f && data.gpu_pwr <= 400.0f) ? data.gpu_pwr : 0.0f;
  snprintf(gpu_wstr, sizeof(gpu_wstr), "%3.0fW", valid_gpu_w > 999 ? 999 : valid_gpu_w);
  draw_text(218, 96, gpu_wstr, pwr_color(valid_gpu_w, GPU_PWR_REF_W));
  int bar_val = (data.gpu_usage >= 0) ? data.gpu_usage : (int)(valid_gpu_w / GPU_PWR_REF_W * 100.0f);
  draw_bar(8, 128, 304, 12, bar_val, (data.gpu_usage >= 0) ? usage_color(data.gpu_usage) : pwr_color(valid_gpu_w, GPU_PWR_REF_W));

  // 4. MEM 区域 (y = 148 ~ 194) (极简清爽，无风扇干扰)
  draw_text(8, 148, "MEM", COLOR_WHITE);
  char mem_str[8];
  snprintf(mem_str, sizeof(mem_str), "%2d%%", data.mem > 99 ? 99 : (data.mem < 0 ? 0 : data.mem));
  draw_text(74, 148, mem_str, usage_color(data.mem));
  fill_rect(131, 148, 181, 28, COLOR_BLACK); // 清除右侧区域
  draw_bar(8, 180, 304, 12, data.mem, usage_color(data.mem));
}

