# PALEO, Paleotone's sound engine

PALEO plays Paleotone's own instruments through the STAMPEDE effects chain. It's portable C++17, with no
platform code in `engine/`, so the same engine runs on the Windows test bench, in the Android app, and later
in the browser. Plan: https://claude.ai/artifact/3Kagh8rE2YXGsjbAeCzrmX (Steps 1 and 2).

| Folder | What |
| --- | --- |
| `faust/dsp` | The instrument designs in Faust: taiko, big skin drum, log drum, seed rattle, struck stones, mouth bow, horn (and braam), drone pipe, gut fiddle, throat choir, the batch-1 instruments (bone flute, reed panpipes, bowed bone, slit drum, clay pot drum, frame drum, bone rasp, bullroarer), pack 2 (contrabass recorder, fujara, upright and bowed contrabass, harmonium, field bugle, parlour guitar), and the reverb buses |
| `faust/gen` | C++ generated from them by `faust/gen.sh` (Faust 2.70), committed so builds need no Faust |
| `engine` | The engine: voices and their control, per-part channel strips, hall/room/tail, ducking, risers, sub drops, bus glue, a worker pool across cores |
| `creatures` | The creature library (Step 2): `build.py` makes seeded variants of each call with SAURUS (`saurus.py`) into `lib/` (FLAC plus `manifest.txt`); sources and licences in `CREDITS.md` |
| `tools/render.cpp` | `paleo-render`: a score to a float WAV, offline |
| `tools/play.cpp` | `paleo-play`: a score played live (WASAPI on Windows, via miniaudio), with a load readout |
| `scores/stampede.txt` | STAMPEDE as arranged for v5, with its creature cues |
| `scores/creature-demo.txt` | Every kind of creature call, close, far, left, right, lower and higher |
| `third_party/miniaudio.h` | miniaudio 0.11.21 (public domain or MIT-0) |

The mix levels in `engine/paleo.cpp` (`kParts`) were measured from the STAMPEDE v5 renders: each part's gain is
the song's target level divided by that part's measured active RMS after its drive and EQ, so the engine's
render matches v5 to within about 0.3 dB in every 8-second section.

The batch-1 instruments have no song to match yet. Their gains were set against the STAMPEDE parts by active level
(for example bone flute 4 dB under the fiddle, rasp 2 dB under the rattle). `scores/kit-demo.txt` plays all eight in one groove, and `scores/kit-demo2.txt` plays pack 2 in a dawn scene and a camp scene.

## Creatures

A creature cue in a score is `t 0 KIND MIDI GAIN [DISTANCE] [PAN]`. KIND is rex, sauropod, ptero, herd, screech,
bark, hiss, chatter, shout or boom. MIDI 60 plays the call at its built pitch; other notes shift it by semitones.
DISTANCE runs from 0 (close) to 1 (far): far calls are darker, quieter and wetter. PAN runs from -1 to 1; left
out, the side is random. Each cue picks a different variant from the last one of its kind and gets its own small
pitch (±0.7 semitone) and throat (±3 dB around 350 to 1250 Hz) changes, so calls never repeat exactly. The bus
is at the v5 creature stem's level: in STAMPEDE it matches v5 within about 2 dB in every 10-second section.

The programs find the library in `creatures` next to the score, `../creatures/lib` from the score's folder,
or `--creatures DIR`. To rebuild it (needs ffmpeg with Rubber Band, numpy, and the source recordings):
`PALEO_SOURCES=/path/to/sources PALEO_REX=/path/to/roar.wav python3 paleo/creatures/build.py`.

Build here: `sh paleo/build.sh` (Linux or macOS). CI (`.github/workflows/paleo.yml`) builds the Windows
programs and renders STAMPEDE to MP3.

On Windows (PowerShell, in the folder holding the programs):

```
.\paleo-play.exe .\stampede.txt
.\paleo-play.exe .\stampede.txt --lead horn --ostinato both
```

Options: `--lead horn|fiddle|choir|blend`, `--ostinato spiccato|mouthbow|both`, `--threads N`,
`--period MS` (default 10), `--gain DB`, `--creatures DIR`.

Not yet: live MIDI input, the app (Step 3) and the generative conductor (Step 4).
