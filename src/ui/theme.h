#ifndef DASH_THEME_H
#define DASH_THEME_H

// Dashboard palette — pure black background for AMOLED (unlit pixels).

#define COL_BG      0x000000
#define COL_PANEL   0x10151B   // tile background
#define COL_TRACK   0x232B34   // gauge/bar background track
#define COL_TEXT    0xEAECEF
#define COL_DIM     0x7A828C   // secondary text, inactive icons

#define COL_ACCENT  0x38BDF8   // neutral readouts (boost, current)
#define COL_COLD    0x4C8DFF   // below operating temperature
#define COL_OK      0x2ECC71   // in the good band
#define COL_WARN    0xF5A623   // attention (regen, soot high, SoC low)
#define COL_ALERT   0xF04438   // overheat / critical

#endif // DASH_THEME_H
