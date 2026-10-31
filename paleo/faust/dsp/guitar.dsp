// SPDX-License-Identifier: GPL-3.0-only
import("paleo.lib");
// parlour guitar (expedition): a small-bodied gut-string guitar, finger-plucked, warm and clear.
finger = (en.ar(0.0008,0.002,trig) + no.noise*en.ar(0.0003,0.004,trig)*0.6) : fi.lowpass(1,5000);
pluckpos(x) = x - x@int(ma.SR/f*0.18);                     // plucked a little way from the bridge
str = kstring(f, 0.88, 0.9985, pluckpos(finger*g));
body = str <: _*0.8, (_ <: fi.resonbp(105,3,0.5), fi.resonbp(220,4,0.35), fi.resonbp(2600,2,0.15) :> _) :> _;
process = body*0.5 : fi.highpass(1,70) : fi.lowpass(2,8000);
