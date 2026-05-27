#include "ui.h"
#include "splash.h"
#include <lvgl.h>
#include "logo.h"
#include "icons.h"
#include "hal/board_caps.h"
#include "hal/audio_hal.h"

// Custom fonts (scaled for 314 PPI, ~1.9x from original 165 PPI)
LV_FONT_DECLARE(font_tiempos_56);
LV_FONT_DECLARE(font_tiempos_34);
LV_FONT_DECLARE(font_styrene_48);
LV_FONT_DECLARE(font_styrene_28);
LV_FONT_DECLARE(font_styrene_24);
LV_FONT_DECLARE(font_styrene_20);
LV_FONT_DECLARE(font_styrene_16);
LV_FONT_DECLARE(font_styrene_14);
LV_FONT_DECLARE(font_mono_32);

// Layout values computed from the active board's geometry. Populated once
// in ui_init() and treated as const for the rest of the program. Adding a
// new display size means extending compute_layout() with another
// breakpoint — never editing the screen-builder functions below.
struct Layout {
    int16_t scr_w, scr_h;
    int16_t margin;
    int16_t title_y;
    int16_t content_y;
    int16_t content_w;

    // Usage screen
    int16_t usage_panel_h;
    int16_t usage_panel_gap;
    int16_t usage_bar_y;
    int16_t usage_reset_y;

    // Bluetooth screen
    int16_t bt_info_panel_h;
    int16_t bt_reset_zone_h;
    const lv_font_t* bt_title_font;
    const lv_font_t* bt_status_font;
    const lv_font_t* bt_device_font;
    const lv_font_t* bt_credit_1_font;
    const lv_font_t* bt_credit_2_font;
};
static Layout L = {};

// Pick layout values from the active board's pixel dimensions. The two
// existing boards happen to land on the two breakpoints below; new ports
// inherit the closer one — visually OK, may need a polish pass for
// pixel-perfect alignment but never blocks the port from booting.
static void compute_layout(const BoardCaps& c) {
    L.scr_w = c.width;
    L.scr_h = c.height;
    L.margin = 20;
    L.title_y = 30;

    if (c.height >= 460) {
        // Large layout — tuned for 480x480 (AMOLED-2.16).
        L.content_y = 100;
        L.usage_panel_h = 150;
        L.usage_panel_gap = 16;
        L.usage_bar_y = 56;
        L.usage_reset_y = 94;
        L.bt_info_panel_h = 160;
        L.bt_reset_zone_h = 110;
        L.bt_title_font    = &font_tiempos_56;
        L.bt_status_font   = &font_styrene_48;
        L.bt_device_font   = &font_styrene_28;
        L.bt_credit_1_font = &font_styrene_24;
        L.bt_credit_2_font = &font_styrene_20;
    } else {
        // Compact layout — tuned for 368x448 (AMOLED-1.8).
        L.content_y = 85;
        L.usage_panel_h = 130;
        L.usage_panel_gap = 12;
        L.usage_bar_y = 48;
        L.usage_reset_y = 78;
        L.bt_info_panel_h = 140;
        L.bt_reset_zone_h = 90;
        L.bt_title_font    = &font_tiempos_34;
        L.bt_status_font   = &font_styrene_28;
        L.bt_device_font   = &font_styrene_20;
        L.bt_credit_1_font = &font_styrene_16;
        L.bt_credit_2_font = &font_styrene_14;
    }

    L.content_w = L.scr_w - 2 * L.margin;
}

// Anthropic brand palette — design tokens live in theme.h
#include "theme.h"
#define COL_BG        THEME_BG
#define COL_PANEL     THEME_PANEL
#define COL_TEXT      THEME_TEXT
#define COL_DIM       THEME_DIM
#define COL_ACCENT    THEME_ACCENT
#define COL_GREEN     THEME_GREEN
#define COL_AMBER     THEME_AMBER
#define COL_RED       THEME_RED
#define COL_BAR_BG    THEME_BAR_BG

// ---- Usage screen widgets ----
static lv_obj_t* usage_container;
static lv_obj_t* lbl_title;
static lv_obj_t* bar_session;
static lv_obj_t* lbl_session_pct;
static lv_obj_t* lbl_session_label;
static lv_obj_t* lbl_session_reset;
static lv_obj_t* bar_weekly;
static lv_obj_t* lbl_weekly_pct;
static lv_obj_t* lbl_weekly_label;
static lv_obj_t* lbl_weekly_reset;
static lv_obj_t* lbl_anim;

// ---- Bluetooth screen widgets ----
static lv_obj_t* ble_container;
static lv_obj_t* lbl_ble_status;
static lv_obj_t* lbl_ble_device;
static lv_obj_t* lbl_ble_mac;

// ---- Permission screen widgets ----
static lv_obj_t* perm_container;
static lv_obj_t* lbl_perm_tool;
static lv_obj_t* lbl_perm_hint;
static lv_obj_t* btn_allow;
static lv_obj_t* btn_deny;

// ---- Pending-prompt badge (overlay, shown on non-permission screens) ----
static lv_obj_t* badge_btn;

// ---- Battery indicator (shared, on top) ----
static lv_obj_t* battery_img;
static lv_obj_t* logo_img;
static lv_image_dsc_t battery_dscs[5];  // empty, low, medium, full, charging

// ---- Shared ----
static lv_image_dsc_t logo_dsc;
static screen_t current_screen = SCREEN_USAGE;

