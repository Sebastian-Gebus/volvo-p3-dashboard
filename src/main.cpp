// Car Dashboard v2 — LilyGo T-Display AMOLED (ESP32-S3)
// OBD-II over BLE (ELM327 "IOS-Vlink") → LVGL dashboard.
//
// Button (GPIO0 / side key): click = next screen,
// double-click = hidden regen stats screen, long press = deep sleep.

#include <Arduino.h>
#include <LilyGo_AMOLED.h>
#include <LV_Helper.h>
#include <OneButton.h>

#include "config.h"
#include "obd/ObdClient.h"
#include "obd/RegenTracker.h"
#include "ui/ui_dashboard.h"

static LilyGo_Class amoled;
static OneButton button(BUTTON_PIN, true);
static ObdClient obd;
static RegenTracker regen;

static void go_to_sleep()
{
    regen.persistNow();
    amoled.enableSleep();
    amoled.clearPMU();
    amoled.enableWakeup();
    // longPressStart fires while the button is still held, and ext0 wake is
    // level-triggered — sleeping now would wake the chip right back up.
    while (digitalRead(BUTTON_PIN) == LOW)
        delay(10);
    delay(50);
    esp_sleep_enable_ext0_wakeup(BUTTON_PIN, LOW);
    esp_deep_sleep_start();
}

void setup()
{
    Serial.begin(115200);

    if (!amoled.begin()) {
        while (true) {
            Serial.println("AMOLED board not detected!");
            delay(1000);
        }
    }

    beginLvglHelper(amoled);

    amoled.setRotation(DISPLAY_ROTATION);   // portrait 240x536
    lv_disp_t *disp = lv_disp_get_default();
    disp->driver->hor_res = amoled.width();
    disp->driver->ver_res = amoled.height();
    lv_disp_drv_update(disp, disp->driver);

    amoled.setBrightness(DEFAULT_BRIGHTNESS);

    dashboard_init();

    button.attachClick([]() { dashboard_next_screen(); });
    button.attachDoubleClick([]() { dashboard_toggle_stats(); });
    button.attachLongPressStart([]() { go_to_sleep(); });

    // Boards with a touch home button: also cycle screens. The driver fires
    // this on every touch poll while held — only act on a fresh press
    // (previous fire longer ago than one poll interval).
    amoled.setHomeButtonCallback([](void *) {
        static uint32_t lastFireMs = 0;
        uint32_t now = millis();
        bool newPress = now - lastFireMs > 400;
        lastFireMs = now;
        if (newPress)
            dashboard_next_screen();
    }, NULL);

    regen.begin(); // load persisted stats from NVS
    obd.begin();   // starts BLE scan, non-blocking
}

void loop()
{
    lv_task_handler();
    button.tick();
    obd.setPollMask(dashboard_poll_mask());
    obd.tick();

    static uint32_t lastTrackMs = 0;
    if (millis() - lastTrackMs >= 500) {
        lastTrackMs = millis();
        regen.tick(obd);
    }

    static uint32_t lastUiMs = 0;
    if (millis() - lastUiMs >= 200) {
        lastUiMs = millis();
        dashboard_update(obd, regen);
    }

    delay(5);
}
