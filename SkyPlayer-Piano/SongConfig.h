#pragma once
#include <string>

struct SongConfig {
    std::string name;
    double bpm = 120.0;
    int    divisor = 16;
    int    hold = 30;
    std::string note_mode = "once";
    int    jitter_enabled = 1;
    int    jitter_time = 15;
    int    jitter_hold = 8;
    int    jitter_coord = 3;
    double jitter_chance = 0.3;

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};