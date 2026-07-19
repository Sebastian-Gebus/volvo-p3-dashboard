#include "ObdClient.h"

#include <ELMduino.h>
#include <ctype.h>
#include <string.h>
#include <math.h>

#include "../config.h"

static ELM327 elm;

void ObdClient::begin()
{
    _ble.begin(ELM_DEVICE_NAME);
    _status = OBD_SCANNING;
    _lastAttemptMs = 0;
}

uint32_t ObdClient::age(int pid) const
{
    return millis() - _updatedMs[pid];
}

void ObdClient::invalidateAll()
{
    for (int i = 0; i < PID_COUNT; i++)
        _valid[i] = false;
}

void ObdClient::tick()
{
    _ble.tick();

    if (_status == OBD_CONNECTED) {
        if (!_ble.connected()) {
            Serial.println("[OBD] link lost");
            _ble.disconnect();
            invalidateAll();
            _status = OBD_LOST;
            _lastAttemptMs = millis();
            return;
        }
        pollTick();
    } else {
        connectTick();
    }
}

void ObdClient::connectTick()
{
    if (!_ble.deviceFound()) {
        _status = OBD_SCANNING;
        return;
    }
    if (_status == OBD_SCANNING)
        _status = OBD_CONNECTING;

    if (millis() - _lastAttemptMs < ELM_RETRY_INTERVAL_MS)
        return;
    _lastAttemptMs = millis();

    if (!_ble.connect())
        return;

    // ELM327 init (blocking, sends ATZ/ATE0/protocol setup). Protocol '6' =
    // ISO 15765-4 CAN 11 bit / 500 kbit (matches the sniff) — avoids the
    // 30 s auto-search ELMduino runs when the ignition is off. begin() is
    // called only once: calling it again leaks its payload buffer, so
    // reconnects re-run just the AT init.
    static bool elmBegun = false;
    bool elmOk;
    if (!elmBegun) {
        elmOk = elm.begin(_ble, false, ELM_INIT_TIMEOUT_MS, '6', 40, 255);
        elmBegun = true;
    } else {
        elmOk = elm.initializeELM('6', 255);
    }
    if (!elmOk) {
        Serial.println("[OBD] ELM init failed");
        _ble.disconnect();
        return;
    }

    Serial.println("[OBD] connected");
    _status = OBD_CONNECTED;
    _activeHeader = nullptr;
    _phase = PHASE_IDLE;
    _pid = 0;
}

void ObdClient::startQuery(int pid)
{
    const PidDef &def = PID_DEFS[pid];

    // Switch target ECU header first if it changed
    if (!_activeHeader || strcmp(_activeHeader, def.header) != 0) {
        elm.sendCommand(def.header);
        _phase = PHASE_WAIT_HEADER;
    } else {
        elm.sendCommand(def.command);
        _phase = PHASE_WAIT_DATA;
    }
}

// Advance past a failed query. After a receive timeout the response may
// still arrive late over BLE — without draining it, the next query would
// parse the previous PID's response and the poller runs one-behind.
void ObdClient::failQuery()
{
    if (elm.nb_rx_state == ELM_SUCCESS)
        Serial.printf("[OBD] %s: unexpected reply '%s'\n",
                      PID_DEFS[_pid].name, elm.payload);
    else
        elm.printError();
    _valid[_pid] = false;
    _pid = (_pid + 1) % PID_COUNT;
    if (elm.nb_rx_state == ELM_TIMEOUT) {
        _phase = PHASE_RESYNC;
        _resyncMs = millis();
    } else {
        _phase = PHASE_IDLE;
    }
}

void ObdClient::pollTick()
{
    if (millis() - _lastPollMs < OBD_TICK_MS)
        return;
    _lastPollMs = millis();

    if (_phase == PHASE_RESYNC) {
        // discard late data until the ELM prompt (or the line goes quiet)
        bool prompt = false;
        while (_ble.available() > 0) {
            if (_ble.read() == '>')
                prompt = true;
            _resyncMs = millis();
        }
        if (prompt || millis() - _resyncMs >= 300)
            _phase = PHASE_IDLE;
        return;
    }

    if (_phase == PHASE_IDLE) {
        // skip PIDs the UI doesn't currently need
        for (int i = 0; i < PID_COUNT && !(_pollMask & (1 << _pid)); i++)
            _pid = (_pid + 1) % PID_COUNT;
        if (!(_pollMask & (1 << _pid)))
            return;                               // mask empty
        startQuery(_pid);
        return;
    }

    // get_response() consumes ONE character per call — drain everything the
    // BLE ring buffer has, otherwise a ~18-char response takes 18 ticks
    // (~900 ms) and a full PID cycle takes >10 s.
    do {
        elm.get_response();
    } while (elm.nb_rx_state == ELM_GETTING_MSG && _ble.available() > 0);
    if (elm.nb_rx_state == ELM_GETTING_MSG)
        return;                                   // still receiving

    const PidDef &def = PID_DEFS[_pid];

    if (_phase == PHASE_WAIT_HEADER) {
        // require an actual "OK" — ELM_SUCCESS alone also covers e.g. a '?'
        // reply (filtered to an empty payload), which must not cache the header
        if (elm.nb_rx_state == ELM_SUCCESS && strstr(elm.payload, "OK")) {
            _activeHeader = def.header;
            elm.sendCommand(def.command);
            _phase = PHASE_WAIT_DATA;
        } else {
            _activeHeader = nullptr;
            failQuery();
        }
        return;
    }

    // PHASE_WAIT_DATA
    if (elm.nb_rx_state == ELM_SUCCESS) {
        if (!parseResponse(_pid))
            _valid[_pid] = false;
        _pid = (_pid + 1) % PID_COUNT;
        _phase = PHASE_IDLE;
    } else {
        failQuery();
    }
}

