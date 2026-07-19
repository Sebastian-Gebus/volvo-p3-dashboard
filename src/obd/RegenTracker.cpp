#include "RegenTracker.h"
#include "../config.h"

void RegenTracker::begin()
{
    _prefs.begin("regen", false);
    _count          = _prefs.getUInt("count", 0);
    _kmSince        = _prefs.getFloat("kmSince", 0);
    _totalKmBetween = _prefs.getFloat("kmTotal", 0);
    _lastDurS       = _prefs.getUInt("lastDur", 0);
    _lastSootBurn   = _prefs.getFloat("lastSoot", 0);
}

void RegenTracker::save()
{
    _prefs.putUInt("count", _count);
    _prefs.putFloat("kmSince", _kmSince);
    _prefs.putFloat("kmTotal", _totalKmBetween);
    _prefs.putUInt("lastDur", _lastDurS);
    _prefs.putFloat("lastSoot", _lastSootBurn);
}

void RegenTracker::persistNow()
{
    save();
    _kmUnsaved = 0;
}

void RegenTracker::tick(const ObdClient &obd)
{
    // ── accumulate distance since last regen ─────────────────
    float trip = obd.tripKm();
    float d = trip - _lastTripKm;
    if (d > 0) {
        _kmSince += d;
        _kmUnsaved += d;
    }
    _lastTripKm = trip;
    if (_kmUnsaved >= REGEN_KM_SAVE_EVERY) {
        _prefs.putFloat("kmSince", _kmSince);
        _kmUnsaved = 0;
    }

    // ── regen detection ──────────────────────────────────────
    if (!obd.valid(PID_DPF_TEMP))
        return;                       // hold state until data returns
    float t = obd.value(PID_DPF_TEMP);
    uint32_t now = millis();

    // A long data dropout must not satisfy the 60 s sustained-temp windows
    // (two hot samples bridging a gap would otherwise count as a regen)
    if (_lastValidMs && now - _lastValidMs > REGEN_DATA_GAP_MS &&
        (_st == CANDIDATE || _st == ENDING))
        _stMs = now;
    _lastValidMs = now;

    switch (_st) {
        case IDLE:
            if (t >= REGEN_ON_TEMP) {
                _st = CANDIDATE;
                _stMs = now;
                _sootStart = obd.valid(PID_DPF_SOOT) ? obd.value(PID_DPF_SOOT) : -1;
            }
            break;

        case CANDIDATE:
            if (t < REGEN_ON_TEMP) {
                _st = IDLE;           // brief spike, not a regen
            } else if (now - _stMs >= REGEN_CONFIRM_MS) {
                _st = ACTIVE;
                _startMs = _stMs;
                Serial.println("[REGEN] started");
            }
            break;

        case ACTIVE:
            if (t < REGEN_OFF_TEMP) {
                _st = ENDING;
                _stMs = now;
            }
            break;

        case ENDING:
            if (t >= REGEN_ON_TEMP) {
                _st = ACTIVE;         // regen still going
            } else if (now - _stMs >= REGEN_END_MS) {
                // completed — _stMs marks when the temp dropped
                _count++;
                _lastDurS = (_stMs - _startMs) / 1000;
                float sootNow = obd.valid(PID_DPF_SOOT) ? obd.value(PID_DPF_SOOT) : -1;
                _lastSootBurn = (_sootStart >= 0 && sootNow >= 0 && _sootStart > sootNow)
                                ? _sootStart - sootNow : 0;
                _totalKmBetween += _kmSince;
                _kmSince = 0;
                persistNow();
                Serial.printf("[REGEN] completed #%u, %us, %.1fg burned\n",
                              _count, _lastDurS, _lastSootBurn);
                _st = IDLE;
            }
            break;
    }
}
