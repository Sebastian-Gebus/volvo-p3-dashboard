#ifndef OBD_CLIENT_H
#define OBD_CLIENT_H

// Non-blocking OBD-II manager: owns the BLE link state machine and
// round-robins through PID_DEFS, publishing parsed values.

#include <Arduino.h>
#include "pid_defs.h"
#include "../ble/BLEClientSerial.h"

enum ObdStatus {
    OBD_SCANNING,      // looking for the BLE adapter
    OBD_CONNECTING,    // adapter found, establishing link / ELM init
    OBD_CONNECTED,     // polling PIDs
    OBD_LOST,          // link dropped, will reconnect
};

class ObdClient {
public:
    void begin();
    void tick();                       // call every loop iteration

    ObdStatus status() const { return _status; }
    bool  valid(int pid) const { return _valid[pid]; }
    float value(int pid) const { return _values[pid]; }
    uint32_t age(int pid) const;       // ms since last good sample

    // Trip distance since power-on, integrated from vehicle speed samples
    float tripKm() const { return _tripKm; }

    // Formatted "31.1°C" / "-" into buf
    void format(int pid, char *buf, size_t len) const;

    // Restrict round-robin polling to these PIDs (bitmask of 1 << pid).
    // Values of masked-out PIDs keep their last state (stale but valid).
    void setPollMask(uint16_t mask) { _pollMask = mask; }

private:
    void connectTick();
    void pollTick();
    void startQuery(int pid);
    bool parseResponse(int pid);
    void failQuery();
    void invalidateAll();

    BLEClientSerial _ble;

    ObdStatus _status = OBD_SCANNING;
    uint32_t  _lastAttemptMs = 0;

    // per-PID data
    float     _values[PID_COUNT] = {0};
    bool      _valid[PID_COUNT]  = {false};
    uint32_t  _updatedMs[PID_COUNT] = {0};

    // trip distance integration
    float     _tripKm = 0;
    uint32_t  _lastSpeedMs = 0;

    // polling state machine
    enum QueryPhase { PHASE_IDLE, PHASE_WAIT_HEADER, PHASE_WAIT_DATA,
                      PHASE_RESYNC };
    QueryPhase  _phase = PHASE_IDLE;
    int         _pid = 0;
    const char *_activeHeader = nullptr;   // last ATSH accepted by the adapter
    uint32_t    _lastPollMs = 0;
    uint32_t    _resyncMs = 0;             // last activity while resyncing
    uint16_t    _pollMask = 0xFFFF;        // PIDs enabled for polling
};

#endif // OBD_CLIENT_H
