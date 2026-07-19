#ifndef UI_DASHBOARD_H
#define UI_DASHBOARD_H

// Hand-coded LVGL dashboard, portrait 240x536.
//
// Screen 1 DRIVE : coolant gauge, oil gauge, trip time + distance tiles
// Screen 2 BOOST : boost gauge, oil gauge, peak-boost tile
// Screen 3 DPF   : DPF temp gauge (+REGEN badge), soot tile,
//                  pressure + glow mini tiles
// Screen 4 POWER : battery SoC gauge, charge current tile
// Hidden STATS   : DPF regen statistics — toggled with a double-click,
//                  not part of the normal cycle.
//
// All screens are created once at init; dashboard_update() refreshes
// every widget (LVGL only renders the active screen).

#include "../obd/ObdClient.h"
#include "../obd/RegenTracker.h"

void dashboard_init();
void dashboard_update(const ObdClient &obd, const RegenTracker &regen);
void dashboard_next_screen();
void dashboard_toggle_stats();

// Bitmask (1 << obd_pid_id) of PIDs the current screen needs. Feed this to
// ObdClient::setPollMask() so only visible values are polled — background
// PIDs (speed/trip, DPF temp+soot for regen detection, glow badge) are
// always included.
uint16_t dashboard_poll_mask();

#endif // UI_DASHBOARD_H
