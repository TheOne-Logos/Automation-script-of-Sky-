#include "SongLibrary.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

static fs::path u8ToPath(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}
static std::string pathToU8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

static bool readFileToString(const std::string& path, std::string& out) {
    std::ifstream f(u8ToPath(path), std::ios::binary);
    if (!f.is_open()) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// 扩展名归一化为小写，比较时不受文件名大小写影响
static std::string lowerExt(const fs::path& p) {
    std::string ext = pathToU8(p.extension());
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    return ext;
}

bool SongLibrary::scan(const std::string& dir) {
    songs_.clear();
    dir_ = dir;

    fs::path dirP = u8ToPath(dir_);
    std::error_code ec;

    if (!fs::exists(dirP, ec) || ec) {
        // 目录不存在则创建，失败也不崩溃（下次运行再试）
        fs::create_directories(dirP, ec);
        return true;
    }
    if (!fs::is_directory(dirP, ec) || ec) return true;

    fs::directory_iterator it(dirP, ec);
    if (ec) return true;
    fs::directory_iterator end;

    for (; it != end; it.increment(ec)) {
        if (ec) break;

        const fs::directory_entry& entry = *it;
        std::error_code fec;
        if (!entry.is_regular_file(fec) || fec) continue;

        // 扩展名大小写不敏感：.txt / .TXT / .Txt 都识别
        if (lowerExt(entry.path()) != ".txt") continue;

        Song s;
        s.txtPath = pathToU8(entry.path());
        s.name = pathToU8(entry.path().stem());

        // cfg 路径：用 replace_extension，避免手拼路径出岔子
        fs::path cfgP = entry.path();
        cfgP.replace_extension(L".cfg");
        s.cfgPath = pathToU8(cfgP);
        s.config.name = s.name;

        std::string content;
        if (!readFileToString(s.txtPath, content)) continue;
        s.events = parseScoreString(content);

        std::error_code cec;
        if (fs::exists(cfgP, cec) && !cec) {
            s.config.load(s.cfgPath);
        }
        else {
            s.config.save(s.cfgPath);
        }
        songs_.push_back(s);
    }

    // 按名字排序，列表顺序更稳定（不依赖文件系统的返回顺序）
    std::sort(songs_.begin(), songs_.end(),
        [](const Song& a, const Song& b) { return a.name < b.name; });

    return true;
}

bool SongLibrary::add(const std::string& srcTxtPath, const SongConfig& cfg) {
    fs::path src = u8ToPath(srcTxtPath);
    std::error_code ec;
    if (!fs::exists(src, ec) || ec) return false;

    fs::path dirP = u8ToPath(dir_);
    fs::create_directories(dirP, ec);   // 已存在不算错误；真实失败时下面 copy_file 会报出来

    std::string stemName = cfg.name.empty() ? pathToU8(src.stem()) : cfg.name;
    fs::path dstTxt = dirP / u8ToPath(stemName + ".txt");
    fs::path dstCfg = dirP / u8ToPath(stemName + ".cfg");

    // 关键修复：如果源和目标其实是同一个文件（已经导入过），跳过复制
    bool sameFile = false;
    std::error_code eec;
    if (fs::exists(dstTxt, eec) && !eec) {
        sameFile = fs::equivalent(src, dstTxt, eec);
        if (eec) sameFile = false;
    }

    if (!sameFile) {
        std::error_code cec;
        fs::copy_file(src, dstTxt, fs::copy_options::overwrite_existing, cec);
        if (cec) return false;
    }

    SongConfig outCfg = cfg;
    outCfg.name = stemName;
    if (!outCfg.save(pathToU8(dstCfg))) return false;

    Song s;
    s.name = stemName;
    s.txtPath = pathToU8(dstTxt);
    s.cfgPath = pathToU8(dstCfg);
    s.config = outCfg;

    std::string content;
    if (readFileToString(s.txtPath, content)) {
        s.events = parseScoreString(content);
    }

    for (size_t i = 0; i < songs_.size(); i++) {
        if (songs_[i].name == stemName) {
            songs_[i] = s;
            return true;
        }
    }
    songs_.push_back(s);

    // 与 scan 保持一致：加完也按名字排序
    std::sort(songs_.begin(), songs_.end(),
        [](const Song& a, const Song& b) { return a.name < b.name; });

    return true;
}

bool SongLibrary::remove(size_t index) {
    if (index >= songs_.size()) return false;
    Song& s = songs_[index];
    std::error_code ec;
    fs::remove(u8ToPath(s.txtPath), ec);
    fs::remove(u8ToPath(s.cfgPath), ec);
    songs_.erase(songs_.begin() + index);
    return true;
}