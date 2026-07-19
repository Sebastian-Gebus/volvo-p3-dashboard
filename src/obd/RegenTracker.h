#ifndef REGEN_TRACKER_H
#define REGEN_TRACKER_H

// Detects DPF regeneration events from DPF temperature (with hysteresis
// and minimum-duration confirmation so a hard highway pull doesn't count)
// and keeps lifetime stats in NVS flash — survives power-off and deep sleep.
//
// Detection: temp >= REGEN_ON_TEMP sustained for REGEN_CONFIRM_MS starts an
// event; it ends after temp < REGEN_OFF_TEMP for REGEN_END_MS. Soot at
// start/end gives "soot burned". Distance between regens comes from the
// trip integrator.
//
// Caveat: events only count while the dashboard is powered and connected.

#include <Arduino.h>
#include <Preferences.h>
#include "ObdClient.h"

class RegenTracker {
public:
    void begin();                      // load persisted stats from NVS
    void tick(const ObdClient &obd);   // call regularly (~every 500 ms)
    void persistNow();                 // call before deep sleep

    bool     active() const { return _st == ACTIVE || _st == ENDING; }
    uint32_t count() const { return _count; }
    float    kmSinceLast() const { return _kmSince; }
    float    avgKmBetween() const { return _count ? _totalKmBetween / _count : 0; }
    uint32_t lastDurationS() const { return _lastDurS; }
    float    lastSootBurned() const { return _lastSootBurn; }

private:
    void save();

    Preferences _prefs;

    // persisted
    uint32_t _count = 0;           // completed regens seen
    float    _kmSince = 0;         // km driven since last regen ended
    float    _totalKmBetween = 0;  // sum of km-between values (for average)
    uint32_t _lastDurS = 0;        // duration of the last regen
    float    _lastSootBurn = 0;    // soot start - soot end of the last regen

    // runtime
    enum St { IDLE, CANDIDATE, ACTIVE, ENDING };
    St       _st = IDLE;
    uint32_t _stMs = 0;            // when the current sub-state was entered
    uint32_t _startMs = 0;         // when the confirmed regen began
    float    _sootStart = -1;      // soot when the candidate began (-1 unknown)
    float    _lastTripKm = 0;
    float    _kmUnsaved = 0;
    uint32_t _lastValidMs = 0;     // last tick with valid DPF temp data
};

#endif // REGEN_TRACKER_H
