#include "../../hal/audio_hal.h"

// AMOLED-1.8 kit has no codec. Stubs satisfy the shared call sites.
void audio_hal_init(void)  {}
void audio_hal_chime(void) {}
void audio_hal_set_volume(int) {}
