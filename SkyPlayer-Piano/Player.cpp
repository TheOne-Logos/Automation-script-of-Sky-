#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable: 6031)
#pragma warning(disable: 4996)

#include "Player.h"
#include <cstdio>
#include <cstdlib>
#include <conio.h>
#include <random>
#include <fstream>
#include <algorithm>

// ============ 音符顺序 ============
static const char* KEY_ORDER[15] = {
    "1","2","3","4","5","6","7","8","9","A","B","C","D","E","F"
};

// ============ 音符 → 物理键映射 ============
// 1 2 3 4 5 6 7 8 9 A B C D E F
// R T Y U I F G H J K V B N M ,
static const int NOTE_VK_MAP[15] = {
    'R', 'T', 'Y', 'U', 'I',
    'F', 'G', 'H', 'J', 'K',
    'V', 'B', 'N', 'M', VK_OEM_COMMA
};

static const bool DEBUG_ACTIONS = false;

static void trim(std::string& s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\r' || s.back() == '\n')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) i++;
    s = s.substr(i);
}

static int jitterValue(int base, int range, double chance) {
    if (range <= 0) return base;
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<double> p(0.0, 1.0);
    if (p(rng) > chance) return base;
    std::uniform_int_distribution<int> d(-range, range);
    return base + d(rng);
}

static LPARAM makeKeyLParam(UINT vk, bool down) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
    LPARAM lp = 1;
    lp |= ((LPARAM)sc << 16);
    if (!down) {
        lp |= (1LL << 30);
        lp |= (1LL << 31);
    }
    if (vk >= VK_PRIOR && vk <= VK_DELETE)        lp |= (1LL << 24);
    else if (vk == VK_LWIN || vk == VK_RWIN)      lp |= (1LL << 24);
    else if (vk == VK_APPS || vk == VK_DIVIDE)    lp |= (1LL << 24);
    else if (vk == VK_NUMLOCK)                    lp |= (1LL << 24);
    return lp;
}

// ============ 构造 ============

Player::Player()
    : hwnd_(NULL), emuMode_(EmuMode::AUTO), autoMode_(true),
    ratioL_(0.15), ratioR_(0.75), ratioT_(0.85), ratioB_(0.92),
    offsetX_(0), offsetY_(0),
    index_(0), savedElapsedMs_(0) {
    freq_.QuadPart = 0;
    start_.QuadPart = 0;
}

// ============ 窗口查找（硬编码） ============

// 雷电：顶层窗口类名固定 "RenderWindow"，FindWindow 一行搞定
bool Player::tryFindLeidian() {
    HWND h = FindWindowW(L"RenderWindow", NULL);
    if (!h) return false;
    hwnd_ = h;
    printf("[window] 雷电 RenderWindow = %p\n", hwnd_);
    return true;
}

// MuMu：渲染子窗口类名固定 "MMUIRenderSubWindowHW"
// 它是子窗口，没有 FindWindow 级别的全局 API，只能枚举一次所有子窗口
// 匹配到立即返回，不做任何"猜测式"搜索
struct MuMuCtx { HWND found; };
static BOOL CALLBACK MuMuChildProc(HWND h, LPARAM lp) {
    MuMuCtx* ctx = (MuMuCtx*)lp;
    wchar_t cls[128] = { 0 };
    GetClassNameW(h, cls, 127);
    if (wcscmp(cls, L"MMUIRenderSubWindowHW") == 0) {
        ctx->found = h;
        return FALSE;   // 找到即停止
    }
    return TRUE;
}

// MuMu 的渲染窗口类名固定是 nemuwin，title = nemudisplay
// 它是深层子窗口，从桌面根节点递归找一次即可
static BOOL CALLBACK FindNemuwinProc(HWND h, LPARAM lp) {
    wchar_t cls[256] = { 0 };
    GetClassNameW(h, cls, 255);
    if (wcscmp(cls, L"nemuwin") == 0) {
        *(HWND*)lp = h;
        return FALSE;   // 找到立即停止
    }
    return TRUE;
}

bool Player::tryFindMumu() {
    HWND found = NULL;
    EnumChildWindows(GetDesktopWindow(), FindNemuwinProc, (LPARAM)&found);
    if (!found) {
        printf("[window] 未找到 nemuwin 窗口（MuMu 未运行或未进游戏？）\n");
        return false;
    }
    hwnd_ = found;
    printf("[window] MuMu nemuwin = %p（渲染窗口）\n", hwnd_);
    return true;
}

