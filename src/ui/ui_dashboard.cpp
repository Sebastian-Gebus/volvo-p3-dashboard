#include "ui_dashboard.h"

#include <lvgl.h>
#include "theme.h"
#include "icons.h"
#include "../config.h"

// ── Component structs ────────────────────────────────────────

#define SCREEN_COUNT 4

struct StatusBar {
    lv_obj_t *bt;
    lv_obj_t *text;
    lv_obj_t *glow;      // glow-plug indicator (right side), amber when active
    lv_obj_t *dots[SCREEN_COUNT];
};

struct Gauge {
    lv_obj_t *arc;
    lv_obj_t *icon;
    lv_obj_t *value;
    lv_obj_t *unit;
    int vmin, vmax;
    int mult;        // value → arc-unit multiplier (boost: 100, bar → 1/100 bar)
};

struct Tile {
    lv_obj_t *icon;
    lv_obj_t *value;
    lv_obj_t *name;
    lv_obj_t *bar;
    int vmin, vmax;
};

// Small half-width tile (two side by side) for trip time / distance
struct MiniTile {
    lv_obj_t *icon;
    lv_obj_t *value;
};

static lv_obj_t *screens[SCREEN_COUNT];
static StatusBar bars[SCREEN_COUNT];
static int active_screen = 0;

// screen 1 — DRIVE
static Gauge g_coolant, g_oil;
static MiniTile m_time, m_dist;
// screen 2 — BOOST
static Gauge g_boost, g_oil2;
static Tile  t_peak;
static float peak_boost = 0;     // max boost (bar) seen this power-on
static bool  peak_seen  = false;
// screen 3 — DPF
static Gauge g_dpf_temp;
static Tile  t_soot;
static MiniTile m_press;
static lv_obj_t *glow_ind;       // icon-only glow indicator (no numeric value)
static lv_obj_t *regen_badge;
// screen 4 — POWER
static Gauge g_soc;
static Tile  t_charge;
// hidden STATS screen
static lv_obj_t *stats_screen;
static lv_obj_t *stats_active_badge;
static lv_obj_t *s_count, *s_km_since, *s_km_avg, *s_last_dur, *s_soot, *s_live;
static bool stats_visible = false;

// ── Builders ─────────────────────────────────────────────────

static lv_obj_t *make_screen()
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(COL_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    return scr;
}

