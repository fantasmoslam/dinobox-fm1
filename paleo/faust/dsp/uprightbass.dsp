// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// upright bass (expedition): gut strings on a big wooden body. p1 < 0.5 plucked (pizzicato), p1 >= 0.5 bowed (arco).
pmx = library("physmodels.lib")[maxLength = 9;];
bowed = p1 >= 0.5;
benv = g : si.smooth(ba.tau2pole(0.12));
vib = 1 + 0.0045*os.osc(4.8+no.lfnoise(0.5)*0.4)*en.asr(0.8,1,0.2,t*bowed);   // the left hand's vibrato on long bowed notes
arco = pmx.violinModel(pmx.f2l(f*vib), 0.6+0.2*benv, benv*0.9*bowed, 0.82)*0.35;
hair = no.noise*benv*bowed*(1+0.5*no.lfnoise(8)) : fi.bandpass(2,900,2800) : *(0.012);   // rosin and bow hair
finger = (en.ar(0.002,0.006,trig) + no.noise*en.ar(0.0005,0.004,trig)*0.25) : fi.lowpass(1,2500);
pluckpos(x) = x - x@int(ma.SR/f*0.25);
damp = select2(t, 0.990, 0.9993);                           // the string rings while held, then the hand damps it
pizz = kstring(f, 0.62, damp, pluckpos(finger*g*(1-bowed)))*0.6;
body = (arco + pizz) <: _*0.7, (_ <: fi.resonbp(110,2,0.6), fi.resonbp(190,3,0.4), fi.resonbp(65,2,0.4), fi.resonbp(260,2,0.5), fi.resonbp(1500,1.5,0.6*bowed) :> _) :> _;
process = body + hair : fi.highpass(1,30) : fi.lowpass(2,4500);
