#pragma once
#include <string>
#include <vector>
#include "ScoreParser.h"
#include "SongConfig.h"

struct Song {
    std::string name;
    std::string txtPath;
    std::string cfgPath;
    SongConfig  config;
    std::vector<ChordEvent> events;
};

class SongLibrary {
public:
    // 扫描目录，读入所有 .txt 与同名 .cfg（无 .cfg 则自动生成默认）
    bool scan(const std::string& dir);

    // 导入新曲目：复制 txt 到 songs/，写 cfg
    bool add(const std::string& srcTxtPath, const SongConfig& cfg);

    // 删除曲目（txt + cfg 一起删）
    bool remove(size_t index);

    size_t size() const { return songs_.size(); }
    const Song& get(size_t i) const { return songs_[i]; }
    Song& get(size_t i) { return songs_[i]; }
    const std::string& dir() const { return dir_; }

private:
    std::vector<Song> songs_;
    std::string dir_;
};