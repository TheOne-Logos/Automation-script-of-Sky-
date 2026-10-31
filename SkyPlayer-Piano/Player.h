#pragma once
#include <windows.h>
#include <map>
#include <vector>
#include <string>
#include "ScoreParser.h"
#include "SongConfig.h"
#include "Config.h"

enum class PlayResult { CONTINUE, FINISHED, PAUSED, QUIT };
enum class EmuMode { AUTO, LEIDIAN, MUMU };

class Player {
public:
    Player();

    // ---- 窗口 / 模拟器模式 ----
    bool initWindow();
    HWND hwnd() const { return hwnd_; }
    void setEmuMode(EmuMode m) { emuMode_ = m; }
    EmuMode emuMode() const { return emuMode_; }
    static void probeWindows();

    // ---- 坐标系统（键盘模式下不参与演奏，仅供 calib 调试） ----
    bool loadKeys(const std::string& path);
    bool saveKeys(const std::string& path) const;
    void showKeys() const;
    void setKey(char k, LONG x, LONG y);
    void setAutoRatio(double l, double r, double t, double b);
    void applyAutoCoords();
    void calibrate();

    void setOffset(LONG dx, LONG dy) { offsetX_ = dx; offsetY_ = dy; }
    LONG getOffsetX() const { return offsetX_; }
    LONG getOffsetY() const { return offsetY_; }
    bool isAutoMode() const { return autoMode_; }

    // ---- 演奏流程 ----
    void prepare(const std::vector<ChordEvent>& events, const SongConfig& scfg);
    void reset();
    void resume(const Config& gcfg);
    PlayResult step(const Config& gcfg);

private:
    HWND hwnd_;
    EmuMode emuMode_ = EmuMode::AUTO;
    std::map<char, POINT> coords_;

    bool   autoMode_;
    double ratioL_, ratioR_, ratioT_, ratioB_;
    LONG   offsetX_, offsetY_;

    std::vector<ChordEvent> events_;
    SongConfig scfg_;
    size_t index_;
    LARGE_INTEGER freq_, start_;
    double savedElapsedMs_;

    struct PendingAction {
        enum Kind { DOWN, UP };
        Kind              kind;
        double            atMs;
        std::vector<char> notes;
    };
    std::vector<PendingAction> pendingActions_;
    std::vector<char> pressedNotes_;

    bool   tryFindLeidian();
    bool   tryFindMumu();

    double nowMs() const;
    void   triggerChord(const ChordEvent& ev);
    void   flushPendingActions(double now);
    void   releaseAll();

    HWND   targetWindow() const;
    static int noteToVk(char c);
    void   sendKeyDown(char c);
    void   sendKeyUp(char c);
};