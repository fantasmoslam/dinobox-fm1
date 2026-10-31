// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// field bugle (expedition): a valveless brass horn; bright as it's blown harder, notes scoop up into pitch.
env = g : si.smooth(ba.tau2pole(0.03));
scoop = 1 - 0.035*en.ar(0.001,0.08,trig);
vib = 1 + 0.003*os.osc(5.5)*en.asr(0.5,1,0.2,t);
ff = f*scoop*vib;
lips = os.sawtooth(ff) + 0.4*os.sawtooth(ff*1.0015);
bright = lips : fi.lowpass(2, ff*(1.5+7*env)) : fi.lowpass(1, ff*(2+6*env));
bell = bright : fi.resonbp(1200,1.2,0.6) + bright*0.6;
blat = no.noise*en.ar(0.002,0.03,trig)*g : fi.bandpass(1,ff,ff*4) : *(0.08);
process = (bell*env*0.4 + blat) : fi.highpass(1,120) : fi.lowpass(2,6000) : *(0.6);