bool Player::initWindow() {
    hwnd_ = NULL;

    if (emuMode_ == EmuMode::AUTO || emuMode_ == EmuMode::LEIDIAN) {
        if (tryFindLeidian()) return true;
        if (emuMode_ == EmuMode::LEIDIAN) {
            printf("[window] 未找到雷电 RenderWindow\n");
            return false;
        }
    }
    if (emuMode_ == EmuMode::AUTO || emuMode_ == EmuMode::MUMU) {
        if (tryFindMumu()) return true;
        if (emuMode_ == EmuMode::MUMU) {
            printf("[window] 未找到 MuMu 窗口\n");
            return false;
        }
    }
    printf("[window] 未找到任何可用模拟器窗口\n");
    return false;
}

// 递归打印子窗口树（UTF-8，避免中文乱码）
static void dumpChildTreeU8(HWND parent, int depth, int maxDepth) {
    if (depth > maxDepth) return;
    for (HWND ch = GetWindow(parent, GW_CHILD); ch; ch = GetWindow(ch, GW_HWNDNEXT)) {
        wchar_t cls[256] = { 0 }, title[256] = { 0 };
        GetClassNameW(ch, cls, 255);
        GetWindowTextW(ch, title, 255);
        char clsU8[512] = { 0 }, titleU8[1024] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsU8, 511, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, title, -1, titleU8, 1023, NULL, NULL);
        printf("%*s[d%d] %p  class=[%s]  title=[%s]\n",
            depth * 2, "", depth, ch, clsU8, titleU8);
        dumpChildTreeU8(ch, depth + 1, maxDepth);
    }
}

void Player::probeWindows() {
    printf("\n========== 模拟器窗口探测（递归深度 6） ==========\n");
    EnumWindows([](HWND top, LPARAM) -> BOOL {
        if (!IsWindowVisible(top)) return TRUE;
        wchar_t cls[256] = { 0 }, title[256] = { 0 };
        GetClassNameW(top, cls, 255);
        GetWindowTextW(top, title, 255);
        bool suspect =
            wcsstr(cls, L"Qt") || wcsstr(cls, L"Render") || wcsstr(cls, L"MuMu") ||
            wcsstr(cls, L"nemu") || wcsstr(cls, L"Ld") ||
            wcsstr(title, L"MuMu") || wcsstr(title, L"雷电") ||
            wcsstr(title, L"夜神");
        if (!suspect) return TRUE;

        char clsU8[512] = { 0 }, titleU8[1024] = { 0 };
        WideCharToMultiByte(CP_UTF8, 0, cls, -1, clsU8, 511, NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, title, -1, titleU8, 1023, NULL, NULL);
        printf("[TOP] %p  class=[%s]  title=[%s]\n", top, clsU8, titleU8);
        dumpChildTreeU8(top, 1, 6);
        printf("\n");
        return TRUE;
        }, 0);
    printf("===================================================\n");
}
HWND Player::targetWindow() const { return hwnd_; }

// ============ 键盘消息发送 ============

