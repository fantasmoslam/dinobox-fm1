# SPDX-License-Identifier: GPL-3.0-only
# SAURUS: reshapes real animal recordings into creature calls. Pitch and throat (formants) move separately, using
# Rubber Band through ffmpeg. Build-time only: neither tool ships in the app.
import numpy as np, subprocess, wave, glob, os, tempfile
SR = 48000
SRC = os.environ.get('PALEO_SOURCES', os.path.expanduser('~/work/src'))   # the saurus2/sources folder
_TMP = tempfile.mkdtemp(prefix='saurus-')

def loadpath(p, a=0, b=None):
    x = np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', p, '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                                     capture_output=True, check=True).stdout, np.float32).astype(float)
    x = x[int(a * SR):(int(b * SR) if b else None)]
    return x / (np.abs(x).max() + 1e-9) * 0.7
def load(idpre, a=0, b=None):
    return loadpath(glob.glob(os.path.join(SRC, idpre + '*'))[0], a, b)
def _w(x, p):
    w = wave.open(p, 'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(SR)
    w.writeframes((np.clip(x, -1, 1) * 32767).astype('<i2').tobytes()); w.close()
def rb(x, semi=0, tempo=1.0, formant='preserved'):
    p = os.path.join(_TMP, 'rb.wav'); _w(x / (np.abs(x).max() + 1e-9) * 0.7, p)
    af = 'rubberband=pitch=%.4f:tempo=%.4f:formant=%s:transients=crisp:window=standard' % (2 ** (semi / 12), tempo, formant)
    return np.frombuffer(subprocess.run(['ffmpeg', '-loglevel', 'error', '-i', p, '-af', af, '-ac', '1', '-ar', str(SR), '-f', 'f32le', '-'],
                                        capture_output=True, check=True).stdout, np.float32).astype(float)
def shape(x, semi=0, throat=1.0, tempo=1.0):
    """move the pitch by semi and the throat (formants) by a ratio, separately"""
    if abs(throat - 1) > 1e-3:
        t = 12 * np.log2(throat); x = rb(rb(x, t, 1.0, 'shifted'), -t, 1.0, 'preserved')
    return rb(x, semi, tempo, 'preserved')
def filt(x, lo=None, hi=None):
    X = np.fft.rfft(x); f = np.fft.rfftfreq(len(x), 1 / SR)
    if lo: X *= 1 / (1 + (lo / np.maximum(f, 1)) ** 4)
    if hi: X *= 1 / (1 + (f / hi) ** 4)
    return np.fft.irfft(X, len(x))
def env(x, att=0.01, rel=0.05):
    n = len(x); e = np.ones(n); a = int(att * SR); r = int(rel * SR)
    if a: e[:a] = np.linspace(0, 1, a)
    if r: e[-r:] *= np.linspace(1, 0, r)
    return x * e
def drive(x, a): return np.tanh(x * (1 + a * 5)) / (1 + a * 1.5)
def nrm(x, p=0.5): return x / (np.abs(x).max() + 1e-9) * p
def lay(*parts):   # (signal, gain, offset seconds)
    L = max(int(o * SR) + len(s) for s, g, o in parts); y = np.zeros(L)
    for s, g, o in parts: y[int(o * SR):int(o * SR) + len(s)] += s * g
    return y
def events(idpre, n=8, minlen=0.35, maxlen=2.5, start=0, stop=None):
    """the n loudest separate calls in a recording"""
    x = load(idpre, start, stop); w = 1200; m = len(x) // w
    db = 20 * np.log10(np.sqrt((x[:m * w].reshape(m, w) ** 2).mean(1)) + 1e-9)
    thr = np.percentile(db, 90) - 10; ev = []; on = False
    for i, v in enumerate(db):
        if v > thr and not on: on = True; s = i
        if (v <= thr or i == m - 1) and on:
            on = False; a, b = s * w / SR, i * w / SR
            if minlen <= b - a <= maxlen: ev.append((db[s:i].max(), a, b))
    ev = sorted(ev, reverse=True)[:n]
    return [x[max(0, int((a - .03) * SR)):int((b + .15) * SR)] for _, a, b in ev]
def trim(x, floor_db=-60):
    """cut the silent tail so the engine doesn't play nothing"""
    e = np.abs(x); thr = e.max() * 10 ** (floor_db / 20); idx = np.nonzero(e > thr)[0]
    return x[:idx[-1] + int(0.05 * SR)] if len(idx) else x
