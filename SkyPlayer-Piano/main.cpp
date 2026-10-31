#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 6031)
#pragma warning(disable: 4996)

#include <windows.h>
#include <timeapi.h>
#include <cstdio>
#include <conio.h>
#include <string>
#include <vector>
#include <filesystem>
#include "Config.h"
#include "ScoreParser.h"
#include "SongLibrary.h"
#include "Player.h"

#pragma comment(lib, "winmm.lib")

namespace fs = std::filesystem;

// ============ UTF-8 <-> fs::path ============
static fs::path u8ToPath(const std::string& s) {
    return fs::path(std::u8string(s.begin(), s.end()));
}
static std::string pathToU8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}

// ============ 全局路径 ============
static std::string g_exeDir;
static std::string g_songsDir;
static std::string g_keysPath;
static std::string g_cfgPath;

void initPaths() {
    char exePath[MAX_PATH] = { 0 };
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    std::string s = exePath;
    size_t pos = s.find_last_of("\\/");
    g_exeDir = (pos == std::string::npos) ? "" : s.substr(0, pos + 1);
    g_songsDir = g_exeDir + "songs";
    g_keysPath = g_exeDir + "keys.cfg";
    g_cfgPath = g_exeDir + "config.cfg";
}

// ============ 键位映射打印 ============
void printKeyMapping() {
    static const char* NOTES[15] = {
        "1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"
    };
    static const char* VKS[15] = {
        "R","T","Y","U","I","F","G","H","J","K","V","B","N","M",","
    };
    printf("当前键位映射（音符 -> 物理键）：\n  ");
    for (int i = 0; i < 15; i++) {
        printf("%s->%s  ", NOTES[i], VKS[i]);
        if ((i + 1) % 5 == 0 && i != 14) printf("\n  ");
    }
    printf("\n（请在模拟器按键映射里把上述 15 个物理键拖到对应琴键位置）\n");
}

// ============ 按键（菜单、暂停等即时响应） ============
static int pollKey() {
    if (!_kbhit()) return -1;
    int c = _getch();
    if (c == 0 || c == 224) { (void)_getch(); return -1; }
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c == ' ') return VK_SPACE;
    if (c == '\r' || c == '\n') return VK_RETURN;
    if (c == 27) return VK_ESCAPE;
    return c;
}

static void flushInput() { while (_kbhit()) (void)_getch(); }

// ============ 读一整行（宽字符，支持中文，返回 UTF-8） ============
std::string readLine() {
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);

    DWORD oldMode = 0;
    GetConsoleMode(hIn, &oldMode);
    DWORD newMode = oldMode | ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT;
    SetConsoleMode(hIn, newMode);

    std::wstring wline;
    wline.resize(2048);
    DWORD read = 0;
    BOOL ok = ReadConsoleW(hIn, &wline[0], (DWORD)wline.size() - 1, &read, NULL);

    SetConsoleMode(hIn, oldMode);

    if (!ok || read == 0) { printf("\n"); return ""; }

    wline.resize(read);
    while (!wline.empty() && (wline.back() == L'\r' || wline.back() == L'\n')) {
        wline.pop_back();
    }
    if (wline.empty()) { printf("\n"); return ""; }

    int len = WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), NULL, 0, NULL, NULL);
    std::string out((size_t)len, 0);
    WideCharToMultiByte(CP_UTF8, 0, wline.c_str(), (int)wline.size(), &out[0], len, NULL, NULL);
    return out;
}

int readInt(const char* prompt, int defaultVal) {
    printf("%s（回车 %d）：", prompt, defaultVal);
    fflush(stdout);
    std::string s = readLine();
    if (s.empty()) return defaultVal;
    try { return std::stoi(s); }
    catch (...) { return defaultVal; }
}

double readDouble(const char* prompt, double defaultVal) {
    printf("%s（回车 %.2f）：", prompt, defaultVal);
    fflush(stdout);
    std::string s = readLine();
    if (s.empty()) return defaultVal;
    try { return std::stod(s); }
    catch (...) { return defaultVal; }
}

