#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <algorithm>
#include <cwchar>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "kernel32.lib")
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' \
version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

struct ProcInfo {
    DWORD        pid = 0;
    std::wstring name;
    std::wstring path;
    std::wstring arch;
    int          iconIndex = -1;
};

struct ModInfo {
    std::wstring name;
    std::wstring path;
    ULONGLONG    base = 0;
    DWORD        size = 0;
};

struct ExportInfo {
    std::wstring name;
    WORD         ordinal = 0;
    DWORD        rva = 0;
    std::wstring forwarder;
};

static std::vector<ProcInfo>   g_procs;
static std::vector<ModInfo>    g_mods;
static std::vector<ExportInfo> g_exports;

static HWND g_hMain = nullptr, g_hProcList = nullptr, g_hModList = nullptr, g_hExpList = nullptr;
static HWND g_hFilter = nullptr, g_hBtnRefresh = nullptr, g_hBtnOffsets = nullptr, g_hStatus = nullptr;
static HWND g_hLblProc = nullptr, g_hLblMod = nullptr, g_hLblExp = nullptr;
static HIMAGELIST g_hImgList = nullptr;
static HFONT g_hFont = nullptr;
static int  g_defIcon = -1;
static int  g_selProc = -1;
static int  g_selMod = -1;

#define IDC_PROCLIST   1001
#define IDC_MODLIST    1002
#define IDC_EXPLIST    1003
#define IDC_FILTER     1004
#define IDC_REFRESH    1005
#define IDC_OFFSETS    1006
#define IDC_DUMPEDIT   1101
#define IDC_DUMPCOPY   1102
#define IDI_APPICON     101

static int g_dpi = 96;

static int S(int v) { return MulDiv(v, g_dpi, 96); }

static void InitDpi(HWND hwnd)
{
    HDC hdc = GetDC(hwnd);
    if (hdc) {
        int d = GetDeviceCaps(hdc, LOGPIXELSX);
        if (d > 0) g_dpi = d;
        ReleaseDC(hwnd, hdc);
    }
}

static int IdealBtnWidth(HWND btn, int minW)
{
    SIZE sz{ 0, 0 };
    if (SendMessageW(btn, BCM_GETIDEALSIZE, 0, (LPARAM)&sz) && sz.cx > 0) {
        int w = sz.cx + S(24);
        return w > minW ? w : minW;
    }
    return minW;
}

static HICON LoadAppIcon(int size)
{
    return (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APPICON),
                             IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
}

static std::wstring Hex64(ULONGLONG v)
{
    wchar_t buf[32];
    swprintf(buf, 32, L"0x%016llX", v);
    return buf;
}

static std::wstring Hex32(DWORD v)
{
    wchar_t buf[16];
    swprintf(buf, 16, L"0x%08X", v);
    return buf;
}

static std::wstring ToLower(std::wstring s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](wchar_t c) { return (wchar_t)towlower(c); });
    return s;
}

static void EnableDebugPrivilege()
{
    HANDLE hTok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hTok))
        return;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid))
        AdjustTokenPrivileges(hTok, FALSE, &tp, sizeof(tp), nullptr, nullptr);
    CloseHandle(hTok);
}

static void SetStatus(const wchar_t* fmt, ...)
{
    wchar_t buf[512];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SendMessageW(g_hStatus, SB_SETTEXTW, 0, (LPARAM)buf);
}

static bool GetProcPathAndArch(DWORD pid, std::wstring& path, std::wstring& arch)
{
    arch = L"?";
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;

    wchar_t buf[MAX_PATH * 2];
    DWORD sz = _countof(buf);
    if (QueryFullProcessImageNameW(h, 0, buf, &sz))
        path.assign(buf, sz);

    BOOL wow = FALSE;
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    if (IsWow64Process(h, &wow))
        arch = wow ? L"x86" : (si.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_INTEL ? L"x86" : L"x64");

    CloseHandle(h);
    return !path.empty();
}

static int LoadIconForPath(const std::wstring& path)
{
    if (path.empty()) return g_defIcon;
    SHFILEINFOW sfi{};
    if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_SMALLICON) && sfi.hIcon) {
        int idx = ImageList_AddIcon(g_hImgList, sfi.hIcon);
        DestroyIcon(sfi.hIcon);
        if (idx >= 0) return idx;
    }
    return g_defIcon;
}

