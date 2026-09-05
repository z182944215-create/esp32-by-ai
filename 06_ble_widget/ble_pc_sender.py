# -*- coding: utf-8 -*-
"""
ESP32 华硕笔记本状态信息监控副屏 - 低功耗蓝牙 (BLE) 极速发射服务
特性：
1. 深度对接华硕 G-Helper 体系：直读 ASUS ATKACPI 嵌入式控制器（CPU温度、性能模式）
2. NVIDIA NVML 显卡底层直读：毫秒级采集 GPU 温度、实时功耗 (W)、GPU 占用率
3. Windows Energy Meter RAPL 采集 CPU Package 封装功耗 (W)
4. 游戏 FPS 引擎：使用与 G-Helper 相同的 DXGI + DxgKrnl ETW 采集机制
5. BLE 极速 GATT 广播推流，自动重连与无感恢复
"""

import asyncio
import ctypes
from ctypes import wintypes, byref, Structure
import json
import mmap
import os
import struct
import subprocess
import sys
import time
from typing import Any, cast

import psutil
from bleak import BleakScanner, BleakClient

# ==================== Win32 基础接口 ====================
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)

# 强制 UTF-8 输出
if sys.platform.startswith('win'):
    try:
        reconfigure = getattr(sys.stdout, "reconfigure", None)
        if reconfigure is not None:
            reconfigure(encoding="utf-8")
    except Exception:
        pass

# BLE UUID
SERVICE_UUID        = "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
CHARACTERISTIC_UUID = "beb5483e-36e1-4688-b7f5-ea07361b26a8"


# ==================== 1. 华硕 ASUS ATKACPI 直读 ====================
ATKACPI_DEVICE = r"\\.\ATKACPI"
CONTROL_CODE   = 0x0022240C
DSTS           = 0x53545344
INIT           = 0x54494E49
DEV_CPU_TEMP   = 0x00120094
DEV_PERF_MODE  = 0x00120075

kernel32.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD,
                                 ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
kernel32.CreateFileW.restype = wintypes.HANDLE
kernel32.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD, ctypes.c_void_p,
                                     wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
                                     ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
kernel32.DeviceIoControl.restype = wintypes.BOOL
kernel32.OpenFileMappingW.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.LPCWSTR]
kernel32.OpenFileMappingW.restype = wintypes.HANDLE
kernel32.CloseHandle.argtypes = [wintypes.HANDLE]
kernel32.CloseHandle.restype = wintypes.BOOL

class AsusAcpiReader:
    def __init__(self):
        self.handle = None
        self._init_handle()

    def _init_handle(self):
        try:
            self.handle = kernel32.CreateFileW(
                ATKACPI_DEVICE, 0x80000000 | 0x40000000, 1 | 2, None, 3, 0x80, None)
            if not self.handle or self.handle == ctypes.c_void_p(-1).value:
                self.handle = None
            else:
                buf = struct.pack("<II", INIT, 8) + b"\x00" * 8
                out = ctypes.create_string_buffer(16)
                ret = wintypes.DWORD(0)
                kernel32.DeviceIoControl(self.handle, CONTROL_CODE, buf, len(buf), out, 16, byref(ret), None)
        except Exception:
            self.handle = None

    def device_get(self, device_id):
        if not self.handle:
            return None
        try:
            args = struct.pack("<I", device_id) + b"\x00" * 4
            buf = struct.pack("<II", DSTS, len(args)) + args
            out = ctypes.create_string_buffer(16)
            ret = wintypes.DWORD(0)
            ok = kernel32.DeviceIoControl(self.handle, CONTROL_CODE, buf, len(buf), out, 16, byref(ret), None)
            if ok:
                val = struct.unpack("<i", out.raw[:4])[0] - 65536
                return val
        except Exception:
            pass
        return None

    def get_cpu_temp(self):
        t = self.device_get(DEV_CPU_TEMP)
        if t is not None and 10 <= t <= 120:
            return t
        return None

    def get_mode(self):
        m = self.device_get(DEV_PERF_MODE)
        modes = {0: "BALANCED", 1: "TURBO", 2: "SILENT", 3: "FULL", 4: "MANUAL"}
        if m is not None and m in modes:
            return modes[m]
        try:
            cfg_path = os.path.expandvars(r'%APPDATA%\GHelper\config.json')
            if os.path.exists(cfg_path):
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    cfg = json.load(f)
                    return modes.get(cfg.get("performance_mode", 0), "BALANCED")
        except Exception:
            pass
        return "BALANCED"


# ==================== 2. NVIDIA NVML 直读 ====================
class nvmlUtilization_t(Structure):
    _fields_ = [('gpu', ctypes.c_uint), ('memory', ctypes.c_uint)]

