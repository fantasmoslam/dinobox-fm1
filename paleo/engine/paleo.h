/* SPDX-License-Identifier: GPL-3.0-only */
/* PALEO: Paleotone's own sound engine. Plays the prehistoric kit, the gut fiddle and the throat choir through
   the STAMPEDE effects chain, sample-accurate to 32 frames, spreading voices over several cores.
   Step 1 scope: a score is loaded whole, then rendered block by block (offline or live). */
#pragma once
#include <string>
#include <vector>
#include <cstddef>

namespace paleo {
/* One score line: time and duration in seconds, part name, MIDI note, velocity 0..1.
   Parts: taiko bigdrum logdrum rattle stone mouthbow horn braam pipe lead ostinato, and
   boneflute panpipes bowedbone slitdrum claypot framedrum rasp bullroarer,
   cbrecorder fujara upright harmonium bugle guitar.
   An optional 6th column sets the voice's p1 (frame drum: 0 centre .. 1 rim; upright: 0 plucked, 1 bowed;
   fujara: 1 opens the note with the rozfuk).
   Directives use the same shape: "riser" and "subdrop" at a time (vel = level), "fade lead" as
   `t tau fade 0 0` (the lead dies away from t with time constant tau), "tail" from t (lead into the long tail). */
struct Event { double t = 0, d = 0; std::string part; int midi = 0; float vel = 0; float p1 = -1; /* optional 6th column */ };
std::vector<Event> read_score(const char* path);

enum class Lead { horn, fiddle, choir, blend };
enum class Ostinato { spiccato, mouthbow, both };
struct Options { int sample_rate = 48000; int threads = 0; /* 0 = all but one core */ Lead lead = Lead::fiddle;
                 Ostinato ostinato = Ostinato::spiccato; };

class Engine {
 public:
  explicit Engine(const Options& o);
  ~Engine();
  /* Load a whole score (not on the audio thread). Prepares the risers by rendering ahead. */
  void load(const std::vector<Event>& score);
  /* An external stereo stem (e.g. pre-rendered creatures): dry L/R plus a hall send L/R, n frames each. */
  void set_stem(std::vector<float> dryL, std::vector<float> dryR, std::vector<float> sendL, std::vector<float> sendR);
  /* Render the next `frames` (any count) into L and R. Output is the song's level before final loudness. */
  void render(float* L, float* R, int frames);
  double seconds() const;
  double length() const;              /* end of the last event plus its ring-out */
  int threads() const;
  double last_load() const;           /* share of real time the last render call took (1.0 = too slow) */
  int active_voices() const;
  struct Impl; Impl* p;
};
}  // namespace paleo