std::string readNoteMode(const std::string& defaultValue) {
    printf("音符模式（once=点一下 / repeat=连续短点 / hold=长按持续）（回车 %s）：",
        defaultValue.c_str());
    fflush(stdout);
    std::string s = readLine();
    if (s == "once" || s == "repeat" || s == "hold") return s;
    return defaultValue;
}

// ============ 全局对象 ============
enum class State { MENU, WAIT_START, COUNTDOWN, PLAYING, PAUSED, QUIT };
Config      gcfg;
Player      g_player;
SongLibrary g_lib;
int         g_selected = -1;

// ============ 帮助 ============
void printHelp() {
    printf("\n================= 命令列表 =================\n");
    printf("  list                          列出所有曲目\n");
    printf("  <数字>                        选中并准备播放\n");
    printf("  add [路径]                    导入曲目\n");
    printf("  edit <数字>                   菜单式修改曲目字段\n");
    printf("  mode <数字> <once|repeat|hold> 快速改音符模式\n");
    printf("  mode <once|repeat|hold>       改当前选中曲目的模式\n");
    printf("  del <数字>                    删除曲目\n");
    printf("  showkeys                      显示键位映射与坐标\n");
    printf("  calib                         进入坐标校准模式（键盘演奏已不使用）\n");
    printf("  autocalib [l r t b]           切换自动比例模式\n");
    printf("  setkey <键> <x> <y>           手动设置某键坐标\n");
    printf("  offset <dx> <dy>              设置全局坐标偏移\n");
    printf("  savekeys                      保存键位到 keys.cfg\n");
    printf("  setcfg                        修改全局配置\n");
    printf("  emu probe                     探测模拟器窗口（确认类名）\n");
    printf("  emu <leidian|mumu|auto>       切换模拟器模式\n");
    printf("  emu status                    查看当前模拟器模式\n");
    printf("  help / ?                      显示本帮助\n");
    printf("  quit / exit                   退出\n");
    printf("============================================\n");
}

void showList() {
    printf("\n==================== 曲目列表 ====================\n");
    if (g_lib.size() == 0) {
        printf("  （songs/ 为空，用 add 导入曲目）\n");
    }
    else {
        for (size_t i = 0; i < g_lib.size(); i++) {
            const Song& s = g_lib.get(i);
            printf("  [%2zu] %-20s  events=%-4zu  BPM=%.0f  div=%d  hold=%d  mode=%-6s  jit=%s\n",
                i + 1, s.name.c_str(), s.events.size(),
                s.config.bpm, s.config.divisor, s.config.hold,
                s.config.note_mode.c_str(),
                s.config.jitter_enabled ? "on" : "off");
        }
    }
    printf("==================================================\n");
}

