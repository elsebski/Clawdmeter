#pragma once

// Waveshare ESP32-S3-Touch-AMOLED-2.16 — original square AMOLED kit.
// 480x480 CO5300 + CST9220 touch + AXP2101 PMU + QMI8658 IMU.
// IMU-driven CPU rotation is enabled.

#define BOARD_NAME           "Waveshare AMOLED 2.16"

// ---- Display geometry (matches BoardCaps; duplicated here as compile-time
// constants because the buffer-size math runs at file scope) ----
#define LCD_WIDTH            480
#define LCD_HEIGHT           480

// ---- QSPI display pins (CO5300) ----
#define LCD_CS               12
#define LCD_SCLK             38
#define LCD_SDIO0            4
#define LCD_SDIO1            5
#define LCD_SDIO2            6
#define LCD_SDIO3            7
#define LCD_RESET            2

// ---- I2C bus (touch + PMU + IMU) ----
#define IIC_SDA              15
#define IIC_SCL              14

// ---- Touch (CST9220 via TouchDrvCST92xx library) ----
#define TP_INT               11
#define TP_RST               2     // shared with LCD_RESET
#define CST9220_ADDR         0x5A

// ---- PMU ----
#define AXP2101_ADDR         0x34

// ---- Audio (ES8311 codec @ 0x18, speaker amp via PA) ----
// Pinout from Waveshare's official Arduino demo (07_ES8311). The codec is
// on the same shared I2C bus as touch/PMU/IMU. I2S clocks are shared with
// the ES7210 mic-array codec (not used by Clawdmeter, but pin reservations
// stand). DOUT goes to the speaker path on ES8311.
#define ES8311_ADDR          0x18
#define AUDIO_MCLK           42    // per Waveshare's 07_ES8311.ino (pin_config.h
                                   //   has 16 but that's for the mic-array path
                                   //   and didn't make sound on this kit)
#define AUDIO_BCLK           9
#define AUDIO_LRCK           45
#define AUDIO_DOUT           8     // ESP -> ES8311 (speaker)
#define AUDIO_PA_EN          46    // speaker amp enable (active HIGH)

// ---- Buttons ----
#define BTN_BACK_GPIO        0     // BOOT — primary, Space (PTT)
#define BTN_FWD_GPIO         18    // secondary, Shift+Tab (mode toggle)

// ---- Capability flags (compile-time; redundant with BoardCaps but lets
// the linker dead-strip whole functions on boards that don't need them) ----
#define BOARD_HAS_SECONDARY_BUTTON 1
#define BOARD_HAS_ROTATION         1
#define BOARD_HAS_IMU              1
#define BOARD_HAS_BATTERY          1
#define BOARD_HAS_IO_EXPANDER      0
