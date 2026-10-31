#pragma once
#include <string>

struct Config {
    int start_delay = 3000;
    int resume_delay = 1000;
    int restart_delay = 1000;
    int pause_key = 0x20;   // VK_SPACE
    int restart_key = 'X';
    int quit_key = 'Q';

    bool load(const std::string& path);
    bool save(const std::string& path) const;
};

std::string vkToName(int vk);
int         nameToVk(const std::string& name);