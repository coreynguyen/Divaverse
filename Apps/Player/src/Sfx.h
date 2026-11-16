#pragma once
// Interface sounds. Every sound is synthesised (short sine / noise bursts with pitch sweeps); nothing is loaded
// from disk. They go out on their own WASAPI stream, opened on the first sound, so music is unaffected.
#include "Common.h"
enum SfxId { SFX_HOVER, SFX_CLICK, SFX_PAGE, SFX_BACK, SFX_TOGGLE, SFX_STAGE, SFX_WHEEL_OPEN, SFX_WHEEL_TICK, SFX_WHEEL_PICK, SFX_DROP, SFX_HIDE, SFX_LOOT_SHAKE, SFX_LOOT, SFX_COUNT };
void SfxConfigure(bool enabled, float volume);
void SfxPlay(SfxId id);
void SfxShutdown();
bool SfxRenderWav(const std::wstring& path);     // QA: every sound in sequence, 48 kHz stereo 16-bit
