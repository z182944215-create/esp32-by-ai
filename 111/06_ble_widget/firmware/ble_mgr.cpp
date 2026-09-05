#include "ble_mgr.h"
#include "config.h"
#include "ui.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

static volatile bool deviceConnected = false;
static volatile bool oldDeviceConnected = false;
static volatile bool newDataAvailable = false;
static String receivedData = "";
static portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;

// BLE 服务连接/断开回调
class MyServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* pServer) override {
    deviceConnected = true;
    Serial.println("[BLE] 客户端已连接");
  }

  void onDisconnect(BLEServer* pServer) override {
    deviceConnected = false;
    Serial.println("[BLE] 客户端已断开");
  }
};

// BLE 特征值写入数据接收回调
class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* pCharacteristic) override {
    String rxValue = String(pCharacteristic->getValue().c_str());
    if (rxValue.length() > 0) {
      portENTER_CRITICAL(&bleMux);
      receivedData = rxValue;
      newDataAvailable = true;
      portEXIT_CRITICAL(&bleMux);
    }
  }
};

void ble_init() {
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setMTU(256); // 协商大 MTU，确保 >140 字节 JSON 完整传输不被截断

  BLEServer* pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  BLEService* pService = pServer->createService(SERVICE_UUID);
  BLECharacteristic* pCharacteristic = pService->createCharacteristic(
    CHARACTERISTIC_UUID,
    BLECharacteristic::PROPERTY_READ |
    BLECharacteristic::PROPERTY_WRITE |
    BLECharacteristic::PROPERTY_NOTIFY
  );

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic->addDescriptor(new BLE2902());
  pService->start();

  BLEAdvertising* pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinPreferred(0x06); // 最小连接间隔 7.5ms
  pAdvertising->setMaxPreferred(0x12); // 最大连接间隔 22.5ms (修复复制粘贴笔误)
  BLEDevice::startAdvertising();

  Serial.println("[BLE] 广播已开启，等待电脑端连接...");
}

bool ble_is_connected() {
  return deviceConnected;
}

bool ble_has_new_data() {
  return newDataAvailable;
}

String ble_get_data() {
  portENTER_CRITICAL(&bleMux);
  String data = receivedData;
  newDataAvailable = false;
  portEXIT_CRITICAL(&bleMux);
  return data;
}

void ble_clear_new_data() {
  portENTER_CRITICAL(&bleMux);
  newDataAvailable = false;
  portEXIT_CRITICAL(&bleMux);
}

void ble_update_status() {
  // 设备断开连接后，延时重启广播并恢复等待画面
  if (!deviceConnected && oldDeviceConnected) {
    delay(500);
    BLEDevice::startAdvertising();
    Serial.println("[BLE] 重新启动广播...");
    ui_show_waiting();
    oldDeviceConnected = deviceConnected;
  }

  // 新设备建立连接后，清空屏幕准备接收新数据
  if (deviceConnected && !oldDeviceConnected) {
    oldDeviceConnected = deviceConnected;
    ui_clear();
    Serial.println("[BLE] 连接就绪，清屏准备接收监控数据");
  }
}

