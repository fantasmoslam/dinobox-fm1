/* SPDX-License-Identifier: GPL-3.0-only */
/* Loads PALEO's creature library (paleo/creatures/lib: manifest.txt plus FLAC files) into an engine.
   Needs miniaudio's decoder, so include it after miniaudio.h with its implementation. */
#pragma once
#include "../engine/paleo.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

/* Looks in dir, or if dir is empty: "creatures" next to the score, then creatures/lib beside the score folder, ./creatures, ./creatures/lib, paleo/creatures/lib. Returns the files loaded. */
static int paleo_load_creatures(paleo::Engine& e, std::string dir, const char* score_path) {
  if (dir.empty()) {
    std::string s = score_path; size_t k = s.find_last_of("/\\");
    std::string here = k == std::string::npos ? std::string(".") : s.substr(0, k);
    for (std::string c : {here + "/creatures", here + "/../creatures/lib", std::string("creatures"), std::string("creatures/lib"), std::string("paleo/creatures/lib")})
      if (std::ifstream(c + "/manifest.txt")) { dir = c; break; }
    if (dir.empty()) return 0;
  }
  std::ifstream m(dir + "/manifest.txt"); std::string line; int n = 0;
  while (std::getline(m, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::istringstream ss(line); std::string kind, file; if (!(ss >> kind >> file)) continue;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 1, 48000); ma_decoder d;
    if (ma_decoder_init_file((dir + "/" + file).c_str(), &cfg, &d) != MA_SUCCESS) { fprintf(stderr, "paleo: can't read %s\n", file.c_str()); continue; }
    std::vector<float> pcm; float buf[4096]; ma_uint64 got = 0;
    while (ma_decoder_read_pcm_frames(&d, buf, 4096, &got) == MA_SUCCESS && got > 0) pcm.insert(pcm.end(), buf, buf + got);
    ma_decoder_uninit(&d);
    e.add_creature(kind, std::move(pcm)); n++;
  }
  return n;
}
