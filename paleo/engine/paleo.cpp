/* SPDX-License-Identifier: GPL-3.0-only */
/* PALEO engine. The voice control and the mix follow the STAMPEDE v5 renderers and mix4.py one for one;
   the levels in kStrips were measured from those renders (see paleo/README.md). */
#include "paleo.h"
#include "dsp.h"
#include "../faust/faust_min.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <mutex>
#include <sstream>
#include <thread>
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
#include <xmmintrin.h>
#endif

dsp* make_taiko(); dsp* make_bigdrum(); dsp* make_logdrum(); dsp* make_rattle(); dsp* make_stone();
dsp* make_mouthbow(); dsp* make_horn(); dsp* make_pipe(); dsp* make_fiddle(); dsp* make_choir(); dsp* make_reverb();
dsp* make_boneflute(); dsp* make_panpipes(); dsp* make_bowedbone(); dsp* make_slitdrum(); dsp* make_claypot();
dsp* make_framedrum(); dsp* make_rasp(); dsp* make_bullroarer();
dsp* make_cbrecorder(); dsp* make_fujara(); dsp* make_uprightbass(); dsp* make_harmonium(); dsp* make_bugle(); dsp* make_guitar();

namespace paleo {
namespace {
const int kChunk = 32;       /* control rate: parameters update every 32 frames (0.67 ms) */
const int kMaxBlock = 1024;  /* render() splits longer requests */
const int kTaskVoices = 6;   /* voices per worker task */

enum class Ctl { struck, horn, pipe, lead, spiccato, held };   /* held: gate and gain on for the note, the voice shapes itself */
struct Strip {                /* mix4.py's chain for one part */
  float pre;                  /* drive into tanh, 0 = clean */
  float lo, hi, dipf, dipd, bf, bb, k;
  float haas_ms, pan;         /* haas_ms < 0: plain pan, no delay */
  float pp_secs, pp_fb, pp_mix;
  float duck_pow, hall, room, weight;
};
struct PartDef { const char* name; dsp* (*make)(); Ctl ctl; int max_voices; int ens; float cents; float ring; Strip s; };

/*                                   pre    lo    hi    dipf dipd  bf    bb   k          haas  pan    pp: secs fb  mix   duck hall room weight */
const PartDef kParts[] = {
  {"taiko",    make_taiko,    Ctl::struck, 64, 1, 0, 2.5f, {0,     32,   0,    300, .25f, 0,    0,   .108762f,  -1,   0,     0,   0,   0,    0,  .2f,  .3f, 1}},
  {"bigdrum",  make_bigdrum,  Ctl::struck, 16, 1, 0, 2.5f, {0,     32,   0,    300, .25f, 0,    0,   .108762f,  -1,   0,     0,   0,   0,    0,  .2f,  .3f, 1}},
  {"logdrum",  make_logdrum,  Ctl::struck, 32, 1, 0, 2.5f, {0,     110,  0,    260, .3f,  0,    0,   .0420878f, 9,    .35f,  .375f,.35f,.22f, 0,  .15f, .2f, 1}},
  {"rattle",   make_rattle,   Ctl::struck, 32, 1, 0, 2.5f, {0,     500,  6500, 0,   0,    0,    0,   .904095f,  14,   -.4f,  0,   0,   0,    0,  0,    .2f, 1}},
  {"stone",    make_stone,    Ctl::struck, 16, 1, 0, 2.5f, {0,     180,  0,    0,   0,    0,    0,   .17612f,   -1,   .3f,   .5f, .4f, .25f, 0,  .6f,  0,   1}},
  {"mouthbow", make_mouthbow, Ctl::struck, 24, 1, 0, 2.5f, {0,     130,  0,    260, .3f,  1800, .8f, .173118f,  11,   -.3f,  .25f,.3f, .18f, .5f,.15f, 0,   1}},
  {"spiccato", make_fiddle,   Ctl::spiccato,48, 3, 7, .25f,{0,     90,   8000, 300, .35f, 2500, .6f, .434632f,  11,   -.3f,  .25f,.3f, .18f, .5f,.15f, 0,   1}},
  {"horn",     make_horn,     Ctl::horn,   48, 1, 0, .5f,  {.19465f,110, 7000, 280, .4f,  1600, 1.2f,.538651f,  13,   .05f,  .75f,.25f,.12f, .3f,.35f, 0,   1}},
  {"fiddle",   make_fiddle,   Ctl::lead,   96, 3, 9, .6f,  {.147046f,65, 7500, 300, .35f, 2200, .6f, .949701f,  18,   0,     .75f,.2f, .1f,  .3f,.35f, 0,   .75f}},
  {"choir",    make_choir,    Ctl::lead,   96, 4, 12,.6f,  {0,     75,   8000, 280, .4f,  2600, .7f, .353994f,  24,   0,     .75f,.2f, .08f, .3f,.35f, 0,   .7f}},
  {"braam",    make_horn,     Ctl::horn,   12, 1, 0, .5f,  {1.5f,  65,   3200, 0,   0,    1200, .8f, .122933f,  17,   0,     0,   0,   0,    0,  .45f, 0,   1}},
  {"pipe",     make_pipe,     Ctl::pipe,   4,  1, 0, .5f,  {0,     70,   2400, 260, .3f,  0,    0,   .0367254f, 21,   0,     0,   0,   0,    1,  .4f,  0,   1}},
  /* batch 1 of new instruments; k set against the parts above by active level (see paleo/README.md) */
  {"boneflute", make_boneflute, Ctl::held, 16, 1, 0, .8f,  {0,     200,  7500, 0,   0,    0,    0,   .606f,     15,   .3f,   .375f,.25f,.12f, .3f,.35f, 0,   1}},
  {"panpipes", make_panpipes, Ctl::held,   24, 1, 0, .6f,  {0,     150,  7000, 0,   0,    0,    0,   .752f,     12,   -.3f,  .25f,.25f,.12f, .3f,.3f,  0,   1}},
  {"bowedbone",make_bowedbone,Ctl::held,   12, 1, 0, 3.5f, {0,     60,   8000, 0,   0,    0,    0,   .448f,     20,   0,     0,   0,   0,    .5f,.5f,  0,   1}},
  {"slitdrum", make_slitdrum, Ctl::struck, 32, 1, 0, 1.5f, {0,     70,   0,    260, .3f,  0,    0,   .720f,     9,    .3f,   0,   0,   0,    0,  .15f, .2f, 1}},
  {"claypot",  make_claypot,  Ctl::struck, 16, 1, 0, 1.5f, {0,     30,   0,    300, .25f, 0,    0,   .28f,      -1,   0,     0,   0,   0,    0,  .15f, .25f,1}},
  {"framedrum",make_framedrum,Ctl::struck, 32, 1, 0, 1.2f, {0,     40,   0,    300, .3f,  0,    0,   .348f,     9,    -.35f, 0,   0,   0,    0,  .15f, .2f, 1}},
  {"rasp",     make_rasp,     Ctl::held,   16, 1, 0, .4f,  {0,     120,  6500, 0,   0,    0,    0,   .661f,     14,   .4f,   0,   0,   0,    0,  0,    .2f, 1}},
  {"bullroarer",make_bullroarer,Ctl::held, 4,  1, 0, 3.0f, {0,     50,   3000, 0,   0,    0,    0,   1.0f,      21,   0,     0,   0,   0,    1,  .4f,  0,   1}},
  /* pack 2: the contrabass recorder and fujara, and the expedition's instruments (it is the 1900s) */
  {"cbrecorder",make_cbrecorder,Ctl::held, 12, 1, 0, .8f,  {0,     40,   6000, 0,   0,    0,    0,   .387f,     13,   .25f,  .375f,.2f, .1f,  .3f,.35f, 0,   1}},
  {"fujara",   make_fujara,   Ctl::held,   12, 1, 0, .9f,  {0,     150,  10000,0,   0,    0,    0,   .568f,     15,   -.25f, .375f,.25f,.12f, .3f,.4f,  0,   1}},
  {"upright",  make_uprightbass,Ctl::held, 16, 1, 0, 2.5f, {0,     30,   5000, 0,   0,    0,    0,   .21f,      -1,   0,     0,   0,   0,    .3f,.2f,  .2f, 1}},
  {"harmonium",make_harmonium,Ctl::held,   24, 1, 0, .5f,  {0,     60,   7000, 0,   0,    0,    0,   .932f,     18,   -.2f,  0,   0,   0,    .5f,.3f,  .2f, 1}},
  {"bugle",    make_bugle,    Ctl::held,   8,  1, 0, .7f,  {0,     100,  8000, 0,   0,    0,    0,   1.8f,      11,   .3f,   .5f, .25f,.15f, .2f,.45f, 0,   1}},
  {"guitar",   make_guitar,   Ctl::held,   24, 1, 0, 3.0f, {0,     60,   9000, 0,   0,    0,    0,   .317f,     10,   .35f,  0,   0,   0,    .3f,.25f, .2f, 1}},
};
const int kNumParts = int(sizeof kParts / sizeof kParts[0]);
const float kDuckRef = 1.07775f, kGlueThr = 1.36736f, kOutGain = 1 / 2.36579f;
/* Reverb returns, scaled so white noise in gives equal power out, like the song's energy-normalised IRs. */
const float kHallReturn = .45f, kRoomReturn = .35f, kTailIn = .7f;

/* Flush denormals to zero on this thread: decaying filters and reverbs otherwise slow down many times over. */
void flush_denormals() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64)
  _mm_setcsr(_mm_getcsr() | 0x8040);
#elif defined(__aarch64__)
  unsigned long long f; asm volatile("mrs %0, fpcr" : "=r"(f)); asm volatile("msr fpcr, %0" ::"r"(f | (1ull << 24)));
#endif
}
double now_s() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