class GpuReader:
    def __init__(self):
        self.available = False
        try:
            self.nvml = ctypes.CDLL('nvml.dll')
            if self.nvml.nvmlInit_v2() == 0:
                self.handle = ctypes.c_void_p()
                if self.nvml.nvmlDeviceGetHandleByIndex_v2(0, byref(self.handle)) == 0:
                    self.available = True
        except Exception:
            self.available = False

    def read(self):
        if not self.available:
            return None, None, None
        try:
            temp = ctypes.c_uint()
            power = ctypes.c_uint()
            util = nvmlUtilization_t()
            self.nvml.nvmlDeviceGetTemperature(self.handle, 0, byref(temp))
            self.nvml.nvmlDeviceGetPowerUsage(self.handle, byref(power))
            self.nvml.nvmlDeviceGetUtilizationRates(self.handle, byref(util))
            return temp.value, power.value / 1000.0, util.gpu
        except Exception:
            return None, None, None


# ==================== 3. CPU 功耗 (RAPL / PDH) ====================
class CpuPowerReader:
    def __init__(self):
        self.pdh_query = None
        self.pdh_counter = None
        self._init_pdh()

    def _init_pdh(self):
        try:
            import win32pdh
            self.win32pdh = win32pdh
            hq = win32pdh.OpenQuery()
            for inst in ["RAPL_Package0_PKG", "Apu Power", "CPU Power", "Socket Power", "Current Socket Power"]:
                try:
                    path = f"\\Energy Meter({inst})\\Power"
                    hc = win32pdh.AddEnglishCounter(hq, path)
                    self.pdh_query = hq
                    self.pdh_counter = hc
                    win32pdh.CollectQueryData(hq)
                    break
                except Exception:
                    continue
        except Exception:
            self.pdh_query = None

    def read(self):
        if self.pdh_query and self.pdh_counter:
            try:
                self.win32pdh.CollectQueryData(self.pdh_query)
                _, val = self.win32pdh.GetFormattedCounterValue(self.pdh_counter, self.win32pdh.PDH_FMT_DOUBLE)
                if val > 0:
                    if val > 500:
                        return val / 1000.0
                    return val
            except Exception:
                pass
        return None


