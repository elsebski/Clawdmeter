#pragma once
// Per-board audio. Shared code calls these unconditionally; boards without
// a codec (e.g. AMOLED-1.8) stub them to no-ops, so we don't gate the call
// sites with #ifdef BOARD_*.
//
// audio_hal_init()  — called once from setup(), after Wire is up.
// audio_hal_chime() — fire-and-forget "ding" when a new permission prompt
//                     arrives. Safe to call from any task.

void audio_hal_init(void);
void audio_hal_chime(void);
// Set codec voice volume in 0..100. Boards without audio ignore.
void audio_hal_set_volume(int pct);
