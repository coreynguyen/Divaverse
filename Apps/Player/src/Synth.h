#pragma once
// Procedural music: every sound is synthesised at run time (oscillators, noise, envelopes, filters, delay).
// A "station" is a seed + style; the piece is generated bar by bar and never ends or repeats exactly.
#include "Audio.h"

struct SynthStation { const wchar_t* name; const wchar_t* mood; uint32_t seed; int bpm; int style; };   // style: 0 ambient, 1 lounge, 2 pulse
const SynthStation* SynthStations(int* count);
std::unique_ptr<AudioSource> MakeSynthSource(int station);
