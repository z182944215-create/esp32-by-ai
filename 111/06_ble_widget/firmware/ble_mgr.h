#pragma once
#include <Arduino.h>

// ============================================================
//  低功耗蓝牙 (BLE) 管理服务接口
// ============================================================

// 初始化 BLE 设备、创建 GATT 服务与特征值、启动广播
void ble_init();

// 检查当前是否有设备连接
bool ble_is_connected();

// 检查是否有新的推流数据到来
bool ble_has_new_data();

// 获取接收到的数据字符串
String ble_get_data();

// 清除新数据就绪标志
void ble_clear_new_data();

// 处理连接状态变化（如断开重连时自动恢复广播并更新界面状态）
void ble_update_status();
