// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// contrabass recorder: a tall square-bored wooden recorder, an octave below the bass recorder (down to F2).
// Deep, hollow and woody; notes speak a little slowly with a soft "chuff", and the breath stays low under the tone.
env = g : si.smooth(ba.tau2pole(0.11));
vib = 1 + 0.003*os.osc(3.8+no.lfnoise(0.6)*0.3)*en.asr(0.9,1,0.2,t);
ff = f*vib*(1 - 0.012*en.ar(0.001,0.1,trig));                      // a slight scoop as the note speaks
tone = os.osc(ff) + 0.3*os.osc(2*ff) + 0.14*os.osc(3*ff) + 0.05*os.osc(4*ff) + 0.02*os.osc(5*ff);
hollow = tone : fi.resonbp(ff*3,2,0.35) : fi.resonbp(700,1.2,1);    // the square wooden bore
breath = no.noise : fi.resonbp(ff,8,1) : fi.resonbp(ff,8,1) : *(0.06);
chuff = no.noise*en.ar(0.008,0.09,trig)*g : fi.resonbp(ff*2,3,1) : fi.lowpass(2,1500) : *(0.35);
process = ((tone*0.7 + hollow*0.6 + breath)*env*(1+no.lfnoise(4)*0.025) + chuff) : fi.lowpass(2,3500) : *(0.55);