int Player::noteToVk(char c) {
    if (c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
    int idx = -1;
    if (c >= '1' && c <= '9') idx = c - '1';
    else if (c >= 'A' && c <= 'F') idx = 9 + (c - 'A');
    if (idx < 0 || idx >= 15) return -1;
    return NOTE_VK_MAP[idx];
}

// 判断当前窗口是不是 MuMu 的 nemuwin
static bool isMuMuWindow(HWND h) {
    if (!h) return false;
    wchar_t cls[256] = { 0 };
    GetClassNameW(h, cls, 255);
    return wcscmp(cls, L"nemuwin") == 0;
}

void Player::sendKeyDown(char c) {
    int vk = noteToVk(c);
    if (vk < 0) return;
    HWND tgt = targetWindow();
    if (!tgt) return;

    LPARAM lp = makeKeyLParam((UINT)vk, true);

    // 先发到 targetWindow 本身（雷电一直走这条路，和以前完全一样）
    PostMessageW(tgt, WM_KEYDOWN, (WPARAM)vk, lp);

    // 如果是 MuMu 的 nemuwin，再额外发到它的父窗口和顶层窗口
    if (isMuMuWindow(tgt)) {
        HWND parent = GetParent(tgt);
        if (parent) PostMessageW(parent, WM_KEYDOWN, (WPARAM)vk, lp);
        HWND root = GetAncestor(tgt, GA_ROOT);
        if (root && root != parent) PostMessageW(root, WM_KEYDOWN, (WPARAM)vk, lp);
    }
}

void Player::sendKeyUp(char c) {
    int vk = noteToVk(c);
    if (vk < 0) return;
    HWND tgt = targetWindow();
    if (!tgt) return;

    LPARAM lp = makeKeyLParam((UINT)vk, false);

    PostMessageW(tgt, WM_KEYUP, (WPARAM)vk, lp);

    if (isMuMuWindow(tgt)) {
        HWND parent = GetParent(tgt);
        if (parent) PostMessageW(parent, WM_KEYUP, (WPARAM)vk, lp);
        HWND root = GetAncestor(tgt, GA_ROOT);
        if (root && root != parent) PostMessageW(root, WM_KEYUP, (WPARAM)vk, lp);
    }
}
// ============ 坐标系统（键盘演奏不使用，仅供 calib 调试） ============

bool Player::loadKeys(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq);
        std::string v = line.substr(eq + 1);
        trim(k); trim(v);
        if (k == "mode") { autoMode_ = (v == "auto"); }
        else if (k == "ratio") { std::sscanf(v.c_str(), "%lf,%lf,%lf,%lf", &ratioL_, &ratioR_, &ratioT_, &ratioB_); }
        else if (k == "offset_x") { offsetX_ = std::stol(v); }
        else if (k == "offset_y") { offsetY_ = std::stol(v); }
        else if (k.size() == 1) {
            char ch = k[0];
            if (ch >= 'a' && ch <= 'f') ch = (char)(ch - 'a' + 'A');
            LONG x = 0, y = 0;
            if (std::sscanf(v.c_str(), "%ld,%ld", &x, &y) == 2) {
                POINT p; p.x = x; p.y = y;
                coords_[ch] = p;
            }
        }
    }
    if (autoMode_) applyAutoCoords();
    return true;
}

bool Player::saveKeys(const std::string& path) const {
    std::ofstream f(path);
    if (!f.is_open()) return false;
    f << "mode=" << (autoMode_ ? "auto" : "manual") << "\n";
    f << "ratio=" << ratioL_ << "," << ratioR_ << "," << ratioT_ << "," << ratioB_ << "\n";
    f << "offset_x=" << offsetX_ << "\n";
    f << "offset_y=" << offsetY_ << "\n";
    for (int i = 0; i < 15; i++) {
        char c = KEY_ORDER[i][0];
        std::map<char, POINT>::const_iterator it = coords_.find(c);
        if (it != coords_.end()) f << c << "=" << it->second.x << "," << it->second.y << "\n";
    }
    return true;
}

void Player::showKeys() const {
    static const char* VK_NAMES[15] = {
        "R","T","Y","U","I","F","G","H","J","K","V","B","N","M",","
    };
    printf("=== 键盘映射 ===\n");
    for (int i = 0; i < 15; i++) printf("  音符 %s  ->  物理键 %s\n", KEY_ORDER[i], VK_NAMES[i]);
    printf("\n=== 坐标（键盘模式下不参与演奏） ===\n");
    printf("mode=%s  ratio=%.2f,%.2f,%.2f,%.2f  offset=(%ld,%ld)\n",
        autoMode_ ? "auto" : "manual",
        ratioL_, ratioR_, ratioT_, ratioB_, offsetX_, offsetY_);
    for (int i = 0; i < 15; i++) {
        char c = KEY_ORDER[i][0];
        std::map<char, POINT>::const_iterator it = coords_.find(c);
        if (it != coords_.end()) printf("  %c = (%ld, %ld)\n", c, it->second.x, it->second.y);
        else                     printf("  %c = <unset>\n", c);
    }
}

void Player::setKey(char k, LONG x, LONG y) {
    if (k >= 'a' && k <= 'f') k = (char)(k - 'a' + 'A');
    POINT p; p.x = x; p.y = y;
    coords_[k] = p;
}

void Player::setAutoRatio(double l, double r, double t, double b) {
    ratioL_ = l; ratioR_ = r; ratioT_ = t; ratioB_ = b;
    autoMode_ = true;
    applyAutoCoords();
}

