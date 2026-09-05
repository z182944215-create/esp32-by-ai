# ESP32 低功耗蓝牙 (BLE) 桌面副屏 · 华硕 G-Helper & 游戏 FPS 增强版

本方案为【低功耗蓝牙 (BLE)】独立版本，深度对接华硕 G-Helper 体系，支持游戏实时帧率 (FPS) 采集与硬件全维监控。

---

## 📸 核心特性

- 🎮 **游戏实时帧率 (FPS)**：
  - 使用与 G-Helper 相同的 Windows ETW 机制，同时捕获 DXGI Present 与 DxgKrnl Flip 事件。
  - 帧率只读取 G-Helper 同款 ETW 数据，不依赖 RTSS 或 AIDA64。
  - 游戏时顶部高亮实时显示 `144 FPS`（动态变色），桌面时平滑切换为 G-Helper 性能模式（`TURBO`/`BALANCED`）或时钟。
- 🚀 **华硕 G-Helper 深度对接**：
  - **ASUS ATKACPI (`\\.\ATKACPI`)**：直读笔记本 EC 硬件寄存器（CPU 温度、风扇转速、性能模式）。
  - **NVIDIA NVML (`nvml.dll`)**：底层 C 接口直读独显 GPU 温度、实时功耗 (W)、GPU 占用率。
  - **Windows Energy Meter RAPL**：硬件级 CPU Package 功耗直读。
- ⚡ **超低功耗与极速刷新**：
  - 蓝牙功耗仅为 Wi-Fi 的 1/5 ~ 1/10，0.3s 极速 GATT 数据推流，无需连接路由器。

---

## 📁 目录文件清单

### ESP32 模块化固件源码 (`firmware/`)：
* `firmware/firmware.ino`：主控调度程序（协调屏幕初始化、蓝牙状态与推流数据分发）。
* `firmware/config.h`：系统引脚配置（ST7789 SPI 引脚、屏幕分辨率、BLE 服务 UUID、颜色常量）。
* `firmware/dseg_font.h`：19x28 JetBrains Mono Bold 高清位图字模库与字符映射函数。
* `firmware/display.h` / `display.cpp`：屏幕底层驱动（硬件复位、寄存器级极速 SPI、窗口定位与点/矩形/文字绘制）。
* `firmware/ui.h` / `ui.cpp`：UI 渲染引擎（动态变色算法、进度条渲染、JSON 数据包解析与副屏各区域排版）。
* `firmware/ble_mgr.h` / `ble_mgr.cpp`：蓝牙服务管理（GATT Server、事件回调、连接/断开检测与断线自动重新广播）。

### 电脑端上位机服务：
* `ble_pc_sender.py`：电脑端 Python 蓝牙发射器（ATKACPI + NVML + RAPL + ETW FPS + BLE 推流）。
* `tools/EtwFpsHelper/`：与 G-Helper 同机制的 DXGI + DxgKrnl ETW 帧率采集器。
* `一键启动_BLE.bat`：Windows 一键提权启动脚本。
* `tools/`：字体生成与效果预览工具。

---

## 🎨 动态配色规则

| 指标 | 绿色/青色（正常） | 黄色/橙色（中高负载） | 红色（极限/危险） |
| :--- | :--- | :--- | :--- |
| **游戏 FPS** | ≥120 FPS（荧光绿） / ≥60 FPS（青色） | 30~59 FPS（黄色） | <30 FPS（红色） |
| **CPU / GPU / MEM 占用率** | ≤60% 绿色 | 61~85% 黄色 | >85% 红色 |
| **CPU 温度** | <70°C 青色 | 70~87°C 黄橙 | ≥88°C 红色 |
| **GPU 温度** | <60°C 青色 | 60~84°C 黄橙 | ≥85°C 红色 |
| **CPU 功耗 (160W标定)** | <56W 青色 | 56~119W 橙色 | ≥120W 红色 |
| **GPU 功耗 (150W标定)** | <52W 青色 | 52~112W 橙色 | ≥113W 红色 |

---

## 🚀 使用步骤

### 1. 烧录 ESP32 固件
**方法 A：使用 Arduino IDE**
1. 打开 Arduino IDE。
2. 打开 `06_ble_widget/firmware/firmware.ino`。
3. 编译并上传到开发板。屏幕显示 `BLE WAITING...` 等待连接。

**方法 B：使用 PlatformIO**
1. 在项目根目录终端执行 `pio run -t upload` 一键编译并烧录。

### 2. 电脑端一键启动
1. 确保电脑已开启蓝牙。
2. 双击运行 `06_ble_widget/一键启动_BLE.bat`（会自动申请管理员权限以启用华硕 EC 硬件直读与 ETW 游戏 FPS 监控）。
3. 脚本将自动搜寻并连接 `ESP32-Dashboard`，副屏随即开启监控！