// ============ 导入 ============
void doImport(std::string srcPath) {
    if (srcPath.empty()) {
        printf("\n=== 导入曲目 ===\n");
        printf("请输入要导入的 .txt 文件完整路径：\n> ");
        fflush(stdout);
        srcPath = readLine();
    }
    while (!srcPath.empty() && (srcPath.front() == '"' || srcPath.front() == '\'')) srcPath.erase(0, 1);
    while (!srcPath.empty() && (srcPath.back() == '"' || srcPath.back() == '\'')) srcPath.pop_back();
    while (!srcPath.empty() && (srcPath.front() == ' ' || srcPath.front() == '\t')) srcPath.erase(0, 1);
    while (!srcPath.empty() && (srcPath.back() == ' ' || srcPath.back() == '\t')) srcPath.pop_back();

    if (srcPath.empty()) { printf("已取消\n"); return; }

    fs::path srcP = u8ToPath(srcPath);
    if (!fs::exists(srcP)) { printf("文件不存在：%s\n", srcPath.c_str()); return; }

    std::string stem = pathToU8(srcP.stem());
    printf("\n找到文件：%s\n", pathToU8(srcP.filename()).c_str());

    printf("请输入曲名（回车使用「%s」）：", stem.c_str());
    fflush(stdout);
    std::string name = readLine();
    if (name.empty()) name = stem;

    int bpm = readInt("请输入 BPM", 120);
    int divisor = readInt("请输入音符时值分母（16=十六分音符）", 16);
    int hold = readInt("请输入按键保持时长 ms", 30);
    std::string noteMode = readNoteMode("hold");

    printf("是否启用人类模拟抖动？（回车启用，输入 n 禁用）：");
    fflush(stdout);
    std::string jr = readLine();
    int jitterEnabled = (jr == "n" || jr == "N") ? 0 : 1;

    int    jt = 15, jh = 8, jc = 3;
    double jch = 0.3;
    if (jitterEnabled) {
        jt = readInt("  时间抖动幅度 ms", 15);
        jh = readInt("  保持抖动幅度 ms", 8);
        jc = readInt("  坐标抖动幅度 px（键盘模式下无效）", 3);
        jch = readDouble("  抖动触发概率 0~1", 0.3);
    }

    SongConfig sc;
    sc.name = name;
    sc.bpm = bpm;
    sc.divisor = divisor;
    sc.hold = hold;
    sc.note_mode = noteMode;
    sc.jitter_enabled = jitterEnabled;
    sc.jitter_time = jt;
    sc.jitter_hold = jh;
    sc.jitter_coord = jc;
    sc.jitter_chance = jch;

    if (!g_lib.add(srcPath, sc)) { printf("导入失败\n"); return; }

    printf("\n已复制到 %s\n", (g_songsDir + "\\" + name + ".txt").c_str());
    printf("已生成   %s\n", (g_songsDir + "\\" + name + ".cfg").c_str());
    printf("曲目 [%zu] %s  (BPM=%d, 时值=%d分, 保持=%dms, 模式=%s, 抖动=%s)\n",
        g_lib.size(), name.c_str(), bpm, divisor, hold, noteMode.c_str(),
        jitterEnabled ? "开" : "关");
}

// ============ 编辑：菜单式 ============
void doEdit(int n) {
    if (n < 1 || n >(int)g_lib.size()) { printf("无效编号\n"); return; }
    Song& s = g_lib.get(n - 1);

    while (true) {
        printf("\n===== 编辑 [%d] %s =====\n", n, s.name.c_str());
        printf("  [1] BPM            当前 %.0f\n", s.config.bpm);
        printf("  [2] divisor        当前 %d\n", s.config.divisor);
        printf("  [3] hold (ms)      当前 %d\n", s.config.hold);
        printf("  [4] note_mode      当前 %s\n", s.config.note_mode.c_str());
        printf("  [5] jitter 开关    当前 %s\n", s.config.jitter_enabled ? "on" : "off");
        printf("  [6] jitter 参数    time=%d  hold=%d  coord=%d(无效)  chance=%.2f\n",
            s.config.jitter_time, s.config.jitter_hold,
            s.config.jitter_coord, s.config.jitter_chance);
        printf("  [7] 全部重问\n");
        printf("  [S] 保存   [Q] 取消返回\n> ");
        fflush(stdout);
        flushInput();

        std::string sel = readLine();
        while (!sel.empty() && (sel.front() == ' ' || sel.front() == '\t')) sel.erase(0, 1);
        while (!sel.empty() && (sel.back() == ' ' || sel.back() == '\t')) sel.pop_back();
        if (sel.empty()) continue;

        char c = sel[0];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');

        if (c == 'Q') { printf("已取消\n"); return; }
        if (c == 'S') {
            if (s.config.save(s.cfgPath)) printf("已保存到 %s\n", s.cfgPath.c_str());
            else                          printf("保存失败\n");
            return;
        }

        if (c == '1') s.config.bpm = readDouble("新的 BPM", s.config.bpm);
        else if (c == '2') s.config.divisor = readInt("新的 divisor", s.config.divisor);
        else if (c == '3') s.config.hold = readInt("新的 hold ms", s.config.hold);
        else if (c == '4') s.config.note_mode = readNoteMode(s.config.note_mode);
        else if (c == '5') {
            printf("启用抖动？(1=开 / 0=关，回车 %d)：", s.config.jitter_enabled);
            fflush(stdout);
            std::string v = readLine();
            if (!v.empty()) {
                try { s.config.jitter_enabled = (std::stoi(v) != 0) ? 1 : 0; }
                catch (...) {}
            }
        }
        else if (c == '6') {
            s.config.jitter_time = readInt("  时间抖动幅度", s.config.jitter_time);
            s.config.jitter_hold = readInt("  保持抖动幅度", s.config.jitter_hold);
            s.config.jitter_coord = readInt("  坐标抖动幅度（键盘模式无效）", s.config.jitter_coord);
            s.config.jitter_chance = readDouble("  抖动触发概率", s.config.jitter_chance);
        }
        else if (c == '7') {
            printf("\n--- 全部重问（回车保留原值）---\n");
            s.config.bpm = readDouble("BPM", s.config.bpm);
            s.config.divisor = readInt("时值分母", s.config.divisor);
            s.config.hold = readInt("按键保持时长 ms", s.config.hold);
            s.config.note_mode = readNoteMode(s.config.note_mode);
            int jEn = readInt("是否启用抖动 (0/1)", s.config.jitter_enabled);
            s.config.jitter_enabled = jEn;
            if (jEn) {
                s.config.jitter_time = readInt("  时间抖动幅度", s.config.jitter_time);
                s.config.jitter_hold = readInt("  保持抖动幅度", s.config.jitter_hold);
                s.config.jitter_coord = readInt("  坐标抖动幅度（键盘模式无效）", s.config.jitter_coord);
                s.config.jitter_chance = readDouble("  抖动触发概率", s.config.jitter_chance);
            }
        }
    }
}

