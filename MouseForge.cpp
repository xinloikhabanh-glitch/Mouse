#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX  // tránh macro min/max của windows.h phá std::min/std::max
#endif

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <thread>
#include <atomic>
#include <mutex>
#include <sstream>
#include <fstream>
#include <chrono>
#include <algorithm>
#include <filesystem>
#include <cwctype>
#include <cmath>
#include <deque>
#include <cstdlib>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "uxtheme.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif

namespace fs = std::filesystem;

// ===================== IDs =====================
enum {
    IDC_TAB = 1000,
    IDC_LOG,
    IDC_STATUS,

    IDC_CFG_COMBO, IDC_CFG_RESCAN, IDC_CFG_EDITKEY, IDC_CFG_BACKUP, IDC_CFG_RESTORE, IDC_CFG_LIST,
    IDC_ADB_COMBO, IDC_ADB_SERIAL_LBL, IDC_ADB_SERIAL, IDC_ADB_CONNECT, IDC_ADB_INFOLIST, IDC_ADB_KEYLIST,
    IDC_OPT_ANIM, IDC_OPT_HWUI, IDC_OPT_AWAKE, IDC_OPT_FINISH, IDC_OPT_GOV, IDC_OPT_SCHED, IDC_OPT_SCAN, IDC_OPT_LIST,
    IDC_OPT_HAPTIC, IDC_OPT_TIMEOUT, IDC_OPT_ROTATION,
    IDC_ROOT_CHECK, IDC_ROOT_ON, IDC_ROOT_OFF, IDC_ROOT_VERIFY, IDC_ROOT_LIST,
    IDC_INFO_LOAD, IDC_INFO_PRIORITY, IDC_INFO_CLEARLOG, IDC_INFO_TEXT,

    IDC_PRESET_COMBO, IDC_PRESET_NAME, IDC_PRESET_SAVE, IDC_PRESET_LOAD, IDC_PRESET_DELETE, IDC_PRESET_REFRESH, IDC_PRESET_LIST,

    IDC_MOUSE_SPEED, IDC_MOUSE_PRECISION, IDC_MOUSE_DBLCLICK, IDC_MOUSE_WHEEL, IDC_MOUSE_TRAILS,
    IDC_MOUSE_APPLY, IDC_MOUSE_RESET, IDC_MOUSE_REFRESH,

    IDC_ENG_ENABLE, IDC_ENG_EMA, IDC_ENG_VELWIN, IDC_ENG_MICROTH, IDC_ENG_ACCEL,
    IDC_ENG_OFFSET, IDC_ENG_CAP, IDC_ENG_REFDPI, IDC_ENG_CURDPI, IDC_ENG_RESET,

    IDC_THEME_TOGGLE,
    IDC_QS_TOGGLE, IDC_QS_P1, IDC_QS_P2, IDC_QS_P3, IDC_QS_P4, IDC_QS_P5,
    IDC_TEST_RESET, IDC_TEST_WATCHDOG, IDC_TEST_CLICKPAD,
    IDC_GUIDE_TEXT,

    IDC_DLG_KEY_LBL, IDC_DLG_KEY, IDC_DLG_VAL_LBL, IDC_DLG_VAL, IDC_DLG_OK, IDC_DLG_CANCEL,
};

// Nút tab tự vẽ: ID liên tục từ IDC_TABBTN_BASE (không trùng enum ở trên)
static const int IDC_TABBTN_BASE = 2000;

// Layout: chiều cao vùng header tiêu đề ở trên cùng, và vị trí Y của tab control bên dưới header.
static const int kHeaderTop = 6;
static const int kHeaderH = 52;
static const int kTabTop = kHeaderTop + kHeaderH + 4;      // y của thanh tab tự vẽ
static const int kTabBarH = 30;
static const int kContentTop = kTabTop + kTabBarH + 16;    // y bắt đầu nội dung mỗi tab
static const int kStatusH = 24;                            // dải trạng thái dưới cùng (tự vẽ)

// ===================== Mouse Engine (EMA smoothing + custom acceleration curve) =====================
// Hoạt động thuần ở tầng raw input <-> OS cursor, giống RawAccel: không đọc bộ nhớ/pixel của bất kỳ
// tiến trình nào, không biết "target" ở đâu. Chỉ biến đổi delta chuột thô trước khi OS di chuyển con trỏ.
// Dấu hiệu nhận biết sự kiện do CHÍNH engine bơm vào (SendInput) để không xử lý lại -> chống vòng lặp tự khuếch đại.
static const ULONG_PTR kInjectTag = 0x4D465233; // "MFR3"
static const UINT WM_APP_TRIP = WM_APP + 1;      // engine tự ngắt (bộ bảo vệ)
static const UINT_PTR kTimerFlush = 2;           // xả nốt phần dư của bộ làm mượt khi ngừng di chuột
static const UINT_PTR kTimerStats = 3;           // cập nhật số liệu tab Test
static HWND g_engineNotifyWnd = nullptr;

struct MouseEngineState {
    std::atomic<bool> enabled{false};
    bool rawRegistered = false;
    bool watchdogOn = true;
    bool tripped = false;

    // Tham số (đơn vị thật; lưu double, UI dùng trackbar int đã nhân hệ số)
    double emaFastMs   = 2.0;   // 0.0 - 10.0 ms
    int    velWindow    = 4;     // 1 - 10 mẫu
    double microThresh  = 2.0;   // 0.0 - 10.0 counts/sự kiện
    double accel        = 0.04;  // 0.00 - 0.50
    double accelOffset  = 5.0;   // 0.0 - 20.0 counts/ms
    double accelCap     = 1.40;  // 1.00 - 3.00
    double refDpi       = 800.0;
    double curDpi       = 800.0;

    // Trạng thái nội bộ của bộ lọc
    double pendX = 0, pendY = 0;     // phần chuyển động chưa "xả" của bộ làm mượt (bảo toàn tổng quãng đường)
    double carryX = 0, carryY = 0;   // phần dư thập phân khi làm tròn ra số nguyên
    double lastMs = -1.0;
    struct Sample { double dist; double dt; };
    std::deque<Sample> hist;

    // Bộ bảo vệ chống chuột mất kiểm soát
    double wdStart = -1.0, wdIn = 0, wdOut = 0, wdRaw = 0;

    // Số liệu cho tab Test
    double stIn = 0, stOut = 0, stMult = 1.0, stVel = 0.0;
    long   stEvents = 0;

    // Cài đặt chuột gốc của Windows: SendInput relative bị OS áp accel/speed thêm lần nữa,
    // nên khi engine bật phải tạm đặt 1:1 (speed 10, tắt precision) rồi khôi phục khi tắt.
    bool savedOs = false;
    INT savedMouse[3] = { 6, 10, 1 };
    INT savedSpeed = 10;
};
static MouseEngineState g_engine;

static double NowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void EngineSendMove(int mx, int my) {
    if (mx == 0 && my == 0) return;
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = mx;
    in.mi.dy = my;
    in.mi.dwFlags = MOUSEEVENTF_MOVE;
    in.mi.dwExtraInfo = kInjectTag;
    SendInput(1, &in, sizeof(INPUT));
}

// Xử lý 1 sự kiện raw mouse delta: làm mượt (bảo toàn quãng đường) + đường cong gia tốc tùy chỉnh,
// rồi bơm chuyển động đã xử lý vào OS bằng SendInput (relative move).
static void MouseEngine_ProcessAndInject(LONG dx, LONG dy) {
    if (!g_engine.enabled.load()) return;
    if (dx == 0 && dy == 0) return;

    double now = NowMs();
    double dt = (g_engine.lastMs >= 0.0) ? (now - g_engine.lastMs) : 4.0;
    if (dt < 0.05) dt = 0.05;    // chuột 8000Hz vẫn cho dt hợp lệ, tránh chia 0
    if (dt > 100.0) dt = 100.0;  // vừa bật lại sau khi đứng yên lâu
    g_engine.lastMs = now;

    double scale = (g_engine.curDpi > 0.0) ? (g_engine.refDpi / g_engine.curDpi) : 1.0;
    if (scale < 0.05) scale = 0.05;
    if (scale > 20.0) scale = 20.0;
    double fx = (double)dx * scale;
    double fy = (double)dy * scale;
    double dist = std::sqrt(fx * fx + fy * fy);

    // --- Làm mượt: bộ lọc trễ BẢO TOÀN tổng quãng đường (không mất/không thêm counts) ---
    // Mỗi sự kiện chỉ "xả" một phần alpha của lượng chưa xả; phần còn lại xả ở sự kiện sau hoặc bởi timer khi ngừng.
    double alpha = 1.0;
    if (g_engine.emaFastMs > 0.05) alpha = 1.0 - std::exp(-dt / g_engine.emaFastMs);
    double sx, sy;
    if (dist <= g_engine.microThresh || alpha >= 0.98) {
        // Chuyển động cực nhỏ (ngắm tĩnh) hoặc không làm mượt: đi thẳng 1:1, xả luôn phần chờ
        sx = g_engine.pendX + fx;
        sy = g_engine.pendY + fy;
        g_engine.pendX = g_engine.pendY = 0;
    } else {
        g_engine.pendX += fx;
        g_engine.pendY += fy;
        sx = g_engine.pendX * alpha;
        sy = g_engine.pendY * alpha;
        g_engine.pendX -= sx;
        g_engine.pendY -= sy;
    }

    // --- Tốc độ (counts/ms) trung bình trượt để quyết định hệ số gia tốc ---
    g_engine.hist.push_back({ dist, dt });
    while ((int)g_engine.hist.size() > (std::max)(1, g_engine.velWindow)) g_engine.hist.pop_front();
    double sumD = 0, sumT = 0;
    for (const auto& sm : g_engine.hist) { sumD += sm.dist; sumT += sm.dt; }
    double vel = sumD / (std::max)(sumT, 1.0);

    double mult = 1.0;
    if (g_engine.accel > 0.0 && vel > g_engine.accelOffset) {
        mult = 1.0 + g_engine.accel * (vel - g_engine.accelOffset);
        if (mult > g_engine.accelCap) mult = g_engine.accelCap;
    }
    if (mult < 1.0) mult = 1.0;
    g_engine.stMult = mult;
    g_engine.stVel = vel;

    // --- Làm tròn giữ phần dư ---
    g_engine.carryX += sx * mult;
    g_engine.carryY += sy * mult;
    int mx = (int)g_engine.carryX;
    int my = (int)g_engine.carryY;
    g_engine.carryX -= mx;
    g_engine.carryY -= my;
    if (mx > 2000) mx = 2000;
    if (mx < -2000) mx = -2000;
    if (my > 2000) my = 2000;
    if (my < -2000) my = -2000;
    EngineSendMove(mx, my);

    // --- Số liệu + bộ bảo vệ ---
    double outMag = std::sqrt((double)mx * mx + (double)my * my);
    g_engine.stIn += dist;
    g_engine.stOut += outMag;
    g_engine.stEvents++;

    if (g_engine.wdStart < 0.0) g_engine.wdStart = now;
    g_engine.wdIn += dist;
    g_engine.wdOut += outMag;
    g_engine.wdRaw += std::sqrt((double)dx * dx + (double)dy * dy);
    if (now - g_engine.wdStart >= 200.0) {
        bool trip = false;
        if (g_engine.watchdogOn) {
            // (1) Ngõ ra lớn hơn ngõ vào nhiều lần so với mức hệ số tối đa cho phép -> đang tự khuếch đại
            double maxRatio = g_engine.accelCap + 2.0;
            if (g_engine.wdOut > g_engine.wdIn * maxRatio + 300.0) trip = true;
            // (2) Tốc độ vật lý bất khả thi trong 200ms (vd chuột "tự chạy") so với DPI đã khai báo
            double dpi = (g_engine.curDpi > 0.0) ? g_engine.curDpi : 800.0;
            if (g_engine.wdRaw > dpi * 80.0) trip = true;
        }
        g_engine.wdStart = now;
        g_engine.wdIn = g_engine.wdOut = g_engine.wdRaw = 0;
        if (trip) {
            g_engine.enabled = false;
            g_engine.tripped = true;
            if (g_engineNotifyWnd) PostMessageW(g_engineNotifyWnd, WM_APP_TRIP, 0, 0);
        }
    }
}

// Gọi định kỳ (timer 10ms): khi đã ngừng di chuột vài ms thì xả nốt phần dư của bộ làm mượt để con trỏ không dừng thiếu vài counts.
static void MouseEngine_Flush() {
    if (!g_engine.enabled.load()) return;
    double now = NowMs();
    if (g_engine.lastMs < 0.0 || now - g_engine.lastMs < 6.0) return;
    if (std::fabs(g_engine.pendX) + std::fabs(g_engine.pendY) < 0.01) return;
    g_engine.carryX += g_engine.pendX;
    g_engine.carryY += g_engine.pendY;
    g_engine.pendX = g_engine.pendY = 0;
    int mx = (int)std::lround(g_engine.carryX);
    int my = (int)std::lround(g_engine.carryY);
    g_engine.carryX -= mx;
    g_engine.carryY -= my;
    EngineSendMove(mx, my);
}

static void MouseEngine_ResetState() {
    g_engine.pendX = g_engine.pendY = 0;
    g_engine.carryX = g_engine.carryY = 0;
    g_engine.lastMs = -1.0;
    g_engine.hist.clear();
    g_engine.wdStart = -1.0;
    g_engine.wdIn = g_engine.wdOut = g_engine.wdRaw = 0;
    g_engine.stIn = g_engine.stOut = 0;
    g_engine.stEvents = 0;
    g_engine.stMult = 1.0;
    g_engine.stVel = 0.0;
    g_engine.tripped = false;
}

// Đăng ký raw input: RIDEV_INPUTSINK để nhận cả khi cửa sổ không active, RIDEV_NOLEGACY để OS
// không tự áp accel/di chuyển con trỏ mặc định nữa — từ đây app tự chịu trách nhiệm di chuyển con trỏ.
static bool MouseEngine_Register(HWND hwnd) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic Desktop
    rid.usUsage = 0x02;     // Mouse
    rid.dwFlags = RIDEV_INPUTSINK | RIDEV_NOLEGACY;
    rid.hwndTarget = hwnd;
    // Lưu cài đặt chuột hiện tại rồi đặt 1:1 (tạm thời, không ghi vào profile người dùng)
    if (!g_engine.savedOs) {
        SystemParametersInfoW(SPI_GETMOUSE, 0, g_engine.savedMouse, 0);
        SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &g_engine.savedSpeed, 0);
        g_engine.savedOs = true;
    }
    INT flat[3] = { 0, 0, 0 };
    SystemParametersInfoW(SPI_SETMOUSE, 0, flat, 0);
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)10, 0);

    MouseEngine_ResetState();
    if (RegisterRawInputDevices(&rid, 1, sizeof(rid))) {
        g_engine.rawRegistered = true;
        return true;
    }
    SystemParametersInfoW(SPI_SETMOUSE, 0, g_engine.savedMouse, 0);
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)g_engine.savedSpeed, 0);
    g_engine.savedOs = false;
    return false;
}

// Gỡ đăng ký raw input -> trả lại hành vi chuột mặc định của Windows ngay lập tức.
static void MouseEngine_Unregister(HWND) {
    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01;
    rid.usUsage = 0x02;
    rid.dwFlags = RIDEV_REMOVE;
    rid.hwndTarget = nullptr;
    RegisterRawInputDevices(&rid, 1, sizeof(rid));
    g_engine.rawRegistered = false;
    if (g_engine.savedOs) {
        SystemParametersInfoW(SPI_SETMOUSE, 0, g_engine.savedMouse, 0);
        SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)g_engine.savedSpeed, 0);
        g_engine.savedOs = false;
    }
}

// RIDEV_NOLEGACY chặn LUÔN cả click/cuộn của OS -> phải tự chuyển tiếp nút + bánh xe bằng SendInput.
static void MouseEngine_ForwardButtons(const RAWMOUSE& m) {
    USHORT f = m.usButtonFlags;
    INPUT in[12]{};
    int n = 0;
    auto add = [&](DWORD flags, DWORD data = 0) {
        in[n].type = INPUT_MOUSE;
        in[n].mi.dwFlags = flags;
        in[n].mi.mouseData = data;
        in[n].mi.dwExtraInfo = kInjectTag;
        n++;
    };
    if (f & RI_MOUSE_LEFT_BUTTON_DOWN)   add(MOUSEEVENTF_LEFTDOWN);
    if (f & RI_MOUSE_LEFT_BUTTON_UP)     add(MOUSEEVENTF_LEFTUP);
    if (f & RI_MOUSE_RIGHT_BUTTON_DOWN)  add(MOUSEEVENTF_RIGHTDOWN);
    if (f & RI_MOUSE_RIGHT_BUTTON_UP)    add(MOUSEEVENTF_RIGHTUP);
    if (f & RI_MOUSE_MIDDLE_BUTTON_DOWN) add(MOUSEEVENTF_MIDDLEDOWN);
    if (f & RI_MOUSE_MIDDLE_BUTTON_UP)   add(MOUSEEVENTF_MIDDLEUP);
    if (f & RI_MOUSE_BUTTON_4_DOWN)      add(MOUSEEVENTF_XDOWN, XBUTTON1);
    if (f & RI_MOUSE_BUTTON_4_UP)        add(MOUSEEVENTF_XUP, XBUTTON1);
    if (f & RI_MOUSE_BUTTON_5_DOWN)      add(MOUSEEVENTF_XDOWN, XBUTTON2);
    if (f & RI_MOUSE_BUTTON_5_UP)        add(MOUSEEVENTF_XUP, XBUTTON2);
    if (f & RI_MOUSE_WHEEL)              add(MOUSEEVENTF_WHEEL, (DWORD)(INT)(SHORT)m.usButtonData);
    if (f & RI_MOUSE_HWHEEL)             add(MOUSEEVENTF_HWHEEL, (DWORD)(INT)(SHORT)m.usButtonData);
    if (n > 0) SendInput((UINT)n, in, sizeof(INPUT));
}

// ===================== Globals =====================
struct AppGlobals {
    HINSTANCE hInst = nullptr;
    HWND hMain = nullptr;
    HWND hTab = nullptr;
    HWND hLog = nullptr;
    HWND hStatus = nullptr;
    HFONT hFontUI = nullptr;
    HFONT hFontMono = nullptr;
    int curTab = 0;
    bool admin = false;
    bool connected = false;

    HWND cfgCombo=nullptr, cfgRescan=nullptr, cfgEditKey=nullptr, cfgBackup=nullptr, cfgRestore=nullptr, cfgList=nullptr;
    std::vector<fs::path> cfgFiles;

    HWND adbCombo=nullptr, adbSerialLbl=nullptr, adbSerial=nullptr, adbConnect=nullptr, adbInfoList=nullptr, adbKeyList=nullptr;
    std::vector<fs::path> adbBins;

    HWND optAnim=nullptr, optHwui=nullptr, optAwake=nullptr, optFinish=nullptr, optGov=nullptr, optSched=nullptr, optScan=nullptr, optList=nullptr;
    HWND optHaptic=nullptr, optTimeout=nullptr, optRotation=nullptr;

    HWND rootCheck=nullptr, rootOn=nullptr, rootOff=nullptr, rootVerify=nullptr, rootList=nullptr;

    HWND infoLoad=nullptr, infoPriority=nullptr, infoClearLog=nullptr, infoText=nullptr;