void Player::applyAutoCoords() {
    if (!hwnd_) return;
    RECT rc; GetClientRect(hwnd_, &rc);
    int W = rc.right - rc.left, H = rc.bottom - rc.top;
    double x0 = W * ratioL_, x1 = W * ratioR_;
    double y0 = H * ratioT_, y1 = H * ratioB_;
    double dx = (x1 - x0) / 4.0, dy = (y1 - y0) / 2.0;
    const char grid[3][5] = {
        {'1','2','3','4','5'},
        {'6','7','8','9','A'},
        {'B','C','D','E','F'}
    };
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 5; c++) {
            POINT p;
            p.x = (LONG)(x0 + c * dx);
            p.y = (LONG)(y0 + r * dy);
            coords_[grid[r][c]] = p;
        }
}

void Player::calibrate() {
    if (!hwnd_) { printf("窗口未连接\n"); return; }
    printf("\n=== 校准模式（仅供坐标记录，键盘演奏不使用） ===\n");
    while (_kbhit()) (void)_getch();
    for (int i = 0; i < 15; i++) {
        char c = KEY_ORDER[i][0];
        printf("请把鼠标移到键 [%c] 上，按空格...\n", c);
        fflush(stdout);
        while (!_kbhit()) Sleep(20);
        (void)_getch();
        Sleep(200);
        POINT p; GetCursorPos(&p);
        ScreenToClient(hwnd_, &p);
        setKey(c, p.x, p.y);
        printf("  记录 %c = (%ld, %ld)\n", c, p.x, p.y);
    }
    autoMode_ = false;
    printf("校准完成\n");
}

// ============ 时间基准 ============

double Player::nowMs() const {
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (now.QuadPart - start_.QuadPart) * 1000.0 / freq_.QuadPart;
}

void Player::releaseAll() {
    pendingActions_.clear();
    for (size_t i = 0; i < pressedNotes_.size(); i++) sendKeyUp(pressedNotes_[i]);
    pressedNotes_.clear();
}

// ============ 触发和弦：只生成 PendingAction ============

void Player::triggerChord(const ChordEvent& ev) {
    if (ev.keys.empty()) return;
    double t0 = nowMs();
    double stepMs = 240000.0 / (scfg_.bpm * scfg_.divisor);
    int holdBase = scfg_.hold;
    if (holdBase < 1) holdBase = 1;

    auto jitteredHold = [&]() -> int {
        int h = holdBase;
        if (scfg_.jitter_enabled)
            h = jitterValue(holdBase, scfg_.jitter_hold, scfg_.jitter_chance);
        return (h < 1) ? 1 : h;
        };

    std::vector<char> notes = ev.keys;

    if (scfg_.note_mode == "hold" && ev.duration > 1) {
        double totalMs = ev.duration * stepMs;
        PendingAction down; down.kind = PendingAction::DOWN; down.atMs = t0; down.notes = notes;
        PendingAction up;   up.kind = PendingAction::UP;   up.atMs = t0 + totalMs; up.notes = notes;
        pendingActions_.push_back(down);
        pendingActions_.push_back(up);
        return;
    }

    if (scfg_.note_mode == "repeat" && ev.duration > 1) {
        double totalMs = ev.duration * stepMs;
        int h1 = jitteredHold();
        PendingAction d1; d1.kind = PendingAction::DOWN; d1.atMs = t0; d1.notes = notes;
        PendingAction u1; u1.kind = PendingAction::UP;   u1.atMs = t0 + h1; u1.notes = notes;
        pendingActions_.push_back(d1);
        pendingActions_.push_back(u1);
        double elapsed = h1;
        while (elapsed + holdBase < totalMs) {
            elapsed += holdBase;
            int h2 = jitteredHold();
            PendingAction d; d.kind = PendingAction::DOWN; d.atMs = t0 + elapsed; d.notes = notes;
            PendingAction u; u.kind = PendingAction::UP;   u.atMs = t0 + elapsed + h2; u.notes = notes;
            pendingActions_.push_back(d);
            pendingActions_.push_back(u);
            elapsed += h2;
        }
        return;
    }

    int h = jitteredHold();
    PendingAction down; down.kind = PendingAction::DOWN; down.atMs = t0; down.notes = notes;
    PendingAction up;   up.kind = PendingAction::UP;   up.atMs = t0 + h; up.notes = notes;
    pendingActions_.push_back(down);
    pendingActions_.push_back(up);
}