// ============ mode 快捷命令 ============
void doMode(const std::string& arg) {
    int idx = -1;
    std::string modeStr;
    int  n1 = 0;
    char m1[32] = { 0 };

    if (std::sscanf(arg.c_str(), "%d %31s", &n1, m1) == 2) {
        idx = n1 - 1;
        modeStr = m1;
    }
    else if (std::sscanf(arg.c_str(), "%31s", m1) == 1) {
        idx = g_selected;
        modeStr = m1;
    }

    if (idx < 0 || idx >= (int)g_lib.size()) {
        printf("用法：mode <数字> <once|repeat|hold>\n");
        printf("   或：mode <once|repeat|hold>  （需先选中曲目）\n");
        return;
    }
    if (modeStr != "once" && modeStr != "repeat" && modeStr != "hold") {
        printf("模式必须是 once / repeat / hold 之一\n");
        return;
    }
    Song& s = g_lib.get(idx);
    s.config.note_mode = modeStr;
    if (s.config.save(s.cfgPath))
        printf("[%d] %s 的 note_mode 已改为 %s\n", idx + 1, s.name.c_str(), modeStr.c_str());
    else
        printf("保存失败\n");
}

// ============ 删除 ============
void doDelete(int n) {
    if (n < 1 || n >(int)g_lib.size()) { printf("无效编号\n"); return; }
    const Song& s = g_lib.get(n - 1);
    printf("确认删除 [%d] %s ？输入 yes 确认：", n, s.name.c_str());
    fflush(stdout);
    std::string r = readLine();
    if (r != "yes") { printf("已取消\n"); return; }
    if (g_lib.remove(n - 1)) printf("已删除\n");
    else                     printf("删除失败\n");
}

// ============ 全局配置 ============
void doSetCfg() {
    printf("\n===== 全局配置 =====（回车保留原值）\n");
    gcfg.start_delay = readInt("开始延迟 ms", gcfg.start_delay);
    gcfg.resume_delay = readInt("继续延迟 ms", gcfg.resume_delay);
    gcfg.restart_delay = readInt("重弹延迟 ms", gcfg.restart_delay);

    printf("暂停/继续按键（当前 %s，回车保留）：", vkToName(gcfg.pause_key).c_str());
    fflush(stdout);
    std::string k1 = readLine();
    if (!k1.empty()) { int vk = nameToVk(k1); if (vk > 0) gcfg.pause_key = vk; else printf("  无效键名，保留原值\n"); }

    printf("重弹按键（当前 %s，回车保留）：", vkToName(gcfg.restart_key).c_str());
    fflush(stdout);
    std::string k2 = readLine();
    if (!k2.empty()) { int vk = nameToVk(k2); if (vk > 0) gcfg.restart_key = vk; else printf("  无效键名，保留原值\n"); }

    printf("返回按键（当前 %s，回车保留）：", vkToName(gcfg.quit_key).c_str());
    fflush(stdout);
    std::string k3 = readLine();
    if (!k3.empty()) { int vk = nameToVk(k3); if (vk > 0) gcfg.quit_key = vk; else printf("  无效键名，保留原值\n"); }

    if (gcfg.save(g_cfgPath)) printf("已保存到 %s\n", g_cfgPath.c_str());
    else                      printf("保存失败\n");
}

