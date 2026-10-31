/* SPDX-License-Identifier: GPL-3.0-only */
/* paleo-play: play a score through PALEO live on this computer's sound output (WASAPI on Windows).
   paleo-play SCORE [--lead horn|fiddle|choir|blend] [--ostinato spiccato|mouthbow|both] [--threads N]
              [--period MS] [--gain DB] [--stem FILE] [--creatures DIR] [--null]
   Prints, once a second: the time, the live voices, and how much of each audio period the engine used. */
#ifndef NOMINMAX
#define NOMINMAX /* keep windows.h from defining min and max macros */
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "../third_party/miniaudio.h"
#include "../engine/paleo.h"
#include "creatures.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

static paleo::Engine* g_engine;
static float g_gain = 1;
static std::atomic<double> g_peak_load{0}, g_sum_load{0};
static std::atomic<int> g_calls{0}, g_over{0};
static std::atomic<bool> g_done{false};
static double g_end = 0;
static std::vector<float> g_l, g_r;

static void callback(ma_device*, void* out, const void*, ma_uint32 frames) {
  static bool ftz = false;
  if (!ftz) {
#if defined(__x86_64__) || defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
    ftz = true;
  }
  float* o = static_cast<float*>(out);
  if (g_done.load() || frames > g_l.size()) { std::fill(o, o + frames * 2, 0.f); return; }
  g_engine->render(g_l.data(), g_r.data(), int(frames));
  for (ma_uint32 i = 0; i < frames; i++) {
    float l = g_l[i] * g_gain, r = g_r[i] * g_gain;   /* gentle limiter at -0.6 dB */
    o[2 * i] = l > .8f || l < -.8f ? std::copysign(.8f + .133f * std::tanh((std::fabs(l) - .8f) / .133f), l) : l;
    o[2 * i + 1] = r > .8f || r < -.8f ? std::copysign(.8f + .133f * std::tanh((std::fabs(r) - .8f) / .133f), r) : r;
  }
  double ld = g_engine->last_load();
  g_sum_load = g_sum_load.load() + ld; g_calls++;
  if (ld > g_peak_load.load()) g_peak_load = ld;
  if (ld > 1.0) g_over++;
  if (g_engine->seconds() >= g_end) g_done = true;
}

int main(int argc, char** argv) {
  if (argc < 2) { fprintf(stderr, "usage: paleo-play SCORE [options]\n"); return 2; }
  paleo::Options o; double period = 10, gain_db = 2.8; const char* stem = nullptr; bool null_out = false; std::string cdir;
  o.threads = std::max(2, int(std::thread::hardware_concurrency()) / 2);
  for (int i = 2; i < argc; i++) {
    std::string k = argv[i], v = i + 1 < argc ? argv[i + 1] : "";
    if (k == "--null") { null_out = true; continue; }
    i++;
    if (k == "--lead") o.lead = v == "horn" ? paleo::Lead::horn : v == "fiddle" ? paleo::Lead::fiddle : v == "choir" ? paleo::Lead::choir : paleo::Lead::blend;
    else if (k == "--ostinato") o.ostinato = v == "mouthbow" ? paleo::Ostinato::mouthbow : v == "both" ? paleo::Ostinato::both : paleo::Ostinato::spiccato;
    else if (k == "--threads") o.threads = atoi(v.c_str());
    else if (k == "--period") period = atof(v.c_str());
    else if (k == "--gain") gain_db = atof(v.c_str());
    else if (k == "--stem") stem = argv[i];
    else if (k == "--creatures") cdir = v;
  }
  g_gain = float(pow(10, gain_db / 20));
  printf("PALEO live player. Preparing the score (risers are rendered ahead)...\n");
  paleo::Engine e(o); g_engine = &e;
  int nc = paleo_load_creatures(e, cdir, argv[1]);
  printf("Creature library: %d calls%s\n", nc, nc ? "" : " (none found; creature cues stay silent)");
  e.load(paleo::read_score(argv[1])); g_end = e.length() + 8;
  if (stem) {
    FILE* f = fopen(stem, "rb");
    if (f) {
      fseek(f, 0, SEEK_END); long n = ftell(f) / 16; fseek(f, 0, SEEK_SET);
      std::vector<float> q(size_t(n) * 4), a(n), b(n), c(n), d(n); size_t got = fread(q.data(), 4, q.size(), f); fclose(f); (void)got;
      for (long i = 0; i < n; i++) { a[i] = q[4 * i]; b[i] = q[4 * i + 1]; c[i] = q[4 * i + 2]; d[i] = q[4 * i + 3]; }
      e.set_stem(a, b, c, d);
    } else printf("Could not open the stem %s; playing without it.\n", stem);
  }
  g_l.resize(1 << 16); g_r.resize(1 << 16);

  ma_context ctx; ma_backend nullb[] = {ma_backend_null};
  if (ma_context_init(null_out ? nullb : nullptr, null_out ? 1 : 0, nullptr, &ctx) != MA_SUCCESS) { fprintf(stderr, "no audio\n"); return 1; }
  ma_device_config c = ma_device_config_init(ma_device_type_playback);
  c.playback.format = ma_format_f32; c.playback.channels = 2; c.sampleRate = ma_uint32(o.sample_rate);
  c.periodSizeInMilliseconds = ma_uint32(period); c.performanceProfile = ma_performance_profile_low_latency;
  c.dataCallback = callback;
  ma_device dev;
  if (ma_device_init(&ctx, &c, &dev) != MA_SUCCESS) { fprintf(stderr, "could not open the sound output\n"); return 1; }
  const char* leads[] = {"horn", "fiddle", "choir", "fiddle + choir"};
  printf("Output: %s, %u Hz, period %.0f ms, engine on %d threads, lead: %s.\n", dev.playback.name, dev.sampleRate, period,
         e.threads(), leads[int(o.lead)]);
  if (dev.sampleRate != ma_uint32(o.sample_rate)) printf("(The device runs at %u Hz; miniaudio converts.)\n", dev.sampleRate);
  ma_device_start(&dev);
  int over_total = 0; double worst = 0;
  while (!g_done.load()) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    int n = g_calls.exchange(0); double s = g_sum_load.exchange(0), pk = g_peak_load.exchange(0); int ov = g_over.exchange(0);
    over_total += ov; worst = std::max(worst, pk);
    printf("%6.1f s  voices %3d  engine load avg %3.0f%%  peak %3.0f%%%s\n", e.seconds(), e.active_voices(),
           n ? 100 * s / n : 0.0, 100 * pk, ov ? "  <- late (would click)" : "");
    fflush(stdout);
  }
  ma_device_uninit(&dev); ma_context_uninit(&ctx);
  printf("Done. Peak load %.0f%%, late periods %d.\n", 100 * worst, over_total);
  return 0;
}