struct Params : UI {
  float *freq = nullptr, *gain = nullptr, *gate = nullptr, *p1 = nullptr, *p2 = nullptr;
  float *rt60 = nullptr, *dark = nullptr;
  void bind(const char* l, float* z) {
    if (!strcmp(l, "freq")) freq = z; else if (!strcmp(l, "gain")) gain = z; else if (!strcmp(l, "gate")) gate = z;
    else if (!strcmp(l, "p1")) p1 = z; else if (!strcmp(l, "p2")) p2 = z; else if (!strcmp(l, "rt60")) rt60 = z;
    else if (!strcmp(l, "dark")) dark = z;
  }
  void addButton(const char* l, float* z) override { bind(l, z); }
  void addNumEntry(const char* l, float* z, float, float, float, float) override { bind(l, z); }
  void addHorizontalSlider(const char* l, float* z, float, float, float, float) override { bind(l, z); }
  void addVerticalSlider(const char* l, float* z, float, float, float, float) override { bind(l, z); }
  static void set(float* z, float v) { if (z) *z = v; }
};

struct Voice {
  dsp* d = nullptr; Params p;
  bool active = false;
  long long start = 0, end = 0;      /* in frames */
  double dur = 0, pre = 0; float vel = 0; int midi = 0; int idx = 0; float ens_div = 1; float p1 = -1;
};
struct Pending { long long start; int part; double dur, pre; float vel, freq; int midi, idx; float ens_div; float p1 = -1; };