static StatusBar make_status_bar(lv_obj_t *parent, int screen_idx)
{
    StatusBar sb;

    sb.bt = lv_label_create(parent);
    lv_label_set_text(sb.bt, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_font(sb.bt, &lv_font_montserrat_14, 0);
    lv_obj_align(sb.bt, LV_ALIGN_TOP_LEFT, 10, 8);

    sb.text = lv_label_create(parent);
    lv_label_set_text(sb.text, "Scanning...");
    lv_obj_set_style_text_font(sb.text, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sb.text, lv_color_hex(COL_DIM), 0);
    lv_obj_align(sb.text, LV_ALIGN_TOP_LEFT, 30, 8);

    // 20 px variant generated at that size — LVGL 8 can't zoom A8 bitmaps,
    // a runtime lv_img_set_zoom() here rendered nothing at all
    sb.glow = lv_img_create(parent);
    lv_img_set_src(sb.glow, &icon_glow_sm);
    lv_obj_set_style_img_recolor(sb.glow, lv_color_hex(COL_WARN), 0);
    lv_obj_set_style_img_recolor_opa(sb.glow, LV_OPA_COVER, 0);
    lv_obj_align(sb.glow, LV_ALIGN_TOP_RIGHT, -52, 5);
    lv_obj_add_flag(sb.glow, LV_OBJ_FLAG_HIDDEN);

    // page indicator dots
    for (int i = 0; i < SCREEN_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(parent);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_set_style_bg_color(dot,
            lv_color_hex(i == screen_idx ? COL_TEXT : COL_TRACK), 0);
        lv_obj_align(dot, LV_ALIGN_TOP_RIGHT,
                     -8 - (SCREEN_COUNT - 1 - i) * 10, 12);
        sb.dots[i] = dot;
    }
    return sb;
}

static Gauge make_gauge(lv_obj_t *parent, int y,
                        const lv_img_dsc_t *icon_src, const char *name,
                        const char *unit, int vmin, int vmax, int mult = 1)
{
    Gauge g;
    g.vmin = vmin;
    g.vmax = vmax;
    g.mult = mult;

    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 240, 180);
    lv_obj_set_pos(cont, 0, y);
    lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(cont, 0, 0);
    lv_obj_set_style_pad_all(cont, 0, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    g.arc = lv_arc_create(cont);
    lv_obj_set_size(g.arc, 176, 176);
    lv_obj_align(g.arc, LV_ALIGN_TOP_MID, 0, 4);
    lv_arc_set_rotation(g.arc, 135);
    lv_arc_set_bg_angles(g.arc, 0, 270);
    lv_arc_set_range(g.arc, vmin, vmax);
    lv_arc_set_value(g.arc, vmin);
    lv_obj_remove_style(g.arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g.arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g.arc, 12, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g.arc, 12, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(g.arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(g.arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g.arc, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_arc_color(g.arc, lv_color_hex(COL_DIM), LV_PART_INDICATOR);

    g.icon = lv_img_create(cont);
    lv_img_set_src(g.icon, icon_src);
    lv_obj_set_style_img_recolor(g.icon, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_img_recolor_opa(g.icon, LV_OPA_COVER, 0);
    lv_obj_align(g.icon, LV_ALIGN_TOP_MID, 0, 34);

    g.value = lv_label_create(cont);
    lv_label_set_text(g.value, "-");
    lv_obj_set_style_text_font(g.value, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g.value, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(g.value, LV_ALIGN_TOP_MID, 0, 78);

    g.unit = lv_label_create(cont);
    lv_label_set_text(g.unit, unit);
    lv_obj_set_style_text_font(g.unit, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(g.unit, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(g.unit, LV_ALIGN_TOP_MID, 0, 132);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 156);

    return g;
}

static Tile make_tile(lv_obj_t *parent, int y,
                      const lv_img_dsc_t *icon_src, const char *name,
                      int vmin, int vmax)
{
    Tile t;
    t.vmin = vmin;
    t.vmax = vmax;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 224, 100);
    lv_obj_set_pos(panel, 8, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    t.icon = lv_img_create(panel);
    lv_img_set_src(t.icon, icon_src);
    lv_obj_set_style_img_recolor(t.icon, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_img_recolor_opa(t.icon, LV_OPA_COVER, 0);
    lv_obj_align(t.icon, LV_ALIGN_LEFT_MID, 14, -8);

    t.name = lv_label_create(panel);
    lv_label_set_text(t.name, name);
    lv_obj_set_style_text_font(t.name, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(t.name, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(t.name, 70, 10);

    t.value = lv_label_create(panel);
    lv_label_set_text(t.value, "-");
    lv_obj_set_style_text_font(t.value, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(t.value, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(t.value, 70, 32);

    t.bar = lv_bar_create(panel);
    lv_obj_set_size(t.bar, 196, 8);
    lv_obj_set_pos(t.bar, 14, 78);
    lv_bar_set_range(t.bar, vmin, vmax);
    lv_bar_set_value(t.bar, vmin, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(t.bar, lv_color_hex(COL_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(t.bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(t.bar, lv_color_hex(COL_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(t.bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(t.bar, 4, LV_PART_INDICATOR);

    return t;
}

static MiniTile make_mini_tile(lv_obj_t *parent, int x, int y,
                               const lv_img_dsc_t *icon_src, const char *name)
{
    MiniTile m;

    lv_obj_t *panel = lv_obj_create(parent);
    lv_obj_set_size(panel, 108, 126);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    m.icon = lv_img_create(panel);
    lv_img_set_src(m.icon, icon_src);   // pass a 28 px _sm icon (no A8 zoom)
    lv_obj_set_style_img_recolor(m.icon, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_img_recolor_opa(m.icon, LV_OPA_COVER, 0);
    lv_obj_align(m.icon, LV_ALIGN_TOP_MID, 0, 6);

    m.value = lv_label_create(panel);
    lv_label_set_text(m.value, "-");
    lv_obj_set_style_text_font(m.value, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(m.value, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(m.value, LV_ALIGN_TOP_MID, 0, 46);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -8);

    return m;
}

// Label pair row for the stats screen; returns the value label
static lv_obj_t *make_stat_row(lv_obj_t *parent, int y, const char *name)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, name);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COL_DIM), 0);
    lv_obj_set_pos(lbl, 16, y);

    lv_obj_t *val = lv_label_create(parent);
    lv_label_set_text(val, "-");
    lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(val, lv_color_hex(COL_TEXT), 0);
    lv_obj_set_pos(val, 16, y + 20);

    return val;
}

static void make_stats_screen()
{
    stats_screen = make_screen();

    lv_obj_t *icon = lv_img_create(stats_screen);
    lv_img_set_src(icon, &icon_dpf);
    lv_obj_set_style_img_recolor(icon, lv_color_hex(COL_ACCENT), 0);
    lv_obj_set_style_img_recolor_opa(icon, LV_OPA_COVER, 0);
    lv_obj_align(icon, LV_ALIGN_TOP_MID, 0, 18);

    lv_obj_t *title = lv_label_create(stats_screen);
    lv_label_set_text(title, "REGEN STATS");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(COL_TEXT), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 64);

    stats_active_badge = lv_label_create(stats_screen);
    lv_label_set_text(stats_active_badge, " REGEN ACTIVE ");
    lv_obj_set_style_text_font(stats_active_badge, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(stats_active_badge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(stats_active_badge, lv_color_hex(COL_WARN), 0);
    lv_obj_set_style_bg_opa(stats_active_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(stats_active_badge, 6, 0);
    lv_obj_set_style_pad_all(stats_active_badge, 4, 0);
    lv_obj_align(stats_active_badge, LV_ALIGN_TOP_MID, 0, 92);
    lv_obj_add_flag(stats_active_badge, LV_OBJ_FLAG_HIDDEN);

    s_count    = make_stat_row(stats_screen, 130, "REGENS COUNTED");
    s_km_since = make_stat_row(stats_screen, 190, "KM SINCE LAST");
    s_km_avg   = make_stat_row(stats_screen, 250, "AVG KM BETWEEN");
    s_last_dur = make_stat_row(stats_screen, 310, "LAST DURATION");
    s_soot     = make_stat_row(stats_screen, 370, "LAST SOOT BURNED");
    s_live     = make_stat_row(stats_screen, 430, "DPF NOW");

    lv_obj_t *hint = lv_label_create(stats_screen);
    lv_label_set_text(hint, "double-click to return");
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(COL_DIM), 0);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -10);
}

// ── Screen construction ──────────────────────────────────────

void dashboard_init()
{
    // Screen 1 — DRIVE
    screens[0] = make_screen();
    bars[0] = make_status_bar(screens[0], 0);
    g_coolant = make_gauge(screens[0], 30, &icon_coolant, "COOLANT",
                           "\xC2\xB0""C", 0, COOLANT_MAX_SCALE);
    g_oil     = make_gauge(screens[0], 216, &icon_oil, "OIL",
                           "\xC2\xB0""C", 0, OIL_MAX_SCALE);
    m_time = make_mini_tile(screens[0], 8,   404, &icon_clock_sm, "TRIP TIME");
    m_dist = make_mini_tile(screens[0], 124, 404, &icon_road_sm,  "TRIP KM");

    // Screen 2 — BOOST
    screens[1] = make_screen();
    bars[1] = make_status_bar(screens[1], 1);
    g_boost = make_gauge(screens[1], 30, &icon_turbo, "BOOST", "bar",
                         0, BOOST_GAUGE_SCALE, 100);
    g_oil2  = make_gauge(screens[1], 216, &icon_oil, "OIL",
                         "\xC2\xB0""C", 0, OIL_MAX_SCALE);
    t_peak  = make_tile(screens[1], 412, &icon_turbo, "PEAK BOOST  bar",
                        0, BOOST_GAUGE_SCALE);

    // Screen 3 — DPF
    screens[2] = make_screen();
    bars[2] = make_status_bar(screens[2], 2);
    g_dpf_temp = make_gauge(screens[2], 30, &icon_dpf, "DPF TEMP",
                            "\xC2\xB0""C", 0, DPF_TEMP_MAX_SCALE);
    t_soot = make_tile(screens[2], 220, &icon_dpf, "DPF SOOT  g",
                       0, DPF_SOOT_MAX_SCALE);
    m_press = make_mini_tile(screens[2], 8, 328, &icon_pressure_sm, "PRESS mbar");

    // glow: icon-only tile — the coil lights up amber while plugs are active
    {
        lv_obj_t *panel = lv_obj_create(screens[2]);
        lv_obj_set_size(panel, 108, 126);
        lv_obj_set_pos(panel, 124, 328);
        lv_obj_set_style_bg_color(panel, lv_color_hex(COL_PANEL), 0);
        lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(panel, 16, 0);
        lv_obj_set_style_border_width(panel, 0, 0);
        lv_obj_set_style_pad_all(panel, 0, 0);
        lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

        glow_ind = lv_img_create(panel);
        lv_img_set_src(glow_ind, &icon_glow);
        lv_obj_set_style_img_recolor(glow_ind, lv_color_hex(COL_TRACK), 0);
        lv_obj_set_style_img_recolor_opa(glow_ind, LV_OPA_COVER, 0);
        lv_obj_align(glow_ind, LV_ALIGN_CENTER, 0, -12);

        lv_obj_t *lbl = lv_label_create(panel);
        lv_label_set_text(lbl, "GLOW");
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(COL_TEXT), 0);
        lv_obj_align(lbl, LV_ALIGN_BOTTOM_MID, 0, -10);
    }

    regen_badge = lv_label_create(screens[2]);
    lv_label_set_text(regen_badge, " REGEN ");
    lv_obj_set_style_text_font(regen_badge, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(regen_badge, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_color(regen_badge, lv_color_hex(COL_WARN), 0);
    lv_obj_set_style_bg_opa(regen_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(regen_badge, 6, 0);
    lv_obj_set_style_pad_all(regen_badge, 4, 0);
    lv_obj_align(regen_badge, LV_ALIGN_TOP_MID, 0, 470);
    lv_obj_add_flag(regen_badge, LV_OBJ_FLAG_HIDDEN);

    // Screen 4 — POWER
    screens[3] = make_screen();
    bars[3] = make_status_bar(screens[3], 3);
    g_soc = make_gauge(screens[3], 30, &icon_battery, "BATTERY", "%", 0, 100);
    t_charge = make_tile(screens[3], 220, &icon_battery, "CHARGE  A", 0, 120);

    make_stats_screen();

    lv_scr_load(screens[0]);
    active_screen = 0;
}

void dashboard_next_screen()
{
    if (stats_visible) {           // leave the hidden screen first
        dashboard_toggle_stats();
        return;
    }
    active_screen = (active_screen + 1) % SCREEN_COUNT;
    lv_scr_load_anim(screens[active_screen], LV_SCR_LOAD_ANIM_MOVE_LEFT,
                     250, 0, false);
}

void dashboard_toggle_stats()
{
    stats_visible = !stats_visible;
    lv_obj_t *target = stats_visible ? stats_screen : screens[active_screen];
    lv_scr_load_anim(target, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false);
}

uint16_t dashboard_poll_mask()
{
    // always polled: trip integration, regen detection, glow badge
    uint16_t m = (1 << PID_SPEED) | (1 << PID_DPF_TEMP) |
                 (1 << PID_DPF_SOOT) | (1 << PID_GLOW_PLUGS);
    if (stats_visible)
        return m;                    // stats screen shows only DPF live data
    switch (active_screen) {
        case 0: m |= (1 << PID_COOLANT_TEMP) | (1 << PID_OIL_TEMP) |
                     (1 << PID_RUNTIME);                              break;
        case 1: m |= (1 << PID_BOOST_PRESSURE) | (1 << PID_OIL_TEMP); break;
        case 2: m |= (1 << PID_DPF_PRESSURE);                         break;
        case 3: m |= (1 << PID_BATT_CHARGE) | (1 << PID_BATT_STATE);  break;
    }
    return m;
}

// ── Updates ──────────────────────────────────────────────────

// Threshold coloring per metric
static uint32_t coolant_color(float v)
{
    if (v < COOLANT_COLD_MAX) return COL_COLD;
    if (v < COOLANT_RED)      return COL_OK;
    return COL_ALERT;
}

static uint32_t oil_color(float v)
{
    if (v < OIL_COLD_MAX) return COL_COLD;
    if (v < OIL_RED)      return COL_OK;
    return COL_ALERT;
}

static uint32_t dpf_temp_color(float v)
{
    return (v >= DPF_REGEN_TEMP) ? COL_WARN : COL_OK;
}

static uint32_t soot_color(float v)
{
    if (v < DPF_SOOT_WARN * 0.75f) return COL_OK;
    if (v < DPF_SOOT_WARN)         return COL_WARN;
    return COL_ALERT;
}

static uint32_t soc_color(float v)
{
    if (v >= 60) return COL_OK;
    if (v >= 30) return COL_WARN;
    return COL_ALERT;
}

static void update_gauge(Gauge &g, const ObdClient &obd, int pid, uint32_t color)
{
    char buf[16];
    obd.format(pid, buf, sizeof(buf));
    lv_label_set_text(g.value, buf);

    if (obd.valid(pid)) {
        float v = obd.value(pid);
        int iv = (int)(v * g.mult);
        if (iv < g.vmin) iv = g.vmin;
        if (iv > g.vmax) iv = g.vmax;
        lv_arc_set_value(g.arc, iv);
        lv_obj_set_style_arc_color(g.arc, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_style_img_recolor(g.icon, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(g.value, lv_color_hex(COL_TEXT), 0);
    } else {
        lv_arc_set_value(g.arc, g.vmin);
        lv_obj_set_style_arc_color(g.arc, lv_color_hex(COL_TRACK), LV_PART_INDICATOR);
        lv_obj_set_style_img_recolor(g.icon, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_color(g.value, lv_color_hex(COL_DIM), 0);
    }
    lv_obj_align(g.value, LV_ALIGN_TOP_MID, 0, 78);   // re-center after text change
}

static void update_mini(MiniTile &m, const ObdClient &obd, int pid, uint32_t color)
{
    char buf[16];
    obd.format(pid, buf, sizeof(buf));
    lv_label_set_text(m.value, buf);
    if (obd.valid(pid)) {
        lv_obj_set_style_text_color(m.value, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_img_recolor(m.icon, lv_color_hex(color), 0);
    } else {
        lv_obj_set_style_text_color(m.value, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_img_recolor(m.icon, lv_color_hex(COL_DIM), 0);
    }
    lv_obj_align(m.value, LV_ALIGN_TOP_MID, 0, 46);
}

static void update_tile(Tile &t, const ObdClient &obd, int pid, uint32_t color)
{
    char buf[16];
    obd.format(pid, buf, sizeof(buf));
    lv_label_set_text(t.value, buf);

    if (obd.valid(pid)) {
        float v = obd.value(pid);
        int iv = (int)v;
        if (iv < t.vmin) iv = t.vmin;
        if (iv > t.vmax) iv = t.vmax;
        lv_bar_set_value(t.bar, iv, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(t.bar, lv_color_hex(color), LV_PART_INDICATOR);
        lv_obj_set_style_img_recolor(t.icon, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(t.value, lv_color_hex(COL_TEXT), 0);
    } else {
        lv_bar_set_value(t.bar, t.vmin, LV_ANIM_OFF);
        lv_obj_set_style_img_recolor(t.icon, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_text_color(t.value, lv_color_hex(COL_DIM), 0);
    }
}

static void update_status(const ObdClient &obd)
{
    const char *txt;
    uint32_t col;
    switch (obd.status()) {
        case OBD_SCANNING:   txt = "Scanning...";     col = COL_WARN;  break;
        case OBD_CONNECTING: txt = "Connecting...";   col = COL_WARN;  break;
        case OBD_CONNECTED:  txt = "Online";          col = COL_OK;    break;
        default:             txt = "Reconnecting..."; col = COL_ALERT; break;
    }

    bool glow_on = obd.valid(PID_GLOW_PLUGS) && obd.value(PID_GLOW_PLUGS) > 0;

    for (int i = 0; i < SCREEN_COUNT; i++) {
        lv_label_set_text(bars[i].text, txt);
        lv_obj_set_style_text_color(bars[i].text, lv_color_hex(col), 0);
        lv_obj_set_style_text_color(bars[i].bt, lv_color_hex(col), 0);
        if (glow_on) lv_obj_clear_flag(bars[i].glow, LV_OBJ_FLAG_HIDDEN);
        else         lv_obj_add_flag(bars[i].glow, LV_OBJ_FLAG_HIDDEN);
    }
}

// Trip time ("1:07") and distance ("12.4") mini tiles
static void update_trip(const ObdClient &obd)
{
    char buf[16];

    if (obd.valid(PID_RUNTIME)) {
        int secs = (int)obd.value(PID_RUNTIME);
        snprintf(buf, sizeof(buf), "%d:%02d", secs / 3600, (secs % 3600) / 60);
        lv_label_set_text(m_time.value, buf);
        lv_obj_set_style_text_color(m_time.value, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_img_recolor(m_time.icon, lv_color_hex(COL_ACCENT), 0);
    } else {
        lv_label_set_text(m_time.value, "-");
        lv_obj_set_style_text_color(m_time.value, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_img_recolor(m_time.icon, lv_color_hex(COL_DIM), 0);
    }
    lv_obj_align(m_time.value, LV_ALIGN_TOP_MID, 0, 46);

    // distance is computed locally, useful as soon as speed has been seen once
    if (obd.valid(PID_SPEED) || obd.tripKm() > 0) {
        float km = obd.tripKm();
        snprintf(buf, sizeof(buf), km < 100 ? "%.1f" : "%.0f", km);
        lv_label_set_text(m_dist.value, buf);
        lv_obj_set_style_text_color(m_dist.value, lv_color_hex(COL_TEXT), 0);
        lv_obj_set_style_img_recolor(m_dist.icon, lv_color_hex(COL_ACCENT), 0);
    } else {
        lv_label_set_text(m_dist.value, "-");
        lv_obj_set_style_text_color(m_dist.value, lv_color_hex(COL_DIM), 0);
        lv_obj_set_style_img_recolor(m_dist.icon, lv_color_hex(COL_DIM), 0);
    }
    lv_obj_align(m_dist.value, LV_ALIGN_TOP_MID, 0, 46);
}

static void update_stats(const ObdClient &obd, const RegenTracker &regen)
{
    char buf[32];

    snprintf(buf, sizeof(buf), "%u", regen.count());
    lv_label_set_text(s_count, buf);

    snprintf(buf, sizeof(buf), "%.1f km", regen.kmSinceLast());
    lv_label_set_text(s_km_since, buf);

    if (regen.count())
        snprintf(buf, sizeof(buf), "%.0f km", regen.avgKmBetween());
    else
        snprintf(buf, sizeof(buf), "-");
    lv_label_set_text(s_km_avg, buf);

    if (regen.lastDurationS())
        snprintf(buf, sizeof(buf), "%u min", (regen.lastDurationS() + 30) / 60);
    else
        snprintf(buf, sizeof(buf), "-");
    lv_label_set_text(s_last_dur, buf);

    if (regen.count())
        snprintf(buf, sizeof(buf), "%.1f g", regen.lastSootBurned());
    else
        snprintf(buf, sizeof(buf), "-");
    lv_label_set_text(s_soot, buf);

    if (obd.valid(PID_DPF_TEMP) && obd.valid(PID_DPF_SOOT))
        snprintf(buf, sizeof(buf), "%d\xC2\xB0""C   %.1f g",
                 (int)obd.value(PID_DPF_TEMP), obd.value(PID_DPF_SOOT));
    else
        snprintf(buf, sizeof(buf), "-");
    lv_label_set_text(s_live, buf);

    if (regen.active())
        lv_obj_clear_flag(stats_active_badge, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(stats_active_badge, LV_OBJ_FLAG_HIDDEN);
}

void dashboard_update(const ObdClient &obd, const RegenTracker &regen)
{
    update_status(obd);
    update_stats(obd, regen);

    float coolant = obd.valid(PID_COOLANT_TEMP) ? obd.value(PID_COOLANT_TEMP) : 0;
    float oil     = obd.valid(PID_OIL_TEMP)     ? obd.value(PID_OIL_TEMP)     : 0;
    float boost   = obd.valid(PID_BOOST_PRESSURE) ? obd.value(PID_BOOST_PRESSURE) : 0;
    float dpf_t   = obd.valid(PID_DPF_TEMP)     ? obd.value(PID_DPF_TEMP)     : 0;
    float soot    = obd.valid(PID_DPF_SOOT)     ? obd.value(PID_DPF_SOOT)     : 0;
    float soc     = obd.valid(PID_BATT_STATE)   ? obd.value(PID_BATT_STATE)   : 0;

    // Screen 1 — DRIVE
    update_gauge(g_coolant, obd, PID_COOLANT_TEMP, coolant_color(coolant));
    update_gauge(g_oil,     obd, PID_OIL_TEMP,     oil_color(oil));
    update_trip(obd);

    // Screen 2 — BOOST
    update_gauge(g_boost, obd, PID_BOOST_PRESSURE, COL_ACCENT);
    update_gauge(g_oil2,  obd, PID_OIL_TEMP,       oil_color(oil));
    if (obd.valid(PID_BOOST_PRESSURE) && (!peak_seen || boost > peak_boost)) {
        peak_boost = boost;
        peak_seen = true;
    }
    if (peak_seen) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.2f", peak_boost);
        lv_label_set_text(t_peak.value, buf);
        lv_obj_set_style_text_color(t_peak.value, lv_color_hex(COL_TEXT), 0);
        int iv = (int)(peak_boost * 100);
        if (iv < t_peak.vmin) iv = t_peak.vmin;
        if (iv > t_peak.vmax) iv = t_peak.vmax;
        lv_bar_set_value(t_peak.bar, iv, LV_ANIM_OFF);
        lv_obj_set_style_img_recolor(t_peak.icon, lv_color_hex(COL_ACCENT), 0);
    }

    // Screen 3 — DPF
    update_gauge(g_dpf_temp, obd, PID_DPF_TEMP, dpf_temp_color(dpf_t));
    update_tile(t_soot, obd, PID_DPF_SOOT, soot_color(soot));
    update_mini(m_press, obd, PID_DPF_PRESSURE, COL_ACCENT);
    // glow coil: amber = active, dim = off, track = no data
    uint32_t glow_col = !obd.valid(PID_GLOW_PLUGS) ? COL_TRACK
                      : obd.value(PID_GLOW_PLUGS) > 0 ? COL_WARN : COL_DIM;
    lv_obj_set_style_img_recolor(glow_ind, lv_color_hex(glow_col), 0);
    if (obd.valid(PID_DPF_TEMP) && dpf_t >= DPF_REGEN_TEMP)
        lv_obj_clear_flag(regen_badge, LV_OBJ_FLAG_HIDDEN);
    else
        lv_obj_add_flag(regen_badge, LV_OBJ_FLAG_HIDDEN);

    // Screen 4 — POWER
    update_gauge(g_soc, obd, PID_BATT_STATE, soc_color(soc));
    update_tile(t_charge, obd, PID_BATT_CHARGE, COL_ACCENT);
}
