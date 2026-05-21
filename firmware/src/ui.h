#pragma once
#include "data.h"
#include "ble.h"

enum screen_t {
    SCREEN_SPLASH,
    SCREEN_USAGE,
    SCREEN_BLUETOOTH,
    SCREEN_PERMISSION,
    SCREEN_CALIBRATE,
    SCREEN_COUNT,
};

void ui_init(void);
void ui_update(const UsageData* data);
void ui_tick_anim(void);
void ui_show_screen(screen_t screen);
void ui_cycle_screen(void);
void ui_toggle_splash(void);
screen_t ui_get_current_screen(void);
void ui_update_ble_status(ble_state_t state, const char* name, const char* mac);
void ui_update_battery(int percent, bool charging);

// Permission prompt
void ui_set_prompt(const char* id, const char* tool, const char* hint);
void ui_clear_prompt(void);
bool ui_has_pending_prompt(void);
// decision: "allow" or "deny". Sends BLE response, clears prompt,
// returns to whichever screen was active before the prompt arrived.
void ui_permission_decide(const char* decision);

// Touch calibration — shows a single target dot in a corner, cycles
// through 4 corners on each tap. touch.cpp's Serial.printf logs raw and
// transformed coords on every press, so the trace reveals which transform
// each IMU quadrant actually needs.
void ui_show_calibration(void);
