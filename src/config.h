#ifndef CONFIG_H
#define CONFIG_H

// ── BLE / ELM327 ─────────────────────────────────────────────
#define ELM_DEVICE_NAME        "IOS-Vlink"
#define ELM_INIT_TIMEOUT_MS    2000     // ELMDuino init AND per-query timeout
#define ELM_RETRY_INTERVAL_MS  3000     // between connect attempts
#define ELM_SCAN_SECONDS       5        // BLE scan burst length (rescans until found)

// ── Polling ──────────────────────────────────────────────────
#define OBD_TICK_MS            50       // state machine tick
#define OBD_VERBOSE_LOG        1        // log every parsed PID with raw response
                                        // (for the live-car debug session)

// ── Display ──────────────────────────────────────────────────
#define DISPLAY_ROTATION       1        // portrait 240x536 on the 1.91" panel
#define DEFAULT_BRIGHTNESS     160      // 0..255

// ── Buttons ──────────────────────────────────────────────────
#define BUTTON_PIN             GPIO_NUM_0

// ── Value thresholds (diesel) — used for gauge coloring ─────
// Gauges: blue below COLD_MAX, green in between, red at/above RED
// Coolant °C
#define COOLANT_COLD_MAX       60
#define COOLANT_RED            100
#define COOLANT_MAX_SCALE      130      // arc full scale
// Oil °C
#define OIL_COLD_MAX           60
#define OIL_RED                110
#define OIL_MAX_SCALE          150
// DPF temp °C — above this a regeneration is likely in progress
#define DPF_REGEN_TEMP         450
#define DPF_TEMP_MAX_SCALE     700
// DPF soot grams
#define DPF_SOOT_WARN          24       // typical forced-regen threshold region
#define DPF_SOOT_MAX_SCALE     32
// Boost shown relative to atmosphere, in bar (0.00 at idle).
// Arc/bar widgets are integer, so their scale is in 1/100 bar.
#define BOOST_GAUGE_SCALE      200      // full scale = 2.00 bar

// ── Regen tracking (persisted in NVS flash) ──────────────────
#define REGEN_ON_TEMP          450      // candidate regen at/above this °C
#define REGEN_OFF_TEMP         400      // regen considered over below this °C
#define REGEN_CONFIRM_MS       60000    // must stay hot this long to count
#define REGEN_END_MS           60000    // must stay cool this long to finish
#define REGEN_DATA_GAP_MS      5000     // DPF-data dropout that resets the timers
#define REGEN_KM_SAVE_EVERY    5.0f     // persist km counter every N km

#endif // CONFIG_H