static void FillProcList(const std::wstring& filter)
{
    ListView_DeleteAllItems(g_hProcList);
    std::wstring f = ToLower(filter);
    int shown = 0;

    for (size_t i = 0; i < g_procs.size(); ++i) {
        const ProcInfo& p = g_procs[i];
        if (!f.empty()) {
            wchar_t pidStr[16];
            swprintf(pidStr, 16, L"%lu", p.pid);
            if (ToLower(p.name).find(f) == std::wstring::npos &&
                std::wstring(pidStr).find(f) == std::wstring::npos)
                continue;
        }

        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_IMAGE | LVIF_PARAM;
        it.iItem = shown;
        it.pszText = (LPWSTR)p.name.c_str();
        it.iImage = p.iconIndex;
        it.lParam = (LPARAM)i;
        int row = ListView_InsertItem(g_hProcList, &it);
        if (row < 0) continue;

        wchar_t pidStr[16];
        swprintf(pidStr, 16, L"%lu", p.pid);
        ListView_SetItemText(g_hProcList, row, 1, pidStr);
        ListView_SetItemText(g_hProcList, row, 2, (LPWSTR)p.arch.c_str());
        ++shown;
    }
    SetStatus(L"Processes: %d (shown %d)", (int)g_procs.size(), shown);
}

static void RefreshProcesses()
{
    g_procs.clear();
    g_selProc = g_selMod = -1;
    ListView_DeleteAllItems(g_hModList);
    ListView_DeleteAllItems(g_hExpList);
    g_mods.clear();
    g_exports.clear();
    EnableWindow(g_hBtnOffsets, FALSE);

    ImageList_RemoveAll(g_hImgList);
    HICON hDef = (HICON)LoadImageW(nullptr, IDI_APPLICATION, IMAGE_ICON,
                                   GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED);
    g_defIcon = hDef ? ImageList_AddIcon(g_hImgList, hDef) : -1;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        SetStatus(L"Failed to take a process snapshot (error %lu)", GetLastError());
        return;
    }

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    if (Process32FirstW(snap, &pe)) {
        do {
            ProcInfo p;
            p.pid = pe.th32ProcessID;
            p.name = pe.szExeFile;
            GetProcPathAndArch(p.pid, p.path, p.arch);
            p.iconIndex = LoadIconForPath(p.path);
            g_procs.push_back(std::move(p));
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);

    std::sort(g_procs.begin(), g_procs.end(), [](const ProcInfo& a, const ProcInfo& b) {
        return ToLower(a.name) < ToLower(b.name);
    });

    wchar_t filter[128] = L"";
    GetWindowTextW(g_hFilter, filter, _countof(filter));
    FillProcList(filter);
}

static void LoadModules(int procIdx)
{
    g_mods.clear();
    g_exports.clear();
    g_selMod = -1;
    ListView_DeleteAllItems(g_hModList);
    ListView_DeleteAllItems(g_hExpList);
    EnableWindow(g_hBtnOffsets, FALSE);
    if (procIdx < 0 || procIdx >= (int)g_procs.size()) return;

    const ProcInfo& p = g_procs[procIdx];
    HANDLE snap = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3 && snap == INVALID_HANDLE_VALUE; ++attempt) {
        snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, p.pid);
        if (snap == INVALID_HANDLE_VALUE && GetLastError() != ERROR_BAD_LENGTH) break;
    }
    if (snap == INVALID_HANDLE_VALUE) {
        SetStatus(L"%s (PID %lu): modules are not accessible (error %lu). Try running as administrator.",
                  p.name.c_str(), p.pid, GetLastError());
        return;
    }

    MODULEENTRY32W me{};
    me.dwSize = sizeof(me);
    if (Module32FirstW(snap, &me)) {
        do {
            ModInfo m;
            m.name = me.szModule;
            m.path = me.szExePath;
            m.base = (ULONGLONG)(ULONG_PTR)me.modBaseAddr;
            m.size = me.modBaseSize;
            g_mods.push_back(std::move(m));
        } while (Module32NextW(snap, &me));
    }
    CloseHandle(snap);

    for (size_t i = 0; i < g_mods.size(); ++i) {
        const ModInfo& m = g_mods[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)m.name.c_str();
        it.lParam = (LPARAM)i;
        int row = ListView_InsertItem(g_hModList, &it);
        if (row < 0) continue;
        std::wstring baseStr = Hex64(m.base);
        std::wstring sizeStr = Hex32(m.size);
        ListView_SetItemText(g_hModList, row, 1, (LPWSTR)baseStr.c_str());
        ListView_SetItemText(g_hModList, row, 2, (LPWSTR)sizeStr.c_str());
        ListView_SetItemText(g_hModList, row, 3, (LPWSTR)m.path.c_str());
    }
    SetStatus(L"%s (PID %lu): %d modules", p.name.c_str(), p.pid, (int)g_mods.size());
}

