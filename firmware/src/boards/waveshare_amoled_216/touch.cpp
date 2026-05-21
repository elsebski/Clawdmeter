#include "../../hal/touch_hal.h"
#include "../../hal/imu_hal.h"
#include "board.h"
#include <Arduino.h>
#include <Wire.h>
#include <TouchDrvCSTXXX.hpp>

static TouchDrvCST92xx touch;

static volatile bool     touch_data_ready = false;
static volatile bool     touch_pressed = false;
static volatile uint16_t touch_x = 0;
static volatile uint16_t touch_y = 0;

static void IRAM_ATTR touch_isr(void) {
    touch_data_ready = true;
}

void touch_hal_init(void) {
    touch.setPins(TP_RST, TP_INT);
    if (!touch.begin(Wire, CST9220_ADDR, IIC_SDA, IIC_SCL)) {
        Serial.println("Touch init failed");
        return;
    }
    touch.setMaxCoordinates(LCD_WIDTH, LCD_HEIGHT);
    touch.setSwapXY(true);
    touch.setMirrorXY(true, false);
    pinMode(TP_INT, INPUT_PULLUP);
    attachInterrupt(TP_INT, touch_isr, FALLING);
    Serial.println("Touch init OK");
}

void touch_hal_read(uint16_t* x, uint16_t* y, bool* pressed) {
    if (touch_data_ready) {
        touch_data_ready = false;
        int16_t tx[5], ty[5];
        uint8_t n = touch.getPoint(tx, ty, touch.getSupportTouchPoint());
        if (n > 0) {
            touch_pressed = true;
            touch_x = (uint16_t)tx[0];
            touch_y = (uint16_t)ty[0];
        } else {
            touch_pressed = false;
        }
    }

    // Display rotation is done in software (display.cpp). Touch coords arrive
    // in panel-native space; apply the inverse so LVGL sees content-space
    // coords matching the rotated UI.
    uint16_t px = touch_x;
    uint16_t py = touch_y;
    const uint16_t S = LCD_WIDTH;  // panel is square 480x480
    // Empirically derived from on-device dot-tap calibration. The IMU's
    // quadrant numbering comes from accel axis dominance, not rotation
    // angle, so it does NOT map linearly to 0/90/180/270 — table is per
    // quadrant.
    //   r=0 → 90° CW    r=1 → 180°
    //   r=2 → 90° CCW   r=3 → identity (device's natural orientation)
    switch (imu_hal_rotation_quadrant()) {
    case 0: *x = py;          *y = S - 1 - px;  break;
    case 1: *x = S - 1 - px;  *y = S - 1 - py;  break;
    case 2: *x = S - 1 - py;  *y = px;          break;
    case 3: *x = px;          *y = py;          break;
    default: *x = px;         *y = py;          break;
    }
    *pressed = touch_pressed;
}
