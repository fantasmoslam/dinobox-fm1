/* SPDX-License-Identifier: GPL-3.0-only */
/* paleo-render: play a score through PALEO offline and write a 32-bit float stereo WAV.
   paleo-render SCORE OUT.wav [--lead horn|fiddle|choir|blend] [--ostinato spiccato|mouthbow|both]
                [--threads N] [--seconds S] [--stem FILE] [--creatures DIR] (FILE: float32, 4 interleaved channels: dry L R, hall send L R) */
#ifndef NOMINMAX
#define NOMINMAX /* keep windows.h from defining min and max macros */
#endif
#define MINIAUDIO_IMPLEMENTATION
#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#include "../third_party/miniaudio.h"
#include "../engine/paleo.h"
#include "creatures.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

static void wav(const char* path, const std::vector<float>& L, const std::vector<float>& R, int sr) {
  FILE* f = fopen(path, "wb"); if (!f) { perror(path); exit(1); }
  unsigned n = unsigned(L.size()), data = n * 8, v;
  auto w32 = [&](unsigned x) { fwrite(&x, 4, 1, f); }; auto w16 = [&](unsigned short x) { fwrite(&x, 2, 1, f); };
  fwrite("RIFF", 1, 4, f); w32(36 + data); fwrite("WAVEfmt ", 1, 8, f); w32(16); w16(3); w16(2); w32(sr); w32(sr * 8); w16(8); w16(32);
  fwrite("data", 1, 4, f); w32(data);
  for (unsigned i = 0; i < n; i++) { fwrite(&L[i], 4, 1, f); fwrite(&R[i], 4, 1, f); }
  (void)v; fclose(f);
}
int main(int argc, char** argv) {
#if defined(__x86_64__) || defined(_M_X64)
  _mm_setcsr(_mm_getcsr() | 0x8040);
#endif
  if (argc < 3) { fprintf(stderr, "usage: paleo-render SCORE OUT.wav [options]\n"); return 2; }
  paleo::Options o; double secs = 0; const char* stem = nullptr; std::string cdir;
  for (int i = 3; i + 1 < argc; i += 2) {
    std::string k = argv[i], v = argv[i + 1];
    if (k == "--lead") o.lead = v == "horn" ? paleo::Lead::horn : v == "fiddle" ? paleo::Lead::fiddle : v == "choir" ? paleo::Lead::choir : paleo::Lead::blend;
    else if (k == "--ostinato") o.ostinato = v == "mouthbow" ? paleo::Ostinato::mouthbow : v == "both" ? paleo::Ostinato::both : paleo::Ostinato::spiccato;
    else if (k == "--threads") o.threads = atoi(v.c_str());
    else if (k == "--seconds") secs = atof(v.c_str());
    else if (k == "--stem") stem = argv[i + 1];
    else if (k == "--creatures") cdir = v;
  }
  auto t0 = std::chrono::steady_clock::now();
  paleo::Engine e(o); int nc = paleo_load_creatures(e, cdir, argv[1]); fprintf(stderr, "creature library: %d calls\n", nc);
  auto score = paleo::read_score(argv[1]); e.load(score);
  double prep = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  if (stem) {
    FILE* f = fopen(stem, "rb"); if (!f) { perror(stem); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f) / 16; fseek(f, 0, SEEK_SET);
    std::vector<float> q(size_t(n) * 4), a(n), b(n), c(n), d(n); fread(q.data(), 4, q.size(), f); fclose(f);
    for (long i = 0; i < n; i++) { a[i] = q[4 * i]; b[i] = q[4 * i + 1]; c[i] = q[4 * i + 2]; d[i] = q[4 * i + 3]; }
    e.set_stem(a, b, c, d);
  }
  if (secs <= 0) secs = e.length() + 8;
  int sr = o.sample_rate; size_t N = size_t(secs * sr); std::vector<float> L(N), R(N);
  const int B = 256; double worst = 0; int voices = 0; std::vector<double> loads; auto t1 = std::chrono::steady_clock::now();
  for (size_t i = 0; i < N; i += B) {
    int n = int(std::min<size_t>(B, N - i)); e.render(&L[i], &R[i], n);
    worst = std::max(worst, e.last_load()); loads.push_back(e.last_load()); voices = std::max(voices, e.active_voices());
  }
  double run = std::chrono::duration<double>(std::chrono::steady_clock::now() - t1).count();
  std::sort(loads.begin(), loads.end());
  printf("block load: median %.0f%%, 99th %.0f%%, 99.9th %.0f%%\n", 100 * loads[loads.size() / 2], 100 * loads[loads.size() * 99 / 100], 100 * loads[loads.size() * 999 / 1000]);
  wav(argv[2], L, R, sr);
  float pk = 0; for (size_t i = 0; i < N; i++) pk = std::max(pk, std::max(fabsf(L[i]), fabsf(R[i])));
  printf("rendered %.1f s on %d threads in %.1f s (%.0f%% of real time; slowest 5 ms block %.0f%%), prepare %.1f s, "
         "most voices %d, peak %.3f\n", secs, e.threads(), run, 100 * run / secs, 100 * worst, prep, voices, pk);
  return 0;
}