static DWORD RvaToOffset(BYTE* data, SIZE_T fileSize, IMAGE_NT_HEADERS32* nt, DWORD rva)
{
    WORD nSec = nt->FileHeader.NumberOfSections;
    IMAGE_SECTION_HEADER* sec = (IMAGE_SECTION_HEADER*)((BYTE*)&nt->OptionalHeader + nt->FileHeader.SizeOfOptionalHeader);
    for (WORD i = 0; i < nSec; ++i) {
        if ((BYTE*)(sec + i + 1) > data + fileSize) break;
        DWORD vsz = sec[i].Misc.VirtualSize ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (rva >= sec[i].VirtualAddress && rva < sec[i].VirtualAddress + vsz) {
            DWORD off = rva - sec[i].VirtualAddress + sec[i].PointerToRawData;
            return (off < fileSize) ? off : 0;
        }
    }
    return 0;
}

static std::wstring AnsiToWide(const char* s)
{
    if (!s) return L"";
    int len = MultiByteToWideChar(CP_ACP, 0, s, -1, nullptr, 0);
    if (len <= 1) return L"";
    std::wstring w(len - 1, L'\0');
    MultiByteToWideChar(CP_ACP, 0, s, -1, &w[0], len);
    return w;
}

static bool ParseExportsCore(BYTE* data, SIZE_T fileSize, std::vector<ExportInfo>& out, std::wstring& err)
{
    IMAGE_DOS_HEADER* dos = (IMAGE_DOS_HEADER*)data;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || (SIZE_T)dos->e_lfanew + sizeof(IMAGE_NT_HEADERS32) > fileSize) {
        err = L"not a PE file";
        return false;
    }
    IMAGE_NT_HEADERS32* nt32 = (IMAGE_NT_HEADERS32*)(data + dos->e_lfanew);
    if (nt32->Signature != IMAGE_NT_SIGNATURE) { err = L"no PE signature"; return false; }

    IMAGE_DATA_DIRECTORY dir{};
    if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC) {
        dir = nt32->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else if (nt32->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC) {
        IMAGE_NT_HEADERS64* nt64 = (IMAGE_NT_HEADERS64*)nt32;
        dir = nt64->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    } else {
        err = L"unknown PE format";
        return false;
    }

    if (!dir.VirtualAddress || !dir.Size) { err = L"module has no export table"; return false; }

    DWORD expOff = RvaToOffset(data, fileSize, nt32, dir.VirtualAddress);
    if (!expOff) { err = L"export section not found"; return false; }

    IMAGE_EXPORT_DIRECTORY* ed = (IMAGE_EXPORT_DIRECTORY*)(data + expOff);
    DWORD funcsOff = RvaToOffset(data, fileSize, nt32, ed->AddressOfFunctions);
    DWORD namesOff = RvaToOffset(data, fileSize, nt32, ed->AddressOfNames);
    DWORD ordsOff  = RvaToOffset(data, fileSize, nt32, ed->AddressOfNameOrdinals);
    if (!funcsOff) { err = L"corrupted export table"; return false; }

    DWORD* funcs = (DWORD*)(data + funcsOff);
    DWORD* names = namesOff ? (DWORD*)(data + namesOff) : nullptr;
    WORD*  ords  = ordsOff ? (WORD*)(data + ordsOff) : nullptr;

    std::vector<std::wstring> byIndex(ed->NumberOfFunctions);
    if (names && ords) {
        for (DWORD i = 0; i < ed->NumberOfNames; ++i) {
            DWORD nOff = RvaToOffset(data, fileSize, nt32, names[i]);
            if (!nOff) continue;
            WORD idx = ords[i];
            if (idx < byIndex.size()) byIndex[idx] = AnsiToWide((const char*)(data + nOff));
        }
    }

    for (DWORD i = 0; i < ed->NumberOfFunctions; ++i) {
        DWORD rva = funcs[i];
        if (!rva) continue;
        ExportInfo e;
        e.ordinal = (WORD)(ed->Base + i);
        e.rva = rva;
        if (i < byIndex.size() && !byIndex[i].empty()) {
            e.name = byIndex[i];
        } else {
            wchar_t buf[32];
            swprintf(buf, 32, L"#%u", e.ordinal);
            e.name = buf;
        }
        if (rva >= dir.VirtualAddress && rva < dir.VirtualAddress + dir.Size) {
            DWORD fOff = RvaToOffset(data, fileSize, nt32, rva);
            if (fOff) e.forwarder = AnsiToWide((const char*)(data + fOff));
        }
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const ExportInfo& a, const ExportInfo& b) {
        return ToLower(a.name) < ToLower(b.name);
    });
    return true;
}