// ============ emu 命令 ============
void doEmu(const std::string& arg) {
    if (arg == "probe") {
        Player::probeWindows();
    }
    else if (arg == "leidian" || arg == "ld") {
        g_player.setEmuMode(EmuMode::LEIDIAN);
        printf("切换到 [雷电] 模式，重新查找窗口...\n");
        if (g_player.initWindow()) printf("  已连接：%p\n", g_player.hwnd());
        else                       printf("  未找到雷电 RenderWindow\n");
    }
    else if (arg == "mumu" || arg == "mm") {
        g_player.setEmuMode(EmuMode::MUMU);
        printf("切换到 [MuMu] 模式，重新查找窗口...\n");
        if (g_player.initWindow()) printf("  已连接：%p\n", g_player.hwnd());
        else                       printf("  未找到 MuMu 窗口\n");
    }
    else if (arg == "auto") {
        g_player.setEmuMode(EmuMode::AUTO);
        printf("切换到 [自动] 模式，重新查找窗口...\n");
        if (g_player.initWindow()) printf("  已连接：%p\n", g_player.hwnd());
        else                       printf("  未找到任何可用窗口\n");
    }
    else if (arg.empty() || arg == "status") {
        const char* m = (g_player.emuMode() == EmuMode::LEIDIAN) ? "leidian"
            : (g_player.emuMode() == EmuMode::MUMU) ? "mumu"
            : "auto";
        printf("当前模式：%s，窗口句柄：%p\n", m, g_player.hwnd());
    }
    else {
        printf("用法：\n");
        printf("  emu probe                探测当前所有模拟器窗口\n");
        printf("  emu leidian              切换到雷电模式\n");
        printf("  emu mumu                 切换到 MuMu 模式\n");
        printf("  emu auto                 切换到自动模式（先雷电后 MuMu）\n");
        printf("  emu                      显示当前模式\n");
    }
}

// ============ 播放 ============
State runPlaying() {
    while (true) {
        PlayResult r = g_player.step(gcfg);
        if (r == PlayResult::CONTINUE) continue;
        if (r == PlayResult::FINISHED) { printf("\n--- 曲终 ---\n"); return State::MENU; }
        if (r == PlayResult::PAUSED) { printf("\n[暂停]\n");       return State::PAUSED; }
        if (r == PlayResult::QUIT) { printf("\n[返回菜单]\n");   return State::MENU; }
    }
}

