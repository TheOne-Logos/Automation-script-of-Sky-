#include "Config.h"
#include <windows.h>
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

std::string vkToName(int vk) {
    if (vk == VK_SPACE)  return "SPACE";
    if (vk == VK_RETURN) return "ENTER";
    if (vk == VK_ESCAPE) return "ESC";
    if (vk == VK_OEM_COMMA) return "COMMA";
    if (vk == VK_OEM_PERIOD) return "PERIOD";
    if (vk >= 'A' && vk <= 'Z') return std::string(1, (char)vk);
    if (vk >= '0' && vk <= '9') return std::string(1, (char)vk);
    return std::to_string(vk);
}

int nameToVk(const std::string& name) {
    if (name == "SPACE")  return VK_SPACE;
    if (name == "ENTER")  return VK_RETURN;
    if (name == "ESC" || name == "ESCAPE") return VK_ESCAPE;
    if (name == "COMMA")  return VK_OEM_COMMA;
    if (name == "PERIOD") return VK_OEM_PERIOD;
    if (name.size() == 1) {
        char c = name[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (c == ',') return VK_OEM_COMMA;
        if (c == '.') return VK_OEM_PERIOD;
        return c;
    }
    try { return std::stoi(name); }
    catch (...) { return -1; }
}

bool Config::load(const std::string& path) {
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
            if (k == "start_delay")        start_delay = std::stoi(v);
            else if (k == "resume_delay")  resume_delay = std::stoi(v);
            else if (k == "restart_delay") restart_delay = std::stoi(v);
            // 键位：仅当 nameToVk 返回正数才覆盖，失败保留原值
            else if (k == "pause_key") {
                int vk = nameToVk(v);
                if (vk > 0) pause_key = vk;
            }
            else if (k == "restart_key") {
                int vk = nameToVk(v);
                if (vk > 0) restart_key = vk;
            }
            else if (k == "quit_key") {
                int vk = nameToVk(v);
                if (vk > 0) quit_key = vk;
            }
        }
        catch (...) {}
    }
    return true;
}

bool Config::save(const std::string& path) const {
    std::ofstream f(u8ToPath(path));
    if (!f.is_open()) return false;
    f << "start_delay=" << start_delay << "\n";
    f << "resume_delay=" << resume_delay << "\n";
    f << "restart_delay=" << restart_delay << "\n";
    f << "pause_key=" << vkToName(pause_key) << "\n";
    f << "restart_key=" << vkToName(restart_key) << "\n";
    f << "quit_key=" << vkToName(quit_key) << "\n";
    return true;
}