// Extract the data bytes from the ASCII hex response.
// elm.payload holds text like "7E80562D9DC0C26" (possibly with \r or
// multi-line noise). We sanitize to pure hex chars, locate the echo
// ("62D9DC" / "4105"), then parse the A/B bytes that follow it.
bool ObdClient::parseResponse(int pid)
{
    const PidDef &def = PID_DEFS[pid];

    char clean[64];
    size_t n = 0;
    for (size_t i = 0; i < elm.recBytes && n < sizeof(clean) - 1; i++) {
        char c = toupper((unsigned char)elm.payload[i]);
        if (isxdigit((unsigned char)c))
            clean[n++] = c;
    }
    clean[n] = '\0';

    const char *echo = strstr(clean, def.echo);
    if (!echo) {
        Serial.printf("[OBD] %s: echo %s not in '%s'\n", def.name, def.echo, clean);
        return false;
    }

    const char *data = echo + strlen(def.echo);
    size_t need = (def.formula == FORMULA_AB_DIV100 ||
                   def.formula == FORMULA_AB_DIV10 ||
                   def.formula == FORMULA_AB_RAW ||
                   def.formula == FORMULA_BOOST_BAR ||
                   def.formula == FORMULA_KELVIN10) ? 4 : 2;
    if (strlen(data) < need) {
        Serial.printf("[OBD] %s: short data '%s'\n", def.name, data);
        return false;
    }

    auto hexByte = [](const char *p) -> int {
        auto nib = [](char c) { return (c <= '9') ? c - '0' : c - 'A' + 10; };
        return (nib(p[0]) << 4) | nib(p[1]);
    };

    int A = hexByte(data);
    int B = (strlen(data) >= 4) ? hexByte(data + 2) : 0;

    float val;
    switch (def.formula) {
        case FORMULA_A_MINUS_40: val = A - 40;                  break;
        case FORMULA_AB_DIV100:  val = (A * 256 + B) / 100.0f;  break;
        case FORMULA_AB_DIV10:   val = (A * 256 + B) / 10.0f;   break;
        case FORMULA_AB_RAW:     val = A * 256 + B;             break;
        case FORMULA_A_ONLY:     val = A;                       break;
        case FORMULA_BOOST_BAR:  val = ((A * 256 + B) / 10.0f - 100.0f) / 100.0f; break;
        case FORMULA_KELVIN10:   val = (A * 256 + B) / 10.0f - 273.15f; break;
        case FORMULA_ANY_ON: {
            int on = A != 0;
            for (int k = 1; k < 4 && strlen(data) >= (size_t)(k + 1) * 2; k++)
                if (hexByte(data + k * 2) != 0) on = 1;
            val = on;
            break;
        }
        default: return false;
    }

#if OBD_VERBOSE_LOG
    Serial.printf("[OBD] %s = %.2f %s (raw '%s')\n", def.name, val, def.unit, clean);
#endif

    _values[pid] = val;
    _valid[pid] = true;
    _updatedMs[pid] = millis();

    // Trip distance: integrate speed over the sample interval. Skip
    // intervals over 30 s (reconnect gaps) to avoid distance jumps.
    if (pid == PID_SPEED) {
        uint32_t now = millis();
        if (_lastSpeedMs) {
            uint32_t dt = now - _lastSpeedMs;
            if (dt < 30000)
                _tripKm += val * (dt / 3600000.0f);
        }
        _lastSpeedMs = now;
    }
    return true;
}

void ObdClient::format(int pid, char *buf, size_t len) const
{
    const PidDef &def = PID_DEFS[pid];
    if (!_valid[pid]) {
        snprintf(buf, len, "-");
        return;
    }
    if (def.decimals == 0)
        snprintf(buf, len, "%d", (int)lroundf(_values[pid]));
    else
        snprintf(buf, len, "%.*f", def.decimals, _values[pid]);
}
