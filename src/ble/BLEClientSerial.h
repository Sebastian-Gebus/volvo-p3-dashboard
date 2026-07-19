#ifndef BLE_CLIENT_SERIAL_H
#define BLE_CLIENT_SERIAL_H

// Stream-compatible BLE serial bridge to an ELM327 BLE adapter
// (vLinker / "IOS-Vlink": service 18F0, notify char 2AF0, write char 2AF1).
//
// Used as the transport for ELMDuino. Fixes over v1:
//  - RX ring buffer guarded by a critical section (notify callback runs on
//    the BLE host task, reader runs on the Arduino loop task)
//  - no duplicate-chunk heuristics — the buffer takes exactly what arrives
//  - single BLEClient reused across reconnects (no leaks, no double
//    notification registrations)
//  - non-blocking scanning with automatic rescan until the device is found
//  - disconnect detection via connected()

#include <Arduino.h>
#include <Stream.h>

class BLEClientSerial : public Stream {
public:
    // Init BLE stack and start scanning for the named device. Non-blocking.
    bool begin(const char *deviceName);

    // Call regularly: restarts the scan if the device hasn't been found yet.
    void tick();

    // True once advertising from the target device has been seen.
    bool deviceFound();

    // Attempt a connection (blocking, a few hundred ms). Requires deviceFound().
    bool connect();

    // Link state — false after the adapter drops off.
    bool connected();

    // Drop the GATT connection (also called automatically on link loss cleanup).
    void disconnect();

    // Stream interface
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
};

#endif // BLE_CLIENT_SERIAL_H