static bool SafeParse(BYTE* data, SIZE_T fileSize, std::vector<ExportInfo>* out, std::wstring* err)
{
    __try {
        return ParseExportsCore(data, fileSize, *out, *err);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *err = L"PE file read error";
        return false;
    }
}

static bool ParseExports(const std::wstring& path, std::vector<ExportInfo>& out, std::wstring& err)
{
    out.clear();
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { err = L"cannot open module file"; return false; }

    LARGE_INTEGER li{};
    GetFileSizeEx(hFile, &li);
    SIZE_T fileSize = (SIZE_T)li.QuadPart;
    if (fileSize < sizeof(IMAGE_DOS_HEADER)) { CloseHandle(hFile); err = L"file is too small"; return false; }

    HANDLE hMap = CreateFileMappingW(hFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
    CloseHandle(hFile);
    if (!hMap) { err = L"CreateFileMapping failed"; return false; }
    BYTE* data = (BYTE*)MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
    if (!data) { CloseHandle(hMap); err = L"MapViewOfFile failed"; return false; }

    bool ok = SafeParse(data, fileSize, &out, &err);

    UnmapViewOfFile(data);
    CloseHandle(hMap);
    return ok;
}

static void LoadExports(int modIdx)
{
    g_exports.clear();
    ListView_DeleteAllItems(g_hExpList);
    EnableWindow(g_hBtnOffsets, FALSE);
    if (modIdx < 0 || modIdx >= (int)g_mods.size()) return;

    const ModInfo& m = g_mods[modIdx];
    std::wstring err;
    if (!ParseExports(m.path, g_exports, err)) {
        SetStatus(L"%s: %s", m.name.c_str(), err.c_str());
        EnableWindow(g_hBtnOffsets, TRUE);
        return;
    }

    SendMessageW(g_hExpList, WM_SETREDRAW, FALSE, 0);
    for (size_t i = 0; i < g_exports.size(); ++i) {
        const ExportInfo& e = g_exports[i];
        LVITEMW it{};
        it.mask = LVIF_TEXT | LVIF_PARAM;
        it.iItem = (int)i;
        it.pszText = (LPWSTR)e.name.c_str();
        it.lParam = (LPARAM)i;
        int row = ListView_InsertItem(g_hExpList, &it);
        if (row < 0) continue;
        wchar_t ord[16];
        swprintf(ord, 16, L"%u", e.ordinal);
        std::wstring rvaStr = Hex32(e.rva);
        std::wstring vaStr = Hex64(m.base + e.rva);
        ListView_SetItemText(g_hExpList, row, 1, ord);
        ListView_SetItemText(g_hExpList, row, 2, (LPWSTR)rvaStr.c_str());
        ListView_SetItemText(g_hExpList, row, 3, (LPWSTR)vaStr.c_str());
        ListView_SetItemText(g_hExpList, row, 4, (LPWSTR)e.forwarder.c_str());
    }
    SendMessageW(g_hExpList, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(g_hExpList, nullptr, TRUE);

    std::wstring baseStr = Hex64(m.base);
    SetStatus(L"%s @ %s: %d exports", m.name.c_str(), baseStr.c_str(), (int)g_exports.size());
    EnableWindow(g_hBtnOffsets, TRUE);
}

static std::wstring BuildOffsetsText()
{
    std::wstring s;
    s.reserve(64 * 1024 + g_exports.size() * 96);

    if (g_selProc >= 0 && g_selProc < (int)g_procs.size()) {
        const ProcInfo& p = g_procs[g_selProc];
        wchar_t hdr[1024];
        swprintf(hdr, 1024, L"Process: %s (PID %lu, %s)\r\nPath:    %s\r\n\r\n",
                 p.name.c_str(), p.pid, p.arch.c_str(), p.path.c_str());
        s += hdr;
    }
    if (g_selMod >= 0 && g_selMod < (int)g_mods.size()) {
        const ModInfo& m = g_mods[g_selMod];
        wchar_t hdr[1024];
        swprintf(hdr, 1024, L"Module:  %s\r\nBase:    %s\r\nSize:    %s\r\nPath:    %s\r\nExports: %d\r\n\r\n",
                 m.name.c_str(), Hex64(m.base).c_str(), Hex32(m.size).c_str(), m.path.c_str(), (int)g_exports.size());
        s += hdr;
    }

    s += L"RVA (offset)   | VA (base+offset)    | Ord   | Name\r\n";
    s += L"---------------+---------------------+-------+---------------------------\r\n";

    ULONGLONG base = (g_selMod >= 0 && g_selMod < (int)g_mods.size()) ? g_mods[g_selMod].base : 0;
    wchar_t line[1024];
    for (const ExportInfo& e : g_exports) {
        swprintf(line, 1024, L"%s     | %s | %5u | %s%s%s\r\n",
                 Hex32(e.rva).c_str(), Hex64(base + e.rva).c_str(), e.ordinal, e.name.c_str(),
                 e.forwarder.empty() ? L"" : L"  ->  ", e.forwarder.c_str());
        s += line;
    }
    if (g_exports.empty())
        s += L"(export table is empty or unavailable)\r\n";
    return s;
}

static void CopyToClipboard(HWND owner, const std::wstring& text)
{
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    SIZE_T bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (h) {
        void* p = GlobalLock(h);
        memcpy(p, text.c_str(), bytes);
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

static LRESULT CALLBACK DumpWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_SIZE: {
        RECT rc; GetClientRect(hwnd, &rc);
        HWND edit = GetDlgItem(hwnd, IDC_DUMPEDIT);
        HWND btn = GetDlgItem(hwnd, IDC_DUMPCOPY);
        const int pad = S(10), btnH = S(26);
        int btnW = IdealBtnWidth(btn, S(170));
        MoveWindow(edit, pad, pad, rc.right - pad * 2, rc.bottom - btnH - pad * 3, TRUE);
        MoveWindow(btn, pad, rc.bottom - pad - btnH, btnW, btnH, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_DUMPCOPY) {
            HWND edit = GetDlgItem(hwnd, IDC_DUMPEDIT);
            int len = GetWindowTextLengthW(edit);
            std::wstring t(len + 1, L'\0');
            GetWindowTextW(edit, &t[0], len + 1);
            t.resize(len);
            CopyToClipboard(hwnd, t);
            MessageBoxW(hwnd, L"Offsets copied to clipboard.", L"Done", MB_ICONINFORMATION);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ShowOffsetsWindow()
{
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DumpWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXICON));
        wc.hIconSm = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
        wc.lpszClassName = L"ProcInspectorDump";
        RegisterClassExW(&wc);
        registered = true;
    }

    std::wstring title = L"Offsets";
    if (g_selMod >= 0 && g_selMod < (int)g_mods.size())
        title += L" - " + g_mods[g_selMod].name;

    HWND hwnd = CreateWindowExW(0, L"ProcInspectorDump", title.c_str(),
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, S(900), S(640),
                                g_hMain, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!hwnd) return;

    HWND edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE |
                                ES_READONLY | ES_AUTOHSCROLL | ES_AUTOVSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU)IDC_DUMPEDIT, GetModuleHandleW(nullptr), nullptr);
    HFONT mono = CreateFontW(-S(14), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
    SendMessageW(edit, WM_SETFONT, (WPARAM)mono, TRUE);

    HWND btn = CreateWindowExW(0, L"BUTTON", L"Copy to clipboard",
                               WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                               0, 0, 0, 0, hwnd, (HMENU)IDC_DUMPCOPY, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(btn, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    std::wstring text = BuildOffsetsText();
    SetWindowTextW(edit, text.c_str());

    ShowWindow(hwnd, SW_SHOW);
    SendMessageW(hwnd, WM_SIZE, 0, 0);
    UpdateWindow(hwnd);
}

static HWND MakeListView(HWND parent, int id)
{
    HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                             WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                             0, 0, 0, 0, parent, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    ListView_SetExtendedListViewStyle(h, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
    SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    return h;
}

static void AddColumn(HWND lv, int idx, const wchar_t* text, int width)
{
    LVCOLUMNW col{};
    col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    col.iSubItem = idx;
    col.pszText = (LPWSTR)text;
    col.cx = S(width);
    ListView_InsertColumn(lv, idx, &col);
}

static void CreateChildren(HWND hwnd)
{
    HINSTANCE hInst = GetModuleHandleW(nullptr);

    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0);
    g_hFont = CreateFontIndirectW(&ncm.lfMessageFont);

    g_hFilter = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                0, 0, 0, 0, hwnd, (HMENU)IDC_FILTER, hInst, nullptr);
    SendMessageW(g_hFilter, WM_SETFONT, (WPARAM)g_hFont, TRUE);
    SendMessageW(g_hFilter, EM_SETCUEBANNER, TRUE, (LPARAM)L"Filter by name or PID...");

    g_hBtnRefresh = CreateWindowExW(0, L"BUTTON", L"Refresh",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                    0, 0, 0, 0, hwnd, (HMENU)IDC_REFRESH, hInst, nullptr);
    SendMessageW(g_hBtnRefresh, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    g_hBtnOffsets = CreateWindowExW(0, L"BUTTON", L"Show offsets",
                                    WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_DISABLED,
                                    0, 0, 0, 0, hwnd, (HMENU)IDC_OFFSETS, hInst, nullptr);
    SendMessageW(g_hBtnOffsets, WM_SETFONT, (WPARAM)g_hFont, TRUE);

    auto mkLabel = [&](const wchar_t* t) {
        HWND h = CreateWindowExW(0, L"STATIC", t, WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
        SendMessageW(h, WM_SETFONT, (WPARAM)g_hFont, TRUE);
        return h;
    };
    g_hLblProc = mkLabel(L"Processes");
    g_hLblMod  = mkLabel(L"Process modules (DLLs)");
    g_hLblExp  = mkLabel(L"Module exports / offsets");

    g_hProcList = MakeListView(hwnd, IDC_PROCLIST);
    AddColumn(g_hProcList, 0, L"Process", 190);
    AddColumn(g_hProcList, 1, L"PID", 70);
    AddColumn(g_hProcList, 2, L"Arch", 55);

    g_hImgList = ImageList_Create(GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                  ILC_COLOR32 | ILC_MASK, 64, 64);
    ListView_SetImageList(g_hProcList, g_hImgList, LVSIL_SMALL);

    g_hModList = MakeListView(hwnd, IDC_MODLIST);
    AddColumn(g_hModList, 0, L"Module", 200);
    AddColumn(g_hModList, 1, L"Base", 150);
    AddColumn(g_hModList, 2, L"Size", 90);
    AddColumn(g_hModList, 3, L"Path", 420);

    g_hExpList = MakeListView(hwnd, IDC_EXPLIST);
    AddColumn(g_hExpList, 0, L"Export", 300);
    AddColumn(g_hExpList, 1, L"Ordinal", 70);
    AddColumn(g_hExpList, 2, L"RVA (offset)", 110);
    AddColumn(g_hExpList, 3, L"VA (base+offset)", 160);
    AddColumn(g_hExpList, 4, L"Forward", 220);

    g_hStatus = CreateWindowExW(0, STATUSCLASSNAMEW, L"", WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
                                0, 0, 0, 0, hwnd, nullptr, hInst, nullptr);
}

static void LayoutChildren(HWND hwnd)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    SendMessageW(g_hStatus, WM_SIZE, 0, 0);
    RECT sb;
    GetWindowRect(g_hStatus, &sb);
    int statusH = sb.bottom - sb.top;

    const int pad  = S(10);
    const int gap  = S(8);
    const int ctlH = S(26);
    const int lblH = S(20);
    const int leftW = S(330);

    int bottom = rc.bottom - statusH - pad;
    if (bottom < pad + ctlH * 3) return;

    int refreshW = IdealBtnWidth(g_hBtnRefresh, S(90));
    if (refreshW > leftW / 2) refreshW = leftW / 2;
    MoveWindow(g_hFilter, pad, pad, leftW - gap - refreshW, ctlH, TRUE);
    MoveWindow(g_hBtnRefresh, pad + leftW - refreshW, pad, refreshW, ctlH, TRUE);

    int listTop = pad + ctlH + gap;
    MoveWindow(g_hLblProc, pad, listTop, leftW, lblH, TRUE);
    MoveWindow(g_hProcList, pad, listTop + lblH, leftW, bottom - listTop - lblH, TRUE);

    int rx = pad + leftW + gap * 2;
    int rw = rc.right - rx - pad;
    if (rw < S(200)) rw = S(200);

    int offW = IdealBtnWidth(g_hBtnOffsets, S(160));
    int btnRowTop = bottom - ctlH;
    int listsBottom = btnRowTop - gap;
    int avail = listsBottom - pad;
    int modH = (avail - gap) / 2;

    MoveWindow(g_hLblMod, rx, pad, rw, lblH, TRUE);
    MoveWindow(g_hModList, rx, pad + lblH, rw, modH - lblH, TRUE);

    int ey = pad + modH + gap;
    MoveWindow(g_hLblExp, rx, ey, rw, lblH, TRUE);
    MoveWindow(g_hExpList, rx, ey + lblH, rw, listsBottom - ey - lblH, TRUE);

    MoveWindow(g_hBtnOffsets, rc.right - pad - offW, btnRowTop, offW, ctlH, TRUE);
}

static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        g_hMain = hwnd;
        InitDpi(hwnd);
        CreateChildren(hwnd);
        RefreshProcesses();
        return 0;

    case WM_SIZE:
        LayoutChildren(hwnd);
        return 0;

    case WM_DPICHANGED: {
        g_dpi = HIWORD(wp);
        RECT* sug = (RECT*)lp;
        SetWindowPos(hwnd, nullptr, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        LayoutChildren(hwnd);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = S(900);
        mmi->ptMinTrackSize.y = S(520);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_REFRESH:
            RefreshProcesses();
            return 0;
        case IDC_OFFSETS:
            ShowOffsetsWindow();
            return 0;
        case IDC_FILTER:
            if (HIWORD(wp) == EN_CHANGE) {
                wchar_t f[128] = L"";
                GetWindowTextW(g_hFilter, f, _countof(f));
                FillProcList(f);
            }
            return 0;
        }
        break;

    case WM_NOTIFY: {
        NMHDR* nh = (NMHDR*)lp;
        if (nh->code == LVN_ITEMCHANGED) {
            NMLISTVIEW* nlv = (NMLISTVIEW*)lp;
            if ((nlv->uNewState & LVIS_SELECTED) && !(nlv->uOldState & LVIS_SELECTED)) {
                if (nh->idFrom == IDC_PROCLIST) {
                    g_selProc = (int)nlv->lParam;
                    LoadModules(g_selProc);
                } else if (nh->idFrom == IDC_MODLIST) {
                    g_selMod = (int)nlv->lParam;
                    LoadExports(g_selMod);
                }
            }
        }
        break;
    }

    case WM_DESTROY:
        if (g_hImgList) ImageList_Destroy(g_hImgList);
        if (g_hFont) DeleteObject(g_hFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow)
{
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    EnableDebugPrivilege();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MainWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"ProcInspectorMain";
    wc.hIcon = LoadAppIcon(GetSystemMetrics(SM_CXICON));
    wc.hIconSm = LoadAppIcon(GetSystemMetrics(SM_CXSMICON));
    if (!RegisterClassExW(&wc)) return 1;

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"ProcInspector - processes, DLLs and offsets",
                                WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 780,
                                nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    return (int)msg.wParam;
}