struct Part {
  const PartDef* def; std::vector<Voice> v; std::vector<int> act; int notes = 0; bool on = true;
  Eq eq; Haas haas; PingPong pp; bool has_pp = false;
  float buf[kMaxBlock];
  double fade_t = 1e30, fade_tau = 1;  /* exponential fade from fade_t */
  bool tail = false; double tail_t = 1e30;
};
struct Task { int part, from, to; float buf[kMaxBlock]; };

/* A worker pool: render() hands out tasks, workers and the calling thread take them until none are left. */
struct Pool {
  std::vector<std::thread> th; std::mutex m; std::condition_variable cv; long gen = 0; bool quit = false;
  std::atomic<int> next{0}, done{0}; int count = 0; std::function<void(int)> job;
  void start(int n) { for (int i = 0; i < n; i++) th.emplace_back([this] { loop(); }); }
  void loop() {
    flush_denormals(); long seen = 0;
    for (;;) {
      { std::unique_lock<std::mutex> l(m); cv.wait(l, [&] { return quit || gen != seen; }); if (quit) return; seen = gen; }
      work();
    }
  }
  void work() { int i; while ((i = next.fetch_add(1)) < count) { job(i); done.fetch_add(1); } }
  void run(int n) {
    count = n; done = 0; next = 0;
    if (!th.empty() && n > 1) { { std::lock_guard<std::mutex> l(m); gen++; } cv.notify_all(); }
    work();
    while (done.load() < n) std::this_thread::yield();
  }
  ~Pool() { { std::lock_guard<std::mutex> l(m); quit = true; } cv.notify_all(); for (auto& t : th) t.join(); }
};