void Player::flushPendingActions(double now) {
    bool any = true;
    while (any) {
        any = false;
        size_t bestIdx = (size_t)-1;
        double bestAt = 0;
        for (size_t i = 0; i < pendingActions_.size(); i++) {
            if (pendingActions_[i].atMs <= now) {
                if (bestIdx == (size_t)-1 || pendingActions_[i].atMs < bestAt ||
                    (pendingActions_[i].atMs == bestAt &&
                        pendingActions_[i].kind == PendingAction::DOWN &&
                        pendingActions_[bestIdx].kind == PendingAction::UP)) {
                    bestIdx = i;
                    bestAt = pendingActions_[i].atMs;
                }
            }
        }
        if (bestIdx == (size_t)-1) break;
        any = true;
        PendingAction act = pendingActions_[bestIdx];
        pendingActions_.erase(pendingActions_.begin() + bestIdx);

        if (act.kind == PendingAction::DOWN) {
            for (size_t j = 0; j < act.notes.size(); j++) {
                char c = act.notes[j];
                bool already = false;
                for (size_t k = 0; k < pressedNotes_.size(); k++)
                    if (pressedNotes_[k] == c) { already = true; break; }
                if (!already) { sendKeyDown(c); pressedNotes_.push_back(c); }
            }
            if (DEBUG_ACTIONS) {
                printf("  [DOWN %.1fms] ", act.atMs);
                for (char c : act.notes) printf("%c", c);
                printf("\n");
            }
        }
        else {
            for (size_t j = 0; j < act.notes.size(); j++) {
                char c = act.notes[j];
                sendKeyUp(c);
                std::vector<char>::iterator it = std::find(pressedNotes_.begin(), pressedNotes_.end(), c);
                if (it != pressedNotes_.end()) pressedNotes_.erase(it);
            }
            if (DEBUG_ACTIONS) {
                printf("  [UP   %.1fms] ", act.atMs);
                for (char c : act.notes) printf("%c", c);
                printf("\n");
            }
        }
    }
}

// ============ 演奏流程 ============

void Player::prepare(const std::vector<ChordEvent>& events, const SongConfig& scfg) {
    events_ = events;
    scfg_ = scfg;
    pendingActions_.clear();
    pressedNotes_.clear();
}

void Player::reset() {
    releaseAll();
    index_ = 0;
    savedElapsedMs_ = 0;
    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&start_);
}

void Player::resume(const Config& gcfg) {
    printf("Resuming in %d ms...\n", gcfg.resume_delay);
    fflush(stdout);
    Sleep(gcfg.resume_delay);
    LARGE_INTEGER now;
    QueryPerformanceFrequency(&freq_);
    QueryPerformanceCounter(&now);
    start_.QuadPart = now.QuadPart - (LONGLONG)(savedElapsedMs_ * freq_.QuadPart / 1000.0);
}

PlayResult Player::step(const Config& gcfg) {
    double elapsed = nowMs();
    flushPendingActions(elapsed);

    if (_kbhit()) {
        int c = _getch();
        if (c == 0 || c == 224) { (void)_getch(); }
        else {
            if (c >= 'a' && c <= 'z') c -= 32;
            if (c == ' ') c = VK_SPACE;
            else if (c == '\r' || c == '\n') c = VK_RETURN;
            if (c == gcfg.pause_key) { savedElapsedMs_ = nowMs(); releaseAll(); return PlayResult::PAUSED; }
            if (c == gcfg.quit_key) { releaseAll(); return PlayResult::QUIT; }
        }
    }

    if (index_ >= events_.size()) {
        if (!pendingActions_.empty() || !pressedNotes_.empty()) { Sleep(1); return PlayResult::CONTINUE; }
        return PlayResult::FINISHED;
    }

    const ChordEvent& ev = events_[index_];
    double stepMs = 240000.0 / (scfg_.bpm * scfg_.divisor);
    double target = ev.timeStep * stepMs;
    if (scfg_.jitter_enabled) target += jitterValue(0, scfg_.jitter_time, scfg_.jitter_chance);

    double t = nowMs();
    if (t >= target) {
        triggerChord(ev);
        index_++;
        flushPendingActions(nowMs());
        return PlayResult::CONTINUE;
    }
    Sleep(1);
    return PlayResult::CONTINUE;
}