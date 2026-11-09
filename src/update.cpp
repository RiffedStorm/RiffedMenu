#include "update.h"
#include "features.h"
#include "log.h"
#include "version.h"
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <cstdio>
#include <string>

static volatile LONG g_ready = 0;
static std::string g_latest;
static bool g_newer = false;

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

static DWORD WINAPI Worker(LPVOID)
{
    Sleep(4000);
    std::string body = Fetch();
    auto key = body.find("\"tag_name\"");
    if (key == std::string::npos) { Log("Update check: no answer"); return 0; }
    auto a = body.find('"', body.find(':', key));
    auto b = a == std::string::npos ? a : body.find('"', a + 1);
    if (b == std::string::npos) return 0;
    g_latest = body.substr(a + 1, b - a - 1);
    int have[3], got[3];
    Parse(RIFFED_VERSION, have);
    Parse(g_latest, got);
    for (int i = 0; i < 3 && !g_newer; ++i) {
        if (got[i] > have[i]) g_newer = true;
        else if (got[i] < have[i]) break;
    }
    Log("Update check: this is %s, latest release is %s", RIFFED_VERSION, g_latest.c_str());
    InterlockedExchange(&g_ready, 1);
    return 0;
}

namespace Update {
void Start() { if (HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr)) CloseHandle(t); }
bool Available() { return g_ready && g_newer; }
std::string Latest() { return g_ready ? g_latest : std::string(); }
void OpenReleasePage() { ShellExecuteA(nullptr, "open", "https://github.com/RiffedStorm/RiffedMenu/releases/latest", nullptr, nullptr, SW_SHOWNORMAL); }
}
