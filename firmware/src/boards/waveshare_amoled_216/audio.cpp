#include "../../hal/audio_hal.h"
#include "board.h"
#include "es8311/es8311.h"

#include <Arduino.h>
#include <ESP_I2S.h>
#include <math.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define SAMPLE_RATE   16000
#define CHIME_FREQ    1000.0f
#define CHIME_MS      250
#define ATTACK_MS     5
#define DECAY_TAU_MS  80
#define VOLUME_PCT    70           // ES8311 voice volume (0..100)
#define AMP_SCALE     14000.0f     // signed-16 peak of the sine, ~43% FS

static I2SClass       i2s;
static es8311_handle_t codec    = nullptr;
static int16_t*       stereo_buf = nullptr;   // L,R interleaved
static size_t         stereo_bytes = 0;
static TaskHandle_t   worker     = nullptr;
static bool           ready      = false;

// Runs the precomputed chime through I2S when notified. Off the main loop
// so the ~250 ms write doesn't stall LVGL.
static void audio_task(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        i2s.write((uint8_t*)stereo_buf, stereo_bytes);
    }
}

// 1 kHz sine, 5 ms attack, exponential decay (~80 ms tau). Interleaved L,R
// so the codec drives the speaker in stereo mode like the Waveshare demo.
static void synthesize_chime(void) {
    const size_t frames = (SAMPLE_RATE * CHIME_MS) / 1000;
    stereo_bytes = frames * 2 * sizeof(int16_t);
    stereo_buf = (int16_t*)heap_caps_malloc(stereo_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!stereo_buf) {
        Serial.println("audio: chime buffer alloc failed");
        return;
    }
    const float attack_s = ATTACK_MS / 1000.0f;
    const float tau_s    = DECAY_TAU_MS / 1000.0f;
    for (size_t i = 0; i < frames; ++i) {
        float t = (float)i / SAMPLE_RATE;
        float env = (t < attack_s) ? (t / attack_s)
                                   : expf(-(t - attack_s) / tau_s);
        int16_t s = (int16_t)(sinf(2.0f * (float)M_PI * CHIME_FREQ * t) * env * AMP_SCALE);
        stereo_buf[2*i]     = s;   // L
        stereo_buf[2*i + 1] = s;   // R
    }
}

void audio_hal_init(void) {
    // Speaker amp on. The Waveshare example keeps it powered the whole time;
    // toggling it added pops without saving meaningful current here.
    pinMode(AUDIO_PA_EN, OUTPUT);
    digitalWrite(AUDIO_PA_EN, HIGH);

    i2s.setPins(AUDIO_BCLK, AUDIO_LRCK, AUDIO_DOUT, -1 /* din unused */, AUDIO_MCLK);
    if (!i2s.begin(I2S_MODE_STD, SAMPLE_RATE, I2S_DATA_BIT_WIDTH_16BIT,
                   I2S_SLOT_MODE_STEREO, I2S_STD_SLOT_BOTH)) {
        Serial.println("audio: i2s.begin failed");
        return;
    }

    codec = es8311_create(0 /* I2C bus = Wire */, ES8311_ADDR);
    if (!codec) {
        Serial.println("audio: es8311_create failed");
        return;
    }
    es8311_clock_config_t clk = {};
    clk.mclk_inverted      = false;
    clk.sclk_inverted      = false;
    clk.mclk_from_mclk_pin = true;
    clk.mclk_frequency     = SAMPLE_RATE * 256;
    clk.sample_frequency   = SAMPLE_RATE;
    if (es8311_init(codec, &clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16) != ESP_OK) {
        Serial.println("audio: es8311_init failed");
        return;
    }
    es8311_sample_frequency_config(codec, clk.mclk_frequency, SAMPLE_RATE);
    es8311_voice_volume_set(codec, VOLUME_PCT, NULL);

    synthesize_chime();
    if (!stereo_buf) return;

    xTaskCreate(audio_task, "chime", 4096, NULL, 5, &worker);
    ready = true;
    Serial.println("audio: ready");
}

void audio_hal_chime(void) {
    if (ready && worker) xTaskNotifyGive(worker);
}

void audio_hal_set_volume(int pct) {
    if (!codec) return;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    es8311_voice_volume_set(codec, pct, NULL);
}