struct Reverb {
  dsp* d = nullptr; Params p; float g = 1;
  void init(int sr, float rt60, float dark, float gain) {
    d = make_reverb(); d->init(sr); d->buildUserInterface(&p); Params::set(p.rt60, rt60); Params::set(p.dark, dark); g = gain;
  }
  void run(float* inL, float* inR, float* outL, float* outR, int n) {
    float* in[2] = {inL, inR}; float* out[2] = {outL, outR}; d->compute(n, in, out);
    for (int i = 0; i < n; i++) { outL[i] *= g; outR[i] *= g; }
  }
};
}  // namespace

struct Engine::Impl {
  Options o; int sr; long long t = 0;
  Part parts[kNumParts]; std::vector<Pending> pend; size_t pend_i = 0;
  std::vector<Task> tasks; int ntasks = 0; Pool pool;
  Reverb hall, room, tailr; Follower duckf, gluef;
  struct Shot { long long start; std::vector<float> L, R; };
  std::vector<Shot> shots;            /* risers and sub drops, prepared at load */
  std::vector<float> sdL, sdR, ssL, ssR;
  double len = 0, load = 0;
  bool capture = false; long long cap_from = 0; std::vector<float> cap;   /* riser pre-pass */

  int part_of(const std::string& n) const { for (int i = 0; i < kNumParts; i++) if (n == kParts[i].name) return i; return -1; }

  void init() {
    sr = o.sample_rate;
    for (int i = 0; i < kNumParts; i++) {
      Part& P = parts[i]; P.def = &kParts[i]; P.v.resize(P.def->max_voices);
      for (auto& v : P.v) { v.d = P.def->make(); v.d->init(sr); v.d->buildUserInterface(&v.p); }
      const Strip& s = P.def->s;
      P.eq.setup(float(sr), s.lo, s.hi, s.dipf, s.dipd, s.bf, s.bb);
      P.haas.setup(float(sr), s.haas_ms < 0 ? 0 : s.haas_ms, s.pan);
      if (s.pp_secs > 0) { P.pp.setup(float(sr), s.pp_secs, s.pp_fb, s.pp_mix); P.has_pp = true; }
      P.on = true;
    }
    auto off = [&](const char* n) { parts[part_of(n)].on = false; };
    if (o.lead == Lead::horn) { off("fiddle"); off("choir"); }
    if (o.lead == Lead::fiddle) { off("horn"); off("choir"); }
    if (o.lead == Lead::choir) { off("horn"); off("fiddle"); }
    if (o.lead == Lead::blend) off("horn");
    int maxt = 0; for (auto& P : parts) maxt += (P.def->max_voices + kTaskVoices - 1) / kTaskVoices;
    tasks.resize(maxt);
    hall.init(sr, 3.4f, 4500, 1); room.init(sr, 1.1f, 6000, 1); tailr.init(sr, 9.0f, 3200, 1);
    calibrate();
    duckf.setup(float(sr), .002f, .18f, 48); gluef.setup(float(sr), .01f, .25f, 96);
    int n = o.threads > 0 ? o.threads - 1 : int(std::thread::hardware_concurrency()) - 1;
    pool.job = [this](int i) { run_task(i); };
    pool.start(std::max(0, std::min(n, 15)));
  }
  /* Set each reverb's gain so white noise comes out at the power it went in, as the song's IRs did. */
  void calibrate() {
    for (Reverb* r : {&hall, &room, &tailr}) {
      Reverb c; c.init(sr, r == &hall ? 3.4f : r == &room ? 1.1f : 9.0f, r == &hall ? 4500.f : r == &room ? 6000.f : 3200.f, 1);
      std::vector<float> a(4096), b(4096), x(4096), y(4096); uint32_t s = 1; double pin = 0, pout = 0;
      for (int blk = 0; blk < int(sr * 12 / 4096); blk++) {
        for (int i = 0; i < 4096; i++) { s = s * 1664525u + 1013904223u; a[i] = (int32_t(s) >> 8) / 8388608.f; b[i] = a[i]; }
        c.run(a.data(), b.data(), x.data(), y.data(), 4096);
        if (blk * 4096 > sr * 4) for (int i = 0; i < 4096; i++) { pin += a[i] * a[i]; pout += x[i] * x[i]; }
      }
      r->g = float(sqrt(pin / std::max(pout, 1e-20)));
      delete c.d;
    }
  }

