#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <psapi.h>
#include <dwmapi.h>
#include <shlwapi.h>

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

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "advapi32.lib")

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

    IDC_DLG_KEY_LBL, IDC_DLG_KEY, IDC_DLG_VAL_LBL, IDC_DLG_VAL, IDC_DLG_OK, IDC_DLG_CANCEL,
};

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

    static constexpr int TAB_COUNT = 6;
    std::vector<HWND> tabControls[TAB_COUNT];
};
static AppGlobals g;

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

// ===================== Tab switching =====================
static void ShowTabPage(int idx) {
    g.curTab = idx;
    for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) {
        int cmd = (i == idx) ? SW_SHOW : SW_HIDE;
        for (HWND h : g.tabControls[i]) {
            if (h) ShowWindow(h, cmd);
        }
    }
}

// ===================== UI construction =====================
static void CreateMainControls(HWND hwnd) {
    g.hFontUI = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g.hFontMono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_MODERN, L"Consolas");

    g.hTab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
        10, 10, 860, 500, hwnd, (HMENU)(INT_PTR)IDC_TAB, g.hInst, nullptr);
    SendMessageW(g.hTab, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    const wchar_t* tabNames[AppGlobals::TAB_COUNT] = { L"Config", L"ADB", L"Tối ưu", L"Root", L"Thông tin", L"Presets" };
    for (int i = 0; i < AppGlobals::TAB_COUNT; ++i) {
        TCITEMW tie{};
        tie.mask = TCIF_TEXT;
        tie.pszText = (LPWSTR)tabNames[i];
        TabCtrl_InsertItem(g.hTab, i, &tie);
    }

    int px = 20, py = 40;

    // ---- Tab 1: Config ----
    HWND cfgLbl1 = CreateWindowExW(0, L"STATIC", L"File config:", WS_CHILD, px, py, 100, 20, hwnd, nullptr, g.hInst, nullptr);
    g.cfgCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        px + 100, py - 2, 590, 200, hwnd, (HMENU)(INT_PTR)IDC_CFG_COMBO, g.hInst, nullptr);
    g.cfgRescan = CreateWindowExW(0, L"BUTTON", L"Quét lại", WS_CHILD,
        px + 700, py - 3, 110, 26, hwnd, (HMENU)(INT_PTR)IDC_CFG_RESCAN, g.hInst, nullptr);
    g.cfgEditKey = CreateWindowExW(0, L"BUTTON", L"Sửa key", WS_CHILD,
        px, py + 34, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_CFG_EDITKEY, g.hInst, nullptr);
    g.cfgBackup = CreateWindowExW(0, L"BUTTON", L"Backup", WS_CHILD,
        px + 120, py + 34, 110, 28, hwnd, (HMENU)(INT_PTR)IDC_CFG_BACKUP, g.hInst, nullptr);
    g.cfgRestore = CreateWindowExW(0, L"BUTTON", L"Restore", WS_CHILD,
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
    g.adbConnect = CreateWindowExW(0, L"BUTTON", L"Dò && nạp", WS_CHILD,
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
    g.optScan = CreateWindowExW(0, L"BUTTON", L"Quét prop + tối ưu", WS_CHILD, px, oy + 150, 220, 30, hwnd, (HMENU)(INT_PTR)IDC_OPT_SCAN, g.hInst, nullptr);
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
    g.rootCheck = CreateWindowExW(0, L"BUTTON", L"Kiểm tra root", WS_CHILD, px, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_CHECK, g.hInst, nullptr);
    g.rootOn = CreateWindowExW(0, L"BUTTON", L"Bật root qua config", WS_CHILD, px + 160, py, 180, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_ON, g.hInst, nullptr);
    g.rootOff = CreateWindowExW(0, L"BUTTON", L"Tắt root qua config", WS_CHILD, px + 350, py, 180, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_OFF, g.hInst, nullptr);
    g.rootVerify = CreateWindowExW(0, L"BUTTON", L"Xác minh root", WS_CHILD, px + 540, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_ROOT_VERIFY, g.hInst, nullptr);
    g.rootList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, py + 46, 800, 400, hwnd, (HMENU)(INT_PTR)IDC_ROOT_LIST, g.hInst, nullptr);

    g.tabControls[3] = { g.rootCheck, g.rootOn, g.rootOff, g.rootVerify, g.rootList };
    for (HWND h : g.tabControls[3]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.rootList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Tab 5: Info ----
    g.infoLoad = CreateWindowExW(0, L"BUTTON", L"Nạp thông tin", WS_CHILD, px, py, 150, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_LOAD, g.hInst, nullptr);
    g.infoPriority = CreateWindowExW(0, L"BUTTON", L"Priority HIGH cho BS", WS_CHILD, px + 160, py, 190, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_PRIORITY, g.hInst, nullptr);
    g.infoClearLog = CreateWindowExW(0, L"BUTTON", L"Xoá log", WS_CHILD, px + 360, py, 120, 30, hwnd, (HMENU)(INT_PTR)IDC_INFO_CLEARLOG, g.hInst, nullptr);
    g.infoText = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        px, py + 46, 800, 400, hwnd, (HMENU)(INT_PTR)IDC_INFO_TEXT, g.hInst, nullptr);

    g.tabControls[4] = { g.infoLoad, g.infoPriority, g.infoClearLog, g.infoText };
    for (HWND h : g.tabControls[4]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);

    // ---- Tab 6: Presets ----
    HWND presetLbl1 = CreateWindowExW(0, L"STATIC", L"Preset đã lưu:", WS_CHILD, px, py, 110, 20, hwnd, nullptr, g.hInst, nullptr);
    g.presetCombo = CreateWindowExW(0, L"COMBOBOX", L"", WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL,
        px + 110, py - 2, 300, 200, hwnd, (HMENU)(INT_PTR)IDC_PRESET_COMBO, g.hInst, nullptr);
    g.presetLoad = CreateWindowExW(0, L"BUTTON", L"Nạp", WS_CHILD, px + 420, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_LOAD, g.hInst, nullptr);
    g.presetDelete = CreateWindowExW(0, L"BUTTON", L"Xoá", WS_CHILD, px + 520, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_DELETE, g.hInst, nullptr);
    g.presetRefresh = CreateWindowExW(0, L"BUTTON", L"Quét lại", WS_CHILD, px + 620, py - 3, 90, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_REFRESH, g.hInst, nullptr);

    HWND presetLbl2 = CreateWindowExW(0, L"STATIC", L"Tên preset mới:", WS_CHILD, px, py + 40, 110, 20, hwnd, nullptr, g.hInst, nullptr);
    g.presetName = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_BORDER,
        px + 110, py + 38, 300, 24, hwnd, (HMENU)(INT_PTR)IDC_PRESET_NAME, g.hInst, nullptr);
    g.presetSave = CreateWindowExW(0, L"BUTTON", L"Lưu lựa chọn hiện tại ở tab Tối ưu thành preset", WS_CHILD,
        px + 420, py + 37, 380, 26, hwnd, (HMENU)(INT_PTR)IDC_PRESET_SAVE, g.hInst, nullptr);

    g.presetList = CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_DISABLENOSCROLL,
        px, py + 80, 800, 354, hwnd, (HMENU)(INT_PTR)IDC_PRESET_LIST, g.hInst, nullptr);

    g.tabControls[5] = { presetLbl1, g.presetCombo, g.presetLoad, g.presetDelete, g.presetRefresh,
                          presetLbl2, g.presetName, g.presetSave, g.presetList };
    for (HWND h : g.tabControls[5]) SendMessageW(h, WM_SETFONT, (WPARAM)g.hFontUI, TRUE);
    SendMessageW(g.presetList, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Log box ----
    g.hLog = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
        10, 520, 860, 150, hwnd, (HMENU)(INT_PTR)IDC_LOG, g.hInst, nullptr);
    SendMessageW(g.hLog, WM_SETFONT, (WPARAM)g.hFontMono, TRUE);

    // ---- Status bar ----
    g.hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
        0, 0, 0, 0, hwnd, (HMENU)(INT_PTR)IDC_STATUS, g.hInst, nullptr);
    SendMessageW(g.hStatus, SB_SETTEXT, 0, (LPARAM)L"Sẵn sàng");

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

            int tabHeight = logTop - 20;
            int tabWidth = client.right - 20;
            if (g.hTab) MoveWindow(g.hTab, 10, 10, tabWidth, tabHeight, TRUE);
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
            default: break;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ===================== Entry point =====================
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    g.hInst = hInstance;

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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"MouseForgeMainWnd";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(101));
    if (!RegisterClassExW(&wc)) return 0;

    WNDCLASSEXW wcDlg{};
    wcDlg.cbSize = sizeof(wcDlg);
    wcDlg.style = CS_HREDRAW | CS_VREDRAW;
    wcDlg.lpfnWndProc = EditKeyDlgProc;
    wcDlg.hInstance = hInstance;
    wcDlg.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wcDlg.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wcDlg.lpszClassName = L"MFEditKeyDlg";
    if (!RegisterClassExW(&wcDlg)) return 0;

    DWORD style = (WS_OVERLAPPEDWINDOW & ~WS_THICKFRAME & ~WS_MAXIMIZEBOX) | WS_CLIPCHILDREN;

    RECT r{ 0, 0, 880, 760 };
    AdjustWindowRect(&r, style, FALSE);

    HWND hwnd = CreateWindowExW(0, L"MouseForgeMainWnd", L"Mouse Forge", style,
        CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
        nullptr, nullptr, hInstance, nullptr);

    if (!hwnd) return 0;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g.hFontUI) DeleteObject(g.hFontUI);
    if (g.hFontMono) DeleteObject(g.hFontMono);

    return (int)msg.wParam;
}
