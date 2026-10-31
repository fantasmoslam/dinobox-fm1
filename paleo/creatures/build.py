# SPDX-License-Identifier: GPL-3.0-only
# Builds PALEO's creature library: many seeded variants of each call, made by SAURUS from real recordings.
#   PALEO_SOURCES=/path/to/saurus2/sources PALEO_REX=/path/to/dylan-trex-dry.wav python3 build.py
# Writes lib/<type>-<nn>.flac (48 kHz mono, peak about -6 dBFS like the STAMPEDE mix) and lib/manifest.txt.
# The engine picks a variant per call and changes its pitch, throat, distance and pan, so calls never repeat exactly.
import os, sys, random, subprocess, numpy as np
from saurus import *
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(HERE, 'lib'); os.makedirs(OUT, exist_ok=True)
REX = loadpath(os.environ.get('PALEO_REX', os.path.expanduser('~/work/ref/dry.wav')))

# sources (see CREDITS.md)
dol = load('456151', 0.45, 2.1); geese = load('369824', 3.2, 4.0); honk = load('369824', 0.2, 0.7)
hiss = load('844558'); tort = load('871251'); tig = load('437978', 4.75, 5.95)
eleph = events('819668', 4, 0.4, 4); camel = events('437937', 6) + events('685761', 6); gator = events('689966', 8, 0.5, 3)
walrus = events('103115', 10, 0.4, 2.5, 0, 240); crow = load('512781'); raven = events('274439', 4, 0.2, 1.2)
shouts = events('245448', 12, 0.15, 0.6); battle = events('160769', 8, 0.25, 1.5)

# ---- the calls (as approved in STAMPEDE), each with a seed
def rex(seed):
    r = random.Random(seed); th = r.uniform(0.8, 0.9)
    return drive(nrm(lay((shape(REX, -3 + r.uniform(-1, 1), th, r.uniform(0.8, 0.9)), 1, 0),
                         (filt(shape(REX, -12, 1.0, 0.85), None, 250), 0.5, 0))), 0.25)
def sauropod(seed):
    r = random.Random(seed); src = eleph[seed % len(eleph)]; st = -12 + r.uniform(-1.5, 1); th = r.uniform(0.55, 0.65)
    tp = r.uniform(0.5, 0.6)
    return nrm(lay((shape(src, st, th, tp), 1, 0), (filt(shape(src, st - 7, th, tp), None, 200), 0.6, 0)))
def ptero(seed):
    r = random.Random(seed); src = crow if seed % 3 == 0 else raven[seed % len(raven)]
    return nrm(shape(src, -5 + r.uniform(-2, 2), r.uniform(0.75, 0.85), r.uniform(0.85, 1.0)))
def herd(seed):
    r = random.Random(seed); src = r.choice(camel + gator + walrus + eleph[1:])
    return drive(nrm(shape(src, r.uniform(-12, -6), r.uniform(0.65, 0.85), r.uniform(0.8, 1.1))), 0.3)
def screech(seed):
    r = random.Random(seed); size = r.uniform(0.9, 1.1); p = r.uniform(-1.5, 1.5)
    a = shape(dol, -8 + p - 3 * (size - 1), 0.75 / size, 1.0 + r.uniform(-.1, .1))
    b = shape(geese, -10 + p - 3 * (size - 1), 0.8 / size, 1.1); h = filt(hiss, 1500, 9000)
    y = lay((env(a, 0.02, 0.4), 1.0, 0), (env(b, 0.01, 0.3), 0.7, 0.05), (env(h[:len(a)], 0.05, 0.4), 0.25, 0.0))
    return drive(nrm(y), 0.35 + 0.2 * r.random())
def bark(seed):
    r = random.Random(seed)
    a = shape(honk, -12 + r.uniform(-1, 1), 0.75, 1.0); t = shape(tort, -6 + r.uniform(-1, 1), 0.8, 0.9)
    return drive(nrm(lay((env(a, 0.005, 0.08), 1, 0), (env(t, 0.002, 0.05), 0.8, 0.0))), 0.5)
def threat_hiss(seed):
    r = random.Random(seed); h = filt(hiss, 700, 8000); g = shape(tig, 2 + r.uniform(-2, 1), r.uniform(0.9, 1.1), 1.0)[:len(h) * 2]
    rate = r.uniform(19, 27); trem = 1 + 0.6 * np.sin(2 * np.pi * rate * np.arange(len(g)) / SR)
    return nrm(lay((env(h, 0.05, 0.3), 0.8, 0), (env(g * trem, 0.2, 0.4), 0.6, 0.2)))
def chatter(seed):
    r = random.Random(seed); n = r.randint(8, 14); parts = []; t = 0
    for i in range(n):
        s = shape(tort, -27 + r.uniform(-2, 2), 0.6, 1.5)
        parts.append((env(s, 0.002, 0.03), 0.6 + 0.4 * r.random(), t)); t += 0.07 + 0.06 * r.random()
    g = shape(tig, -12, 0.7, 1.3); parts.append((env(g[:int(t * SR) + 4800], 0.05, 0.2), 0.6, 0))
    return drive(nrm(lay(*parts)), 0.3)
def shout(seed):
    r = random.Random(seed); src = battle[r.randrange(len(battle))] if r.random() < 0.5 else shouts[r.randrange(len(shouts))]
    return drive(nrm(shape(src, r.uniform(-6, -2), r.uniform(0.72, 0.9))), 0.35)
def boom(seed):
    r = random.Random(seed); src = r.choice(eleph + gator[:3] + [REX])
    return nrm(env(filt(shape(src, r.choice([-12, -14, -17]), 0.6, 0.6), 25, 450), 0.04, 0.8))

TYPES = [('rex', rex, 8), ('sauropod', sauropod, 8), ('ptero', ptero, 10), ('herd', herd, 32), ('screech', screech, 12),
         ('bark', bark, 12), ('hiss', threat_hiss, 4), ('chatter', chatter, 8), ('shout', shout, 24), ('boom', boom, 8)]
only = sys.argv[1:]
man = []
for name, fn, n in TYPES:
    if only and name not in only: continue
    for i in range(n):
        y = trim(fn(1000 * (TYPES.index((name, fn, n)) + 1) + i)).astype(np.float32)
        f = '%s-%02d.flac' % (name, i)
        subprocess.run(['ffmpeg', '-loglevel', 'error', '-y', '-f', 'f32le', '-ar', str(SR), '-ac', '1', '-i', '-',
                        '-c:a', 'flac', '-sample_fmt', 's32', '-compression_level', '8', os.path.join(OUT, f)], input=y.tobytes(), check=True)
        man.append('%s %s' % (name, f)); print(f, '%.2f s' % (len(y) / SR), flush=True)
if not only:
    open(os.path.join(OUT, 'manifest.txt'), 'w').write('# type file (built by build.py)\n' + '\n'.join(man) + '\n')
