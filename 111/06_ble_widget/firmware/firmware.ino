// ============================================================
// 阶段 6：低功耗蓝牙 (BLE) 桌面副屏（华硕 G-Helper + 游戏 FPS + 19x28 黄金大字模）
// 架构：模块化设计（配置/字库/屏幕驱动/界面渲染/蓝牙服务解耦）
// ============================================================

#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "ui.h"
#include "ble_mgr.h"

void setup() {
  Serial.begin(115200);
  delay(300);

  // 1. 初始化屏幕硬件与引脚，显示等待连接提示
  display_init();
  ui_show_waiting();

  // 2. 初始化蓝牙 GATT 服务与广播
  ble_init();

  Serial.println("[SYSTEM] ESP32 蓝牙副屏就绪！");
}

void loop() {
  // 1. 处理蓝牙连接状态与自动重新广播
  ble_update_status();

  // 2. 检查并处理收到的蓝牙推流数据
  if (ble_has_new_data()) {
    String jsonPayload = ble_get_data();
    ble_clear_new_data();

    DashboardData data;
    if (parse_dashboard_json(jsonPayload, data)) {
      ui_render_dashboard(data);
    }
  }

  delay(10);
}