  void schedule(const std::vector<Event>& score) {
    pend.clear(); shots.clear(); len = 0;
    std::vector<int> notes(kNumParts, 0);
    auto add = [&](int pi, const Event& e) {
      const PartDef& D = kParts[pi];
      int ens = D.ens;
      for (int v = 0; v < ens; v++) {
        bool sp = D.ctl == Ctl::spiccato, ld = D.ctl == Ctl::lead;
        double lag = sp ? .004 * v : ld ? .012 * v : 0, pre = sp ? .07 : 0;
        double det = ens > 1 ? D.cents * (2.0 * v / (ens - 1) - 1) : 0;
        Pending p; p.start = (long long)(std::max(0.0, e.t + lag - pre) * sr); p.part = pi; p.dur = e.d; p.pre = pre;
        p.vel = e.vel; p.midi = e.midi; p.freq = float(440 * pow(2, (e.midi - 69) / 12.0) * pow(2, det / 1200));
        p.idx = notes[pi]++; p.ens_div = float(ens); p.p1 = e.p1; pend.push_back(p);
      }
      len = std::max(len, e.t + e.d + D.ring + 1);
    };
    for (auto& e : score) {
      if (e.part == "lead") {
        if (o.lead == Lead::horn) add(part_of("horn"), e);
        if (o.lead == Lead::fiddle || o.lead == Lead::blend) add(part_of("fiddle"), e);
        if (o.lead == Lead::choir || o.lead == Lead::blend) add(part_of("choir"), e);
      } else if (e.part == "ostinato") {
        if (o.ostinato != Ostinato::mouthbow) add(part_of("spiccato"), e);
        if (o.ostinato != Ostinato::spiccato) add(part_of("mouthbow"), e);
      } else if (e.part == "fade") {
        for (const char* n : {"horn", "fiddle", "choir"}) { parts[part_of(n)].fade_t = e.t; parts[part_of(n)].fade_tau = e.d; }
      } else if (e.part == "tail") {
        for (const char* n : {"horn", "fiddle", "choir"}) { parts[part_of(n)].tail = true; parts[part_of(n)].tail_t = e.t; }
      } else if (e.part == "subdrop") {
        Shot s; s.start = (long long)(e.t * sr); int n = int(1.8 * sr); s.L.resize(n); double ph = 0;
        for (int i = 0; i < n; i++) { double tt = double(i) / sr, f = 60 * exp(-tt * 1.2) + 28; ph += 2 * 3.14159265358979 * f / sr; s.L[i] = float(sin(ph) * exp(-tt * 2.2) * e.vel); }
        s.R = s.L; shots.push_back(std::move(s));
      } else if (e.part == "riser") {
        /* filled in by prepare_risers() */
      } else {
        int pi = part_of(e.part); if (pi >= 0) add(pi, e); else fprintf(stderr, "paleo: unknown part %s\n", e.part.c_str());
      }
    }
    std::stable_sort(pend.begin(), pend.end(), [](const Pending& a, const Pending& b) { return a.start < b.start; });
    pend_i = 0;
  }

  void start_voice(const Pending& q) {
    Part& P = parts[q.part]; if (!P.on) return;
    int slot = -1; long long oldest = (1LL << 62);
    for (int i = 0; i < int(P.v.size()); i++) { if (!P.v[i].active) { slot = i; break; } if (P.v[i].start < oldest) { oldest = P.v[i].start; slot = i; } }
    Voice& v = P.v[slot];
    if (v.active) P.act.erase(std::find(P.act.begin(), P.act.end(), slot));
    v.d->instanceClear();
    const PartDef& D = *P.def;
    Params::set(v.p.freq, q.freq); Params::set(v.p.gain, q.vel); Params::set(v.p.gate, 0);
    if (D.ctl == Ctl::lead || D.ctl == Ctl::spiccato) Params::set(v.p.p2, float(4.4 + 1.3 * fmod(q.idx * 0.618, 1.0)));
    if (!strcmp(D.name, "mouthbow")) Params::set(v.p.p1, float(3.6 - 3.4 * (0.5 + 0.5 * sin(q.idx * 0.9))));
    if (D.ctl == Ctl::horn) Params::set(v.p.p1, 3.3f);
    if (q.p1 >= 0) Params::set(v.p.p1, q.p1);   /* the score's own p1 (e.g. where a frame drum is hit) */
    v.active = true; v.start = q.start; v.dur = q.dur; v.pre = q.pre; v.vel = q.vel; v.midi = q.midi; v.idx = q.idx;
    v.ens_div = q.ens_div; v.end = q.start + (long long)((q.dur + D.ring) * sr);
    P.act.push_back(slot);
  }

