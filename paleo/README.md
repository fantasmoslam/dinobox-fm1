# PALEO, Paleotone's sound engine

PALEO plays Paleotone's own instruments through the STAMPEDE effects chain. It's portable C++17, with no
platform code in `engine/`, so the same engine runs on the Windows test bench, in the Android app, and later
in the browser. Plan: https://claude.ai/artifact/3Kagh8rE2YXGsjbAeCzrmX (Step 1).

| Folder | What |
| --- | --- |
| `faust/dsp` | The instrument designs in Faust: taiko, big skin drum, log drum, seed rattle, struck stones, mouth bow, horn (and braam), drone pipe, gut fiddle, throat choir, the batch-1 instruments (bone flute, reed panpipes, bowed bone, slit drum, clay pot drum, frame drum, bone rasp, bullroarer), pack 2 (contrabass recorder, fujara, upright and bowed contrabass, harmonium, field bugle, parlour guitar), and the reverb buses |
| `faust/gen` | C++ generated from them by `faust/gen.sh` (Faust 2.70), committed so builds need no Faust |
| `engine` | The engine: voices and their control, per-part channel strips, hall/room/tail, ducking, risers, sub drops, bus glue, a worker pool across cores |
| `tools/render.cpp` | `paleo-render`: a score to a float WAV, offline |
| `tools/play.cpp` | `paleo-play`: a score played live (WASAPI on Windows, via miniaudio), with a load readout |
| `scores/stampede.txt` | STAMPEDE as arranged for v5 |
| `third_party/miniaudio.h` | miniaudio 0.11.21 (public domain or MIT-0) |

The mix levels in `engine/paleo.cpp` (`kParts`) were measured from the STAMPEDE v5 renders: each part's gain is
the song's target level divided by that part's measured active RMS after its drive and EQ, so the engine's
render matches v5 to within about 0.3 dB in every 8-second section.

The batch-1 instruments have no song to match yet. Their gains were set against the STAMPEDE parts by active level
(for example bone flute 4 dB under the fiddle, rasp 2 dB under the rattle). `scores/kit-demo.txt` plays all eight in one groove, and `scores/kit-demo2.txt` plays pack 2 in a dawn scene and a camp scene.

Build here: `sh paleo/build.sh` (Linux or macOS). CI (`.github/workflows/paleo.yml`) builds the Windows
programs and renders STAMPEDE to MP3.

On Windows (PowerShell, in the folder holding the programs):

```
.\paleo-play.exe .\stampede.txt
.\paleo-play.exe .\stampede.txt --lead horn --ostinato both
```

Options: `--lead horn|fiddle|choir|blend`, `--ostinato spiccato|mouthbow|both`, `--threads N`,
`--period MS` (default 10), `--gain DB`.

Not yet in Step 1: the creatures (Step 2; `--stem` can add a pre-rendered creature track), live MIDI input,
and the generative conductor (Step 4).