    HWND presetCombo=nullptr, presetName=nullptr, presetSave=nullptr, presetLoad=nullptr, presetDelete=nullptr, presetRefresh=nullptr, presetList=nullptr;
    std::vector<fs::path> presetFiles;

    HWND hHeaderTitle=nullptr, hHeaderSub=nullptr;

    HWND mouseSpeedTrack=nullptr, mouseSpeedVal=nullptr;
    HWND mousePrecision=nullptr;
    HWND mouseDblTrack=nullptr, mouseDblVal=nullptr;
    HWND mouseWheelTrack=nullptr, mouseWheelVal=nullptr;
    HWND mouseTrailsTrack=nullptr, mouseTrailsVal=nullptr;
    HWND mouseApply=nullptr, mouseReset=nullptr, mouseRefresh=nullptr;

    HWND engEnable=nullptr;
    HWND engEmaTrack=nullptr, engEmaVal=nullptr;
    HWND engVelTrack=nullptr, engVelVal=nullptr;
    HWND engMicroTrack=nullptr, engMicroVal=nullptr;
    HWND engAccelTrack=nullptr, engAccelVal=nullptr;
    HWND engOffsetTrack=nullptr, engOffsetVal=nullptr;
    HWND engCapTrack=nullptr, engCapVal=nullptr;
    HWND engRefDpi=nullptr, engCurDpi=nullptr;
    HWND engReset=nullptr;
    HWND engStatus=nullptr;

    HFONT hFontTitle = nullptr;
    HWND hHeaderPanel = nullptr;
    HBRUSH hBrushHeader = nullptr;
    HBRUSH hBrushWindow = nullptr;

    // v3: giao diện mới
    HFONT hFontBold = nullptr;
    HWND themeToggle = nullptr;
    int engState = 0;        // 0 = tắt, 1 = bật, 2 = đã tự ngắt do bộ bảo vệ
    int verdictLevel = 3;    // 0 tốt, 1 cảnh báo, 2 nguy hiểm, 3 chưa đo
    std::vector<HWND> accentLabels, mutedLabels;

    HWND qsToggle = nullptr, qsStatus = nullptr;
    HWND testVals[6] = {};
    HWND testVerdict = nullptr, testWatchdog = nullptr, testReset = nullptr, testClickPad = nullptr;
    int testClicks = 0;
    HWND guideText = nullptr;

    static constexpr int TAB_COUNT = 11;
    std::vector<HWND> tabControls[TAB_COUNT];
};
static AppGlobals g;

// Thứ tự HIỂN THỊ của các nút tab -> chỉ số nội dung (tabControls[...]). Tab đơn giản đặt trước, nâng cao đặt sau.
static const int kTabContentIdx[AppGlobals::TAB_COUNT] = { 8, 7, 9, 6, 2, 5, 0, 1, 3, 10, 4 };
static const wchar_t* const kTabNames[AppGlobals::TAB_COUNT] = {
    L"Bắt đầu", L"Engine", L"Test", L"Chuột", L"Tối ưu", L"Presets", L"Config", L"ADB", L"Root", L"Hướng dẫn", L"Thông tin"
};

// ===================== Theme (Tối / Sáng) =====================
struct Theme {
    COLORREF bg, panel, text, muted, accent, accentText, btn, border, edit, good, warn, bad;
};
static const Theme kThemeDark  = { RGB(13,17,23),  RGB(22,27,34),  RGB(230,237,243), RGB(139,148,158), RGB(0,207,232),
                                   RGB(2,24,30),   RGB(33,38,45),  RGB(56,63,72),    RGB(22,27,34),
                                   RGB(63,185,80), RGB(210,153,34), RGB(248,81,73) };
static const Theme kThemeLight = { RGB(244,246,249), RGB(255,255,255), RGB(24,32,44),   RGB(96,108,124), RGB(0,120,212),
                                   RGB(255,255,255), RGB(228,233,240), RGB(202,209,219), RGB(255,255,255),
                                   RGB(26,127,55), RGB(154,103,0),  RGB(207,34,46) };
static bool g_dark = true;
static const Theme& Th() { return g_dark ? kThemeDark : kThemeLight; }
static HBRUSH g_brBg[2] = { nullptr, nullptr };
static HBRUSH g_brEdit[2] = { nullptr, nullptr };
static HBRUSH BrBg()   { return g_brBg[g_dark ? 0 : 1]; }
static HBRUSH BrEdit() { return g_brEdit[g_dark ? 0 : 1]; }
static void CreateThemeBrushes() {
    g_brBg[0] = CreateSolidBrush(kThemeDark.bg);
    g_brBg[1] = CreateSolidBrush(kThemeLight.bg);
    g_brEdit[0] = CreateSolidBrush(kThemeDark.edit);
    g_brEdit[1] = CreateSolidBrush(kThemeLight.edit);
}
static COLORREF Shade(COLORREF c, int d) {
    auto cl = [](int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); };
    return RGB(cl(GetRValue(c) + d), cl(GetGValue(c) + d), cl(GetBValue(c) + d));
}

static bool IsPresetBtn(int id)  { return id >= IDC_QS_P1 && id <= IDC_QS_P5; }
static bool IsPrimaryBtn(int id) {
    return id == IDC_QS_TOGGLE || id == IDC_MOUSE_APPLY || id == IDC_OPT_SCAN || id == IDC_PRESET_SAVE ||
           id == IDC_ADB_CONNECT || id == IDC_DLG_OK || id == IDC_ENG_RESET;
}

// Vẽ nút BUTTON tự vẽ (BS_OWNERDRAW): nút thường, nút chính (màu nhấn), thẻ preset 2 dòng, và nút tab.
static void DrawOwnerButton(const DRAWITEMSTRUCT* d) {
    const Theme& t = Th();
    HDC dc = d->hDC;
    RECT rc = d->rcItem;
    int id = (int)d->CtlID;
    wchar_t buf[256] = {};
    GetWindowTextW(d->hwndItem, buf, 256);
    std::wstring txt(buf);
    bool pressed = (d->itemState & ODS_SELECTED) != 0;
    HFONT fUI = g.hFontUI ? g.hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT fBold = g.hFontBold ? g.hFontBold : fUI;

    FillRect(dc, &rc, BrBg());
    SetBkMode(dc, TRANSPARENT);
    HFONT oldFont = (HFONT)SelectObject(dc, fUI);

    if (id >= IDC_TABBTN_BASE && id < IDC_TABBTN_BASE + AppGlobals::TAB_COUNT) {
        bool sel = (id - IDC_TABBTN_BASE) == g.curTab;
        SelectObject(dc, sel ? fBold : fUI);
        SetTextColor(dc, sel ? t.accent : t.muted);
        RECT tr = rc;
        DrawTextW(dc, txt.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (sel) {
            RECT ul = { rc.left + 6, rc.bottom - 3, rc.right - 6, rc.bottom };
            HBRUSH b = CreateSolidBrush(t.accent);
            FillRect(dc, &ul, b);
            DeleteObject(b);
        }
        SelectObject(dc, oldFont);
        return;
    }

    bool primary = IsPrimaryBtn(id);
    COLORREF fill = primary ? t.accent : t.btn;
    COLORREF txtCol = primary ? t.accentText : t.text;
    if (id == IDC_QS_TOGGLE && g.engState == 1) { fill = t.bad; txtCol = RGB(255, 255, 255); }
    if (pressed) fill = Shade(fill, -24);
    COLORREF border = primary ? fill : t.border;

    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, 10, 10);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);

    size_t nl = txt.find(L'\n');
    if (nl != std::wstring::npos) {
        std::wstring l1 = txt.substr(0, nl), l2 = txt.substr(nl + 1);
        UINT al = IsPresetBtn(id) ? DT_LEFT : DT_CENTER;
        RECT r1 = { rc.left + 14, rc.top + 8,  rc.right - 14, rc.top + 30 };
        RECT r2 = { rc.left + 14, rc.top + 30, rc.right - 14, rc.bottom - 6 };
        SelectObject(dc, fBold);
        SetTextColor(dc, txtCol);
        DrawTextW(dc, l1.c_str(), -1, &r1, al | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, fUI);
        SetTextColor(dc, primary ? Shade(txtCol, 40) : t.muted);
        DrawTextW(dc, l2.c_str(), -1, &r2, al | DT_WORDBREAK);
    } else {
        SetTextColor(dc, txtCol);
        SelectObject(dc, primary ? fBold : fUI);
        RECT tr = rc;
        DrawTextW(dc, txt.c_str(), -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    SelectObject(dc, oldFont);
}

static bool HwndIn(const std::vector<HWND>& v, HWND h) { return std::find(v.begin(), v.end(), h) != v.end(); }

// Màu nền/chữ cho STATIC, EDIT, LISTBOX, trackbar... theo theme hiện tại. Dùng chung cho cửa sổ chính và hộp thoại.
static LRESULT ThemeCtlColor(UINT msg, HDC hdc, HWND ctl) {
    const Theme& t = Th();
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX) {
        SetTextColor(hdc, t.text);
        SetBkColor(hdc, t.edit);
        return (LRESULT)BrEdit();
    }
    wchar_t cls[16] = {};
    GetClassNameW(ctl, cls, 16);
    if (lstrcmpiW(cls, L"Edit") == 0) {           // EDIT chỉ-đọc (log, thông tin, hướng dẫn) cũng gửi WM_CTLCOLORSTATIC
        SetTextColor(hdc, t.text);
        SetBkColor(hdc, t.edit);
        return (LRESULT)BrEdit();
    }
    COLORREF col = t.text;
    if (ctl == g.engStatus || ctl == g.qsStatus) col = g.engState == 1 ? t.good : (g.engState == 2 ? t.bad : t.muted);
    else if (ctl == g.testVerdict) col = g.verdictLevel == 0 ? t.good : (g.verdictLevel == 1 ? t.warn : (g.verdictLevel == 2 ? t.bad : t.muted));
    else if (HwndIn(g.accentLabels, ctl)) col = t.accent;
    else if (HwndIn(g.mutedLabels, ctl)) col = t.muted;
    SetTextColor(hdc, col);
    SetBkColor(hdc, t.bg);
    SetBkMode(hdc, TRANSPARENT);
    return (LRESULT)BrBg();
}

// Vẽ phần header (tên app + trạng thái engine), đường kẻ dưới thanh tab và dải trạng thái dưới cùng.
static void PaintMain(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    const Theme& t = Th();
    RECT client;
    GetClientRect(hwnd, &client);
    SetBkMode(dc, TRANSPARENT);

    HFONT fUI = g.hFontUI ? g.hFontUI : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HFONT fBold = g.hFontBold ? g.hFontBold : fUI;
    HFONT fTitle = g.hFontTitle ? g.hFontTitle : fBold;
    HFONT oldFont = (HFONT)SelectObject(dc, fTitle);

    SetTextColor(dc, t.accent);
    TextOutW(dc, 16, kHeaderTop + 2, L"Mouse Forge", 11);
    SelectObject(dc, fUI);
    SetTextColor(dc, t.muted);
    const wchar_t* sub = L"Bộ tinh chỉnh chuột & BlueStacks  •  v3";
    TextOutW(dc, 18, kHeaderTop + 34, sub, lstrlenW(sub));

    // Chip trạng thái engine (vẽ bằng hình tròn, không phụ thuộc glyph của font)
    COLORREF pc = g.engState == 1 ? t.good : (g.engState == 2 ? t.bad : t.muted);
    const wchar_t* pt = g.engState == 1 ? L"ENGINE ĐANG BẬT" : (g.engState == 2 ? L"ĐÃ TỰ NGẮT (BẢO VỆ)" : L"ENGINE TẮT");
    RECT pill = { 540, kHeaderTop + 10, 762, kHeaderTop + 38 };
    HBRUSH pb = CreateSolidBrush(t.panel);
    HPEN pp = CreatePen(PS_SOLID, 1, pc);
    HGDIOBJ ob = SelectObject(dc, pb);
    HGDIOBJ op = SelectObject(dc, pp);
    RoundRect(dc, pill.left, pill.top, pill.right, pill.bottom, 28, 28);
    HBRUSH dotB = CreateSolidBrush(pc);
    SelectObject(dc, dotB);
    Ellipse(dc, pill.left + 14, pill.top + 9, pill.left + 24, pill.top + 19);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(pb);
    DeleteObject(pp);
    DeleteObject(dotB);
    SelectObject(dc, fBold);
    SetTextColor(dc, pc);
    RECT tr = { pill.left + 28, pill.top, pill.right - 6, pill.bottom };
    DrawTextW(dc, pt, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    // Đường kẻ dưới thanh tab
    HPEN sp = CreatePen(PS_SOLID, 1, t.border);
    HGDIOBJ oldPen = SelectObject(dc, sp);
    MoveToEx(dc, 10, kTabTop + kTabBarH, nullptr);
    LineTo(dc, client.right - 10, kTabTop + kTabBarH);
    SelectObject(dc, oldPen);
    DeleteObject(sp);

    // Dải trạng thái dưới cùng
    SelectObject(dc, fUI);
    SetTextColor(dc, t.muted);
    std::wstring st = L"Ctrl+Alt+F7 = tắt engine khẩn cấp    |    Quyền Admin: ";
    st += g.admin ? L"có" : L"không (ghi config/priority sẽ bị chặn)";
    TextOutW(dc, 12, client.bottom - kStatusH + 4, st.c_str(), (int)st.size());

    SelectObject(dc, oldFont);
    EndPaint(hwnd, &ps);
}

// Áp theme cho các control con dùng visual style (listbox/edit/combobox/checkbox) + thanh tiêu đề cửa sổ.
static BOOL CALLBACK ThemeChildProc(HWND h, LPARAM) {
    wchar_t cls[32] = {};
    GetClassNameW(h, cls, 32);
    if (lstrcmpiW(cls, L"ListBox") == 0) {
        SetWindowTheme(h, g_dark ? L"DarkMode_Explorer" : nullptr, nullptr);
    } else if (lstrcmpiW(cls, L"Edit") == 0 || lstrcmpiW(cls, L"ComboBox") == 0) {
        SetWindowTheme(h, g_dark ? L"DarkMode_CFD" : nullptr, nullptr);
    } else if (lstrcmpiW(cls, L"Button") == 0) {
        LONG_PTR type = GetWindowLongPtrW(h, GWL_STYLE) & 0xF;
        if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX) {
            // Checkbox kiểu classic để chữ nhận màu từ WM_CTLCOLORSTATIC (theme mặc định luôn vẽ chữ đen)
            if (g_dark) SetWindowTheme(h, L"", L""); else SetWindowTheme(h, nullptr, nullptr);
        }
    }
    return TRUE;
}

