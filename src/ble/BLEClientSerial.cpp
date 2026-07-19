#include "BLEClientSerial.h"

#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

#include "../config.h"

// ── UUIDs (vLinker "IOS-Vlink") ──────────────────────────────
static BLEUUID SERVICE_UUID("18F0");
static BLEUUID NOTIFY_CHAR_UUID("2AF0");   // adapter → us
static BLEUUID WRITE_CHAR_UUID("2AF1");    // us → adapter

// ── RX ring buffer (BLE host task writes, loop task reads) ──
static const size_t RX_BUF_SIZE = 1024;
static uint8_t  rxBuf[RX_BUF_SIZE];
static volatile size_t rxHead = 0;   // write index
static volatile size_t rxTail = 0;   // read index
static portMUX_TYPE rxMux = portMUX_INITIALIZER_UNLOCKED;

static size_t rxCount_unsafe()
{
    return (rxHead + RX_BUF_SIZE - rxTail) % RX_BUF_SIZE;
}

// ── Connection state ─────────────────────────────────────────
static String targetName;
static BLEAdvertisedDevice *foundDevice = nullptr;
static BLEClient  *client = nullptr;
static BLERemoteCharacteristic *writeChar = nullptr;
static volatile bool linkUp = false;
static volatile bool scanning = false;

static void notifyCallback(BLERemoteCharacteristic *chr, uint8_t *data,
                           size_t length, bool isNotify)
{
    portENTER_CRITICAL(&rxMux);
    for (size_t i = 0; i < length; i++) {
        size_t next = (rxHead + 1) % RX_BUF_SIZE;
        if (next == rxTail) break;   // full — drop the rest, ELMDuino will time out and retry
        rxBuf[rxHead] = data[i];
        rxHead = next;
    }
    portEXIT_CRITICAL(&rxMux);
}

class ClientCallbacks : public BLEClientCallbacks {
    void onConnect(BLEClient *c) override { linkUp = true; }
    void onDisconnect(BLEClient *c) override
    {
        linkUp = false;
        Serial.println("[BLE] disconnected");
    }
};

class ScanCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertised) override
    {
        if (foundDevice) return;
        if (advertised.getName() == std::string(targetName.c_str())) {
            Serial.printf("[BLE] found %s (%s)\n", targetName.c_str(),
                          advertised.getAddress().toString().c_str());
            foundDevice = new BLEAdvertisedDevice(advertised);
            BLEDevice::getScan()->stop();
        }
    }
};

static void scanComplete(BLEScanResults results)
{
    scanning = false;
}

static void startScan()
{
    if (scanning || foundDevice) return;
    BLEScan *scan = BLEDevice::getScan();
    scan->clearResults();
    scanning = true;
    scan->start(ELM_SCAN_SECONDS, scanComplete, false);
}

// ── Public API ───────────────────────────────────────────────

bool BLEClientSerial::begin(const char *deviceName)
{
    targetName = deviceName;
    BLEDevice::init("");
    BLEScan *scan = BLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(new ScanCallbacks());
    scan->setInterval(1349);
    scan->setWindow(449);
    scan->setActiveScan(true);
    startScan();
    return true;
}

void BLEClientSerial::tick()
{
    if (!foundDevice && !scanning)
        startScan();
}

bool BLEClientSerial::deviceFound()
{
    return foundDevice != nullptr;
}

bool BLEClientSerial::connected()
{
    return linkUp && writeChar != nullptr;
}

void BLEClientSerial::disconnect()
{
    writeChar = nullptr;
    if (client && client->isConnected())
        client->disconnect();
    linkUp = false;
}

// After several failed attempts the cached advertisement may be stale
// (adapter re-advertising under another address) — forget it and rescan.
static uint8_t connectFails = 0;
static bool connectFailed()
{
    if (++connectFails >= 5) {
        Serial.println("[BLE] repeated connect failures, rescanning");
        delete foundDevice;
        foundDevice = nullptr;
        connectFails = 0;
    }
    return false;
}

bool BLEClientSerial::connect()
{
    if (!foundDevice) return false;

    if (!client) {
        client = BLEDevice::createClient();
        client->setClientCallbacks(new ClientCallbacks());
    }

    writeChar = nullptr;
    flush();

    Serial.printf("[BLE] connecting to %s...\n",
                  foundDevice->getAddress().toString().c_str());
    if (!client->connect(foundDevice)) {
        Serial.println("[BLE] connect failed");
        return connectFailed();
    }

    BLERemoteService *service = client->getService(SERVICE_UUID);
    if (!service) {
        Serial.println("[BLE] service 18F0 not found");
        client->disconnect();
        return connectFailed();
    }

    BLERemoteCharacteristic *notifyChar = service->getCharacteristic(NOTIFY_CHAR_UUID);
    BLERemoteCharacteristic *wc = service->getCharacteristic(WRITE_CHAR_UUID);
    if (!notifyChar || !wc || !notifyChar->canNotify()) {
        Serial.println("[BLE] characteristics missing");
        client->disconnect();
        return connectFailed();
    }

    notifyChar->registerForNotify(notifyCallback, true);
    writeChar = wc;
    connectFails = 0;
    Serial.println("[BLE] link up");
    return true;
}

// ── Stream interface ─────────────────────────────────────────

int BLEClientSerial::available()
{
    portENTER_CRITICAL(&rxMux);
    size_t n = rxCount_unsafe();
    portEXIT_CRITICAL(&rxMux);
    return (int)n;
}

int BLEClientSerial::read()
{
    int c = -1;
    portENTER_CRITICAL(&rxMux);
    if (rxTail != rxHead) {
        c = rxBuf[rxTail];
        rxTail = (rxTail + 1) % RX_BUF_SIZE;
    }
    portEXIT_CRITICAL(&rxMux);
    return c;
}

int BLEClientSerial::peek()
{
    int c = -1;
    portENTER_CRITICAL(&rxMux);
    if (rxTail != rxHead)
        c = rxBuf[rxTail];
    portEXIT_CRITICAL(&rxMux);
    return c;
}

void BLEClientSerial::flush()
{
    portENTER_CRITICAL(&rxMux);
    rxTail = rxHead;
    portEXIT_CRITICAL(&rxMux);
}

size_t BLEClientSerial::write(uint8_t c)
{
    return write(&c, 1);
}

size_t BLEClientSerial::write(const uint8_t *buffer, size_t size)
{
    if (!connected()) return 0;
    // One GATT write per command — ELM commands are short (< default 20-byte MTU payload)
    writeChar->writeValue(const_cast<uint8_t *>(buffer), size, false);
    return size;
}
