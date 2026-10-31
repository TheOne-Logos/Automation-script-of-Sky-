#include "SongConfig.h"
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static fs::path u8ToPath(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}

static void trim(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    s = s.substr(i);
}

bool SongConfig::load(const std::string& path) {
    std::ifstream f(u8ToPath(path));
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        trim(k); trim(v);

        try {
            if (k == "name")           name = v;
            else if (k == "bpm")            bpm = std::stod(v);
            else if (k == "divisor")        divisor = std::stoi(v);
            else if (k == "hold")           hold = std::stoi(v);
            else if (k == "note_mode")      note_mode = v;
            else if (k == "jitter_enabled") jitter_enabled = std::stoi(v);
            else if (k == "jitter_time")    jitter_time = std::stoi(v);
            else if (k == "jitter_hold")    jitter_hold = std::stoi(v);
            else if (k == "jitter_coord")   jitter_coord = std::stoi(v);
            else if (k == "jitter_chance")  jitter_chance = std::stod(v);
        }
        catch (...) {}
    }
    return true;
}

bool SongConfig::save(const std::string& path) const {
    std::ofstream f(u8ToPath(path));
    if (!f.is_open()) return false;
    f << "name=" << name << "\n";
    f << "bpm=" << bpm << "\n";
    f << "divisor=" << divisor << "\n";
    f << "hold=" << hold << "\n";
    f << "note_mode=" << note_mode << "\n";
    f << "jitter_enabled=" << jitter_enabled << "\n";
    f << "jitter_time=" << jitter_time << "\n";
    f << "jitter_hold=" << jitter_hold << "\n";
    f << "jitter_coord=" << jitter_coord << "\n";
    f << "jitter_chance=" << jitter_chance << "\n";
    return true;
}