static void ApplyTheme() {
    if (!g.hMain) return;
    BOOL dark = g_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(g.hMain, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    COLORREF cap = Th().bg, txtc = Th().text;
    DwmSetWindowAttribute(g.hMain, DWMWA_CAPTION_COLOR, &cap, sizeof(cap));
    DwmSetWindowAttribute(g.hMain, DWMWA_TEXT_COLOR, &txtc, sizeof(txtc));
    EnumChildWindows(g.hMain, ThemeChildProc, 0);
    if (g.themeToggle) SetWindowTextW(g.themeToggle, g_dark ? L"Chế độ sáng" : L"Chế độ tối");
    RedrawWindow(g.hMain, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

static HWND g_dlgHwnd = nullptr;
static HWND g_dlgKeyEdit = nullptr;
static HWND g_dlgValEdit = nullptr;
static bool g_dlgResult = false;
static std::wstring g_dlgKeyOut, g_dlgValOut;

// ===================== Utility functions =====================
static std::wstring trim(const std::wstring& s) {
    size_t a = s.find_first_not_of(L" \t\r\n");
    if (a == std::wstring::npos) return L"";
    size_t b = s.find_last_not_of(L" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::wstring to_w(const std::string& s) {
    if (s.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    if (len <= 0) return L"";
    std::wstring w((size_t)len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), len);
    return w;
}

static std::string to_a(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    if (len <= 0) return "";
    std::string s((size_t)len, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring now_stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t buf[32];
    swprintf(buf, 32, L"%04d%02d%02d_%02d%02d%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return std::wstring(buf);
}

static std::wstring safe_name(std::wstring s) {
    const std::wstring bad = L"\\/:*?\"<>|";
    for (auto& c : s) {
        if (bad.find(c) != std::wstring::npos) c = L'_';
    }
    return s;
}

static bool is_admin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&ntAuth, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
                                  0, 0, 0, 0, 0, 0, &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

static void log_line(const std::wstring& s) {
    if (!g.hLog) return;
    int len = GetWindowTextLengthW(g.hLog);
    SendMessageW(g.hLog, EM_SETSEL, (WPARAM)len, (LPARAM)len);
    std::wstring line = s + L"\r\n";
    SendMessageW(g.hLog, EM_REPLACESEL, FALSE, (LPARAM)line.c_str());
    SendMessageW(g.hLog, EM_SCROLLCARET, 0, 0);
}

// ===================== Process runner =====================
struct ProcResult {
    bool launched = false;
    DWORD exit_code = 0;
    std::wstring stdout_text;
    std::wstring stderr_text;
};

struct PipeReadCtx {
    HANDLE hRead;
    std::string* out;
};

static DWORD WINAPI pipe_reader_thread(LPVOID param) {
    PipeReadCtx* ctx = (PipeReadCtx*)param;
    char buf[4096];
    DWORD n = 0;
    for (;;) {
        BOOL ok = ReadFile(ctx->hRead, buf, sizeof(buf), &n, nullptr);
        if (!ok || n == 0) break;
        ctx->out->append(buf, n);
    }
    return 0;
}

static ProcResult run_capture(const std::wstring& cmdline, DWORD timeout_ms = 15000) {
    ProcResult result;

    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    HANDLE hOutRead = nullptr, hOutWrite = nullptr;
    HANDLE hErrRead = nullptr, hErrWrite = nullptr;

    if (!CreatePipe(&hOutRead, &hOutWrite, &sa, 0)) return result;
    if (!SetHandleInformation(hOutRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        return result;
    }
    if (!CreatePipe(&hErrRead, &hErrWrite, &sa, 0)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        return result;
    }
    if (!SetHandleInformation(hErrRead, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hOutRead); CloseHandle(hOutWrite);
        CloseHandle(hErrRead); CloseHandle(hErrWrite);
        return result;
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = hOutWrite;
    si.hStdError = hErrWrite;
    si.hStdInput = nullptr;

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdline;

    BOOL ok = CreateProcessW(
        nullptr,
        mutableCmd.data(),
        nullptr, nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr, nullptr,
        &si, &pi
    );

    CloseHandle(hOutWrite);
    CloseHandle(hErrWrite);

    if (!ok) {
        CloseHandle(hOutRead);
        CloseHandle(hErrRead);
        return result;
    }

    result.launched = true;

    std::string outBuf, errBuf;
    PipeReadCtx outCtx{ hOutRead, &outBuf };
    PipeReadCtx errCtx{ hErrRead, &errBuf };

    HANDLE hOutThread = CreateThread(nullptr, 0, pipe_reader_thread, &outCtx, 0, nullptr);
    HANDLE hErrThread = CreateThread(nullptr, 0, pipe_reader_thread, &errCtx, 0, nullptr);

    DWORD waitRes = WaitForSingleObject(pi.hProcess, timeout_ms);
    if (waitRes == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 2000);
    }

    GetExitCodeProcess(pi.hProcess, &result.exit_code);

    if (hOutThread) { WaitForSingleObject(hOutThread, 3000); CloseHandle(hOutThread); }
    if (hErrThread) { WaitForSingleObject(hErrThread, 3000); CloseHandle(hErrThread); }

    CloseHandle(hOutRead);
    CloseHandle(hErrRead);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);

    result.stdout_text = to_w(outBuf);
    result.stderr_text = to_w(errBuf);

    return result;
}

// ===================== File discovery =====================
static std::vector<fs::path> find_bluestacks_configs() {
    std::vector<fs::path> result;
    std::vector<std::wstring> roots;

    auto add_env = [&](const wchar_t* name) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) roots.push_back(buf);
    };

    add_env(L"PROGRAMDATA");
    add_env(L"APPDATA");
    add_env(L"LOCALAPPDATA");
    add_env(L"PROGRAMFILES");
    add_env(L"PROGRAMFILES(X86)");

    std::vector<std::wstring> subdirs = { L"BlueStacks_nxt", L"BlueStacks_msi2" };

    for (const auto& r : roots) {
        for (const auto& sd : subdirs) {
            fs::path base = fs::path(r) / sd;
            std::error_code ec;
            if (!fs::exists(base, ec) || !fs::is_directory(base, ec)) continue;

            fs::path direct = base / L"bluestacks.conf";
            if (fs::exists(direct, ec)) result.push_back(direct);

            for (auto& entry : fs::directory_iterator(base, ec)) {
                if (ec) break;
                if (entry.is_directory()) {
                    fs::path candidate = entry.path() / L"bluestacks.conf";
                    std::error_code ec2;
                    if (fs::exists(candidate, ec2)) result.push_back(candidate);
                }
            }
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());
    return result;
}

static std::optional<fs::path> which_in_path(const std::wstring& exe) {
    wchar_t buf[MAX_PATH];
    DWORD n = SearchPathW(nullptr, exe.c_str(), nullptr, MAX_PATH, buf, nullptr);
    if (n > 0 && n < MAX_PATH) return fs::path(buf);
    return std::nullopt;
}

static std::vector<fs::path> find_adb_binaries() {
    std::vector<fs::path> result;
    std::vector<std::wstring> roots;

    auto add_env = [&](const wchar_t* name) {
        wchar_t buf[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(name, buf, MAX_PATH);
        if (n > 0 && n < MAX_PATH) roots.push_back(buf);
    };
    add_env(L"PROGRAMDATA");
    add_env(L"APPDATA");
    add_env(L"LOCALAPPDATA");
    add_env(L"PROGRAMFILES");
    add_env(L"PROGRAMFILES(X86)");

    std::vector<std::wstring> candidates = {
        L"BlueStacks_nxt\\HD-Adb.exe",
        L"BlueStacks_msi2\\HD-Adb.exe",
        L"BlueStacks\\HD-Adb.exe",
        L"BlueStacks_nxt\\Engine\\HD-Adb.exe",
        L"BlueStacks_msi2\\Engine\\HD-Adb.exe",
    };

    for (const auto& r : roots) {
        for (const auto& c : candidates) {
            fs::path p = fs::path(r) / c;
            std::error_code ec;
            if (fs::exists(p, ec)) result.push_back(p);
        }
    }

    std::sort(result.begin(), result.end());
    result.erase(std::unique(result.begin(), result.end()), result.end());

    auto pathAdb = which_in_path(L"adb.exe");
    if (pathAdb.has_value()) result.push_back(*pathAdb);

    return result;
}

// ===================== Config file =====================
struct ConfigFile {
    fs::path path;
    std::vector<std::wstring> lines;
    std::map<std::wstring, std::wstring> kv;
    std::vector<std::wstring> order;
    std::wstring newline = L"\r\n";
    bool valid = false;
};

static std::wstring detect_newline(const std::string& raw) {
    if (raw.find("\r\n") != std::string::npos) return L"\r\n";
    if (raw.find('\n') != std::string::npos) return L"\n";
    return L"\r\n";
}

static ConfigFile load_config(const fs::path& p) {
    ConfigFile cf;
    cf.path = p;

    std::ifstream f(p, std::ios::binary);
    if (!f.is_open()) return cf;

    std::ostringstream ss;
    ss << f.rdbuf();
    std::string raw = ss.str();
    f.close();

    if (raw.size() >= 3 &&
        (unsigned char)raw[0] == 0xEF && (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        raw = raw.substr(3);
    }

    cf.newline = detect_newline(raw);
    std::wstring wraw = to_w(raw);

    std::wstring cur;
    std::vector<std::wstring> rawLines;
    for (wchar_t c : wraw) {
        if (c == L'\n') {
            if (!cur.empty() && cur.back() == L'\r') cur.pop_back();
            rawLines.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) rawLines.push_back(cur);

    cf.lines = rawLines;

    for (const auto& line : rawLines) {
        std::wstring t = trim(line);
        if (t.empty()) continue;
        size_t eq = t.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring key = trim(t.substr(0, eq));
        std::wstring val = trim(t.substr(eq + 1));
        if (val.size() >= 2 && val.front() == L'"' && val.back() == L'"') {
            val = val.substr(1, val.size() - 2);
        }
        if (key.empty()) continue;
        if (cf.kv.find(key) == cf.kv.end()) cf.order.push_back(key);
        cf.kv[key] = val;
    }

    cf.valid = true;
    return cf;
}

static bool save_config(const ConfigFile& original, const ConfigFile& updated, const fs::path& out_path) {
    std::vector<std::wstring> newLines;
    std::map<std::wstring, bool> written;

    for (const auto& line : original.lines) {
        std::wstring t = trim(line);
        size_t eq = t.find(L'=');
        if (!t.empty() && eq != std::wstring::npos) {
            std::wstring key = trim(t.substr(0, eq));
            auto it = updated.kv.find(key);
            if (it != updated.kv.end()) {
                newLines.push_back(key + L"=\"" + it->second + L"\"");
                written[key] = true;
                continue;
            }
        }
        newLines.push_back(line);
    }

    for (const auto& key : updated.order) {
        if (written.find(key) == written.end()) {
            auto it = updated.kv.find(key);
            if (it != updated.kv.end()) {
                newLines.push_back(key + L"=\"" + it->second + L"\"");
            }
        }
    }

    std::wstring nl = updated.newline.empty() ? L"\r\n" : updated.newline;
    std::wstring outContent;
    for (const auto& l : newLines) {
        outContent += l;
        outContent += nl;
    }

    std::string utf8 = to_a(outContent);

    fs::path tmpPath = out_path;
    tmpPath += L".tmp";

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) return false;
        out.write(utf8.data(), (std::streamsize)utf8.size());
        if (!out.good()) { out.close(); return false; }
    }

    std::error_code ec;
    if (fs::exists(out_path, ec)) {
        fs::path bak = out_path;
        bak += (L".bak_" + now_stamp());
        fs::copy_file(out_path, bak, fs::copy_options::overwrite_existing, ec);
    }

    BOOL ok = MoveFileExW(tmpPath.c_str(), out_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    return ok == TRUE;
}

// ===================== ADB runner =====================
struct AdbRunner {
    fs::path adb;
    std::wstring serial;

    std::wstring base_cmd() const {
        return L"\"" + adb.wstring() + L"\" -s " + serial;
    }

    ProcResult exec(const std::wstring& args, DWORD timeout_ms = 8000) const {
        std::wstring full = base_cmd() + L" " + args;
        return run_capture(full, timeout_ms);
    }

    bool connected() const {
        ProcResult r = run_capture(L"\"" + adb.wstring() + L"\" get-state", 5000);
        if (!r.launched) return false;
        std::wstring out = r.stdout_text + r.stderr_text;
        return out.find(L"device") != std::wstring::npos && out.find(L"unknown") == std::wstring::npos;
    }

    std::wstring get_global(const std::wstring& k) const {
        return trim(exec(L"shell settings get global " + k, 6000).stdout_text);
    }
    std::wstring get_system(const std::wstring& k) const {
        return trim(exec(L"shell settings get system " + k, 6000).stdout_text);
    }
    std::wstring get_secure(const std::wstring& k) const {
        return trim(exec(L"shell settings get secure " + k, 6000).stdout_text);
    }
    bool put_global(const std::wstring& k, const std::wstring& v) const {
        ProcResult r = exec(L"shell settings put global " + k + L" " + v, 6000);
        return r.launched && r.exit_code == 0;
    }
    bool put_system(const std::wstring& k, const std::wstring& v) const {
        ProcResult r = exec(L"shell settings put system " + k + L" " + v, 6000);
        return r.launched && r.exit_code == 0;
    }
    std::wstring prop(const std::wstring& n) const {
        return trim(exec(L"shell getprop " + n, 6000).stdout_text);
    }
    std::wstring shell(const std::wstring& c) const {
        ProcResult r = exec(L"shell " + c, 8000);
        std::wstring out = r.stdout_text;
        if (!r.stderr_text.empty()) out += L" " + r.stderr_text;
        return trim(out);
    }
    bool shell_ok(const std::wstring& c) const {
        ProcResult r = exec(L"shell " + c, 8000);
        return r.launched && r.exit_code == 0;
    }
    bool shell_su_ok(const std::wstring& c) const {
        ProcResult r = exec(L"shell su -c \"" + c + L"\"", 8000);
        if (r.launched && r.exit_code == 0) return true;
        return shell_ok(c);
    }
};

// ===================== Process helper =====================
static std::vector<DWORD> find_bs_process(const std::wstring& exeName) {
    std::vector<DWORD> pids;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return pids;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, exeName.c_str()) == 0) {
                pids.push_back(pe.th32ProcessID);
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pids;
}

static std::optional<std::wstring> find_instance_prefix(const ConfigFile& cf) {
    for (const auto& key : cf.order) {
        if (key.rfind(L"bst.instance.", 0) == 0) {
            size_t firstDot = key.find(L'.');
            size_t secondDot = key.find(L'.', firstDot + 1);
            size_t thirdDot = key.find(L'.', secondDot + 1);
            if (thirdDot != std::wstring::npos) {
                return key.substr(0, thirdDot);
            }
        }
    }
    return std::nullopt;
}

// ===================== Key lists =====================
static const std::vector<std::pair<std::wstring, std::wstring>> SAFE_GLOBAL_KEYS = {
    {L"window_animation_scale", L"global"},
    {L"transition_animation_scale", L"global"},
    {L"animator_duration_scale", L"global"},
    {L"stay_on_while_plugged_in", L"global"},
    {L"always_finish_activities", L"global"},
    {L"debug.hwui.profile", L"global"},
    // --- v2: thêm các key AOSP có thật, đã xác minh trong AOSP Settings provider ---
    {L"haptic_feedback_enabled", L"system"},
    {L"screen_off_timeout", L"system"},
    {L"accelerometer_rotation", L"system"},
    {L"development_settings_enabled", L"global"},
    {L"wifi_sleep_policy", L"global"},
    {L"auto_time", L"global"},
};

static const std::vector<std::tuple<std::wstring, std::wstring, std::wstring>> USELESS_KEYS = {
    {L"touch.pressure.scale", L"system", L"AOSP không đọc, không nằm trong đường đi touch event"},
    {L"touch.size.scale", L"system", L"AOSP không đọc, không nằm trong đường đi touch event"},
    {L"touch_sensitivity", L"system", L"không tồn tại trong AOSP"},
    {L"touch_sensitivity", L"secure", L"không tồn tại trong AOSP"},
    {L"pointer_speed", L"system", L"chỉ ảnh hưởng con trỏ chuột Android, không ảnh hưởng aim FPS"},
    {L"sensitivity", L"system", L"không tồn tại trong AOSP"},
};

static const std::vector<std::wstring> IMPORTANT_PROPS = {
    L"ro.hardware.egl", L"ro.hardware.vulkan", L"ro.opengles.version",
    L"debug.hwui.renderer", L"ro.build.version.sdk", L"ro.product.cpu.abi",
    L"ro.kernel.qemu", L"ro.hardware", L"ro.hardware.input", L"persist.sys.input.touch_hz"
};

static const std::vector<std::wstring> SYSFS_INPUT_PATHS = {
    L"/sys/class/input/input0/device/poll_interval",
    L"/sys/class/input/input0/device/touch_rate",
    L"/sys/class/input/input0/device/report_rate",
    L"/sys/module/msm_performance/parameters/touchboost",
    L"/proc/touchpanel/game_switch_enable",
};

// ===================== Edit-key modal dialog =====================
static LRESULT CALLBACK EditKeyDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CreateWindowExW(0, L"STATIC", L"Key:", WS_CHILD | WS_VISIBLE,
            10, 15, 60, 20, hwnd, (HMENU)(INT_PTR)IDC_DLG_KEY_LBL, g.hInst, nullptr);
        g_dlgKeyEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            80, 12, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_DLG_KEY, g.hInst, nullptr);
        CreateWindowExW(0, L"STATIC", L"Value:", WS_CHILD | WS_VISIBLE,
            10, 50, 60, 20, hwnd, (HMENU)(INT_PTR)IDC_DLG_VAL_LBL, g.hInst, nullptr);
        g_dlgValEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_BORDER,
            80, 47, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_DLG_VAL, g.hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE,
            130, 90, 100, 28, hwnd, (HMENU)(INT_PTR)IDC_DLG_OK, g.hInst, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Huỷ", WS_CHILD | WS_VISIBLE,
            250, 90, 100, 28, hwnd, (HMENU)(INT_PTR)IDC_DLG_CANCEL, g.hInst, nullptr);

        if (g.hFontUI) {
            SendMessageW(g_dlgKeyEdit, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
            SendMessageW(g_dlgValEdit, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
        }
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id == IDC_DLG_OK) {
            wchar_t kbuf[512], vbuf[1024];
            GetWindowTextW(g_dlgKeyEdit, kbuf, 512);
            GetWindowTextW(g_dlgValEdit, vbuf, 1024);
            g_dlgKeyOut = trim(kbuf);
            g_dlgValOut = trim(vbuf);
            if (g_dlgKeyOut.empty()) {
                MessageBoxW(hwnd, L"Key không được để trống.", L"Lỗi", MB_OK | MB_ICONERROR);
                return 0;
            }
            g_dlgResult = true;
            DestroyWindow(hwnd);
            return 0;
        } else if (id == IDC_DLG_CANCEL) {
            g_dlgResult = false;
            DestroyWindow(hwnd);
            return 0;
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
        return ThemeCtlColor(msg, (HDC)wParam, (HWND)lParam);
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, BrBg());
        return 1;
    }
    case WM_CLOSE:
        g_dlgResult = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        g_dlgHwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static bool ShowEditKeyDialog(HWND parent, std::wstring& outKey, std::wstring& outVal) {
    g_dlgResult = false;
    g_dlgKeyOut.clear();
    g_dlgValOut.clear();

    RECT pr;
    GetWindowRect(parent, &pr);
    int w = 400, h = 160;
    int x = pr.left + ((pr.right - pr.left) - w) / 2;
    int y = pr.top + ((pr.bottom - pr.top) - h) / 2;

    EnableWindow(parent, FALSE);

    g_dlgHwnd = CreateWindowExW(WS_EX_DLGMODALFRAME, L"MFEditKeyDlg", L"Sửa key",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        x, y, w, h, parent, nullptr, g.hInst, nullptr);

    if (!g_dlgHwnd) {
        EnableWindow(parent, TRUE);
        return false;
    }

    ShowWindow(g_dlgHwnd, SW_SHOW);
    UpdateWindow(g_dlgHwnd);

    MSG msg;
    while (g_dlgHwnd != nullptr) {
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) break;
        if (!IsDialogMessageW(g_dlgHwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);

    if (g_dlgResult) {
        outKey = g_dlgKeyOut;
        outVal = g_dlgValOut;
        return true;
    }
    return false;
}

// ===================== Config tab handlers =====================
static void RefreshConfigCombo() {
    g.cfgFiles = find_bluestacks_configs();
    SendMessageW(g.cfgCombo, CB_RESETCONTENT, 0, 0);
    for (auto& p : g.cfgFiles) {
        SendMessageW(g.cfgCombo, CB_ADDSTRING, 0, (LPARAM)p.wstring().c_str());
    }
    if (!g.cfgFiles.empty()) SendMessageW(g.cfgCombo, CB_SETCURSEL, 0, 0);
}

static void RefreshAdbCombo() {
    g.adbBins = find_adb_binaries();
    SendMessageW(g.adbCombo, CB_RESETCONTENT, 0, 0);
    for (auto& p : g.adbBins) {
        SendMessageW(g.adbCombo, CB_ADDSTRING, 0, (LPARAM)p.wstring().c_str());
    }
    if (!g.adbBins.empty()) SendMessageW(g.adbCombo, CB_SETCURSEL, 0, 0);
}

static void OnConfigRescan() {
    RefreshConfigCombo();
    RefreshAdbCombo();
    log_line(L"[Config] đã quét lại: " + std::to_wstring(g.cfgFiles.size()) + L" file config, " +
             std::to_wstring(g.adbBins.size()) + L" ADB binary");
}

static void FillConfigList() {
    int idx = (int)SendMessageW(g.cfgCombo, CB_GETCURSEL, 0, 0);
    SendMessageW(g.cfgList, LB_RESETCONTENT, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.cfgFiles.size()) return;
    ConfigFile cf = load_config(g.cfgFiles[idx]);
    if (!cf.valid) { log_line(L"[Config] không đọc được file"); return; }
    for (auto& key : cf.order) {
        std::wstring line = key + L" = " + cf.kv[key];
        SendMessageW(g.cfgList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

static void OnConfigBackup() {
    if (g.cfgFiles.empty()) { log_line(L"[Config] không có file để backup"); return; }

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path backupRoot = exeDir / L"backups" / now_stamp();

    std::error_code ec;
    fs::create_directories(backupRoot, ec);
    if (ec) { log_line(L"[Config] không tạo được thư mục backup"); return; }

    std::wstringstream meta;
    meta << L"{\n  \"backups\": [\n";
    bool first = true;
    int count = 0;
    for (auto& p : g.cfgFiles) {
        std::wstring name = safe_name(p.filename().wstring()) + L"_" + std::to_wstring(count);
        fs::path dest = backupRoot / name;
        std::error_code ec2;
        fs::copy_file(p, dest, fs::copy_options::overwrite_existing, ec2);
        if (!ec2) {
            if (!first) meta << L",\n";
            first = false;
            meta << L"    {\"original\": \"" << p.wstring() << L"\", \"backup\": \"" << dest.wstring() << L"\"}";
            count++;
        }
    }
    meta << L"\n  ]\n}\n";

    fs::path metaPath = backupRoot / L"meta.json";
    std::string metaUtf8 = to_a(meta.str());
    std::ofstream mf(metaPath, std::ios::binary | std::ios::trunc);
    if (mf.is_open()) {
        mf.write(metaUtf8.data(), (std::streamsize)metaUtf8.size());
        mf.close();
    }

    log_line(L"[Config] đã backup " + std::to_wstring(count) + L" file vào " + backupRoot.wstring());
}

static std::optional<fs::path> find_latest_backup_dir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path backupsRoot = exeDir / L"backups";

    std::error_code ec;
    if (!fs::exists(backupsRoot, ec)) return std::nullopt;

    std::optional<fs::path> latest;
    FILETIME latestTime{};
    for (auto& entry : fs::directory_iterator(backupsRoot, ec)) {
        if (ec) break;
        if (!entry.is_directory()) continue;
        fs::path meta = entry.path() / L"meta.json";
        std::error_code ec2;
        if (!fs::exists(meta, ec2)) continue;
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (GetFileAttributesExW(meta.c_str(), GetFileExInfoStandard, &fad)) {
            if (!latest.has_value() || CompareFileTime(&fad.ftLastWriteTime, &latestTime) > 0) {
                latestTime = fad.ftLastWriteTime;
                latest = entry.path();
            }
        }
    }
    return latest;
}

static std::vector<std::pair<std::wstring, std::wstring>> parse_meta_json(const std::wstring& content) {
    std::vector<std::pair<std::wstring, std::wstring>> result;
    size_t pos = 0;
    while (true) {
        size_t origPos = content.find(L"\"original\": \"", pos);
        if (origPos == std::wstring::npos) break;
        origPos += 13;
        size_t origEnd = content.find(L"\"", origPos);
        if (origEnd == std::wstring::npos) break;
        std::wstring original = content.substr(origPos, origEnd - origPos);

        size_t bakPos = content.find(L"\"backup\": \"", origEnd);
        if (bakPos == std::wstring::npos) break;
        bakPos += 11;
        size_t bakEnd = content.find(L"\"", bakPos);
        if (bakEnd == std::wstring::npos) break;
        std::wstring backup = content.substr(bakPos, bakEnd - bakPos);

        result.push_back({ original, backup });
        pos = bakEnd;
    }
    return result;
}

static void OnConfigRestore() {
    auto dirOpt = find_latest_backup_dir();
    if (!dirOpt.has_value()) { log_line(L"[Config] không tìm thấy backup nào"); return; }
    fs::path dir = *dirOpt;
    fs::path metaPath = dir / L"meta.json";

    std::ifstream f(metaPath, std::ios::binary);
    if (!f.is_open()) { log_line(L"[Config] không đọc được meta.json"); return; }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::wstring content = to_w(ss.str());

    auto pairs = parse_meta_json(content);
    if (pairs.empty()) { log_line(L"[Config] meta.json rỗng hoặc lỗi"); return; }

    std::wstring msg = L"Khôi phục " + std::to_wstring(pairs.size()) + L" file từ backup:\n" +
                        dir.wstring() + L"\n\nBạn có chắc chắn?";
    int r = MessageBoxW(g.hMain, msg.c_str(), L"Xác nhận Restore", MB_YESNO | MB_ICONWARNING);
    if (r != IDYES) { log_line(L"[Config] huỷ restore"); return; }

    int okCount = 0, failCount = 0;
    for (auto& pr : pairs) {
        std::error_code ec;
        fs::path orig(pr.first);
        fs::path bak(pr.second);
        if (!fs::exists(bak, ec)) { failCount++; continue; }

        if (fs::exists(orig, ec)) {
            fs::path preRestoreBak = orig;
            preRestoreBak += (L".bak_" + now_stamp());
            fs::copy_file(orig, preRestoreBak, fs::copy_options::overwrite_existing, ec);
        }

        fs::path tmp = orig;
        tmp += L".tmp";
        fs::copy_file(bak, tmp, fs::copy_options::overwrite_existing, ec);
        if (ec) { failCount++; continue; }

        BOOL ok = MoveFileExW(tmp.c_str(), orig.c_str(), MOVEFILE_REPLACE_EXISTING);
        if (ok) okCount++; else failCount++;
    }

    log_line(L"[Config] restore xong: OK=" + std::to_wstring(okCount) + L" FAIL=" + std::to_wstring(failCount));
    FillConfigList();
}

static void OnConfigEditKey() {
    int idx = (int)SendMessageW(g.cfgCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.cfgFiles.size()) {
        log_line(L"[Edit] chưa chọn file config");
        return;
    }
    fs::path path = g.cfgFiles[idx];

    if (!g.admin) {
        MessageBoxW(g.hMain, L"Cần quyền Administrator để sửa file config.", L"Lỗi", MB_OK | MB_ICONERROR);
        log_line(L"[Edit] thiếu quyền admin");
        return;
    }

    std::wstring key, val;
    if (!ShowEditKeyDialog(g.hMain, key, val)) {
        log_line(L"[Edit] đã huỷ");
        return;
    }

    ConfigFile original = load_config(path);
    if (!original.valid) { log_line(L"[Edit] không đọc được file"); return; }

    ConfigFile updated = original;
    if (updated.kv.find(key) == updated.kv.end()) updated.order.push_back(key);
    updated.kv[key] = val;

    bool ok = save_config(original, updated, path);
    if (ok) {
        log_line(L"[Edit] OK " + key + L" = " + val);
        FillConfigList();
    } else {
        log_line(L"[Edit] ghi thất bại " + key);
    }
}

// ===================== ADB tab handlers =====================
static std::optional<AdbRunner> current_adb_runner() {
    int idx = (int)SendMessageW(g.adbCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.adbBins.size()) return std::nullopt;
    wchar_t buf[256];
    GetWindowTextW(g.adbSerial, buf, 256);
    AdbRunner r;
    r.adb = g.adbBins[idx];
    r.serial = buf;
    return r;
}

static void OnAdbConnect() {
    auto ro = current_adb_runner();
    if (!ro.has_value()) { log_line(L"[ADB] chưa chọn ADB binary"); return; }
    AdbRunner adb = *ro;

    log_line(L"[ADB] đang kết nối " + adb.serial + L" ...");
    run_capture(L"\"" + adb.adb.wstring() + L"\" connect " + adb.serial, 8000);
    Sleep(700);
    bool ok = adb.connected();
    if (!ok) {
        log_line(L"[ADB] không kết nối được");
        g.connected = false;
        return;
    }
    g.connected = true;
    log_line(L"[ADB] đã kết nối");

    SendMessageW(g.adbInfoList, LB_RESETCONTENT, 0, 0);
    std::wstring brand = adb.prop(L"ro.product.brand");
    std::wstring model = adb.prop(L"ro.product.model");
    std::wstring ver = adb.prop(L"ro.build.version.release");
    std::wstring abi = adb.prop(L"ro.product.cpu.abi");

    auto addInfo = [&](const std::wstring& s) { SendMessageW(g.adbInfoList, LB_ADDSTRING, 0, (LPARAM)s.c_str()); };
    addInfo(L"Brand: " + brand);
    addInfo(L"Model: " + model);
    addInfo(L"Android: " + ver);
    addInfo(L"ABI: " + abi);

    SendMessageW(g.adbKeyList, LB_RESETCONTENT, 0, 0);
    auto addKey = [&](const std::wstring& s) { SendMessageW(g.adbKeyList, LB_ADDSTRING, 0, (LPARAM)s.c_str()); };
    addKey(L"--- Key an toàn (có tác dụng thật) ---");
    for (auto& kv : SAFE_GLOBAL_KEYS) addKey(kv.first + L" (" + kv.second + L")");
    addKey(L"--- Key vô dụng (không áp dụng) ---");
    for (auto& t : USELESS_KEYS) addKey(std::get<0>(t) + L" (" + std::get<1>(t) + L") - " + std::get<2>(t));

    log_line(L"[ADB] đã nạp thông tin thiết bị và danh sách key");
}

// ===================== Optimize tab handler =====================
static void OnOptScan() {
    auto ro = current_adb_runner();
    if (!ro.has_value()) { log_line(L"[Optimize] chưa chọn ADB binary"); return; }
    AdbRunner adb = *ro;
    if (!adb.connected()) {
        log_line(L"[Optimize] ADB chưa kết nối, hãy bấm Dò & nạp ở tab ADB trước");
        return;
    }

    SendMessageW(g.optList, LB_RESETCONTENT, 0, 0);
    auto addL = [&](const std::wstring& s) {
        SendMessageW(g.optList, LB_ADDSTRING, 0, (LPARAM)s.c_str());
        log_line(L"[Optimize] " + s);
    };

    addL(L"--- Prop quan trọng ---");
    for (auto& p : IMPORTANT_PROPS) {
        std::wstring v = adb.prop(p);
        addL(p + L" = " + (v.empty() ? L"(trống)" : v));
    }

    std::wstring idOut = adb.shell(L"id");
    bool hasRoot = idOut.find(L"uid=0") != std::wstring::npos;
    addL(hasRoot ? L"Root: có" : L"Root: không");

    bool doAnim = SendMessageW(g.optAnim, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doHwui = SendMessageW(g.optHwui, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doAwake = SendMessageW(g.optAwake, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doFinish = SendMessageW(g.optFinish, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doGov = SendMessageW(g.optGov, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doSched = SendMessageW(g.optSched, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doHaptic = SendMessageW(g.optHaptic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doTimeout = SendMessageW(g.optTimeout, BM_GETCHECK, 0, 0) == BST_CHECKED;
    bool doRotation = SendMessageW(g.optRotation, BM_GETCHECK, 0, 0) == BST_CHECKED;

    int okCount = 0, failCount = 0, skipCount = 0;

    if (doAnim) {
        bool a1 = adb.put_global(L"window_animation_scale", L"0");
        bool a2 = adb.put_global(L"transition_animation_scale", L"0");
        bool a3 = adb.put_global(L"animator_duration_scale", L"0");
        if (a1 && a2 && a3) { addL(L"OK animation scale = 0"); okCount++; }
        else { addL(L"FAIL animation scale"); failCount++; }
    }
    if (doHwui) {
        bool ok = adb.put_global(L"debug.hwui.profile", L"false");
        if (ok) { addL(L"OK debug.hwui.profile = false"); okCount++; }
        else { addL(L"FAIL debug.hwui.profile"); failCount++; }
    }
    if (doAwake) {
        bool ok = adb.put_global(L"stay_on_while_plugged_in", L"3");
        if (ok) { addL(L"OK stay_on_while_plugged_in = 3"); okCount++; }
        else { addL(L"FAIL stay_on_while_plugged_in"); failCount++; }
    }
    if (doFinish) {
        bool ok = adb.put_global(L"always_finish_activities", L"0");
        if (ok) { addL(L"OK always_finish_activities = 0"); okCount++; }
        else { addL(L"FAIL always_finish_activities"); failCount++; }
    }
    if (doHaptic) {
        bool ok = adb.put_system(L"haptic_feedback_enabled", L"0");
        if (ok) { addL(L"OK haptic_feedback_enabled = 0"); okCount++; }
        else { addL(L"FAIL haptic_feedback_enabled"); failCount++; }
    }
    if (doTimeout) {
        bool ok = adb.put_system(L"screen_off_timeout", L"2147483647");
        if (ok) { addL(L"OK screen_off_timeout = 2147483647 (không tự tắt màn hình)"); okCount++; }
        else { addL(L"FAIL screen_off_timeout"); failCount++; }
    }
    if (doRotation) {
        bool ok = adb.put_system(L"accelerometer_rotation", L"0");
        if (ok) { addL(L"OK accelerometer_rotation = 0 (khoá xoay tự động)"); okCount++; }
        else { addL(L"FAIL accelerometer_rotation"); failCount++; }
    }

    if (doGov) {
        if (!hasRoot) {
            addL(L"SKIP governor CPU: không có root");
            skipCount++;
        } else {
            std::wstring lsOut = adb.shell(L"su -c \"ls /sys/devices/system/cpu\" 2>/dev/null || ls /sys/devices/system/cpu");
            std::wstringstream ss(lsOut);
            std::wstring tok;
            int coreOk = 0, coreFail = 0;
            while (ss >> tok) {
                if (tok.size() > 3 && tok.compare(0, 3, L"cpu") == 0 && iswdigit(tok[3])) {
                    std::wstring path = L"/sys/devices/system/cpu/" + tok + L"/cpufreq/scaling_governor";
                    std::wstring cmd = L"echo performance > " + path;
                    bool ok = adb.shell_su_ok(cmd);
                    if (ok) coreOk++; else coreFail++;
                }
            }
            if (coreOk > 0) { addL(L"OK governor cpu (" + std::to_wstring(coreOk) + L" core) = performance"); okCount++; }
            if (coreFail > 0) { addL(L"FAIL governor cho " + std::to_wstring(coreFail) + L" core"); failCount++; }
            if (coreOk == 0 && coreFail == 0) { addL(L"SKIP governor CPU: không tìm thấy core"); skipCount++; }
        }
    }

    if (doSched) {
        if (!hasRoot) {
            addL(L"SKIP scheduler: không có root");
            skipCount++;
        } else {
            std::wstring path = L"/sys/kernel/debug/sched_features";
            std::wstring out = adb.shell(L"su -c \"test -e " + path + L" && echo YES || echo NO\"");
            bool exists = out.find(L"YES") != std::wstring::npos;
            if (exists) {
                addL(L"NOTE scheduler: " + path + L" tồn tại nhưng không rõ giá trị an toàn, không ghi tự động");
            } else {
                addL(L"SKIP scheduler: " + path + L" không tồn tại");
                skipCount++;
            }
        }
    }

    addL(L"--- Thăm dò sysfs input (chỉ đọc, không ghi) ---");
    for (auto& p : SYSFS_INPUT_PATHS) {
        std::wstring out = adb.shell(L"test -e " + p + L" && echo YES || echo NO");
        bool exists = out.find(L"YES") != std::wstring::npos;
        if (exists) addL(L"NOTE " + p + L": tồn tại, không rõ giá trị hợp lệ nên không ghi");
        else { addL(L"SKIP " + p + L": không tồn tại"); skipCount++; }
    }

    addL(L"=== Tổng kết: OK=" + std::to_wstring(okCount) + L" FAIL=" + std::to_wstring(failCount) +
         L" SKIP=" + std::to_wstring(skipCount) + L" ===");
}

// ===================== Root tab handlers =====================
static void OnRootSetEnabled(bool enable) {
    int idx = (int)SendMessageW(g.cfgCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.cfgFiles.size()) {
        log_line(L"[Root] chưa chọn file config ở tab Config");
        return;
    }
    fs::path path = g.cfgFiles[idx];

    auto pids = find_bs_process(L"HD-Player.exe");
    if (!pids.empty()) {
        int r = MessageBoxW(g.hMain,
            L"BlueStacks đang chạy. Việc ghi config có thể không có tác dụng cho tới khi bạn tắt hẳn và mở lại. Tiếp tục ghi?",
            L"Cảnh báo", MB_YESNO | MB_ICONWARNING);
        if (r != IDYES) { log_line(L"[Root] huỷ do BlueStacks đang chạy"); return; }
    }

    if (!g.admin) {
        MessageBoxW(g.hMain, L"Cần chạy Mouse Forge với quyền Administrator để thao tác này.", L"Lỗi", MB_OK | MB_ICONERROR);
        log_line(L"[Root] thiếu quyền admin");
        return;
    }

    ConfigFile original = load_config(path);
    if (!original.valid) { log_line(L"[Root] không đọc được file config"); return; }

    auto prefixOpt = find_instance_prefix(original);
    if (!prefixOpt.has_value()) {
        log_line(L"[Root] không tìm thấy instance prefix (bst.instance.*) trong config");
        return;
    }
    std::wstring prefix = *prefixOpt;

    ConfigFile updated = original;
    std::wstring val = enable ? L"1" : L"0";
    std::wstring k1 = prefix + L".enable_root_access";
    std::wstring k2 = L"bst.feature.rooting";
    if (updated.kv.find(k1) == updated.kv.end()) updated.order.push_back(k1);
    updated.kv[k1] = val;
    if (updated.kv.find(k2) == updated.kv.end()) updated.order.push_back(k2);
    updated.kv[k2] = val;

    bool ok = save_config(original, updated, path);
    if (ok) {
        log_line(L"[Root] đã ghi " + k1 + L" = " + val);
        log_line(L"[Root] đã ghi " + k2 + L" = " + val);
        log_line(L"[Root] hướng dẫn: 1) Tắt hẳn BlueStacks kể cả icon tray  2) Mở lại BlueStacks  3) Bấm Xác minh root");
        if (enable) log_line(L"[Root] cảnh báo: FreeFire có thể phát hiện root và hạn chế tài khoản chính");
    } else {
        log_line(L"[Root] ghi config thất bại");
    }
}

static void OnRootCheck() {
    auto ro = current_adb_runner();
    if (!ro.has_value()) { log_line(L"[Root] chưa chọn ADB binary"); return; }
    AdbRunner adb = *ro;

    SendMessageW(g.rootList, LB_RESETCONTENT, 0, 0);
    auto addLine = [&](const std::wstring& s) { SendMessageW(g.rootList, LB_ADDSTRING, 0, (LPARAM)s.c_str()); };

    std::wstring r1 = adb.shell(L"id");
    std::wstring r2 = adb.shell(L"which su");
    std::wstring r3 = adb.shell(L"su -c id");

    addLine(L"id: " + r1);
    addLine(L"which su: " + r2);
    addLine(L"su -c id: " + r3);

    bool hasRoot = (r1.find(L"uid=0") != std::wstring::npos) || (r3.find(L"uid=0") != std::wstring::npos);
    addLine(hasRoot ? L"=> ĐÃ có root" : L"=> CHƯA có root");
    log_line(hasRoot ? L"[Root] kiểm tra: ĐÃ có root" : L"[Root] kiểm tra: CHƯA có root");
}

static void OnRootVerify() {
    auto ro = current_adb_runner();
    if (!ro.has_value()) { log_line(L"[Root] chưa chọn ADB binary"); return; }
    AdbRunner adb = *ro;

    std::wstring r1 = adb.shell(L"id");
    std::wstring r3 = adb.shell(L"su -c id");
    bool hasRoot = (r1.find(L"uid=0") != std::wstring::npos) || (r3.find(L"uid=0") != std::wstring::npos);

    SendMessageW(g.rootList, LB_RESETCONTENT, 0, 0);
    SendMessageW(g.rootList, LB_ADDSTRING, 0, (LPARAM)(L"id: " + r1).c_str());
    SendMessageW(g.rootList, LB_ADDSTRING, 0, (LPARAM)(L"su -c id: " + r3).c_str());

    if (hasRoot) {
        SendMessageW(g.rootList, LB_ADDSTRING, 0, (LPARAM)L"=> Xác minh: ĐÃ có root");
        log_line(L"[Root] xác minh: ĐÃ có root");
    } else {
        SendMessageW(g.rootList, LB_ADDSTRING, 0, (LPARAM)L"=> Xác minh: CHƯA có root");
        SendMessageW(g.rootList, LB_ADDSTRING, 0,
            (LPARAM)L"Gợi ý: BlueStacks MSI có bản khoá root — thử tắt hẳn rồi khởi động lại máy, hoặc cài bản BlueStacks khác.");
        log_line(L"[Root] xác minh: CHƯA có root");
    }
}

// ===================== Info tab handlers =====================
static void OnInfoLoad() {
    std::wstring text =
        L"MOUSE FORGE — Panel tinh chỉnh BlueStacks\r\n\r\n"
        L"Chức năng:\r\n"
        L"- Sửa file cấu hình bluestacks.conf (backup/restore an toàn)\r\n"
        L"- Kết nối ADB, áp các Android setting có tác dụng thật\r\n"
        L"- Bật/tắt root qua config, xác minh root bằng ADB\r\n"
        L"- Đặt priority HIGH cho tiến trình BlueStacks trên host\r\n\r\n"
        L"GIỚI HẠN TRUNG THỰC:\r\n"
        L"- Không hứa chống rung tâm hay lố tâm FreeFire.\r\n"
        L"- Không chèn sensi vào engine Unity của game.\r\n"
        L"- Không dùng key rác như touch.pressure.scale hay touch.size.scale.\r\n"
        L"- Giảm delay tối đa chỉ khoảng 3-8ms ở tầng guest Android.\r\n"
        L"- Root chỉ để tool ghi được governor CPU và scheduler, không phải để aim chính xác hơn.\r\n"
        L"- Bật root có thể làm FreeFire phát hiện và hạn chế tài khoản.\r\n\r\n"
        L"Hướng dẫn tối ưu sâu:\r\n"
        L"1) Backup config trước khi sửa.\r\n"
        L"2) Áp các key an toàn ở tab Tối ưu.\r\n"
        L"3) Nếu cần governor/scheduler, phải có root thật (tab Root).\r\n"
        L"4) Đặt priority HIGH cho HD-Player.exe nếu máy host yếu.\r\n";

    SetWindowTextW(g.infoText, text.c_str());
    log_line(L"[Info] đã nạp thông tin");
}

static void OnInfoPriority() {
    if (!g.admin) {
        MessageBoxW(g.hMain, L"Cần quyền Administrator để đổi priority.", L"Lỗi", MB_OK | MB_ICONERROR);
        log_line(L"[Priority] thiếu quyền admin");
        return;
    }
    auto pids = find_bs_process(L"HD-Player.exe");
    if (pids.empty()) {
        log_line(L"[Priority] không tìm thấy HD-Player.exe đang chạy");
        return;
    }
    int okCount = 0, failCount = 0;
    for (DWORD pid : pids) {
        HANDLE hp = OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
        if (!hp) { failCount++; continue; }
        BOOL ok = SetPriorityClass(hp, HIGH_PRIORITY_CLASS);
        if (ok) okCount++; else failCount++;
        CloseHandle(hp);
    }
    log_line(L"[Priority] đã đặt HIGH cho " + std::to_wstring(okCount) + L" tiến trình, thất bại " + std::to_wstring(failCount));
}

static void OnInfoClearLog() {
    SetWindowTextW(g.hLog, L"");
}

// ===================== Preset tab handlers =====================
static fs::path presets_dir() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    fs::path exeDir = fs::path(exePath).parent_path();
    fs::path dir = exeDir / L"presets";
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

struct OptCheckState {
    bool anim, hwui, awake, finish, gov, sched, haptic, timeout, rotation;
};

static OptCheckState read_opt_checks() {
    OptCheckState s{};
    s.anim     = SendMessageW(g.optAnim, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.hwui     = SendMessageW(g.optHwui, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.awake    = SendMessageW(g.optAwake, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.finish   = SendMessageW(g.optFinish, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.gov      = SendMessageW(g.optGov, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.sched    = SendMessageW(g.optSched, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.haptic   = SendMessageW(g.optHaptic, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.timeout  = SendMessageW(g.optTimeout, BM_GETCHECK, 0, 0) == BST_CHECKED;
    s.rotation = SendMessageW(g.optRotation, BM_GETCHECK, 0, 0) == BST_CHECKED;
    return s;
}

static void apply_opt_checks(const OptCheckState& s) {
    SendMessageW(g.optAnim, BM_SETCHECK, s.anim ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optHwui, BM_SETCHECK, s.hwui ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optAwake, BM_SETCHECK, s.awake ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optFinish, BM_SETCHECK, s.finish ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optGov, BM_SETCHECK, s.gov ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optSched, BM_SETCHECK, s.sched ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optHaptic, BM_SETCHECK, s.haptic ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optTimeout, BM_SETCHECK, s.timeout ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(g.optRotation, BM_SETCHECK, s.rotation ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void RefreshPresetCombo() {
    g.presetFiles.clear();
    fs::path dir = presets_dir();
    std::error_code ec;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (entry.is_regular_file() && entry.path().extension() == L".txt") {
            g.presetFiles.push_back(entry.path());
        }
    }
    SendMessageW(g.presetCombo, CB_RESETCONTENT, 0, 0);
    for (auto& p : g.presetFiles) {
        SendMessageW(g.presetCombo, CB_ADDSTRING, 0, (LPARAM)p.stem().wstring().c_str());
    }
    if (!g.presetFiles.empty()) SendMessageW(g.presetCombo, CB_SETCURSEL, 0, 0);
    SendMessageW(g.presetList, LB_RESETCONTENT, 0, 0);
}

static void FillPresetList() {
    int idx = (int)SendMessageW(g.presetCombo, CB_GETCURSEL, 0, 0);
    SendMessageW(g.presetList, LB_RESETCONTENT, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.presetFiles.size()) return;
    std::ifstream f(g.presetFiles[idx], std::ios::binary);
    if (!f.is_open()) return;
    std::ostringstream ss;
    ss << f.rdbuf();
    std::wstring content = to_w(ss.str());
    std::wstringstream wss(content);
    std::wstring line;
    while (std::getline(wss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) SendMessageW(g.presetList, LB_ADDSTRING, 0, (LPARAM)line.c_str());
    }
}

static void OnPresetSave() {
    wchar_t nameBuf[256];
    GetWindowTextW(g.presetName, nameBuf, 256);
    std::wstring name = trim(nameBuf);
    if (name.empty()) {
        MessageBoxW(g.hMain, L"Nhập tên preset trước khi lưu.", L"Lỗi", MB_OK | MB_ICONERROR);
        return;
    }
    name = safe_name(name);

    OptCheckState s = read_opt_checks();
    std::wstringstream out;
    out << L"anim=" << (s.anim ? 1 : 0) << L"\n";
    out << L"hwui=" << (s.hwui ? 1 : 0) << L"\n";
    out << L"awake=" << (s.awake ? 1 : 0) << L"\n";
    out << L"finish=" << (s.finish ? 1 : 0) << L"\n";
    out << L"gov=" << (s.gov ? 1 : 0) << L"\n";
    out << L"sched=" << (s.sched ? 1 : 0) << L"\n";
    out << L"haptic=" << (s.haptic ? 1 : 0) << L"\n";
    out << L"timeout=" << (s.timeout ? 1 : 0) << L"\n";
    out << L"rotation=" << (s.rotation ? 1 : 0) << L"\n";

    fs::path dest = presets_dir() / (name + L".txt");
    std::string utf8 = to_a(out.str());
    std::ofstream of(dest, std::ios::binary | std::ios::trunc);
    if (!of.is_open()) {
        log_line(L"[Preset] không ghi được file " + dest.wstring());
        return;
    }
    of.write(utf8.data(), (std::streamsize)utf8.size());
    of.close();

    log_line(L"[Preset] đã lưu preset \"" + name + L"\"");
    RefreshPresetCombo();
}

static void OnPresetLoad() {
    int idx = (int)SendMessageW(g.presetCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.presetFiles.size()) {
        log_line(L"[Preset] chưa chọn preset");
        return;
    }
    std::ifstream f(g.presetFiles[idx], std::ios::binary);
    if (!f.is_open()) { log_line(L"[Preset] không đọc được file preset"); return; }
    std::ostringstream ss;
    ss << f.rdbuf();
    std::wstring content = to_w(ss.str());

    std::map<std::wstring, bool> kv;
    std::wstringstream wss(content);
    std::wstring line;
    while (std::getline(wss, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;
        std::wstring k = line.substr(0, eq);
        std::wstring v = line.substr(eq + 1);
        kv[k] = (v == L"1");
    }

    OptCheckState s = read_opt_checks();
    if (kv.count(L"anim")) s.anim = kv[L"anim"];
    if (kv.count(L"hwui")) s.hwui = kv[L"hwui"];
    if (kv.count(L"awake")) s.awake = kv[L"awake"];
    if (kv.count(L"finish")) s.finish = kv[L"finish"];
    if (kv.count(L"gov")) s.gov = kv[L"gov"];
    if (kv.count(L"sched")) s.sched = kv[L"sched"];
    if (kv.count(L"haptic")) s.haptic = kv[L"haptic"];
    if (kv.count(L"timeout")) s.timeout = kv[L"timeout"];
    if (kv.count(L"rotation")) s.rotation = kv[L"rotation"];
    apply_opt_checks(s);

    log_line(L"[Preset] đã nạp preset \"" + g.presetFiles[idx].stem().wstring() + L"\" vào tab Tối ưu. Sang tab Tối ưu và bấm \"Quét prop + tối ưu\" để áp dụng.");
    FillPresetList();
}

static void OnPresetDelete() {
    int idx = (int)SendMessageW(g.presetCombo, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR || idx < 0 || (size_t)idx >= g.presetFiles.size()) {
        log_line(L"[Preset] chưa chọn preset");
        return;
    }
    std::wstring name = g.presetFiles[idx].stem().wstring();
    int r = MessageBoxW(g.hMain, (L"Xoá preset \"" + name + L"\"?").c_str(), L"Xác nhận", MB_YESNO | MB_ICONWARNING);
    if (r != IDYES) return;

    std::error_code ec;
    fs::remove(g.presetFiles[idx], ec);
    if (ec) log_line(L"[Preset] xoá thất bại: " + name);
    else log_line(L"[Preset] đã xoá preset \"" + name + L"\"");
    RefreshPresetCombo();
}

// ===================== Mouse tab handlers (Windows thật, qua SystemParametersInfo) =====================
static void UpdateMouseLabelsFromTrackbars() {
    int speed = (int)SendMessageW(g.mouseSpeedTrack, TBM_GETPOS, 0, 0);
    SetWindowTextW(g.mouseSpeedVal, std::to_wstring(speed).c_str());

    int dbl = (int)SendMessageW(g.mouseDblTrack, TBM_GETPOS, 0, 0);
    SetWindowTextW(g.mouseDblVal, std::to_wstring(dbl).c_str());

    int wheel = (int)SendMessageW(g.mouseWheelTrack, TBM_GETPOS, 0, 0);
    SetWindowTextW(g.mouseWheelVal, std::to_wstring(wheel).c_str());

    int trails = (int)SendMessageW(g.mouseTrailsTrack, TBM_GETPOS, 0, 0);
    SetWindowTextW(g.mouseTrailsVal, std::to_wstring(trails).c_str());
}

static void OnMouseRefresh() {
    int speed = 10;
    SystemParametersInfoW(SPI_GETMOUSESPEED, 0, &speed, 0);
    SendMessageW(g.mouseSpeedTrack, TBM_SETPOS, TRUE, speed);

    INT mouseParams[3] = { 6, 10, 1 };
    SystemParametersInfoW(SPI_GETMOUSE, 0, mouseParams, 0);
    SendMessageW(g.mousePrecision, BM_SETCHECK, mouseParams[2] != 0 ? BST_CHECKED : BST_UNCHECKED, 0);

    UINT dbl = GetDoubleClickTime();
    if (dbl < 200) dbl = 200;
    if (dbl > 900) dbl = 900;
    SendMessageW(g.mouseDblTrack, TBM_SETPOS, TRUE, (LPARAM)dbl);

    UINT wheelLines = 3;
    SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
    if (wheelLines > 20) wheelLines = 20;
    SendMessageW(g.mouseWheelTrack, TBM_SETPOS, TRUE, (LPARAM)wheelLines);

    UINT trails = 0;
    SystemParametersInfoW(SPI_GETMOUSETRAILS, 0, &trails, 0);
    if (trails > 10) trails = 10;
    SendMessageW(g.mouseTrailsTrack, TBM_SETPOS, TRUE, (LPARAM)trails);

    UpdateMouseLabelsFromTrackbars();
    log_line(L"[Chuột] đã đọc cấu hình chuột hiện tại từ Windows");
}

static void OnMouseApply() {
    int speed = (int)SendMessageW(g.mouseSpeedTrack, TBM_GETPOS, 0, 0);
    SystemParametersInfoW(SPI_SETMOUSESPEED, 0, (PVOID)(INT_PTR)speed, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    bool precision = SendMessageW(g.mousePrecision, BM_GETCHECK, 0, 0) == BST_CHECKED;
    INT mouseParams[3] = { precision ? 6 : 0, precision ? 10 : 0, precision ? 1 : 0 };
    SystemParametersInfoW(SPI_SETMOUSE, 0, mouseParams, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    UINT dbl = (UINT)SendMessageW(g.mouseDblTrack, TBM_GETPOS, 0, 0);
    SetDoubleClickTime(dbl);

    int wheel = (int)SendMessageW(g.mouseWheelTrack, TBM_GETPOS, 0, 0);
    SystemParametersInfoW(SPI_SETWHEELSCROLLLINES, (UINT)wheel, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    int trails = (int)SendMessageW(g.mouseTrailsTrack, TBM_GETPOS, 0, 0);
    SystemParametersInfoW(SPI_SETMOUSETRAILS, (UINT)trails, nullptr, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);

    log_line(L"[Chuột] đã áp dụng cấu hình chuột vào Windows (speed=" + std::to_wstring(speed) +
        L", precision=" + (precision ? L"on" : L"off") +
        L", dblclick=" + std::to_wstring(dbl) + L"ms, wheel=" + std::to_wstring(wheel) +
        L", trails=" + std::to_wstring(trails) + L")");
}

static void OnMouseReset() {
    SendMessageW(g.mouseSpeedTrack, TBM_SETPOS, TRUE, 10);
    SendMessageW(g.mousePrecision, BM_SETCHECK, BST_CHECKED, 0);
    SendMessageW(g.mouseDblTrack, TBM_SETPOS, TRUE, 500);
    SendMessageW(g.mouseWheelTrack, TBM_SETPOS, TRUE, 3);
    SendMessageW(g.mouseTrailsTrack, TBM_SETPOS, TRUE, 0);
    UpdateMouseLabelsFromTrackbars();
    log_line(L"[Chuột] đã nạp giá trị mặc định của Windows (chưa áp dụng, bấm \"Áp dụng vào Windows\" để lưu thật)");
}

// ===================== Engine tab handlers =====================
static std::wstring FormatFixed(double v, int decimals) {
    if (decimals <= 0) return std::to_wstring((long long)std::llround(v));
    double mul = std::pow(10.0, decimals);
    long long scaled = (long long)std::llround(v * mul);
    bool neg = scaled < 0;
    if (neg) scaled = -scaled;
    long long intPart = scaled / (long long)mul;
    long long frac = scaled % (long long)mul;
    std::wstring fracStr = std::to_wstring(frac);
    while ((int)fracStr.size() < decimals) fracStr = L"0" + fracStr;
    return (neg ? L"-" : L"") + std::to_wstring(intPart) + L"." + fracStr;
}

// Đọc toàn bộ control của tab Engine -> đổ vào g_engine (áp dụng NGAY, kể cả khi engine đang chạy)
// và cập nhật nhãn số hiển thị cạnh mỗi thanh trượt.
static void EngineSyncFromControls() {
    // EN_CHANGE có thể bắn khi control còn đang được tạo -> chưa có HWND
    if (!g.engEmaTrack || !g.engVelTrack || !g.engMicroTrack || !g.engAccelTrack ||
        !g.engOffsetTrack || !g.engCapTrack || !g.engRefDpi || !g.engCurDpi || !g.engCapVal) return;
    double ema = (double)SendMessageW(g.engEmaTrack, TBM_GETPOS, 0, 0) / 10.0;
    int velw = (int)SendMessageW(g.engVelTrack, TBM_GETPOS, 0, 0);
    double micro = (double)SendMessageW(g.engMicroTrack, TBM_GETPOS, 0, 0) / 10.0;
    double accel = (double)SendMessageW(g.engAccelTrack, TBM_GETPOS, 0, 0) / 100.0;
    double offset = (double)SendMessageW(g.engOffsetTrack, TBM_GETPOS, 0, 0) / 10.0;
    double cap = (double)SendMessageW(g.engCapTrack, TBM_GETPOS, 0, 0) / 100.0;

    g_engine.emaFastMs = ema;
    g_engine.velWindow = velw;
    g_engine.microThresh = micro;
    g_engine.accel = accel;
    g_engine.accelOffset = offset;
    g_engine.accelCap = cap;

    SetWindowTextW(g.engEmaVal, FormatFixed(ema, 1).c_str());
    SetWindowTextW(g.engVelVal, std::to_wstring(velw).c_str());
    SetWindowTextW(g.engMicroVal, FormatFixed(micro, 1).c_str());
    SetWindowTextW(g.engAccelVal, FormatFixed(accel, 2).c_str());
    SetWindowTextW(g.engOffsetVal, FormatFixed(offset, 1).c_str());
    SetWindowTextW(g.engCapVal, FormatFixed(cap, 2).c_str());

    wchar_t dpiBuf[16] = {};
    GetWindowTextW(g.engRefDpi, dpiBuf, 16);
    double refDpi = _wtof(dpiBuf);
    if (refDpi >= 100.0 && refDpi <= 32000.0) g_engine.refDpi = refDpi;
    GetWindowTextW(g.engCurDpi, dpiBuf, 16);
    double curDpi = _wtof(dpiBuf);
    if (curDpi >= 100.0 && curDpi <= 32000.0) g_engine.curDpi = curDpi;
}

static void UpdateEngineUi() {
    int st = g_engine.enabled.load() ? 1 : (g_engine.tripped ? 2 : 0);
    g.engState = st;
    if (g.engEnable) SendMessageW(g.engEnable, BM_SETCHECK, st == 1 ? BST_CHECKED : BST_UNCHECKED, 0);
    if (g.engStatus) SetWindowTextW(g.engStatus, st == 1 ? L"ĐANG BẬT" : (st == 2 ? L"ĐÃ TỰ NGẮT" : L"TẮT"));
    if (g.qsToggle) {
        SetWindowTextW(g.qsToggle, st == 1 ? L"TẮT ENGINE" : L"BẬT ENGINE");
        InvalidateRect(g.qsToggle, nullptr, TRUE);
    }
    if (g.qsStatus) {
        SetWindowTextW(g.qsStatus,
            st == 1 ? L"Đang BẬT: chuột đang đi qua bộ lọc của Mouse Forge." :
            (st == 2 ? L"Đã TỰ NGẮT vì thấy chuột bất thường. Kiểm tra Cur DPI ở tab Engine rồi bật lại." :
                       L"Đang TẮT: chuột chạy như Windows bình thường."));
    }
    if (g.hMain) {
        RECT hr = { 0, 0, 900, kTabTop };
        InvalidateRect(g.hMain, &hr, TRUE);
    }
}

// Điểm duy nhất bật/tắt engine (nút Bắt đầu, checkbox tab Engine, phím tắt, bộ bảo vệ, thoát app).
static void EngineSetEnabled(bool on, const std::wstring& why, bool trippedByGuard = false) {
    if (on) {
        if (g_engine.enabled.load()) { UpdateEngineUi(); return; }
        EngineSyncFromControls();
        if (MouseEngine_Register(g.hMain)) {
            g_engine.tripped = false;
            g_engine.enabled = true;
            SetTimer(g.hMain, kTimerFlush, 10, nullptr);
            log_line(L"[Engine] đã BẬT. " + why);
        } else {
            log_line(L"[Engine] KHÔNG bật được (RegisterRawInputDevices thất bại)");
        }
    } else {
        g_engine.enabled = false;
        KillTimer(g.hMain, kTimerFlush);
        if (g_engine.rawRegistered || g_engine.savedOs) MouseEngine_Unregister(g.hMain);
        g_engine.tripped = trippedByGuard;
        log_line(L"[Engine] đã TẮT. " + why);
    }
    UpdateEngineUi();
}

static void OnEngToggle() {
    bool wantOn = SendMessageW(g.engEnable, BM_GETCHECK, 0, 0) == BST_CHECKED;
    EngineSetEnabled(wantOn, L"(từ tab Engine)");
}

// Đặt vị trí các thanh trượt Engine từ giá trị thật (dùng llround để không bị lệch 1 nấc do sai số số thực)
static void SetEngineSliders(double ema, int vel, double micro, double accel, double offset, double cap) {
    SendMessageW(g.engEmaTrack,    TBM_SETPOS, TRUE, (LPARAM)std::llround(ema * 10.0));
    SendMessageW(g.engVelTrack,    TBM_SETPOS, TRUE, (LPARAM)vel);
    SendMessageW(g.engMicroTrack,  TBM_SETPOS, TRUE, (LPARAM)std::llround(micro * 10.0));
    SendMessageW(g.engAccelTrack,  TBM_SETPOS, TRUE, (LPARAM)std::llround(accel * 100.0));
    SendMessageW(g.engOffsetTrack, TBM_SETPOS, TRUE, (LPARAM)std::llround(offset * 10.0));
    SendMessageW(g.engCapTrack,    TBM_SETPOS, TRUE, (LPARAM)std::llround(cap * 100.0));
}

// Kiểu chuột dựng sẵn cho tab "Bắt đầu" (mô tả bằng lời thường, không cần hiểu thuật ngữ)
struct EnginePreset { const wchar_t* name; double ema; int vel; double micro; double accel; double offset; double cap; };
static const EnginePreset kEnginePresets[4] = {
    { L"Chính xác 1:1",        0.0, 4, 2.0, 0.00, 5.0, 1.00 },
    { L"Mượt & ổn định",       3.0, 4, 2.0, 0.00, 5.0, 1.00 },
    { L"Cân bằng (khuyên dùng)", 2.0, 4, 2.0, 0.03, 3.0, 1.50 },
    { L"Xoay nhanh",           1.5, 3, 1.5, 0.06, 2.0, 2.00 },
};

static void OnQuickPreset(int idx) {
    if (idx < 0 || idx > 4) return;
    if (idx == 4) {
        EngineSetEnabled(false, L"(về mặc định Windows)");
        return;
    }
    const EnginePreset& p = kEnginePresets[idx];
    SetEngineSliders(p.ema, p.vel, p.micro, p.accel, p.offset, p.cap);
    EngineSyncFromControls();
    log_line(std::wstring(L"[Bắt đầu] đã chọn kiểu \"") + p.name + L"\".");
    if (!g_engine.enabled.load()) log_line(L"[Bắt đầu] bấm BẬT ENGINE để dùng kiểu này.");
}

// Đồng bộ toàn bộ UI Engine theo g_engine (dùng khi nạp cài đặt đã lưu lúc khởi động)
static void SyncEngineUiFromState() {
    double refD = g_engine.refDpi, curD = g_engine.curDpi;   // chụp trước: SetWindowText sẽ bắn EN_CHANGE và ghi đè g_engine
    bool wd = g_engine.watchdogOn;
    SetEngineSliders(g_engine.emaFastMs, g_engine.velWindow, g_engine.microThresh,
                     g_engine.accel, g_engine.accelOffset, g_engine.accelCap);
    SetWindowTextW(g.engRefDpi, std::to_wstring((long long)std::llround(refD)).c_str());
    SetWindowTextW(g.engCurDpi, std::to_wstring((long long)std::llround(curD)).c_str());
    g_engine.watchdogOn = wd;
    if (g.testWatchdog) SendMessageW(g.testWatchdog, BM_SETCHECK, wd ? BST_CHECKED : BST_UNCHECKED, 0);
    EngineSyncFromControls();
}

// ===================== Cài đặt lưu (theme + thông số engine) =====================
static fs::path settings_path() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    return fs::path(exe).parent_path() / L"MouseForge.ini";
}
static double ClampD(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void LoadSettings() {
    std::ifstream f(settings_path());
    if (!f.is_open()) return;
    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (!v.empty() && v.back() == '\r') v.pop_back();
        double d = 0;
        try { d = std::stod(v); } catch (...) { continue; }
        if (k == "dark") g_dark = d != 0.0;
        else if (k == "ema") g_engine.emaFastMs = ClampD(d, 0.0, 10.0);
        else if (k == "vel") g_engine.velWindow = (int)ClampD(d, 1.0, 10.0);
        else if (k == "micro") g_engine.microThresh = ClampD(d, 0.0, 10.0);
        else if (k == "accel") g_engine.accel = ClampD(d, 0.0, 0.5);
        else if (k == "offset") g_engine.accelOffset = ClampD(d, 0.0, 20.0);
        else if (k == "cap") g_engine.accelCap = ClampD(d, 1.0, 3.0);
        else if (k == "ref") g_engine.refDpi = ClampD(d, 100.0, 32000.0);
        else if (k == "cur") g_engine.curDpi = ClampD(d, 100.0, 32000.0);
        else if (k == "watchdog") g_engine.watchdogOn = d != 0.0;
    }
}

static void SaveSettings() {
    std::ofstream f(settings_path(), std::ios::trunc);
    if (!f.is_open()) return;
    f << "dark=" << (g_dark ? 1 : 0) << "\n";
    f << "ema=" << g_engine.emaFastMs << "\n";
    f << "vel=" << g_engine.velWindow << "\n";
    f << "micro=" << g_engine.microThresh << "\n";
    f << "accel=" << g_engine.accel << "\n";
    f << "offset=" << g_engine.accelOffset << "\n";
    f << "cap=" << g_engine.accelCap << "\n";
    f << "ref=" << g_engine.refDpi << "\n";
    f << "cur=" << g_engine.curDpi << "\n";
    f << "watchdog=" << (g_engine.watchdogOn ? 1 : 0) << "\n";
}

// ===================== Tab Test: bảng đo trực tiếp =====================
static double g_lastStatMs = -1.0;
static std::wstring g_lastTestTxt[7];
static void SetTestText(int i, HWND h, const std::wstring& t) {
    if (!h) return;
    if (g_lastTestTxt[i] == t) return;   // chỉ cập nhật khi đổi, tránh nhấp nháy
    g_lastTestTxt[i] = t;
    SetWindowTextW(h, t.c_str());
}

static void UpdateTestTab() {
    if (!g.testVerdict) return;
    double now = NowMs();
    double win = (g_lastStatMs < 0.0) ? 250.0 : now - g_lastStatMs;
    if (win < 50.0) win = 50.0;
    g_lastStatMs = now;

    double inRate = g_engine.stIn * 1000.0 / win;
    double outRate = g_engine.stOut * 1000.0 / win;
    double evRate = (double)g_engine.stEvents * 1000.0 / win;
    double ratio = inRate > 1.0 ? outRate / inRate : 0.0;
    g_engine.stIn = g_engine.stOut = 0.0;
    g_engine.stEvents = 0;

    bool on = g_engine.enabled.load();
    SetTestText(0, g.testVals[0], FormatFixed(inRate, 0) + L" counts/giây");
    SetTestText(1, g.testVals[1], FormatFixed(outRate, 0) + L" counts/giây");
    SetTestText(2, g.testVals[2], inRate > 1.0 ? FormatFixed(ratio, 2) + L"x" : std::wstring(L"—"));
    SetTestText(3, g.testVals[3], FormatFixed(g_engine.stMult, 2) + L"x");
    SetTestText(4, g.testVals[4], FormatFixed(g_engine.stVel, 1) + L" counts/ms");
    SetTestText(5, g.testVals[5], FormatFixed(evRate, 0) + L" Hz");

    int level = 3;
    std::wstring vt;
    if (g.engState == 2) { level = 2; vt = L"ĐÃ TỰ NGẮT: phát hiện chuột chạy bất thường."; }
    else if (!on) { level = 3; vt = L"Engine đang tắt. Bật ở tab \"Bắt đầu\" để đo."; }
    else if (inRate < 20.0) { level = 3; vt = L"Hãy di chuột để bắt đầu đo..."; }
    else if (ratio > g_engine.accelCap + 1.0) { level = 2; vt = L"NGUY HIỂM: chuột ra nhanh hơn chuột thật nhiều lần. Hãy tắt engine!"; }
    else if (ratio > 1.6) { level = 1; vt = L"Đang tăng tốc mạnh (bình thường nếu bạn vung nhanh)."; }
    else { level = 0; vt = L"BÌNH THƯỜNG: chuột ra gần bằng chuột vào."; }
    if (level != g.verdictLevel) {
        g.verdictLevel = level;
        InvalidateRect(g.testVerdict, nullptr, TRUE);
    }
    SetTestText(6, g.testVerdict, vt);
}

static void OnEngReset() {
    SendMessageW(g.engEmaTrack, TBM_SETPOS, TRUE, 20);     // 2.0
    SendMessageW(g.engVelTrack, TBM_SETPOS, TRUE, 4);
    SendMessageW(g.engMicroTrack, TBM_SETPOS, TRUE, 20);   // 2.0
    SendMessageW(g.engAccelTrack, TBM_SETPOS, TRUE, 4);    // 0.04
    SendMessageW(g.engOffsetTrack, TBM_SETPOS, TRUE, 50);  // 5.0
    SendMessageW(g.engCapTrack, TBM_SETPOS, TRUE, 140);    // 1.40
    SetWindowTextW(g.engRefDpi, L"800");
    SetWindowTextW(g.engCurDpi, L"800");
    EngineSyncFromControls();
    log_line(L"[Engine] đã khôi phục thông số mặc định (EMA=2.0, VelWin=4, Micro=2.0, Accel=0.04, Offset=5.0, Cap=1.40, DPI=800/800)");
}

// ===================== Tab switching =====================
static void ShowTabPage(int idx) {
    g.curTab = idx;
    for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) {
        int cmd = (i == idx) ? SW_SHOW : SW_HIDE;
        for (HWND h : g.tabControls[kTabContentIdx[i]]) {
            if (h) ShowWindow(h, cmd);
        }
        HWND tb = GetDlgItem(g.hMain, IDC_TABBTN_BASE + i);
        if (tb) InvalidateRect(tb, nullptr, TRUE);
    }
}

// ===================== UI construction =====================
static void CreateMainControls(HWND hwnd) {
    g.hFontUI = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g.hFontMono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");

    g.hFontBold = CreateFontW(-14, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g.hFontTitle = CreateFontW(-26, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_engineNotifyWnd = hwnd;

    // ---- Header: tên app + chip trạng thái được vẽ trong WM_PAINT; ở đây chỉ có nút đổi giao diện ----
    g.themeToggle = CreateWindowExW(0, L"BUTTON", g_dark ? L"Chế độ sáng" : L"Chế độ tối",
        WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
        774, kHeaderTop + 10, 96, 28, hwnd, (HMENU)(INT_PTR)IDC_THEME_TOGGLE, g.hInst, nullptr);

    // ---- Thanh tab tự vẽ (nút BS_OWNERDRAW) ----
    {
        HDC hdc = GetDC(hwnd);
        HFONT oldF = (HFONT)SelectObject(hdc, g.hFontBold);
        int widths[AppGlobals::TAB_COUNT];
        int total = 0;
        for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) {
            SIZE sz{};
            GetTextExtentPoint32W(hdc, kTabNames[i], lstrlenW(kTabNames[i]), &sz);
            widths[i] = sz.cx + 26;
            total += widths[i];
        }
        SelectObject(hdc, oldF);
        ReleaseDC(hwnd, hdc);
        if (total > 860) {
            double f = 860.0 / (double)total;
            for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) widths[i] = (int)(widths[i] * f);
        }
        int x = 10;
        for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) {
            CreateWindowExW(0, L"BUTTON", kTabNames[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
                x, kTabTop, widths[i], kTabBarH, hwnd, (HMENU)(INT_PTR)(IDC_TABBTN_BASE + i), g.hInst, nullptr);
            x += widths[i];
        }
    }

    int px = 20, py = kContentTop;

    // ---- Tab 1: Config ----
    HWND cfgLbl1 = CreateWindowExW(0, L"STATIC", L"File config:", WS_CHILD, px, py, 100, 20, hwnd, nullptr, g.hInst, nullptr);
    g.cfgCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        px + 100, py - 2, 590, 200, hwnd, (HMENU)(INT_PTR)IDC_CFG_COMBO, g.hInst, nullptr);
    g.cfgRescan = CreateWindowExW(0, L"BUTTON", L"Quét lại", WS_CHILD | BS_OWNERDRAW,
        px + 700, py - 3, 110, 26, hwnd, (HMENU)(INT_PTR)IDC_CFG_RESCAN, g.hInst, nullptr);
    g.cfgEditKey = CreateWindowExW(0, L"BUTTON", L"Sửa key", WS_CHILD | BS_OWNERDRAW,
        px, py + 34, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_CFG_EDITKEY, g.hInst, nullptr);
    g.cfgBackup = CreateWindowExW(0, L"BUTTON", L"Backup", WS_CHILD | BS_OWNERDRAW,
        px + 120, py + 34, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_CFG_BACKUP, g.hInst, nullptr);
    g.cfgRestore = CreateWindowExW(0, L"BUTTON", L"Restore", WS_CHILD | BS_OWNERDRAW,
        px + 240, py + 34, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_CFG_RESTORE, g.hInst, nullptr);
    g.cfgList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, py + 74, 800, 360, hwnd, (HMENU)(INT_PTR)IDC_CFG_LIST, g.hInst, nullptr);

    g.tabControls[0] = { cfgLbl1, g.cfgCombo, g.cfgRescan, g.cfgEditKey, g.cfgBackup, g.cfgRestore, g.cfgList };
    for (HWND h : g.tabControls[0]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.cfgList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Tab 2: ADB ----
    HWND adbLbl1 = CreateWindowExW(0, L"STATIC", L"HD-Adb.exe:", WS_CHILD, px, py, 100, 20, hwnd, nullptr, g.hInst, nullptr);
    g.adbCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        px + 100, py - 2, 500, 200, hwnd, (HMENU)(INT_PTR)IDC_ADB_COMBO, g.hInst, nullptr);
    g.adbSerialLbl = CreateWindowExW(0, L"STATIC", L"Serial:", WS_CHILD,
        px, py + 36, 100, 20, hwnd, (HMENU)(INT_PTR)IDC_ADB_SERIAL_LBL, g.hInst, nullptr);
    g.adbSerial = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"127.0.0.1:5555", WS_CHILD | WS_BORDER,
        px + 100, py + 34, 200, 24, hwnd, (HMENU)(INT_PTR)IDC_ADB_SERIAL, g.hInst, nullptr);
    g.adbConnect = CreateWindowExW(0, L"BUTTON", L"Dò && nạp", WS_CHILD | BS_OWNERDRAW,
        px + 320, py + 33, 120, 28, hwnd, (HMENU)(INT_PTR)IDC_ADB_CONNECT, g.hInst, nullptr);
    HWND adbLbl2 = CreateWindowExW(0, L"STATIC", L"Thông tin thiết bị:", WS_CHILD, px, py + 72, 200, 20, hwnd, nullptr, g.hInst, nullptr);
    HWND adbLbl3 = CreateWindowExW(0, L"STATIC", L"Key Android:", WS_CHILD, px + 410, py + 72, 200, 20, hwnd, nullptr, g.hInst, nullptr);
    g.adbInfoList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_CHILD | WS_VSCROLL | LBS_NOTIFY,
        px, py + 96, 390, 340, hwnd, (HMENU)(INT_PTR)IDC_ADB_INFOLIST, g.hInst, nullptr);
    g.adbKeyList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px + 410, py + 96, 410, 340, hwnd, (HMENU)(INT_PTR)IDC_ADB_KEYLIST, g.hInst, nullptr);

    g.tabControls[1] = { adbLbl1, g.adbCombo, g.adbSerialLbl, g.adbSerial, g.adbConnect, adbLbl2, adbLbl3, g.adbInfoList, g.adbKeyList };
    for (HWND h : g.tabControls[1]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    // ---- Tab 3: Optimize ----
    int oy = py;
    g.optAnim = CreateWindowExW(0, L"BUTTON", L"animation scale", WS_CHILD | BS_AUTOCHECKBOX, px, oy, 220, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_ANIM, g.hInst, nullptr);
    g.optHwui = CreateWindowExW(0, L"BUTTON", L"HWUI profile", WS_CHILD | BS_AUTOCHECKBOX, px, oy + 28, 220, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_HWUI, g.hInst, nullptr);
    g.optAwake = CreateWindowExW(0, L"BUTTON", L"stay awake", WS_CHILD | BS_AUTOCHECKBOX, px, oy + 56, 220, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_AWAKE, g.hInst, nullptr);
    g.optFinish = CreateWindowExW(0, L"BUTTON", L"always finish off", WS_CHILD | BS_AUTOCHECKBOX, px, oy + 84, 220, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_FINISH, g.hInst, nullptr);
    g.optGov = CreateWindowExW(0, L"BUTTON", L"governor CPU (cần root)", WS_CHILD | BS_AUTOCHECKBOX, px + 280, oy, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_GOV, g.hInst, nullptr);
    g.optSched = CreateWindowExW(0, L"BUTTON", L"scheduler (cần root)", WS_CHILD | BS_AUTOCHECKBOX, px + 280, oy + 28, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_SCHED, g.hInst, nullptr);
    g.optHaptic = CreateWindowExW(0, L"BUTTON", L"tắt haptic feedback", WS_CHILD | BS_AUTOCHECKBOX, px + 280, oy + 56, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_HAPTIC, g.hInst, nullptr);
    g.optTimeout = CreateWindowExW(0, L"BUTTON", L"không tự tắt màn hình", WS_CHILD | BS_AUTOCHECKBOX, px + 280, oy + 84, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_TIMEOUT, g.hInst, nullptr);
    g.optRotation = CreateWindowExW(0, L"BUTTON", L"khoá xoay màn hình tự động", WS_CHILD | BS_AUTOCHECKBOX, px + 280, oy + 112, 280, 22, hwnd, (HMENU)(INT_PTR)IDC_OPT_ROTATION, g.hInst, nullptr);
    g.optScan = CreateWindowExW(0, L"BUTTON", L"Quét prop + tối ưu", WS_CHILD | BS_OWNERDRAW, px, oy + 150, 220, 30, hwnd, (HMENU)(INT_PTR)IDC_OPT_SCAN, g.hInst, nullptr);
    g.optList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, oy + 190, 800, 250, hwnd, (HMENU)(INT_PTR)IDC_OPT_LIST, g.hInst, nullptr);

    for (HWND h : { g.optAnim, g.optHwui, g.optAwake, g.optFinish, g.optGov, g.optSched, g.optHaptic, g.optTimeout, g.optRotation }) {
        SendMessageW(h, BM_SETCHECK, BST_CHECKED, 0);
    }
    g.tabControls[2] = { g.optAnim, g.optHwui, g.optAwake, g.optFinish, g.optGov, g.optSched, g.optHaptic, g.optTimeout, g.optRotation, g.optScan, g.optList };
    for (HWND h : g.tabControls[2]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.optList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Tab 4: Root ----
    g.rootCheck = CreateWindowExW(0, L"BUTTON", L"Kiểm tra root", WS_CHILD | BS_OWNERDRAW, px, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_CHECK, g.hInst, nullptr);
    g.rootOn = CreateWindowExW(0, L"BUTTON", L"Bật root qua config", WS_CHILD | BS_OWNERDRAW, px + 160, py, 180, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_ON, g.hInst, nullptr);
    g.rootOff = CreateWindowExW(0, L"BUTTON", L"Tắt root qua config", WS_CHILD | BS_OWNERDRAW, px + 350, py, 180, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_OFF, g.hInst, nullptr);
    g.rootVerify = CreateWindowExW(0, L"BUTTON", L"Xác minh root", WS_CHILD | BS_OWNERDRAW, px + 540, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_VERIFY, g.hInst, nullptr);
    g.rootList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, py + 46, 800, 400, hwnd, (HMENU)(INT_PTR)IDC_ROOT_LIST, g.hInst, nullptr);

    g.tabControls[3] = { g.rootCheck, g.rootOn, g.rootOff, g.rootVerify, g.rootList };
    for (HWND h : g.tabControls[3]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.rootList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Tab 5: Info ----
    g.infoLoad = CreateWindowExW(0, L"BUTTON", L"Nạp thông tin", WS_CHILD | BS_OWNERDRAW, px, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_LOAD, g.hInst, nullptr);
    g.infoPriority = CreateWindowExW(0, L"BUTTON", L"Priority HIGH cho BS", WS_CHILD | BS_OWNERDRAW, px + 160, py, 190, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_PRIORITY, g.hInst, nullptr);
    g.infoClearLog = CreateWindowExW(0, L"BUTTON", L"Xoá log", WS_CHILD | BS_OWNERDRAW, px + 360, py, 120, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_CLEARLOG, g.hInst, nullptr);
    g.infoText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        px, py + 46, 800, 400, hwnd, (HMENU)(INT_PTR)IDC_INFO_TEXT, g.hInst, nullptr);

    g.tabControls[4] = { g.infoLoad, g.infoPriority, g.infoClearLog, g.infoText };
    for (HWND h : g.tabControls[4]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    // ---- Tab 6: Presets ----
    HWND presetLbl1 = CreateWindowExW(0, L"STATIC", L"Preset đã lưu:", WS_CHILD, px, py, 110, 20, hwnd, nullptr, g.hInst, nullptr);
    g.presetCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        px + 110, py - 2, 300, 200, hwnd, (HMENU)(INT_PTR)IDC_PRESET_COMBO, g.hInst, nullptr);
    g.presetLoad = CreateWindowExW(0, L"BUTTON", L"Nạp", WS_CHILD | BS_OWNERDRAW, px + 420, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_LOAD, g.hInst, nullptr);
    g.presetDelete = CreateWindowExW(0, L"BUTTON", L"Xoá", WS_CHILD | BS_OWNERDRAW, px + 520, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_DELETE, g.hInst, nullptr);
    g.presetRefresh = CreateWindowExW(0, L"BUTTON", L"Quét lại", WS_CHILD | BS_OWNERDRAW, px + 620, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_REFRESH, g.hInst, nullptr);

    HWND presetLbl2 = CreateWindowExW(0, L"STATIC", L"Tên preset mới:", WS_CHILD, px, py + 40, 110, 20, hwnd, nullptr, g.hInst, nullptr);
    g.presetName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER,
        px + 110, py + 38, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_PRESET_NAME, g.hInst, nullptr);
    g.presetSave = CreateWindowExW(0, L"BUTTON", L"Lưu lựa chọn hiện tại ở tab Tối ưu thành preset", WS_CHILD | BS_OWNERDRAW,
        px + 420, py + 37, 380, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_SAVE, g.hInst, nullptr);

    g.presetList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, py + 80, 800, 354, hwnd, (HMENU)(INT_PTR)IDC_PRESET_LIST, g.hInst, nullptr);

    g.tabControls[5] = { presetLbl1, g.presetCombo, g.presetLoad, g.presetDelete, g.presetRefresh,
                          presetLbl2, g.presetName, g.presetSave, g.presetList };
    for (HWND h : g.tabControls[5]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.presetList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Tab 7: Chuột (cấu hình chuột thật của Windows qua SystemParametersInfo) ----
    HWND mLbl1 = CreateWindowExW(0, L"STATIC", L"Tốc độ con trỏ (1–20):", WS_CHILD, px, py, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.mouseSpeedTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py - 4, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_SPEED, g.hInst, nullptr);
    SendMessageW(g.mouseSpeedTrack, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
    g.mouseSpeedVal = CreateWindowExW(0, L"STATIC", L"10", WS_CHILD, px + 620, py, 40, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND mLbl2 = CreateWindowExW(0, L"STATIC", L"Độ nhạy double-click (200–900ms):", WS_CHILD, px, py + 40, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.mouseDblTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 36, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_DBLCLICK, g.hInst, nullptr);
    SendMessageW(g.mouseDblTrack, TBM_SETRANGE, TRUE, MAKELPARAM(200, 900));
    SendMessageW(g.mouseDblTrack, TBM_SETLINESIZE, 0, 10);
    g.mouseDblVal = CreateWindowExW(0, L"STATIC", L"500", WS_CHILD, px + 620, py + 40, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND mLbl3 = CreateWindowExW(0, L"STATIC", L"Số dòng cuộn chuột (1–20):", WS_CHILD, px, py + 80, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.mouseWheelTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 76, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_WHEEL, g.hInst, nullptr);
    SendMessageW(g.mouseWheelTrack, TBM_SETRANGE, TRUE, MAKELPARAM(1, 20));
    g.mouseWheelVal = CreateWindowExW(0, L"STATIC", L"3", WS_CHILD, px + 620, py + 80, 40, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND mLbl4 = CreateWindowExW(0, L"STATIC", L"Vệt chuột / mouse trails (0=tắt, 2–10):", WS_CHILD, px, py + 120, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.mouseTrailsTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 116, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_TRAILS, g.hInst, nullptr);
    SendMessageW(g.mouseTrailsTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 10));
    g.mouseTrailsVal = CreateWindowExW(0, L"STATIC", L"0", WS_CHILD, px + 620, py + 120, 40, 20, hwnd, nullptr, g.hInst, nullptr);

    g.mousePrecision = CreateWindowExW(0, L"BUTTON", L"Enhance pointer precision (chống trôi tay khi ngắm chậm)",
        WS_CHILD | BS_AUTOCHECKBOX, px, py + 160, 520, 22, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_PRECISION, g.hInst, nullptr);
    SendMessageW(g.mousePrecision, BM_SETCHECK, BST_CHECKED, 0);

    g.mouseRefresh = CreateWindowExW(0, L"BUTTON", L"Đọc giá trị Windows hiện tại", WS_CHILD | BS_OWNERDRAW,
        px, py + 200, 220, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_REFRESH, g.hInst, nullptr);
    g.mouseApply = CreateWindowExW(0, L"BUTTON", L"Áp dụng vào Windows", WS_CHILD | BS_OWNERDRAW,
        px + 230, py + 200, 220, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_APPLY, g.hInst, nullptr);
    g.mouseReset = CreateWindowExW(0, L"BUTTON", L"Khôi phục mặc định Windows", WS_CHILD | BS_OWNERDRAW,
        px + 460, py + 200, 220, 30, hwnd, (HMENU)(INT_PTR)IDC_MOUSE_RESET, g.hInst, nullptr);

    HWND mNote = CreateWindowExW(0, L"STATIC",
        L"Các giá trị này áp dụng cho con chuột thật trên toàn Windows (giống Control Panel > Mouse), "
        L"không đọc/ghi bộ nhớ tiến trình game và không liên quan tới aim-assist trong game.",
        WS_CHILD | SS_LEFT, px, py + 244, 800, 40, hwnd, nullptr, g.hInst, nullptr);

    g.tabControls[6] = { mLbl1, g.mouseSpeedTrack, g.mouseSpeedVal, mLbl2, g.mouseDblTrack, g.mouseDblVal,
                          mLbl3, g.mouseWheelTrack, g.mouseWheelVal, mLbl4, g.mouseTrailsTrack, g.mouseTrailsVal,
                          g.mousePrecision, g.mouseRefresh, g.mouseApply, g.mouseReset, mNote };
    for (HWND h : g.tabControls[6]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    // ---- Tab 8: Engine (EMA smoothing + custom acceleration curve trên raw input) ----
    g.engEnable = CreateWindowExW(0, L"BUTTON", L"Bật Mouse Engine (EMA smoothing + accel curve tùy chỉnh)",
        WS_CHILD | BS_AUTOCHECKBOX, px, py, 560, 22, hwnd, (HMENU)(INT_PTR)IDC_ENG_ENABLE, g.hInst, nullptr);
    g.engStatus = CreateWindowExW(0, L"STATIC", L"TẮT", WS_CHILD, px + 570, py + 2, 100, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl1 = CreateWindowExW(0, L"STATIC", L"EMA Fast (0.0–10.0 ms):", WS_CHILD, px, py + 36, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engEmaTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 32, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_EMA, g.hInst, nullptr);
    SendMessageW(g.engEmaTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(g.engEmaTrack, TBM_SETPOS, TRUE, (LPARAM)(g_engine.emaFastMs * 10));
    g.engEmaVal = CreateWindowExW(0, L"STATIC", L"2.0", WS_CHILD, px + 620, py + 36, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl2 = CreateWindowExW(0, L"STATIC", L"Velocity Window (1–10 mẫu):", WS_CHILD, px, py + 68, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engVelTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 64, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_VELWIN, g.hInst, nullptr);
    SendMessageW(g.engVelTrack, TBM_SETRANGE, TRUE, MAKELPARAM(1, 10));
    SendMessageW(g.engVelTrack, TBM_SETPOS, TRUE, (LPARAM)g_engine.velWindow);
    g.engVelVal = CreateWindowExW(0, L"STATIC", L"4", WS_CHILD, px + 620, py + 68, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl3 = CreateWindowExW(0, L"STATIC", L"Micro Threshold (0.0–10.0 px):", WS_CHILD, px, py + 100, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engMicroTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 96, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_MICROTH, g.hInst, nullptr);
    SendMessageW(g.engMicroTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(g.engMicroTrack, TBM_SETPOS, TRUE, (LPARAM)(g_engine.microThresh * 10));
    g.engMicroVal = CreateWindowExW(0, L"STATIC", L"2.0", WS_CHILD, px + 620, py + 100, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl4 = CreateWindowExW(0, L"STATIC", L"Acceleration (0.00–0.50):", WS_CHILD, px, py + 132, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engAccelTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 128, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_ACCEL, g.hInst, nullptr);
    SendMessageW(g.engAccelTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 50));
    SendMessageW(g.engAccelTrack, TBM_SETPOS, TRUE, (LPARAM)(g_engine.accel * 100));
    g.engAccelVal = CreateWindowExW(0, L"STATIC", L"0.04", WS_CHILD, px + 620, py + 132, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl5 = CreateWindowExW(0, L"STATIC", L"Accel Offset (0.0–20.0):", WS_CHILD, px, py + 164, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engOffsetTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 160, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_OFFSET, g.hInst, nullptr);
    SendMessageW(g.engOffsetTrack, TBM_SETRANGE, TRUE, MAKELPARAM(0, 200));
    SendMessageW(g.engOffsetTrack, TBM_SETPOS, TRUE, (LPARAM)(g_engine.accelOffset * 10));
    g.engOffsetVal = CreateWindowExW(0, L"STATIC", L"5.0", WS_CHILD, px + 620, py + 164, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl6 = CreateWindowExW(0, L"STATIC", L"Accel Cap (1.00–3.00):", WS_CHILD, px, py + 196, 220, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engCapTrack = CreateWindowExW(0, TRACKBAR_CLASSW, L"", WS_CHILD | TBS_AUTOTICKS | TBS_HORZ,
        px + 230, py + 192, 380, 30, hwnd, (HMENU)(INT_PTR)IDC_ENG_CAP, g.hInst, nullptr);
    SendMessageW(g.engCapTrack, TBM_SETRANGE, TRUE, MAKELPARAM(100, 300));
    SendMessageW(g.engCapTrack, TBM_SETPOS, TRUE, (LPARAM)(g_engine.accelCap * 100));
    g.engCapVal = CreateWindowExW(0, L"STATIC", L"1.40", WS_CHILD, px + 620, py + 196, 60, 20, hwnd, nullptr, g.hInst, nullptr);

    HWND eLbl7 = CreateWindowExW(0, L"STATIC", L"Ref DPI:", WS_CHILD, px, py + 232, 70, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engRefDpi = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800", WS_CHILD | WS_BORDER | ES_NUMBER,
        px + 74, py + 229, 80, 24, hwnd, (HMENU)(INT_PTR)IDC_ENG_REFDPI, g.hInst, nullptr);
    HWND eLbl8 = CreateWindowExW(0, L"STATIC", L"Cur DPI:", WS_CHILD, px + 170, py + 232, 70, 20, hwnd, nullptr, g.hInst, nullptr);
    g.engCurDpi = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"800", WS_CHILD | WS_BORDER | ES_NUMBER,
        px + 244, py + 229, 80, 24, hwnd, (HMENU)(INT_PTR)IDC_ENG_CURDPI, g.hInst, nullptr);

    g.engReset = CreateWindowExW(0, L"BUTTON", L"Khôi phục mặc định + Áp dụng", WS_CHILD | BS_OWNERDRAW,
        px + 400, py + 228, 260, 28, hwnd, (HMENU)(INT_PTR)IDC_ENG_RESET, g.hInst, nullptr);

    HWND eNote = CreateWindowExW(0, L"STATIC",
        L"Cơ chế: đăng ký raw input (RIDEV_NOLEGACY) để tự xử lý delta chuột thô trước khi OS di chuyển con trỏ,\r\n"
        L"rồi bơm lại bằng SendInput — thuần xử lý tín hiệu (giống RawAccel), KHÔNG đọc bộ nhớ/pixel của bất kỳ\r\n"
        L"tiến trình game nào. Thay đổi tham số có hiệu lực NGAY khi kéo thanh trượt (không cần bấm Áp dụng), lúc\r\n"
        L"engine đang bật. Nếu chuột bị \"lag\"/mất kiểm soát: bỏ tick \"Bật Mouse Engine\" hoặc bấm phím cứu hộ Ctrl+Alt+F7.\r\n"
        L"Khi bật, app tạm đặt speed=10 + tắt precision của Windows để tránh accel chồng accel (tự khôi phục khi tắt).",
        WS_CHILD | SS_LEFT, px, py + 268, 800, 80, hwnd, nullptr, g.hInst, nullptr);

    g.tabControls[7] = { g.engEnable, g.engStatus, eLbl1, g.engEmaTrack, g.engEmaVal, eLbl2, g.engVelTrack, g.engVelVal,
                          eLbl3, g.engMicroTrack, g.engMicroVal, eLbl4, g.engAccelTrack, g.engAccelVal,
                          eLbl5, g.engOffsetTrack, g.engOffsetVal, eLbl6, g.engCapTrack, g.engCapVal,
                          eLbl7, g.engRefDpi, eLbl8, g.engCurDpi, g.engReset, eNote };
    for (HWND h : g.tabControls[7]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    auto mkStatic = [&](const wchar_t* text, int x, int y, int w, int h) -> HWND {
        return CreateWindowExW(0, L"STATIC", text, WS_CHILD | SS_LEFT, x, y, w, h, hwnd, nullptr, g.hInst, nullptr);
    };

    // ---- Tab 9: Bắt đầu (dễ dùng nhất: chọn kiểu chuột -> bật) ----
    HWND qsHead = mkStatic(L"Bắt đầu nhanh", px, py, 500, 26);
    g.accentLabels.push_back(qsHead);
    HWND qsDesc = mkStatic(
        L"Bước 1: chọn kiểu chuột bên dưới.     Bước 2: bấm BẬT ENGINE.\r\n"
        L"Thấy chuột lạ? Bấm TẮT ENGINE hoặc nhấn Ctrl+Alt+F7 để trả chuột về bình thường ngay.",
        px, py + 30, 800, 42);
    g.mutedLabels.push_back(qsDesc);
    g.qsToggle = CreateWindowExW(0, L"BUTTON", L"BẬT ENGINE", WS_CHILD | BS_OWNERDRAW,
        px, py + 80, 250, 48, hwnd, (HMENU)(INT_PTR)IDC_QS_TOGGLE, g.hInst, nullptr);
    g.qsStatus = mkStatic(L"Đang TẮT: chuột chạy như Windows bình thường.", px + 270, py + 94, 530, 24);
    HWND qsPick = mkStatic(L"Chọn kiểu chuột:", px, py + 148, 400, 24);
    g.accentLabels.push_back(qsPick);

    const wchar_t* presetText[5] = {
        L"Chính xác 1:1\nKhông gia tốc, không làm mượt. Hợp ngắm tĩnh.",
        L"Mượt & ổn định\nGiảm rung tay, di chuyển êm hơn.",
        L"Cân bằng (khuyên dùng)\nMượt nhẹ + tăng tốc nhẹ khi vung nhanh.",
        L"Xoay nhanh\nVung tay ngắn mà xoay xa hơn.",
        L"Về mặc định Windows\nTắt engine, trả chuột về như ban đầu.",
    };
    HWND presetBtns[5];
    for (int i = 0; i < 5; ++i) {
        presetBtns[i] = CreateWindowExW(0, L"BUTTON", presetText[i], WS_CHILD | BS_OWNERDRAW,
            px + (i % 2) * 410, py + 178 + (i / 2) * 74, 390, 64, hwnd, (HMENU)(INT_PTR)(IDC_QS_P1 + i), g.hInst, nullptr);
    }
    HWND qsTip = mkStatic(
        L"Muốn tinh chỉnh sâu? Vào tab \"Engine\" và kéo các thanh trượt.\r\n"
        L"Muốn biết chuột đang nhanh/chậm bao nhiêu? Xem tab \"Test\". Không hiểu một thông số? Xem tab \"Hướng dẫn\".",
        px, py + 336, 800, 44);
    g.mutedLabels.push_back(qsTip);

    g.tabControls[8] = { qsHead, qsDesc, g.qsToggle, g.qsStatus, qsPick,
                          presetBtns[0], presetBtns[1], presetBtns[2], presetBtns[3], presetBtns[4], qsTip };
    for (HWND h : g.tabControls[8]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(qsHead, WM_SETFONT, (WPARAM)g.hFontBold, TRUE);
    SendMessageW(qsPick, WM_SETFONT, (WPARAM)g.hFontBold, TRUE);
    SendMessageW(g.qsStatus, WM_SETFONT, (WPARAM)g.hFontBold, TRUE);

    // ---- Tab 10: Test (bảng đo trực tiếp) ----
    HWND tsHead = mkStatic(L"Đo chuột trực tiếp", px, py, 500, 26);
    g.accentLabels.push_back(tsHead);
    HWND tsDesc = mkStatic(
        L"Bật engine rồi di chuột. Các số dưới đây cập nhật 4 lần/giây để bạn thấy engine có làm chuột nhanh hoặc chậm bất thường không.",
        px, py + 30, 800, 42);
    g.mutedLabels.push_back(tsDesc);
    const wchar_t* testLabels[6] = {
        L"Chuột thật (vào):", L"Sau engine (ra):", L"Tỉ lệ ra / vào:",
        L"Hệ số gia tốc:", L"Tốc độ tay hiện tại:", L"Tần số báo cáo:" };
    HWND tsLbl[6];
    for (int i = 0; i < 6; ++i) {
        tsLbl[i] = mkStatic(testLabels[i], px, py + 84 + i * 30, 220, 22);
        g.testVals[i] = mkStatic(L"—", px + 230, py + 84 + i * 30, 300, 22);
    }
    g.testVerdict = mkStatic(L"Engine đang tắt. Bật ở tab \"Bắt đầu\" để đo.", px, py + 84 + 6 * 30 + 10, 800, 26);
    g.testWatchdog = CreateWindowExW(0, L"BUTTON", L"Tự ngắt engine khi phát hiện chuột chạy bất thường (khuyên bật)",
        WS_CHILD | BS_AUTOCHECKBOX, px, py + 84 + 6 * 30 + 50, 660, 24, hwnd, (HMENU)(INT_PTR)IDC_TEST_WATCHDOG, g.hInst, nullptr);
    SendMessageW(g.testWatchdog, BM_SETCHECK, BST_CHECKED, 0);
    g.testReset = CreateWindowExW(0, L"BUTTON", L"Đặt lại số liệu", WS_CHILD | BS_OWNERDRAW,
        px, py + 84 + 6 * 30 + 88, 200, 32, hwnd, (HMENU)(INT_PTR)IDC_TEST_RESET, g.hInst, nullptr);
    g.testClickPad = CreateWindowExW(0, L"BUTTON", L"Bấm thử vào đây\nSố click: 0", WS_CHILD | BS_OWNERDRAW,
        px + 570, py + 84, 230, 130, hwnd, (HMENU)(INT_PTR)IDC_TEST_CLICKPAD, g.hInst, nullptr);
    HWND tsNote = mkStatic(
        L"Cách đọc: \"Tỉ lệ ra / vào\" quanh 1.00x–1.50x là bình thường. Nếu lên trên 3x, hoặc con trỏ tự trôi khi bạn không chạm chuột, "
        L"engine sẽ tự ngắt để bảo vệ bạn.\r\n"
        L"Nút bên phải để thử click: nếu số click tăng khi bạn bấm thì chuột trái vẫn hoạt động bình thường lúc engine đang bật.",
        px, py + 84 + 6 * 30 + 134, 800, 64);
    g.mutedLabels.push_back(tsNote);

    g.tabControls[9] = { tsHead, tsDesc, tsLbl[0], g.testVals[0], tsLbl[1], g.testVals[1], tsLbl[2], g.testVals[2],
                          tsLbl[3], g.testVals[3], tsLbl[4], g.testVals[4], tsLbl[5], g.testVals[5],
                          g.testVerdict, g.testWatchdog, g.testReset, g.testClickPad, tsNote };
    for (HWND h : g.tabControls[9]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(tsHead, WM_SETFONT, (WPARAM)g.hFontBold, TRUE);
    SendMessageW(g.testVerdict, WM_SETFONT, (WPARAM)g.hFontBold, TRUE);

    // ---- Tab 11: Hướng dẫn ----
    g.guideText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        L"HƯỚNG DẪN NHANH\r\n"
        L"\r\n"
        L"1) Tab \"Bắt đầu\" — nên dùng đầu tiên\r\n"
        L"   • Chọn 1 trong 4 kiểu chuột, rồi bấm BẬT ENGINE.\r\n"
        L"   • Chuột lạ / quá nhanh / không click được? Bấm TẮT ENGINE hoặc nhấn Ctrl+Alt+F7.\r\n"
        L"   • \"Về mặc định Windows\" = tắt hết, trả chuột về như ban đầu.\r\n"
        L"\r\n"
        L"2) Tab \"Engine\" — chỉnh chi tiết (áp dụng ngay khi kéo thanh trượt)\r\n"
        L"   • EMA Fast (ms): độ làm mượt. Càng cao càng êm nhưng hơi trễ tay. 0 = tắt làm mượt.\r\n"
        L"   • Velocity Window: số mẫu dùng để đo tốc độ tay. Cao = gia tốc ổn định hơn nhưng phản ứng chậm hơn.\r\n"
        L"   • Micro Threshold: chuyển động nhỏ hơn mức này đi thẳng 1:1 (giữ chính xác khi ngắm tĩnh).\r\n"
        L"   • Acceleration: chuột nhanh thêm bao nhiêu khi vung nhanh. 0 = không gia tốc.\r\n"
        L"   • Accel Offset: tốc độ tay tối thiểu để gia tốc bắt đầu. Đặt thấp = gia tốc kích hoạt sớm.\r\n"
        L"   • Accel Cap: mức nhân tối đa. 1.50 nghĩa là nhanh nhất gấp 1.5 lần bình thường.\r\n"
        L"   • Cur DPI: DPI thật của chuột bạn (xem trên chuột hoặc phần mềm của hãng).\r\n"
        L"     Ref DPI: DPI \"chuẩn\" bạn muốn cảm giác giống. Không rõ thì để 800 / 800.\r\n"
        L"\r\n"
        L"3) Tab \"Test\" — xem chuột thật so với chuột sau engine\r\n"
        L"   • \"Tỉ lệ ra / vào\" quanh 1.00x–1.50x là bình thường. Quá 3x là bất thường.\r\n"
        L"   • Nút \"Bấm thử vào đây\" để chắc chắn click vẫn hoạt động khi engine bật.\r\n"
        L"\r\n"
        L"4) Tab \"Chuột\" — cài đặt chuột của Windows (tốc độ con trỏ, double-click, cuộn, vệt chuột).\r\n"
        L"   Độc lập với Engine. Khi Engine bật, app tạm đặt tốc độ 10 + tắt precision rồi tự trả lại khi tắt.\r\n"
        L"\r\n"
        L"5) Tab \"Tối ưu\", \"Config\", \"ADB\", \"Root\" — dành cho BlueStacks (cần Admin / ADB).\r\n"
        L"   Tab \"Presets\" lưu/nạp bộ tuỳ chọn của tab Tối ưu.\r\n"
        L"\r\n"
        L"AN TOÀN\r\n"
        L"   • Engine tự ngắt nếu thấy chuột chạy bất thường (có thể tắt bảo vệ ở tab Test — không khuyên).\r\n"
        L"   • Thoát app là chuột được trả về cài đặt Windows gốc.\r\n"
        L"   • Mouse Forge chỉ xử lý chuyển động chuột thô. KHÔNG đọc bộ nhớ hay hình ảnh của game/giả lập,\r\n"
        L"     không tự ngắm, không tự bù giật.\r\n"
        L"   • Nút góc trên bên phải đổi giao diện Sáng / Tối. Cài đặt lưu ở file MouseForge.ini cạnh file .exe.\r\n",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        px, py, 800, 440, hwnd, (HMENU)(INT_PTR)IDC_GUIDE_TEXT, g.hInst, nullptr);
    g.tabControls[10] = { g.guideText };
    SendMessageW(g.guideText, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    // ---- Log box ----
    g.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 620, 860, 150, hwnd, (HMENU)(INT_PTR)IDC_LOG, g.hInst, nullptr);
    SendMessageW(g.hLog, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // Đồng bộ UI Engine theo cài đặt đã lưu, áp theme, hiện tab đầu tiên
    SyncEngineUiFromState();
    UpdateEngineUi();
    ApplyTheme();
    ShowTabPage(0);
}

// ===================== Main window procedure =====================
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        g.hMain = hwnd;
        g.admin = is_admin();
        CreateMainControls(hwnd);
        if (!g.admin) {
            SetWindowTextW(hwnd, L"Mouse Forge [không phải admin]");
        } else {
            SetWindowTextW(hwnd, L"Mouse Forge");
        }
        RefreshConfigCombo();
        RefreshAdbCombo();
        FillConfigList();
        RefreshPresetCombo();
        OnMouseRefresh();
        RegisterHotKey(hwnd, 1, MOD_CONTROL | MOD_ALT, VK_F7);
        SetTimer(hwnd, kTimerStats, 250, nullptr);
        log_line(L"[App] Mouse Forge đã khởi động");
        if (!g.admin) {
            log_line(L"[App] cảnh báo: chưa chạy với quyền Administrator, ghi config/priority sẽ bị chặn");
        }
        return 0;
    }
    case WM_SIZE: {
        if (g.hStatus) {
            SendMessageW(g.hStatus, WM_SIZE, 0, 0);
            RECT statusRect;
            GetWindowRect(g.hStatus, &statusRect);
            int statusHeight = statusRect.bottom - statusRect.top;

            RECT client;
            GetClientRect(hwnd, &client);
            int logHeight = 150;
            int logTop = client.bottom - statusHeight - logHeight - 10;
            if (logTop < 100) logTop = 100;
            int logWidth = client.right - 20;

            if (g.hLog) MoveWindow(g.hLog, 10, logTop, logWidth, logHeight, TRUE);

            int tabHeight = logTop - kTabTop - 10;
            int tabWidth = client.right - 20;
            if (g.hTab) MoveWindow(g.hTab, 10, kTabTop, tabWidth, tabHeight, TRUE);
        }
        break;
    }
    case WM_NOTIFY: {
        LPNMHDR nm = (LPNMHDR)lParam;
        if (nm->hwndFrom == g.hTab && nm->code == TCN_SELCHANGE) {
            int sel = TabCtrl_GetCurSel(g.hTab);
            ShowTabPage(sel);
        }
        break;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam);
        int code = HIWORD(wParam);
        switch (id) {
            case IDC_CFG_RESCAN:   OnConfigRescan(); break;
            case IDC_CFG_EDITKEY:  OnConfigEditKey(); break;
            case IDC_CFG_BACKUP:   OnConfigBackup(); break;
            case IDC_CFG_RESTORE:  OnConfigRestore(); break;
            case IDC_CFG_COMBO:    if (code == CBN_SELCHANGE) FillConfigList(); break;
            case IDC_ADB_CONNECT:  OnAdbConnect(); break;
            case IDC_OPT_SCAN:     OnOptScan(); break;
            case IDC_ROOT_CHECK:   OnRootCheck(); break;
            case IDC_ROOT_ON:      OnRootSetEnabled(true); break;
            case IDC_ROOT_OFF:     OnRootSetEnabled(false); break;
            case IDC_ROOT_VERIFY:  OnRootVerify(); break;
            case IDC_INFO_LOAD:    OnInfoLoad(); break;
            case IDC_INFO_PRIORITY:OnInfoPriority(); break;
            case IDC_INFO_CLEARLOG:OnInfoClearLog(); break;
            case IDC_PRESET_SAVE:    OnPresetSave(); break;
            case IDC_PRESET_LOAD:    OnPresetLoad(); break;
            case IDC_PRESET_DELETE:  OnPresetDelete(); break;
            case IDC_PRESET_REFRESH: RefreshPresetCombo(); break;
            case IDC_PRESET_COMBO:   if (code == CBN_SELCHANGE) FillPresetList(); break;
            case IDC_MOUSE_APPLY:    OnMouseApply(); break;
            case IDC_MOUSE_RESET:    OnMouseReset(); break;
            case IDC_MOUSE_REFRESH:  OnMouseRefresh(); break;
            case IDC_ENG_ENABLE:     OnEngToggle(); break;
            case IDC_ENG_RESET:      OnEngReset(); break;
            case IDC_ENG_REFDPI:     if (code == EN_CHANGE) EngineSyncFromControls(); break;
            case IDC_ENG_CURDPI:     if (code == EN_CHANGE) EngineSyncFromControls(); break;
            case IDC_THEME_TOGGLE:
                g_dark = !g_dark;
                ApplyTheme();
                SaveSettings();
                break;
            case IDC_QS_TOGGLE:
                EngineSetEnabled(!g_engine.enabled.load(), L"(từ tab Bắt đầu)");
                break;
            case IDC_TEST_RESET:
                g_engine.stIn = g_engine.stOut = 0.0;
                g_engine.stEvents = 0;
                g.testClicks = 0;
                if (g.testClickPad) SetWindowTextW(g.testClickPad, L"Bấm thử vào đây\nSố click: 0");
                log_line(L"[Test] đã đặt lại số liệu đo");
                break;
            case IDC_TEST_WATCHDOG:
                g_engine.watchdogOn = SendMessageW(g.testWatchdog, BM_GETCHECK, 0, 0) == BST_CHECKED;
                log_line(g_engine.watchdogOn ? L"[Test] đã BẬT bộ bảo vệ chống chuột mất kiểm soát"
                                             : L"[Test] đã TẮT bộ bảo vệ (không khuyến khích)");
                break;
            case IDC_TEST_CLICKPAD:
                g.testClicks++;
                if (g.testClickPad) {
                    SetWindowTextW(g.testClickPad,
                        (L"Bấm thử vào đây\nSố click: " + std::to_wstring(g.testClicks)).c_str());
                }
                break;
            default:
                if (id >= IDC_QS_P1 && id <= IDC_QS_P5) {
                    OnQuickPreset(id - IDC_QS_P1);
                } else if (id >= IDC_TABBTN_BASE && id < IDC_TABBTN_BASE + AppGlobals::TAB_COUNT) {
                    ShowTabPage(id - IDC_TABBTN_BASE);
                }
                break;
        }
        break;
    }
    case WM_HSCROLL: {
        HWND ctl = (HWND)lParam;
        if (ctl == g.mouseSpeedTrack || ctl == g.mouseDblTrack || ctl == g.mouseWheelTrack || ctl == g.mouseTrailsTrack) {
            UpdateMouseLabelsFromTrackbars();
        }
        if (ctl == g.engEmaTrack || ctl == g.engVelTrack || ctl == g.engMicroTrack ||
            ctl == g.engAccelTrack || ctl == g.engOffsetTrack || ctl == g.engCapTrack) {
            EngineSyncFromControls();
        }
        break;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
        return ThemeCtlColor(msg, (HDC)wParam, (HWND)lParam);
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, BrBg());
        return 1;
    }
    case WM_PAINT:
        PaintMain(hwnd);
        return 0;
    case WM_DRAWITEM:
        DrawOwnerButton((const DRAWITEMSTRUCT*)lParam);
        return TRUE;
    case WM_TIMER:
        if (wParam == kTimerFlush) MouseEngine_Flush();
        else if (wParam == kTimerStats) UpdateTestTab();
        return 0;
    case WM_INPUT: {
        if (g_engine.enabled.load()) {
            RAWINPUT raw{};
            UINT dwSize = sizeof(raw);
            UINT got = GetRawInputData((HRAWINPUT)lParam, RID_INPUT, &raw, &dwSize, sizeof(RAWINPUTHEADER));
            // QUAN TRỌNG: hDevice có thể KHÁC NULL ngay cả với sự kiện do chính SendInput bơm ra (tuỳ driver/máy) —
            // dựa vào hDevice để lọc là nguyên nhân gây vòng lặp tự khuếch đại (chuột "bay"/mất kiểm soát).
            // ulExtraInformation mới là cách đáng tin cậy: engine luôn gắn kInjectTag khi tự bơm (xem EngineSendMove/
            // MouseEngine_ForwardButtons), nên chỉ cần so khớp đúng tag này để bỏ qua sự kiện do chính mình tạo ra.
            bool isSelfInjected = (raw.data.mouse.ulExtraInformation == kInjectTag);
            if (got != (UINT)-1 && raw.header.dwType == RIM_TYPEMOUSE && !isSelfInjected) {
                if (!(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) {
                    MouseEngine_ProcessAndInject(raw.data.mouse.lLastX, raw.data.mouse.lLastY);
                }
                MouseEngine_ForwardButtons(raw.data.mouse);
            }
        }
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    case WM_APP_TRIP: {
        // Bộ bảo vệ (watchdog) vừa tự phát hiện chuyển động bất thường và đã tắt cờ enabled ngay lập tức
        // (nên KHÔNG còn tự khuếch đại thêm nữa). Nhưng raw input vẫn đang đăng ký RIDEV_NOLEGACY, nếu không
        // gỡ ra ở đây thì Windows vẫn bị chặn xử lý chuột mặc định -> con trỏ đứng hình hoàn toàn. Phải gọi
        // EngineSetEnabled(false, ...) để: gỡ đăng ký raw input, khôi phục cài đặt chuột gốc của Windows,
        // và đồng bộ lại checkbox/nút Bắt đầu về trạng thái TẮT.
        EngineSetEnabled(false,
            L"(bộ bảo vệ tự động tắt vì phát hiện chuyển động bất thường — có thể do vòng lặp phản hồi hoặc phần cứng lỗi)",
            true);
        MessageBoxW(hwnd,
            L"Mouse Engine đã tự động TẮT vì phát hiện chuyển động chuột bất thường.\n\n"
            L"Chuột của bạn đã được trả về mặc định Windows và hoạt động bình thường trở lại.\n"
            L"Vào tab \"Engine\" hoặc \"Bắt đầu\" nếu muốn bật lại.",
            L"Mouse Forge — Bộ bảo vệ đã kích hoạt", MB_OK | MB_ICONWARNING);
        return 0;
    }
    case WM_HOTKEY:
        // Phím tắt cứu hộ Ctrl+Alt+F7: tắt engine ngay kể cả khi chuột đang không dùng được
        if (wParam == 1 && g_engine.enabled.load()) {
            SendMessageW(g.engEnable, BM_SETCHECK, BST_UNCHECKED, 0);
            OnEngToggle();
            log_line(L"[Engine] đã tắt bằng phím tắt Ctrl+Alt+F7");
        }
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        // An toàn: nếu Mouse Engine đang bật, luôn trả lại chuột mặc định Windows trước khi thoát.
        UnregisterHotKey(hwnd, 1);
        KillTimer(hwnd, kTimerFlush);
        KillTimer(hwnd, kTimerStats);
        if (g_engine.enabled.load() || g_engine.rawRegistered) {
            g_engine.enabled = false;
            MouseEngine_Unregister(hwnd);
        }
        SaveSettings();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===================== Entry point =====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g.hInst = hInstance;

    // Nạp cài đặt đã lưu (theme + thông số engine) TRƯỚC khi tạo brush/cửa sổ,
    // để wc.hbrBackground và giao diện ban đầu dùng đúng theme đã lưu.
    LoadSettings();
    CreateThemeBrushes();

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_TAB_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = BrBg();
    wc.lpszClassName = L"MouseForgeMainWnd";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!RegisterClassExW(&wc)) return 0;

    WNDCLASSEXW wcDlg{};
    wcDlg.cbSize = sizeof(wcDlg);
    wcDlg.style = CS_HREDRAW | CS_VREDRAW;
    wcDlg.lpfnWndProc = EditKeyDlgProc;
    wcDlg.hInstance = hInstance;
    wcDlg.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcDlg.hbrBackground = BrBg();
    wcDlg.lpszClassName = L"MFEditKeyDlg";
    if (!RegisterClassExW(&wcDlg)) return 0;

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN;

    RECT r{ 0, 0, 880, 760 + kTabTop - 10 };
    AdjustWindowRect(&r, style, FALSE);

    HWND hwnd = CreateWindowExW(0, L"MouseForgeMainWnd", L"Mouse Forge", style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;
    // (Caption bar Windows 11 đã được tô đúng theme bởi ApplyTheme() gọi từ CreateMainControls ở trên.)

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g.hFontUI) DeleteObject(g.hFontUI);
    if (g.hFontMono) DeleteObject(g.hFontMono);
    if (g.hFontTitle) DeleteObject(g.hFontTitle);
    if (g.hBrushHeader) DeleteObject(g.hBrushHeader);
    if (g.hBrushWindow) DeleteObject(g.hBrushWindow);

    return (int)msg.wParam;
}
