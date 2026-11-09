#include "update.h"
#include "features.h"
#include "log.h"
#include "version.h"
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <cstdio>
#include <string>

static SRWLOCK g_lock = SRWLOCK_INIT;
static bool g_ready = false, g_newer = false;
static std::string g_latest;

static void Parse(const std::string& v, int out[3])
{
    size_t i = 0;
    while (i < v.size() && !isdigit((unsigned char)v[i])) ++i;
    out[0] = out[1] = out[2] = 0;
    sscanf(v.c_str() + i, "%d.%d.%d", &out[0], &out[1], &out[2]);
}

static std::string Fetch()
{
    std::string body;
    HINTERNET s = WinHttpOpen(L"RiffedMenu", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!s) return body;
    WinHttpSetTimeouts(s, 8000, 8000, 8000, 8000);
    HINTERNET c = WinHttpConnect(s, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", L"/repos/RiffedStorm/RiffedMenu/releases/latest", nullptr,
                                         WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json\r\n", (DWORD)-1, nullptr, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
        DWORD n = 0;
        do {
            if (!WinHttpQueryDataAvailable(r, &n) || !n) break;
            std::string chunk(n, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(r, &chunk[0], n, &got)) break;
            body.append(chunk, 0, got);
        } while (n && body.size() < 512 * 1024);
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return body;
}

static void Check()
{
    std::string body = Fetch();
    auto key = body.find("\"tag_name\"");
    if (key == std::string::npos) { Log("Update check: no answer"); return; }
    auto a = body.find('"', body.find(':', key));
    auto b = a == std::string::npos ? a : body.find('"', a + 1);
    if (b == std::string::npos) return;
    std::string latest = body.substr(a + 1, b - a - 1);
    int have[3], got[3];
    bool newer = false;
    Parse(RIFFED_VERSION, have);
    Parse(latest, got);
    for (int i = 0; i < 3 && !newer; ++i) {
        if (got[i] > have[i]) newer = true;
        else if (got[i] < have[i]) break;
    }
    AcquireSRWLockExclusive(&g_lock);
    bool changed = latest != g_latest;
    g_latest = latest;
    g_newer = newer;
    g_ready = true;
    ReleaseSRWLockExclusive(&g_lock);
    if (changed) Log("Update check: this is %s, latest release is %s", RIFFED_VERSION, latest.c_str());
}

static DWORD WINAPI Worker(LPVOID)
{
    constexpr DWORD kInterval = 30 * 60 * 1000;
    Sleep(4000);
    Check();
    DWORD last = GetTickCount();
    for (;;) {
        Sleep(60000);
        if (!g_toggles.updateNotify || !g_toggles.updatePeriodic || GetTickCount() - last < kInterval) continue;
        last = GetTickCount();
        Check();
    }
}

namespace Update {
void Start() { if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t); }
bool Available()
{
    AcquireSRWLockShared(&g_lock);
    bool yes = g_ready && g_newer;
    ReleaseSRWLockShared(&g_lock);
    return yes;
}

std::string Latest()
{
    AcquireSRWLockShared(&g_lock);
    std::string tag = g_latest;
    ReleaseSRWLockShared(&g_lock);
    return tag;
}
void OpenReleasePage() { ShellExecuteA(nullptr, "open", "https://github.com/RiffedStorm/RiffedMenu/releases/latest", nullptr, nullptr, SW_SHOWNORMAL); }
}