// ---- Permission state ----
static PromptData prompt_state = {};
static screen_t screen_before_prompt = SCREEN_USAGE;

// ---- Settings (volatile — reset to defaults on boot; NVS persistence is
//      a follow-up). The toggles actually gate behaviour, they aren't
//      decorative.
static bool settings_permissions_enabled = true;
static bool settings_sound_enabled       = true;
static bool settings_pace_enabled        = true;
static bool settings_autoswitch_enabled  = false;  // auto-open prompt screen
static int  settings_volume_pct          = 70;

// Last UsageData we rendered, kept so display-affecting toggles can
// re-render instantly instead of waiting for the next 60 s BLE poll.
static UsageData last_usage = {};

// ---- Settings screen widgets ----
static lv_obj_t* settings_container;
static lv_obj_t* sw_permissions;
static lv_obj_t* sw_sound;
static lv_obj_t* sw_pace;
static lv_obj_t* sw_autoswitch;
static lv_obj_t* slider_volume;

// Animation state
static uint32_t anim_last_ms = 0;
static uint8_t anim_spinner_idx = 0;
static uint8_t anim_phase = 0;
static uint8_t anim_msg_idx = 0;
static uint32_t anim_msg_start = 0;
#define ANIM_MSG_MS     4000

static const char* const spinner_frames[] = {
    "\xC2\xB7", "\xE2\x9C\xBB", "\xE2\x9C\xBD",
    "\xE2\x9C\xB6", "\xE2\x9C\xB3", "\xE2\x9C\xA2",
};
#define SPINNER_COUNT 6
#define SPINNER_PHASES (2 * (SPINNER_COUNT - 1))  // 10: ping-pong 0..5..0

static const uint16_t spinner_ms[SPINNER_COUNT] = {
    260, 130, 130, 130, 130, 260,
};

static const char* const anim_messages[] = {
    "Accomplishing", "Elucidating", "Perusing",
    "Actioning", "Enchanting", "Philosophising",
    "Actualizing", "Envisioning", "Pondering",
    "Baking", "Finagling", "Pontificating",
    "Booping", "Flibbertigibbeting", "Processing",
    "Brewing", "Forging", "Puttering",
    "Calculating", "Forming", "Puzzling",
    "Cerebrating", "Frolicking", "Reticulating",
    "Channelling", "Generating", "Ruminating",
    "Churning", "Germinating", "Scheming",
    "Clauding", "Hatching", "Schlepping",
    "Coalescing", "Herding", "Shimmying",
    "Cogitating", "Honking", "Shucking",
    "Combobulating", "Hustling", "Simmering",
    "Computing", "Ideating", "Smooshing",
    "Concocting", "Imagining", "Spelunking",
    "Conjuring", "Incubating", "Spinning",
    "Considering", "Inferring", "Stewing",
    "Contemplating", "Jiving", "Sussing",
    "Cooking", "Manifesting", "Synthesizing",
    "Crafting", "Marinating", "Thinking",
    "Creating", "Meandering", "Tinkering",
    "Crunching", "Moseying", "Transmuting",
    "Deciphering", "Mulling", "Unfurling",
    "Deliberating", "Mustering", "Unravelling",
    "Determining", "Musing", "Vibing",
    "Discombobulating", "Noodling", "Wandering",
    "Divining", "Percolating", "Whirring",
    "Doing", "Wibbling",
    "Effecting", "Wizarding",
    "Working", "Wrangling",
};
#define ANIM_MSG_COUNT (sizeof(anim_messages) / sizeof(anim_messages[0]))

static lv_color_t pct_color(float pct) {
    if (pct >= 80.0f) return COL_RED;
    if (pct >= 50.0f) return COL_AMBER;
    return COL_GREEN;
}

static void format_reset_time(int mins, char* buf, size_t len) {
    if (mins < 0) {
        snprintf(buf, len, "---");
    } else if (mins < 60) {
        snprintf(buf, len, "Resets in %dm", mins);
    } else if (mins < 1440) {
        snprintf(buf, len, "Resets in %dh %dm", mins / 60, mins % 60);
    } else {
        snprintf(buf, len, "Resets in %dd %dh", mins / 1440, (mins % 1440) / 60);
    }
}

// Forward decls — callbacks defined near ui_show_screen below
static void global_click_cb(lv_event_t* e);
static void ble_reset_click_cb(lv_event_t* e);

static lv_obj_t* make_panel(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* panel = lv_obj_create(parent);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, w, h);
    lv_obj_set_style_bg_color(panel, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 8, 0);
    lv_obj_set_style_border_width(panel, 0, 0);
    lv_obj_set_style_pad_left(panel, 16, 0);
    lv_obj_set_style_pad_right(panel, 16, 0);
    lv_obj_set_style_pad_top(panel, 12, 0);
    lv_obj_set_style_pad_bottom(panel, 12, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);
    return panel;
}

static lv_obj_t* make_bar(lv_obj_t* parent, int x, int y, int w, int h) {
    lv_obj_t* bar = lv_bar_create(parent);
    lv_obj_set_pos(bar, x, y);
    lv_obj_set_size(bar, w, h);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(bar, COL_BAR_BG, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 6, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, COL_GREEN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 6, LV_PART_INDICATOR);
    return bar;
}