  /* Control-rate update for one voice at time tt (seconds since its start); returns the output gate. */
  float control(const PartDef& D, Voice& v, double tt) {
    switch (D.ctl) {
      case Ctl::struck: Params::set(v.p.gate, tt < v.dur ? 1.f : 0.f); return 1;
      case Ctl::held: { bool on = tt < v.dur; Params::set(v.p.gate, on ? 1.f : 0.f); Params::set(v.p.gain, on ? v.vel : 0.f); return 1; }
      case Ctl::horn: {
        double env = tt < v.dur ? std::min(1.0, tt / .08) * (1 - .15 * tt / v.dur) : 0;
        Params::set(v.p.gate, tt < v.dur ? 1.f : 0.f); Params::set(v.p.gain, float(v.vel * env));
        Params::set(v.p.p1, float(3.6 - .8 * std::min(1.0, tt / .5))); return 1;
      }
      case Ctl::pipe: {
        double env = std::min(1.0, tt / 4.0) * std::max(0.0, std::min(1.0, (v.dur - tt) / 4.0));
        Params::set(v.p.gate, tt < v.dur ? 1.f : 0.f); Params::set(v.p.gain, float(v.vel * env));
        Params::set(v.p.p1, float(2.6 + 1.3 * sin(6.2832 * tt / 3.3) * sin(6.2832 * tt / 11.0))); return 1;
      }
      case Ctl::lead: case Ctl::spiccato: {
        bool sp = D.ctl == Ctl::spiccato; double onlen = sp ? std::min(v.dur + v.pre, .21) : v.dur;
        bool on = tt < onlen;
        double env = on ? std::min(1.0, tt / (sp ? .015 : .12)) * (1 - .1 * tt / v.dur) : 0;
        Params::set(v.p.gate, on ? 1.f : 0.f); Params::set(v.p.gain, float(v.vel * env));
        Params::set(v.p.p1, float(3.3 - 2.6 * std::min(1.0, tt / .7) * (v.midi > 50 ? 1.0 : .5)));
        return sp ? float(tt < onlen ? 1.0 : exp(-(tt - onlen) / .035)) : 1.f;
      }
    }
    return 1;
  }

  long long blk_t = 0; int blk_n = 0;
  void run_task(int ti) {
    Task& T = tasks[ti]; Part& P = parts[T.part]; const PartDef& D = *P.def;
    memset(T.buf, 0, sizeof(float) * blk_n);
    float tmp[kChunk]; float* outs[1] = {tmp};
    for (int a = T.from; a < T.to; a++) {
      Voice& v = P.v[P.act[a]];
      for (int c = 0; c < blk_n; c += kChunk) {
        long long ct = blk_t + c; int n = std::min(kChunk, blk_n - c);
        if (ct + n <= v.start || ct >= v.end) continue;
        float og = control(D, v, double(ct - v.start) / sr);
        v.d->compute(n, nullptr, outs);
        float g = og / v.ens_div; for (int k = 0; k < n; k++) T.buf[c + k] += tmp[k] * g;
      }
    }
  }

  void render_block(float* L, float* R, int n) {
    blk_t = t; blk_n = n;
    while (pend_i < pend.size() && pend[pend_i].start < t + n) start_voice(pend[pend_i++]);
    ntasks = 0;
    for (int pi = 0; pi < kNumParts; pi++) {
      Part& P = parts[pi]; if (!P.on) continue;
      for (int a = 0; a < int(P.act.size()); a += kTaskVoices) tasks[ntasks++] = {pi, a, std::min(a + kTaskVoices, int(P.act.size()))};
    }
    pool.run(ntasks);
    for (auto& P : parts) memset(P.buf, 0, sizeof(float) * n);
    for (int i = 0; i < ntasks; i++) { Part& P = parts[tasks[i].part]; for (int k = 0; k < n; k++) P.buf[k] += tasks[i].buf[k]; }
    for (auto& P : parts) {   /* retire finished voices */
      for (int a = 0; a < int(P.act.size());) { Voice& v = P.v[P.act[a]]; if (t + n >= v.end) { v.active = false; P.act.erase(P.act.begin() + a); } else a++; }
    }
    mix(L, R, n);
    t += n;
  }

