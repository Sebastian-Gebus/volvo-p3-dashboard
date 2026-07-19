#ifndef PID_DEFS_H
#define PID_DEFS_H

#include <stdint.h>

// Every PID (standard and enhanced mode 0x22) goes through the same
// query pipeline: set header → send command → parse ASCII hex response.
//
// Response format (ELM327 headers ON is NOT used; these are the default
// ELM327 formatted lines, spaces stripped by ELMDuino):
//   request "22D9DC" → response "7E80562D9DC0C26"
//                                ^CAN ^len ^echo ^data (hex chars)
//   request "0105"   → response "7E803410551"
//
// We locate the echo ("62"+PID or "41"+PID) inside the response text and
// parse the hex characters that follow. This is robust against the CAN id
// / length prefix varying between ECUs (7E8 vs 72E).
//
// Verified against the BLE sniff in docs/volvo_pids.txt (engine off, car
// recently driven) and the first live-car test (2026-07-18):
//   Oil Temp  data 0C26 → 3110 = 0.1 K units → 311.0 K = 37.9 °C
//   DPF Temp  data 0CA1 → 3233 → 323.3 K = 50.2 °C
//     (live test proved 0.1 K: the old /100 reading showed DPF 40–47 "°C"
//      while driving = 400–470 in 0.1 K = 127–197 °C, typical DPF temps;
//      oil "~30 °C" = ~358 K = ~85 °C, typical warm oil)
//   DPF Soot  data 02D0 → 7.2 g
//   DPF Press data 0000 → 0 mbar (raw 0.1 kPa units = mbar directly)
//   Batt Chg  data 0000 → 0.0 A
//   Batt SoC  data 53   → 83 %
//   Glow Plug data 00000000 → 4 data bytes, meaning unknown → any nonzero = ON
//   Boost     data 03D6 → 98.2 kPa (≈ atmospheric)
//   Coolant   data 51   → 0x51-40  = 41 °C

// Enum order = polling order; grouped by ECU header to minimize ATSH switches
// (7DF standard group → 7E0 ECM group → 726 CEM).
enum obd_pid_id {
    PID_COOLANT_TEMP = 0,  // 7DF
    PID_SPEED,             // 7DF — also integrated into trip distance
    PID_RUNTIME,           // 7DF — engine run time, shown as trip time
    PID_OIL_TEMP,          // 7E0
    PID_BOOST_PRESSURE,    // 7E0
    PID_DPF_SOOT,          // 7E0
    PID_DPF_TEMP,          // 7E0
    PID_DPF_PRESSURE,      // 7E0
    PID_BATT_CHARGE,       // 7E0
    PID_GLOW_PLUGS,        // 7E0
    PID_BATT_STATE,        // 726
    PID_COUNT
};

enum pid_formula_t {
    FORMULA_A_MINUS_40,   // A - 40                (standard temp PIDs)
    FORMULA_AB_DIV100,    // (A*256 + B) / 100
    FORMULA_AB_DIV10,     // (A*256 + B) / 10
    FORMULA_AB_RAW,       // A*256 + B             (runtime seconds)
    FORMULA_A_ONLY,       // A
    FORMULA_BOOST_BAR,    // ((A*256 + B)/10 - 100) / 100 — absolute kPa → relative bar
    FORMULA_KELVIN10,     // (A*256 + B)/10 - 273.15 — 0.1 K units → °C
    FORMULA_ANY_ON,       // 1 if any of up to 4 data bytes nonzero, else 0
                          // (glow plugs: which byte holds the status is unknown)
};

struct PidDef {
    const char   *name;      // short label
    const char   *header;    // ATSH command selecting the target ECU
    const char   *command;   // hex request
    const char   *echo;      // response-mode echo to search for in the reply
    pid_formula_t formula;
    const char   *unit;
    uint8_t       decimals;  // display precision
};

static const PidDef PID_DEFS[PID_COUNT] = {
    // name        header      command   echo      formula             unit             dec
    { "Coolant",   "ATSH7DF",  "0105",   "4105",   FORMULA_A_MINUS_40, "\xC2\xB0""C",   0 },
    { "Speed",     "ATSH7DF",  "010D",   "410D",   FORMULA_A_ONLY,     "km/h",          0 },
    { "Runtime",   "ATSH7DF",  "011F",   "411F",   FORMULA_AB_RAW,     "s",             0 },
    { "Oil",       "ATSH7E0",  "22D9DC", "62D9DC", FORMULA_KELVIN10,   "\xC2\xB0""C",   0 },
    { "Boost",     "ATSH7E0",  "22D941", "62D941", FORMULA_BOOST_BAR,  "bar",           2 },
    { "DPF Soot",  "ATSH7E0",  "22D9F2", "62D9F2", FORMULA_AB_DIV100,  "g",             1 },
    { "DPF Temp",  "ATSH7E0",  "22D9F9", "62D9F9", FORMULA_KELVIN10,   "\xC2\xB0""C",   0 },
    // raw is 0.1 kPa units (live-verified plausible) = exactly mbar/hPa
    { "DPF Press", "ATSH7E0",  "22DAC8", "62DAC8", FORMULA_AB_RAW,     "mbar",          0 },
    { "Batt Chg",  "ATSH7E0",  "22D924", "62D924", FORMULA_AB_DIV100,  "A",             1 },
    { "Glow",      "ATSH7E0",  "22D9A0", "62D9A0", FORMULA_ANY_ON,     "",              0 },
    { "Batt SoC",  "ATSH726",  "224028", "624028", FORMULA_A_ONLY,     "%",             0 },
};

#endif // PID_DEFS_H