static void init_icon_dsc(lv_image_dsc_t* dsc, int w, int h, const uint16_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565;
    dsc->header.stride = w * 2;
    dsc->data = (const uint8_t*)data;
    dsc->data_size = w * h * 2;
}

static void init_icon_dsc_rgb565a8(lv_image_dsc_t* dsc, int w, int h, const uint8_t* data) {
    dsc->header.w = w;
    dsc->header.h = h;
    dsc->header.cf = LV_COLOR_FORMAT_RGB565A8;
    dsc->header.stride = w * 2;
    dsc->data = data;
    dsc->data_size = w * h * 3;
}

static lv_obj_t* make_pill(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_style_bg_color(lbl, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(lbl, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_left(lbl, 18, 0);
    lv_obj_set_style_pad_right(lbl, 18, 0);
    lv_obj_set_style_pad_top(lbl, 6, 0);
    lv_obj_set_style_pad_bottom(lbl, 6, 0);
    return lbl;
}

static void init_battery_icons(void) {
    init_icon_dsc_rgb565a8(&battery_dscs[0], ICON_BATTERY_W, ICON_BATTERY_H, icon_battery_data);
    init_icon_dsc_rgb565a8(&battery_dscs[1], ICON_BATTERY_LOW_W, ICON_BATTERY_LOW_H, icon_battery_low_data);
    init_icon_dsc_rgb565a8(&battery_dscs[2], ICON_BATTERY_MEDIUM_W, ICON_BATTERY_MEDIUM_H, icon_battery_medium_data);
    init_icon_dsc_rgb565a8(&battery_dscs[3], ICON_BATTERY_FULL_W, ICON_BATTERY_FULL_H, icon_battery_full_data);
    init_icon_dsc_rgb565a8(&battery_dscs[4], ICON_BATTERY_CHARGING_W, ICON_BATTERY_CHARGING_H, icon_battery_charging_data);
}

// ======== Usage Screen ========

static void make_usage_panel(lv_obj_t* parent, int y, const char* pill_text,
                             lv_obj_t** out_pct, lv_obj_t** out_pill,
                             lv_obj_t** out_bar, lv_obj_t** out_reset) {
    lv_obj_t* panel = make_panel(parent, L.margin, y, L.content_w, L.usage_panel_h);

    *out_pct = lv_label_create(panel);
    lv_label_set_text(*out_pct, "---%");
    lv_obj_set_style_text_font(*out_pct, &font_styrene_48, 0);
    lv_obj_set_style_text_color(*out_pct, COL_TEXT, 0);
    lv_obj_set_pos(*out_pct, 0, 0);

    *out_pill = make_pill(panel, pill_text);
    lv_obj_align(*out_pill, LV_ALIGN_TOP_RIGHT, 0, 1);

    *out_bar = make_bar(panel, 0, L.usage_bar_y, L.content_w - 32, 24);

    *out_reset = lv_label_create(panel);
    lv_label_set_text(*out_reset, "---");
    lv_obj_set_style_text_font(*out_reset, &font_styrene_28, 0);
    lv_obj_set_style_text_color(*out_reset, COL_DIM, 0);
    lv_obj_set_pos(*out_reset, 0, L.usage_reset_y);
}

static void init_usage_screen(lv_obj_t* scr) {
    usage_container = lv_obj_create(scr);
    lv_obj_set_size(usage_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(usage_container, 0, 0);
    lv_obj_set_style_bg_opa(usage_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(usage_container, 0, 0);
    lv_obj_set_style_pad_all(usage_container, 0, 0);
    lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(usage_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lbl_title = lv_label_create(usage_container);
    lv_label_set_text(lbl_title, "Usage");
    lv_obj_set_style_text_font(lbl_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_title, COL_TEXT, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_MID, 16, L.title_y);

    make_usage_panel(usage_container, L.content_y, "Current",
                     &lbl_session_pct, &lbl_session_label,
                     &bar_session, &lbl_session_reset);
    make_usage_panel(usage_container,
                     L.content_y + L.usage_panel_h + L.usage_panel_gap, "Weekly",
                     &lbl_weekly_pct, &lbl_weekly_label,
                     &bar_weekly, &lbl_weekly_reset);

    lbl_anim = lv_label_create(usage_container);
    lv_label_set_text(lbl_anim, "");
    lv_obj_set_style_text_font(lbl_anim, &font_mono_32, 0);
    lv_obj_set_style_text_color(lbl_anim, COL_ACCENT, 0);
    lv_obj_align(lbl_anim, LV_ALIGN_BOTTOM_MID, 0, -15);
}

// ======== Bluetooth Screen ========

static void init_bluetooth_screen(lv_obj_t* scr) {
    ble_container = lv_obj_create(scr);
    lv_obj_set_size(ble_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(ble_container, 0, 0);
    lv_obj_set_style_bg_opa(ble_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ble_container, 0, 0);
    lv_obj_set_style_pad_all(ble_container, 0, 0);
    lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(ble_container, global_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl_ble_title = lv_label_create(ble_container);
    lv_label_set_text(lbl_ble_title, "Bluetooth");
    lv_obj_set_style_text_font(lbl_ble_title, L.bt_title_font, 0);
    lv_obj_set_style_text_color(lbl_ble_title, COL_TEXT, 0);
    lv_obj_align(lbl_ble_title, LV_ALIGN_TOP_MID, 16, L.title_y);

    lv_obj_t* p_info = make_panel(ble_container, L.margin, L.content_y,
                                  L.content_w, L.bt_info_panel_h);

    static lv_image_dsc_t icon_bt_dsc;
    init_icon_dsc(&icon_bt_dsc, ICON_BLUETOOTH_W, ICON_BLUETOOTH_H, icon_bluetooth_data);

    lv_obj_t* bt_img = lv_image_create(p_info);
    lv_image_set_src(bt_img, &icon_bt_dsc);
    lv_obj_set_pos(bt_img, 0, 0);

    lbl_ble_status = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_status, "Initializing...");
    lv_obj_set_style_text_font(lbl_ble_status, L.bt_status_font, 0);
    lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_status, 56, 2);

    lbl_ble_device = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_device, "Device: ---");
    lv_obj_set_style_text_font(lbl_ble_device, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_ble_device, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_device, 0, 64);

    lbl_ble_mac = lv_label_create(p_info);
    lv_label_set_text(lbl_ble_mac, "Address: ---");
    lv_obj_set_style_text_font(lbl_ble_mac, L.bt_device_font, 0);
    lv_obj_set_style_text_color(lbl_ble_mac, COL_DIM, 0);
    lv_obj_set_pos(lbl_ble_mac, 0, 100);

    int reset_y = L.content_y + L.bt_info_panel_h + 16;
    lv_obj_t* reset_zone = lv_obj_create(ble_container);
    lv_obj_set_pos(reset_zone, L.margin, reset_y);
    lv_obj_set_size(reset_zone, L.content_w, L.bt_reset_zone_h);
    lv_obj_set_style_bg_color(reset_zone, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(reset_zone, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(reset_zone, 8, 0);
    lv_obj_set_style_border_width(reset_zone, 0, 0);
    lv_obj_set_style_pad_column(reset_zone, 14, 0);
    lv_obj_set_flex_flow(reset_zone, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(reset_zone, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(reset_zone, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(reset_zone, ble_reset_click_cb, LV_EVENT_CLICKED, NULL);

    static lv_image_dsc_t icon_trash_dsc;
    init_icon_dsc(&icon_trash_dsc, ICON_TRASH2_W, ICON_TRASH2_H, icon_trash2_data);
    lv_obj_t* trash_img = lv_image_create(reset_zone);
    lv_image_set_src(trash_img, &icon_trash_dsc);

    lv_obj_t* reset_lbl = lv_label_create(reset_zone);
    lv_label_set_text(reset_lbl, "Reset Bluetooth");
    lv_obj_set_style_text_font(reset_lbl, L.bt_device_font, 0);
    lv_obj_set_style_text_color(reset_lbl, COL_DIM, 0);

    lv_obj_t* lbl_credit = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit, "Built by @hermannbjorgvin");
    lv_obj_set_style_text_font(lbl_credit, L.bt_credit_1_font, 0);
    lv_obj_set_style_text_color(lbl_credit, COL_DIM, 0);
    lv_obj_align(lbl_credit, LV_ALIGN_BOTTOM_MID, 0, -46);

    lv_obj_t* lbl_credit2 = lv_label_create(ble_container);
    lv_label_set_text(lbl_credit2, "Clawd animation by @amaanbuilds");
    lv_obj_set_style_text_font(lbl_credit2, L.bt_credit_2_font, 0);
    lv_obj_set_style_text_color(lbl_credit2, COL_DIM, 0);
    lv_obj_align(lbl_credit2, LV_ALIGN_BOTTOM_MID, 0, -20);

    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Permission Screen ========

static void perm_allow_click_cb(lv_event_t* e) {
    (void)e;
    ui_permission_decide("allow");
}

static void perm_deny_click_cb(lv_event_t* e) {
    (void)e;
    ui_permission_decide("deny");
}

static lv_obj_t* make_decision_button(lv_obj_t* parent, const char* text,
                                      lv_color_t color, int x, int y, int w, int h,
                                      lv_event_cb_t cb) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, color, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 12, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);
    return btn;
}

static void init_permission_screen(lv_obj_t* scr) {
    perm_container = lv_obj_create(scr);
    lv_obj_set_size(perm_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(perm_container, 0, 0);
    lv_obj_set_style_bg_opa(perm_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(perm_container, 0, 0);
    lv_obj_set_style_pad_all(perm_container, 0, 0);
    lv_obj_clear_flag(perm_container, LV_OBJ_FLAG_SCROLLABLE);
    // No global_click_cb here — taps on this screen go to Allow/Deny buttons.

    // Title matches the header rhythm used by the Usage / Bluetooth screens:
    // short word, same font + position, so the logo and battery stay visible
    // and uncovered. Keeping it brief leaves the centre clear for the long
    // tool name underneath.
    lv_obj_t* lbl_perm_title = lv_label_create(perm_container);
    lv_label_set_text(lbl_perm_title, "Allow?");
    lv_obj_set_style_text_font(lbl_perm_title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(lbl_perm_title, COL_ACCENT, 0);
    lv_obj_align(lbl_perm_title, LV_ALIGN_TOP_MID, 16, L.title_y);

    // Tool name (big)
    lbl_perm_tool = lv_label_create(perm_container);
    lv_label_set_text(lbl_perm_tool, "Tool");
    lv_obj_set_style_text_font(lbl_perm_tool, &font_styrene_48, 0);
    lv_obj_set_style_text_color(lbl_perm_tool, COL_TEXT, 0);
    lv_obj_align(lbl_perm_tool, LV_ALIGN_TOP_MID, 0, L.content_y);

    // Hint (wrapped)
    int hint_y = L.content_y + 70;
    int btn_h = 90;
    int btn_y = L.scr_h - btn_h - L.margin;
    int hint_h = btn_y - hint_y - 16;
    lbl_perm_hint = lv_label_create(perm_container);
    lv_label_set_text(lbl_perm_hint, "");
    lv_obj_set_style_text_font(lbl_perm_hint, &font_styrene_24, 0);
    lv_obj_set_style_text_color(lbl_perm_hint, COL_DIM, 0);
    lv_obj_set_style_text_align(lbl_perm_hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(lbl_perm_hint, L.margin, hint_y);
    lv_obj_set_size(lbl_perm_hint, L.content_w, hint_h);
    lv_label_set_long_mode(lbl_perm_hint, LV_LABEL_LONG_WRAP);

    // Allow / Deny side-by-side at the bottom
    int gap = 16;
    int btn_w = (L.content_w - gap) / 2;
    btn_allow = make_decision_button(perm_container, "Allow", COL_GREEN,
                                     L.margin, btn_y, btn_w, btn_h, perm_allow_click_cb);
    btn_deny  = make_decision_button(perm_container, "Deny", COL_RED,
                                     L.margin + btn_w + gap, btn_y, btn_w, btn_h, perm_deny_click_cb);

    lv_obj_add_flag(perm_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Calibration Screen ========

static lv_obj_t* cal_container;
static lv_obj_t* cal_status;
static lv_obj_t* cal_dot;
static uint8_t  cal_idx = 0;

struct CalTarget { int cx, cy; const char* name; };
static const CalTarget cal_targets[4] = {
    {40,  40,  "1: TOP-LEFT"},
    {440, 40,  "2: TOP-RIGHT"},
    {440, 440, "3: BOTTOM-RIGHT"},
    {40,  440, "4: BOTTOM-LEFT"},
};

static void cal_render(void) {
    const CalTarget& t = cal_targets[cal_idx];
    lv_obj_set_pos(cal_dot, t.cx - 20, t.cy - 20);  // dot is 40x40
    char buf[64];
    snprintf(buf, sizeof(buf), "Tap dot %s", t.name);
    lv_label_set_text(cal_status, buf);
    Serial.printf("CAL target idx=%u name=%s content=(%d,%d)\n",
        cal_idx, t.name, t.cx, t.cy);
}

static void cal_click_cb(lv_event_t* e) {
    (void)e;
    cal_idx = (cal_idx + 1) % 4;
    cal_render();
}

static void init_calibration_screen(lv_obj_t* scr) {
    cal_container = lv_obj_create(scr);
    lv_obj_set_size(cal_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(cal_container, 0, 0);
    lv_obj_set_style_bg_color(cal_container, COL_BG, 0);
    lv_obj_set_style_bg_opa(cal_container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(cal_container, 0, 0);
    lv_obj_set_style_pad_all(cal_container, 0, 0);
    lv_obj_clear_flag(cal_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(cal_container, cal_click_cb, LV_EVENT_CLICKED, NULL);

    cal_status = lv_label_create(cal_container);
    lv_label_set_text(cal_status, "Tap dot");
    lv_obj_set_style_text_font(cal_status, &font_styrene_28, 0);
    lv_obj_set_style_text_color(cal_status, COL_TEXT, 0);
    lv_obj_align(cal_status, LV_ALIGN_CENTER, 0, 0);

    cal_dot = lv_obj_create(cal_container);
    lv_obj_set_size(cal_dot, 40, 40);
    lv_obj_set_style_bg_color(cal_dot, COL_AMBER, 0);
    lv_obj_set_style_bg_opa(cal_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cal_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(cal_dot, 0, 0);
    lv_obj_clear_flag(cal_dot, LV_OBJ_FLAG_SCROLLABLE);
    // Bubble so the screen-level handler picks up the click — we don't care
    // *where* exactly the user tapped, only that they tapped intending the dot.
    lv_obj_add_flag(cal_dot, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_add_flag(cal_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Settings Screen ========

static void settings_volume_cb(lv_event_t* e) {
    int v = lv_slider_get_value((lv_obj_t*)lv_event_get_target(e));
    settings_volume_pct = v;
    audio_hal_set_volume(v);
}

static void settings_volume_release_cb(lv_event_t* e) {
    (void)e;
    // Audible preview on release — quick way to check the new level.
    audio_hal_chime();
}

static void settings_perm_cb(lv_event_t* e) {
    settings_permissions_enabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e),
                                                    LV_STATE_CHECKED);
}
static void settings_sound_cb(lv_event_t* e) {
    settings_sound_enabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e),
                                              LV_STATE_CHECKED);
}
static void settings_pace_cb(lv_event_t* e) {
    settings_pace_enabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e),
                                             LV_STATE_CHECKED);
    // Re-render against the cached payload so the weekly label flips
    // immediately instead of waiting for the daemon's next 60 s poll.
    if (last_usage.valid) ui_update(&last_usage);
}
static void settings_autoswitch_cb(lv_event_t* e) {
    settings_autoswitch_enabled = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e),
                                                   LV_STATE_CHECKED);
}

static void settings_test_chime_cb(lv_event_t* e) {
    (void)e;
    audio_hal_chime();
}

static void settings_calibrate_cb(lv_event_t* e) {
    (void)e;
    ui_show_calibration();
}

// Single labelled toggle row: "Label" on the left, switch on the right.
static lv_obj_t* make_toggle_row(lv_obj_t* parent, int y, const char* label,
                                 bool initial, lv_event_cb_t cb) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_set_pos(lbl, L.margin, y);

    lv_obj_t* sw = lv_switch_create(parent);
    lv_obj_set_size(sw, 70, 36);
    lv_obj_set_pos(sw, L.scr_w - L.margin - 70, y);
    if (initial) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

static lv_obj_t* make_action_button(lv_obj_t* parent, const char* text,
                                    int x, int y, int w, int h, lv_event_cb_t cb) {
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);
    return btn;
}

static void init_settings_screen(lv_obj_t* scr) {
    settings_container = lv_obj_create(scr);
    lv_obj_set_size(settings_container, L.scr_w, L.scr_h);
    lv_obj_set_pos(settings_container, 0, 0);
    lv_obj_set_style_bg_opa(settings_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(settings_container, 0, 0);
    lv_obj_set_style_pad_all(settings_container, 0, 0);
    lv_obj_clear_flag(settings_container, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title = lv_label_create(settings_container);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &font_tiempos_56, 0);
    lv_obj_set_style_text_color(title, COL_TEXT, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 16, L.title_y);

    // Volume — "Volume N%" label + slider row.
    int y = L.content_y;
    lv_obj_t* vol_lbl = lv_label_create(settings_container);
    lv_label_set_text(vol_lbl, "Volume");
    lv_obj_set_style_text_font(vol_lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(vol_lbl, COL_TEXT, 0);
    lv_obj_set_pos(vol_lbl, L.margin, y);
    slider_volume = lv_slider_create(settings_container);
    lv_obj_set_size(slider_volume, L.content_w, 16);
    lv_obj_set_pos(slider_volume, L.margin, y + 40);
    lv_slider_set_range(slider_volume, 0, 100);
    lv_slider_set_value(slider_volume, settings_volume_pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(slider_volume, settings_volume_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider_volume, settings_volume_release_cb,
                        LV_EVENT_RELEASED, NULL);

    // Toggle rows
    y += 72;
    sw_permissions = make_toggle_row(settings_container, y, "Permissions",
                                     settings_permissions_enabled, settings_perm_cb);
    y += 46;
    sw_sound       = make_toggle_row(settings_container, y, "Sound",
                                     settings_sound_enabled, settings_sound_cb);
    y += 46;
    sw_pace        = make_toggle_row(settings_container, y, "Pace",
                                     settings_pace_enabled, settings_pace_cb);
    y += 46;
    sw_autoswitch  = make_toggle_row(settings_container, y, "Auto-open",
                                     settings_autoswitch_enabled, settings_autoswitch_cb);

    // Two action buttons side-by-side at the bottom.
    int btn_h = 70;
    int btn_y = L.scr_h - btn_h - L.margin;
    int gap = 16;
    int btn_w = (L.content_w - gap) / 2;
    make_action_button(settings_container, "Test chime",
                       L.margin, btn_y, btn_w, btn_h, settings_test_chime_cb);
    make_action_button(settings_container, "Calibrate",
                       L.margin + btn_w + gap, btn_y, btn_w, btn_h,
                       settings_calibrate_cb);

    lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
}

// ======== Pending-prompt badge ========

static void badge_click_cb(lv_event_t* e) {
    (void)e;
    if (prompt_state.active) ui_show_screen(SCREEN_PERMISSION);
}

static void init_badge(lv_obj_t* scr) {
    badge_btn = lv_obj_create(scr);
    int sz = 48;
    // Sit to the left of the battery icon
    int x = L.scr_w - 48 - L.margin - sz - 10;
    int y = L.title_y - 2;
    lv_obj_set_pos(badge_btn, x, y);
    lv_obj_set_size(badge_btn, sz, sz);
    lv_obj_set_style_bg_color(badge_btn, COL_RED, 0);
    lv_obj_set_style_bg_opa(badge_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(badge_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(badge_btn, 3, 0);
    lv_obj_set_style_border_color(badge_btn, COL_TEXT, 0);
    lv_obj_set_style_border_opa(badge_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(badge_btn, 0, 0);
    lv_obj_clear_flag(badge_btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(badge_btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(badge_btn, badge_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* lbl = lv_label_create(badge_btn);
    lv_label_set_text(lbl, "?");
    lv_obj_set_style_text_font(lbl, &font_styrene_28, 0);
    lv_obj_set_style_text_color(lbl, COL_TEXT, 0);
    lv_obj_center(lbl);

    lv_obj_add_flag(badge_btn, LV_OBJ_FLAG_HIDDEN);
}

static void apply_badge_visibility(void) {
    if (!badge_btn) return;
    bool show = prompt_state.active && current_screen != SCREEN_PERMISSION;
    if (show) lv_obj_clear_flag(badge_btn, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_add_flag(badge_btn, LV_OBJ_FLAG_HIDDEN);
}

// ======== Public API ========

void ui_init(void) {
    compute_layout(board_caps());

    lv_obj_t* scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, COL_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    init_icon_dsc_rgb565a8(&logo_dsc, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    init_battery_icons();

    init_usage_screen(scr);
    init_bluetooth_screen(scr);
    init_permission_screen(scr);
    init_calibration_screen(scr);
    init_settings_screen(scr);
    splash_init(scr);

    if (splash_get_root()) {
        lv_obj_add_event_cb(splash_get_root(), global_click_cb, LV_EVENT_CLICKED, NULL);
    }

    logo_img = lv_image_create(scr);
    lv_image_set_src(logo_img, &logo_dsc);
    lv_obj_set_pos(logo_img, L.margin, L.title_y - 10);

    battery_img = lv_image_create(scr);
    lv_image_set_src(battery_img, &battery_dscs[0]);
    lv_obj_set_pos(battery_img, L.scr_w - 48 - L.margin, L.title_y);

    // Badge created last so it sits on top of everything else.
    init_badge(scr);
}

void ui_update(const UsageData* data) {
    if (!data->valid) return;
    last_usage = *data;

    int s_pct = (int)(data->session_pct + 0.5f);

    lv_label_set_text_fmt(lbl_session_pct, "%d%%", s_pct);
    lv_bar_set_value(bar_session, s_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_session, pct_color(data->session_pct), LV_PART_INDICATOR);

    char buf[48];
    format_reset_time(data->session_reset_mins, buf, sizeof(buf));
    lv_label_set_text(lbl_session_reset, buf);

    int w_pct = (int)(data->weekly_pct + 0.5f);
    lv_label_set_text_fmt(lbl_weekly_pct, "%d%%", w_pct);
    lv_bar_set_value(bar_weekly, w_pct, LV_ANIM_ON);
    lv_obj_set_style_bg_color(bar_weekly, pct_color(data->weekly_pct), LV_PART_INDICATOR);

    char reset_buf[40];
    format_reset_time(data->weekly_reset_mins, reset_buf, sizeof(reset_buf));
    if (data->weekly_reset_mins > 0 && settings_pace_enabled) {
        // Pace gauge: if you stay <= this %, you're on track to not exceed
        // the weekly quota. days_remaining is ceiling of wr / 1440 so a
        // partial day rounds up (you still own today's slice).
        int days = (data->weekly_reset_mins + 1439) / 1440;
        if (days < 1) days = 1;
        int today_max = 100 / days;
        snprintf(buf, sizeof(buf), "%s | Today %d%%", reset_buf, today_max);
    } else {
        snprintf(buf, sizeof(buf), "%s", reset_buf);
    }
    lv_label_set_text(lbl_weekly_reset, buf);
}

void ui_tick_anim(void) {
    if (current_screen != SCREEN_USAGE) return;

    uint32_t now = lv_tick_get();

    if (now - anim_msg_start >= ANIM_MSG_MS) {
        anim_msg_idx = (anim_msg_idx + 1) % ANIM_MSG_COUNT;
        anim_msg_start = now;
    }

    if (now - anim_last_ms >= spinner_ms[anim_spinner_idx]) {
        anim_last_ms = now;
        anim_phase = (anim_phase + 1) % SPINNER_PHASES;
        anim_spinner_idx = (anim_phase < SPINNER_COUNT) ? anim_phase
                                                        : (SPINNER_PHASES - anim_phase);

        static char buf[80];
        snprintf(buf, sizeof(buf), "%s %s\xE2\x80\xA6",
                 spinner_frames[anim_spinner_idx],
                 anim_messages[anim_msg_idx]);
        lv_label_set_text(lbl_anim, buf);
    }
}

static screen_t prev_non_splash_screen = SCREEN_USAGE;
static void apply_battery_visibility(void) {
    if (!battery_img) return;
    if (current_screen == SCREEN_SPLASH) lv_obj_add_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
    else                                  lv_obj_clear_flag(battery_img, LV_OBJ_FLAG_HIDDEN);
}

static void global_click_cb(lv_event_t* e) {
    (void)e;
    // Pending prompt always takes priority — any tap anywhere (except on the
    // permission screen itself) jumps to it. The badge is just a hint.
    if (prompt_state.active && current_screen != SCREEN_PERMISSION) {
        ui_show_screen(SCREEN_PERMISSION);
        return;
    }
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else if (current_screen == SCREEN_PERMISSION) { /* no-op: handled by buttons */ }
    else                                  ui_show_screen(SCREEN_SPLASH);
}

static void ble_reset_click_cb(lv_event_t* e) {
    (void)e;
    ble_clear_bonds();
}

void ui_show_screen(screen_t screen) {
    lv_obj_add_flag(usage_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ble_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(perm_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(cal_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(settings_container, LV_OBJ_FLAG_HIDDEN);
    splash_hide();

    switch (screen) {
    case SCREEN_SPLASH:     splash_show(); break;
    case SCREEN_USAGE:      lv_obj_clear_flag(usage_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_SETTINGS:   lv_obj_clear_flag(settings_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_BLUETOOTH:  lv_obj_clear_flag(ble_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_PERMISSION: lv_obj_clear_flag(perm_container, LV_OBJ_FLAG_HIDDEN); break;
    case SCREEN_CALIBRATE:  lv_obj_clear_flag(cal_container, LV_OBJ_FLAG_HIDDEN); break;
    default: break;
    }

    if (logo_img) {
        if (screen == SCREEN_SPLASH) lv_obj_add_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
        else                          lv_obj_clear_flag(logo_img, LV_OBJ_FLAG_HIDDEN);
    }

    // Remember the screen the user was on before they jumped into a
    // permission prompt — so we can return to it after the decision.
    if (screen != SCREEN_SPLASH && screen != SCREEN_PERMISSION) {
        prev_non_splash_screen = screen;
        screen_before_prompt = screen;
    }
    current_screen = screen;
    apply_battery_visibility();
    apply_badge_visibility();
}

void ui_cycle_screen(void) {
    screen_t next;
    switch (current_screen) {
    case SCREEN_USAGE:     next = SCREEN_SETTINGS;  break;
    case SCREEN_SETTINGS:  next = SCREEN_BLUETOOTH; break;
    case SCREEN_BLUETOOTH: next = SCREEN_USAGE;     break;
    default:               next = SCREEN_USAGE;     break;
    }
    ui_show_screen(next);
}

void ui_toggle_splash(void) {
    if (current_screen == SCREEN_SPLASH) ui_show_screen(prev_non_splash_screen);
    else                                  ui_show_screen(SCREEN_SPLASH);
}

screen_t ui_get_current_screen(void) {
    return current_screen;
}

void ui_update_ble_status(ble_state_t state, const char* name, const char* mac) {
    switch (state) {
    case BLE_STATE_CONNECTED:
        lv_label_set_text(lbl_ble_status, "Connected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_GREEN, 0);
        break;
    case BLE_STATE_ADVERTISING:
        lv_label_set_text(lbl_ble_status, "Advertising...");
        lv_obj_set_style_text_color(lbl_ble_status, COL_AMBER, 0);
        break;
    case BLE_STATE_DISCONNECTED:
        lv_label_set_text(lbl_ble_status, "Disconnected");
        lv_obj_set_style_text_color(lbl_ble_status, COL_RED, 0);
        break;
    default:
        lv_label_set_text(lbl_ble_status, "Initializing...");
        lv_obj_set_style_text_color(lbl_ble_status, COL_DIM, 0);
        break;
    }

    if (name) {
        static char nbuf[48];
        snprintf(nbuf, sizeof(nbuf), "Device: %s", name);
        lv_label_set_text(lbl_ble_device, nbuf);
    }
    if (mac) {
        static char mbuf[48];
        snprintf(mbuf, sizeof(mbuf), "Address: %s", mac);
        lv_label_set_text(lbl_ble_mac, mbuf);
    }
}

void ui_set_prompt(const char* id, const char* tool, const char* hint) {
    // If the user has flipped Permissions off in Settings, immediately tell
    // the host we're punting on this one so the daemon doesn't sit on the
    // 120 s timeout. The hook treats anything that isn't "allow"/"deny" as
    // "ask", which lets Claude Code's normal prompt fire right away.
    if (!settings_permissions_enabled) {
        ble_send_decision(id ? id : "", "ask");
        return;
    }

    // Only chime on the leading edge — re-asserting the same prompt (e.g.
    // daemon reconnects and re-pushes) shouldn't ring again.
    const bool fresh = !prompt_state.active;

    strlcpy(prompt_state.id, id ? id : "", sizeof(prompt_state.id));
    strlcpy(prompt_state.tool, tool ? tool : "", sizeof(prompt_state.tool));
    strlcpy(prompt_state.hint, hint ? hint : "", sizeof(prompt_state.hint));
    prompt_state.active = true;

    if (lbl_perm_tool) lv_label_set_text(lbl_perm_tool, prompt_state.tool);
    if (lbl_perm_hint) lv_label_set_text(lbl_perm_hint, prompt_state.hint);

    // Remember where the user was so we can return there after the decision.
    if (current_screen != SCREEN_PERMISSION) screen_before_prompt = current_screen;
    apply_badge_visibility();
    if (fresh && settings_sound_enabled) audio_hal_chime();
    // Auto-open jumps straight to the prompt instead of waiting for a tap
    // on the badge. Only on the leading edge so a re-pushed prompt doesn't
    // yank the user back if they've navigated away.
    if (fresh && settings_autoswitch_enabled && current_screen != SCREEN_PERMISSION) {
        ui_show_screen(SCREEN_PERMISSION);
    }
}

void ui_clear_prompt(void) {
    prompt_state.active = false;
    prompt_state.id[0] = '\0';
    apply_badge_visibility();
    if (current_screen == SCREEN_PERMISSION) ui_show_screen(screen_before_prompt);
}

bool ui_has_pending_prompt(void) {
    return prompt_state.active;
}

void ui_show_calibration(void) {
    cal_idx = 0;
    ui_show_screen(SCREEN_CALIBRATE);
    cal_render();
}

void ui_permission_decide(const char* decision) {
    if (!prompt_state.active) return;
    ble_send_decision(prompt_state.id, decision);
    screen_t return_to = screen_before_prompt;
    prompt_state.active = false;
    prompt_state.id[0] = '\0';
    apply_badge_visibility();
    ui_show_screen(return_to);
}

void ui_update_battery(int percent, bool charging) {
    int idx;
    if (charging) {
        idx = 4;
    } else if (percent < 0) {
        idx = 0;
    } else if (percent <= 10) {
        idx = 0;
    } else if (percent <= 35) {
        idx = 1;
    } else if (percent <= 75) {
        idx = 2;
    } else {
        idx = 3;
    }
    lv_image_set_src(battery_img, &battery_dscs[idx]);
    apply_battery_visibility();
}