  float hinL[kMaxBlock], hinR[kMaxBlock], rinL[kMaxBlock], rinR[kMaxBlock], tinL[kMaxBlock], tinR[kMaxBlock];
  float hoL[kMaxBlock], hoR[kMaxBlock], roL[kMaxBlock], roR[kMaxBlock], toL[kMaxBlock], toR[kMaxBlock];
  void mix(float* L, float* R, int n) {
    memset(hinL, 0, 4 * n); memset(hinR, 0, 4 * n); memset(rinL, 0, 4 * n); memset(rinR, 0, 4 * n);
    memset(tinL, 0, 4 * n); memset(tinR, 0, 4 * n); memset(L, 0, 4 * n); memset(R, 0, 4 * n);
    /* the drums' level drives the ducking of the drone, the ostinato and the leads */
    static float duck[kMaxBlock]; Part& dr = parts[part_of("taiko")];
    for (int pi = 0; pi < kNumParts; pi++) {
      Part& P = parts[pi]; if (!P.on) continue; const Strip& s = P.def->s;
      bool is_drum = pi == part_of("taiko"), is_lead = P.def->ctl == Ctl::lead || pi == part_of("horn");
      bool is_braam = pi == part_of("braam");
      for (int k = 0; k < n; k++) {
        float x = P.buf[k];
        if (s.pre > 0) x = tanhf(x * s.pre) / 0.76159416f;
        x = P.eq.tick(x) * s.k;
        double tt = double(t + k) / sr;
        if (tt > P.fade_t) x *= float(exp(-(tt - P.fade_t) / P.fade_tau));
        if (is_drum) duck[k] = 1 - .35f * std::min(1.f, std::max(0.f, duckf.tick(x) / kDuckRef));
        if (capture && is_braam) cap_add(k, x);
        float l, r;
        if (s.haas_ms < 0) { l = x * P.haas.gl; r = x * P.haas.gr; } else P.haas.tick(x, l, r);
        if (P.has_pp) P.pp.tick(l, r);
        if (o.lead == Lead::blend) { l *= s.weight; r *= s.weight; }   /* the blend: fiddle x0.75 + choir x0.7 */
        if (capture && is_lead) cap_add(k, (l + r) * .5f);
        if (s.duck_pow > 0) { float g = s.duck_pow == 1 ? duck[k] : powf(duck[k], s.duck_pow); l *= g; r *= g; }
        L[k] += l; R[k] += r;
        hinL[k] += l * s.hall; hinR[k] += r * s.hall; rinL[k] += l * s.room; rinR[k] += r * s.room;
        if (P.tail && tt >= P.tail_t) { tinL[k] += l * kTailIn; tinR[k] += r * kTailIn; }
      }
    }
    (void)dr;
    if (capture) return;
    for (int k = 0; k < n; k++) {   /* external stem: creatures */
      size_t i = size_t(t + k);
      if (i < sdL.size()) { L[k] += sdL[i]; R[k] += sdR[i]; hinL[k] += ssL[i]; hinR[k] += ssR[i]; }
    }
    for (auto& s : shots) {          /* risers and sub drops */
      long long a = std::max(t, s.start), b = std::min(t + n, s.start + (long long)s.L.size());
      for (long long i = a; i < b; i++) { L[i - t] += s.L[i - s.start]; R[i - t] += s.R[i - s.start]; }
    }
    hall.run(hinL, hinR, hoL, hoR, n); room.run(rinL, rinR, roL, roR, n); tailr.run(tinL, tinR, toL, toR, n);
    for (int k = 0; k < n; k++) {
      float l = L[k] + hoL[k] * kHallReturn + roL[k] * kRoomReturn + toL[k];
      float r = R[k] + hoR[k] * kHallReturn + roR[k] * kRoomReturn + toR[k];
      float e = gluef.tick(.5f * (l + r) * 2), g = e > kGlueThr ? powf(kGlueThr / e, .4f) : 1.f;   /* bus glue on L+R */
      L[k] = l * g * kOutGain; R[k] = r * g * kOutGain;
    }
  }
  void cap_add(int k, float x) { long long i = t + k - cap_from; if (i >= 0 && i < (long long)cap.size()) cap[size_t(i)] += x; }