# ==================== 4. 游戏实时帧率 (G-Helper 同款 ETW) ====================
class FpsMonitor:
    SHARED_MEMORY_NAME = "Esp32FpsSharedMem"

    def __init__(self):
        self.shm = None
        self.helper_process = None
        self.status = 0
        self.startup_error = ""
        self._ensure_helper_started()

    def _open_shared_memory(self):
        if self.shm is not None:
            return True

        mapping_handle = kernel32.OpenFileMappingW(
            0x0004, False, self.SHARED_MEMORY_NAME)  # FILE_MAP_READ
        if not mapping_handle:
            return False
        kernel32.CloseHandle(mapping_handle)

        try:
            mmap_factory = cast(Any, mmap.mmap)
            self.shm = mmap_factory(
                -1, 256, self.SHARED_MEMORY_NAME, access=mmap.ACCESS_READ)
            return True
        except Exception:
            self.shm = None
            return False

    def _ensure_helper_started(self):
        self._stop_existing_helpers()
        base_dir = os.path.dirname(os.path.abspath(__file__))
        project_path = os.path.join(
            base_dir, "tools", "EtwFpsHelper", "EtwFpsHelper.csproj")
        exe_path = os.path.join(
            base_dir, "tools", "EtwFpsHelper", "bin", "Release",
            "net10.0", "EtwFpsHelper.exe")

        if not os.path.exists(project_path):
            self.startup_error = "缺少 EtwFpsHelper.csproj"
            return

        try:
            if not os.path.exists(exe_path):
                build = subprocess.run(
                    ["dotnet", "build", project_path, "--configuration", "Release", "--nologo"],
                    capture_output=True, text=True, timeout=60, check=False,
                    creationflags=subprocess.CREATE_NO_WINDOW)
                if build.returncode != 0:
                    details = (build.stderr or build.stdout or "编译失败").strip()
                    self.startup_error = details.splitlines()[-1]
                    return

            self.helper_process = subprocess.Popen(
                [exe_path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                creationflags=subprocess.CREATE_NO_WINDOW)

            deadline = time.monotonic() + 8.0
            while time.monotonic() < deadline:
                if self._open_shared_memory():
                    self._read_shared_memory()
                    if self.status != 0:
                        return
                time.sleep(0.1)
            self.startup_error = "ETW helper 启动超时"
            if self.shm is not None:
                self.shm.close()
                self.shm = None
        except Exception as error:
            self.startup_error = str(error)

    def _stop_existing_helpers(self):
        helpers = []
        try:
            for process in psutil.process_iter(["name"]):
                if (process.info.get("name") or "").lower() == "etwfpshelper.exe":
                    process.terminate()
                    helpers.append(process)
            if helpers:
                _, alive = psutil.wait_procs(helpers, timeout=2)
                for process in alive:
                    process.kill()
        except Exception:
            pass

    def _read_shared_memory(self):
        if self.shm is None and not self._open_shared_memory():
            return 0

        try:
            shm = self.shm
            if shm is None:
                return 0
            shm.seek(0)
            raw = shm.read(12)
            if len(raw) != 12:
                return 0
            fps, _pid, self.status = struct.unpack("<iii", raw)
            return fps if self.status > 0 and 0 < fps <= 999 else 0
        except Exception:
            shm = self.shm
            if shm is not None:
                try:
                    shm.close()
                except Exception:
                    pass
            self.shm = None
            self.status = 0
            return 0

    @property
    def available(self):
        self._read_shared_memory()
        return self.status > 0

    def get_fps(self):
        return self._read_shared_memory()

    def close(self):
        if self.shm is not None:
            try:
                self.shm.close()
            except Exception:
                pass
            self.shm = None
        if self.helper_process is not None and self.helper_process.poll() is None:
            try:
                self.helper_process.terminate()
                self.helper_process.wait(timeout=2)
            except Exception:
                self.helper_process.kill()


# ==================== 5. 数据汇聚与 JSON 打包 ====================
acpi_reader = AsusAcpiReader()
gpu_reader = GpuReader()
cpu_power_reader = CpuPowerReader()
fps_monitor = FpsMonitor()

def get_stats_json():
    cpu_percent = int(psutil.cpu_percent(interval=None))
    mem_percent = int(psutil.virtual_memory().percent)

    # 1. 华硕 EC 读 CPU 温度与性能模式
    cpu_temp = acpi_reader.get_cpu_temp()
    mode = acpi_reader.get_mode()

    # 2. NVML 读 GPU
    gpu_temp, gpu_pwr, gpu_usage = gpu_reader.read()

    # 3. CPU 功耗
    cpu_pwr = cpu_power_reader.read()

    # 4. 游戏实时帧率 (G-Helper ETW)
    fps = fps_monitor.get_fps()

    payload = {
        "fps": int(fps) if fps else 0,
        "cpu": cpu_percent,
        "mem": mem_percent,
        "cpu_temp": int(round(cpu_temp)) if cpu_temp else 0,
        "cpu_pwr": int(round(cpu_pwr)) if cpu_pwr else 0,
        "gpu_temp": int(round(gpu_temp)) if gpu_temp else 0,
        "gpu_pwr": int(round(gpu_pwr)) if gpu_pwr else 0,
        "gpu_usage": int(round(gpu_usage)) if gpu_usage is not None and gpu_usage >= 0 else -1,
        "mode": mode,
        "time": time.strftime("%H:%M:%S")
    }

    return json.dumps(payload, separators=(',', ':'))


# ==================== 6. BLE 发送主循环 ====================
async def run_ble_sender():
    print("=" * 65)
    print("  ESP32 华硕副屏 - BLE 蓝牙极速发射器 (G-Helper 协同版)")
    print("=" * 65)
    print(f"[配置] 华硕 ATKACPI: {'已就绪' if acpi_reader.handle else '未就绪 (请以管理员身份运行)'}")
    print(f"[配置] NVIDIA NVML:  {'已就绪' if gpu_reader.available else '未检测到 NVIDIA 显卡'}")
    print(f"[配置] CPU 功耗引擎: {'已连接 RAPL' if cpu_power_reader.pdh_query else '不可用'}")
    fps_status = "已就绪 (G-Helper ETW)" if fps_monitor.available else "启动失败"
    print(f"[配置] 游戏 FPS 引擎: {fps_status}")
    if not fps_monitor.available:
        if fps_monitor.status == -5:
            error = "权限不足，请以管理员身份运行一键启动脚本"
        else:
            error = fps_monitor.startup_error or f"ETW 错误码 {fps_monitor.status}"
        print(f"[错误] 游戏 FPS 引擎: {error}")
    print(f"[配置] 性能模式:     {acpi_reader.get_mode()}")
    print("=" * 65)

    while True:
        print("\n[搜索] 正在搜索蓝牙副屏 [ESP32-Dashboard]...")
        try:
            device = await BleakScanner.find_device_by_name("ESP32-Dashboard", timeout=6.0)
        except Exception as e:
            print(f"[错误] 蓝牙扫描失败: {e}")
            await asyncio.sleep(3)
            continue

        if not device:
            print("[提示] 未发现设备，请确保 ESP32 已通电并在广播中，3 秒后重试...")
            await asyncio.sleep(3)
            continue

        print(f"[发现] 找到蓝牙副屏！设备地址: {device.address}")
        print("[连接] 正在建立 BLE GATT 连接...")

        try:
            async with BleakClient(device) as client:
                print("[成功] 蓝牙连接成功！开始以 0.3s 极速推流硬件状态与游戏帧率...\n")
                while client.is_connected:
                    raw_json = get_stats_json()
                    await client.write_gatt_char(CHARACTERISTIC_UUID, raw_json.encode('utf-8'), response=False)
                    await asyncio.sleep(0.3)
        except Exception as e:
            print(f"[警告] 蓝牙连接中断: {e}，准备重新自动连接...")
            await asyncio.sleep(2)


if __name__ == "__main__":
    try:
        asyncio.run(run_ble_sender())
    except KeyboardInterrupt:
        print("\n[退出] 已退出 BLE 数据发射服务。")
    except Exception as e:
        print(f"\n[异常崩溃] 错误信息: {e}")
        input("\n按回车键退出...")
    finally:
        fps_monitor.close()
