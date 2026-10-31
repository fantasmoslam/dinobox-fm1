// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// fujara: the Slovak shepherd's overtone flute, a tube taller than its player with three finger holes. It is played
// high in the harmonics of its low tube (here a D2 tube), and that is where its buzz comes from: the whole tube keeps
// vibrating at its low fundamental under the note, so the note carries the neighbouring tube harmonics with it.
// The higher the harmonic, the stronger the buzz, as on the real instrument (measured: neighbours about -10 dB at
// the 8th harmonic, -35 dB at the 4th). p1 >= 0.5 opens the note with a "rozfuk", the fall down the harmonics.
env = g : si.smooth(ba.tau2pole(0.05));
vib = 1 + 0.005*os.osc(5+no.lfnoise(0.8)*0.6)*en.asr(0.5,1,0.2,t);
age = (+(1) ~ *(1-trig)) / ma.SR;                                   // seconds since the note began
step = (age/0.11) : int : min(3);
fall = ba.selectn(4, step, 7/4, 6/4, 5/4, 1) : si.smooth(ba.tau2pole(0.012));
ratio = select2(p1 >= 0.5, 1, fall);
ff = f*vib*ratio;
tube = 73.42;                                                       // D2
n = f/tube + 0.5 : int : max(2) : min(12) : float;                       // which harmonic of the tube is sounding
nn = (n*ratio) : round : max(2);                                     // the rozfuk passes through higher ones
F = ff/nn;                                                           // the tube fundamental under this note
w = ba.selectn(13, nn : min(12) : int, 0,0,.15,.25,.05,1.2,.5,3,5,5,5,5,5);   // buzz by harmonic, as measured on the real one
saw = os.sawtooth(F);
pure = saw : fi.resonbp(ff,30,1) : fi.resonbp(ff,30,1) : *(nn*1.4/900);   // only the note itself (resonbp peaks at gain x Q)
buzz = saw : fi.resonbp(ff,8,1) : fi.resonbp(ff,3,1) : *(nn*1.4/24);       // the note with its tube neighbours
tone = (pure + buzz*w)/(1+w);                                               // both in phase on the note, so no cancelling
breath = no.noise : fi.bandpass(2,400,1500) : *(0.05);
puff = no.noise*en.ar(0.004,0.08,trig)*g : fi.bandpass(1,300,2000) : *(0.12);
process = ((tone*0.55 + breath)*env*(1+no.lfnoise(5)*0.04) + puff) : fi.lowpass(2,9000) : *(0.6);