  /* Risers: the leads and braams of the first 1.2 s after each hit, through the hall, reversed and swelling
     into the hit (3 s long, peak level = vel), as mix4.py made them. Needs the future, so it renders ahead. */
  void prepare_risers(const std::vector<Event>& score) {
    std::vector<double> hits; std::vector<float> lv;
    for (auto& e : score) if (e.part == "riser") { hits.push_back(e.t); lv.push_back(e.vel); }
    if (hits.empty()) return;
    for (auto& P : parts) P.on = P.on && (P.def->ctl == Ctl::lead || !strcmp(P.def->name, "horn") || !strcmp(P.def->name, "braam"));
    capture = true; cap_from = 0; cap.assign(size_t((*std::max_element(hits.begin(), hits.end()) + 1.3) * sr), 0.f);
    std::vector<float> l(kMaxBlock), r(kMaxBlock);
    while (t < (long long)cap.size()) render_block(l.data(), r.data(), kMaxBlock);
    capture = false;
    for (size_t h = 0; h < hits.size(); h++) {
      Reverb rv; rv.init(sr, 3.4f, 4500, hall.g);
      int n = int(3.0 * sr), seg = int(1.2 * sr); size_t a = size_t(hits[h] * sr);
      std::vector<float> in(n, 0.f), oL(n), oR(n);
      for (int i = 0; i < seg && a + i < cap.size(); i++) in[i] = cap[a + i];
      std::vector<float> in2 = in; rv.run(in.data(), in2.data(), oL.data(), oR.data(), n); delete rv.d;
      Shot s; s.L.resize(n); s.R.resize(n); float pk = 1e-9f;
      for (int i = 0; i < n; i++) { float ramp = float(i) / (n - 1); ramp *= ramp; s.L[i] = oL[n - 1 - i] * ramp; s.R[i] = oR[n - 1 - i] * ramp; pk = std::max(pk, std::max(fabsf(s.L[i]), fabsf(s.R[i]))); }
      for (int i = 0; i < n; i++) { s.L[i] *= lv[h] / pk; s.R[i] *= lv[h] / pk; }
      s.start = (long long)(hits[h] * sr) - n; shots.push_back(std::move(s));
    }
  }
  void reset_state() {
    t = 0;
    for (auto& P : parts) { for (auto& v : P.v) v.active = false; P.act.clear(); }
    /* fresh effects so the pre-pass leaves nothing behind */
    for (auto& P : parts) {
      const Strip& s = P.def->s; P.eq.setup(float(sr), s.lo, s.hi, s.dipf, s.dipd, s.bf, s.bb);
      P.haas.setup(float(sr), s.haas_ms < 0 ? 0 : s.haas_ms, s.pan);
      if (P.has_pp) P.pp.setup(float(sr), s.pp_secs, s.pp_fb, s.pp_mix);
    }
    duckf = Follower(); duckf.setup(float(sr), .002f, .18f, 48); gluef = Follower(); gluef.setup(float(sr), .01f, .25f, 96);
    for (Reverb* r : {&hall, &room, &tailr}) r->d->instanceClear();
  }
};

Engine::Engine(const Options& o) : p(new Impl) { flush_denormals(); p->o = o; p->init(); }
Engine::~Engine() {
  for (auto& P : p->parts) for (auto& v : P.v) delete v.d;
  delete p->hall.d; delete p->room.d; delete p->tailr.d; delete p;
}
void Engine::load(const std::vector<Event>& score) {
  flush_denormals();
  bool saved[kNumParts]; for (int i = 0; i < kNumParts; i++) saved[i] = p->parts[i].on;
  p->schedule(score);
  p->prepare_risers(score);
  for (int i = 0; i < kNumParts; i++) p->parts[i].on = saved[i];
  p->reset_state(); p->pend_i = 0;
}
void Engine::set_stem(std::vector<float> a, std::vector<float> b, std::vector<float> c, std::vector<float> d) {
  p->sdL = std::move(a); p->sdR = std::move(b); p->ssL = std::move(c); p->ssR = std::move(d);
}
void Engine::render(float* L, float* R, int frames) {
  flush_denormals(); double t0 = now_s(); int done = 0;
  while (done < frames) { int n = std::min(kMaxBlock, frames - done); p->render_block(L + done, R + done, n); done += n; }
  p->load = (now_s() - t0) / (double(frames) / p->sr);
}
double Engine::seconds() const { return double(p->t) / p->sr; }
double Engine::length() const { return p->len; }
int Engine::threads() const { return int(p->pool.th.size()) + 1; }
double Engine::last_load() const { return p->load; }
int Engine::active_voices() const { int n = 0; for (auto& P : p->parts) n += int(P.act.size()); return n; }

std::vector<Event> read_score(const char* path) {
  std::vector<Event> out; std::ifstream f(path); std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream s(line); Event e;
    if (s >> e.t >> e.d >> e.part >> e.midi >> e.vel) { float p; if (s >> p) e.p1 = p; out.push_back(e); }
  }
  return out;
}
}  // namespace paleo