// ============ main ============
int main() {
    timeBeginPeriod(1);

    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);

    initPaths();
    printf("exe 目录：%s\n", g_exeDir.c_str());
    printf("曲目目录：%s\n", g_songsDir.c_str());
    printKeyMapping();

    if (!gcfg.load(g_cfgPath)) {
        gcfg.save(g_cfgPath);
        printf("已生成默认 config.cfg\n");
    }

    // 启动时默认 AUTO 模式：先试雷电，找不到再试 MuMu
    if (!g_player.initWindow()) {
        printf("\n[提示] 启动时未找到任何模拟器窗口。\n");
        printf("  - 请先打开雷电或 MuMu 并进入游戏\n");
        printf("  - 然后在菜单输入 emu probe 查看窗口情况\n");
        printf("  - 或输入 emu mumu / emu leidian 手动指定模式\n");
        printf("（不影响继续使用其它菜单命令）\n");
    }

    // keys.cfg 仅用于 calib/showkeys 调试，键盘演奏不使用坐标
    if (!g_player.loadKeys(g_keysPath)) {
        printf("[info] keys.cfg 不存在，用默认坐标（仅供 calib 调试，键盘演奏不受影响）\n");
        g_player.setKey('1', 167, 91); g_player.setKey('2', 241, 88);
        g_player.setKey('3', 319, 90); g_player.setKey('4', 394, 91);
        g_player.setKey('5', 469, 87);
        g_player.setKey('6', 168, 162); g_player.setKey('7', 245, 160);
        g_player.setKey('8', 319, 162); g_player.setKey('9', 396, 162);
        g_player.setKey('A', 466, 162);
        g_player.setKey('B', 170, 234); g_player.setKey('C', 245, 236);
        g_player.setKey('D', 318, 232); g_player.setKey('E', 396, 236);
        g_player.setKey('F', 470, 238);
        g_player.saveKeys(g_keysPath);
        printf("已生成默认 keys.cfg\n");
    }

    g_lib.scan(g_songsDir);
    printf("已扫描 %s：找到 %zu 首曲目\n", g_songsDir.c_str(), g_lib.size());
    printf("输入 help 查看命令\n");

    State state = State::MENU;

    while (state != State::QUIT) {
        switch (state) {

        case State::MENU: {
            showList();
            printf("> ");
            fflush(stdout);
            flushInput();

            std::string cmd = readLine();
            while (!cmd.empty() && (cmd.front() == ' ' || cmd.front() == '\t')) cmd.erase(0, 1);
            while (!cmd.empty() && (cmd.back() == ' ' || cmd.back() == '\t')) cmd.pop_back();
            if (cmd.empty()) break;

            std::string verb, arg;
            size_t sp = cmd.find(' ');
            if (sp == std::string::npos) verb = cmd;
            else { verb = cmd.substr(0, sp); arg = cmd.substr(sp + 1); }

            bool isNum = !cmd.empty();
            for (size_t i = 0; i < cmd.size(); i++) {
                char c = cmd[i];
                if (c < '0' || c > '9') { isNum = false; break; }
            }
            if (isNum) {
                int n = std::stoi(cmd);
                if (n >= 1 && n <= (int)g_lib.size()) {
                    g_selected = n - 1;
                    state = State::WAIT_START;
                }
                else {
                    printf("无效编号（1~%zu）\n", g_lib.size());
                }
                break;
            }

            if (verb == "list") { /* 已展示 */ }
            else if (verb == "help" || verb == "?") printHelp();
            else if (verb == "add")      doImport(arg);
            else if (verb == "edit") { try { doEdit(std::stoi(arg)); } catch (...) { printf("用法：edit <数字>\n"); } }
            else if (verb == "mode")     doMode(arg);
            else if (verb == "del") { try { doDelete(std::stoi(arg)); } catch (...) { printf("用法：del <数字>\n"); } }
            else if (verb == "showkeys") g_player.showKeys();
            else if (verb == "calib") { g_player.calibrate(); g_player.saveKeys(g_keysPath); printf("已保存 keys.cfg\n"); }
            else if (verb == "savekeys") { if (g_player.saveKeys(g_keysPath)) printf("已保存\n"); else printf("失败\n"); }
            else if (verb == "setcfg")   doSetCfg();
            else if (verb == "emu")      doEmu(arg);
            else if (verb == "autocalib") {
                std::string line = arg;
                if (line.empty()) {
                    printf("输入 4 个比例（左 右 上 下，用空格分隔）：");
                    fflush(stdout);
                    line = readLine();
                }
                double l = 0, r = 0, t = 0, b = 0;
                if (std::sscanf(line.c_str(), "%lf %lf %lf %lf", &l, &r, &t, &b) == 4) {
                    g_player.setAutoRatio(l, r, t, b);
                    g_player.saveKeys(g_keysPath);
                    printf("已切换到自动比例模式并保存 keys.cfg\n");
                    g_player.showKeys();
                }
                else {
                    printf("格式错误，示例：autocalib 0.15 0.75 0.85 0.92\n");
                }
            }
            else if (verb == "setkey") {
                char k = 0;
                long x = 0, y = 0;
                if (std::sscanf(arg.c_str(), "%c %ld %ld", &k, &x, &y) == 3) {
                    g_player.setKey(k, x, y);
                    g_player.saveKeys(g_keysPath);
                    printf("已设置 %c = (%ld, %ld) 并保存\n", k, x, y);
                }
                else {
                    printf("用法：setkey <键> <x> <y>  例如：setkey 1 182 99\n");
                }
            }
            else if (verb == "offset") {
                long dx = 0, dy = 0;
                if (std::sscanf(arg.c_str(), "%ld %ld", &dx, &dy) == 2) {
                    g_player.setOffset(dx, dy);
                    g_player.saveKeys(g_keysPath);
                    printf("已设置 offset=(%ld, %ld) 并保存\n", dx, dy);
                }
                else {
                    printf("用法：offset <dx> <dy>  例如：offset 0 0\n");
                }
            }
            else if (verb == "quit" || verb == "exit") state = State::QUIT;
            else printf("未知命令：%s（输入 help 查看）\n", verb.c_str());
            break;
        }

        case State::WAIT_START: {
            const Song& s = g_lib.get(g_selected);
            printf("\n选中：[%d] %s\n", g_selected + 1, s.name.c_str());
            printf("  events=%zu  BPM=%.0f  divisor=%d  hold=%d  mode=%s  jitter=%s\n",
                s.events.size(), s.config.bpm, s.config.divisor, s.config.hold,
                s.config.note_mode.c_str(),
                s.config.jitter_enabled ? "on" : "off");
            printf("按 Enter 开始（Q 返回）...\n");
            fflush(stdout);
            flushInput();

            bool done = false;
            while (!done) {
                int c = pollKey();
                if (c == -1) { Sleep(20); continue; }
                if (c == 'Q') { state = State::MENU; done = true; }
                else { state = State::COUNTDOWN; done = true; }
            }
            break;
        }

        case State::COUNTDOWN: {
            const Song& s = g_lib.get(g_selected);
            g_player.prepare(s.events, s.config);

            printf("Starting in %.1f s...\n", gcfg.start_delay / 1000.0);
            fflush(stdout);
            flushInput();

            LARGE_INTEGER freq, t0, now;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&t0);
            bool cancel = false;
            double lastShown = -1;
            while (true) {
                QueryPerformanceCounter(&now);
                double el = (now.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
                double remain = (gcfg.start_delay - el) / 1000.0;
                if (remain <= 0) break;
                if (remain - lastShown >= 1.0) {
                    printf("  %.0f...\n", remain); fflush(stdout);
                    lastShown = remain;
                }
                int c = pollKey();
                if (c == 'Q') { cancel = true; break; }
                Sleep(20);
            }
            if (cancel) { state = State::MENU; break; }

            g_player.reset();
            state = State::PLAYING;
            break;
        }

        case State::PLAYING: {
            state = runPlaying();
            break;
        }

        case State::PAUSED: {
            printf("  [%s] 继续   [%s] 重弹   [%s] 返回\n",
                vkToName(gcfg.pause_key).c_str(),
                vkToName(gcfg.restart_key).c_str(),
                vkToName(gcfg.quit_key).c_str());
            printf("> ");
            fflush(stdout);
            flushInput();
            bool done = false;
            while (!done) {
                int c = pollKey();
                if (c == -1) { Sleep(20); continue; }
                if (c == gcfg.pause_key) {
                    g_player.resume(gcfg);
                    state = State::PLAYING; done = true;
                }
                else if (c == gcfg.restart_key) {
                    printf("Restarting in %d ms...\n", gcfg.restart_delay);
                    fflush(stdout);
                    Sleep(gcfg.restart_delay);
                    g_player.reset();
                    state = State::PLAYING; done = true;
                }
                else if (c == gcfg.quit_key) {
                    printf("Back to menu.\n");
                    state = State::MENU; done = true;
                }
            }
            break;
        }

        } // switch
    }

    printf("\nBye.\n");
    timeEndPeriod(1);
    return 0;
}