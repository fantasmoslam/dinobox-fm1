// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// harmonium (expedition): a portable pump reed organ. Two brass reeds per note, slightly apart, with bellows swell.
env = g : si.smooth(ba.tau2pole(0.09));
bellows = 1 + 0.04*no.lfnoise(1.5);
reed(x) = os.sawtooth(x) : fi.lowpass(1, x*5) : fi.highpass(1, x*0.7);
voice = reed(f) + reed(f*1.0029)*0.8 + os.square(f*0.5)*0.12;          // second reed a few cents sharp, soft sub
breath = no.noise : fi.bandpass(1,800,3000) : *(0.004);
process = (voice*0.25 + breath)*env*bellows : fi.resonbp(1100,1,0.5) + (voice*0.15*env) : fi.lowpass(2,5000) : *(0.6);
