#define DIRECTINPUT_VERSION 0x0800
#include "hooks.h"
#include "features.h"
#include "game.h"
#include "log.h"
#include "menu.h"
#include "combat.h"
#include "config.h"
#include "update.h"
#include "wheel.h"
#include <windows.h>
#include <d3d9.h>
#include <dinput.h>
#include <MinHook.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "imgui.h"
#include "imgui_impl_dx9.h"
#include "imgui_impl_win32.h"

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern HMODULE g_module;

static bool MHCheck(MH_STATUS s, const char* what)
{
    if (s == MH_OK) return true;
    Log("MinHook %s failed: %s", what, MH_StatusToString(s));
    return false;
}

template <typename T>
static bool Hook(void* target, T detour, T* original, const char* name)
{
    if (!MHCheck(MH_CreateHook(target, (void*)detour, (void**)original), name)) return false;
    if (!MHCheck(MH_EnableHook(target), name)) return false;
    Log("Hooked %s at %p", name, target);
    return true;
}

// ===========================================================================
// Render
// ===========================================================================
// The overlay is drawn from whichever path the game really uses on this PC:
//   1. IDirect3DDevice9::Present (normal)
//   2. EndScene while rendering to the back buffer (fallback if no Present is seen)
// Overlays such as RTSS/Afterburner, Discord, Steam or ReShade hook these functions too,
// so we hook through the device vtable (see SwapSlot) and a watchdog re-hooks new devices.
using Present_t   = HRESULT(WINAPI*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset_t     = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using EndScene_t  = HRESULT(WINAPI*)(IDirect3DDevice9*);

static HWND    g_hwnd = nullptr;
static WNDPROC g_origWndProc = nullptr;
static bool    g_imguiReady = false;
static IDirect3DDevice9* g_device = nullptr;

static volatile LONG g_cntPresent = 0, g_cntEndScene = 0, g_cntFrames = 0, g_cntReset = 0;
static volatile DWORD g_lastPresentTick = 0;   // last time any Present-type hook ran
static bool g_inOverlay = false;               // our own BeginScene/EndScene must not recurse

static std::string ModuleOf(const void* p)
{
    HMODULE m = nullptr;
    char name[MAX_PATH] = "?";
    if (p && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)p, &m) && m) {
        GetModuleFileNameA(m, name, MAX_PATH);
        const char* base = strrchr(name, '\\');
        char out[MAX_PATH + 16];
        snprintf(out, sizeof(out), "%s+0x%lX", base ? base + 1 : name, (unsigned long)((uintptr_t)p - (uintptr_t)m));
        return out;
    }
    return "<no module / heap>";
}

static void LogLoadedModules()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    if (snap == INVALID_HANDLE_VALUE) return;
    MODULEENTRY32 me{};
    me.dwSize = sizeof(me);
    std::string line;
    for (BOOL ok = Module32First(snap, &me); ok; ok = Module32Next(snap, &me)) {
        line += me.szModule;
        line += ' ';
        if (line.size() > 160) { Log("  modules: %s", line.c_str()); line.clear(); }
    }
    if (!line.empty()) Log("  modules: %s", line.c_str());
    CloseHandle(snap);
}

static bool IsInputMsg(UINT msg)
{
    return (msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN ||
           msg == WM_CHAR || msg == WM_SYSCHAR || msg == WM_INPUT;
}

static bool Captured() { return Menu::IsOpen() || Wheel::IsOpen(); }

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (g_imguiReady && Captured()) {
        ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp);
        if (IsInputMsg(msg)) return 0;            // swallow - game must not see it (key-ups still pass through)
        if (msg == WM_SETCURSOR) { SetCursor(nullptr); return TRUE; }
    }
    return CallWindowProcW(g_origWndProc, hwnd, msg, wp, lp);
}

// Roboto (Apache 2.0, shipped with Dear ImGui) is built into the .asi: Polish letters then work on
// every PC, with no font files to install.
__asm__(".section .rdata,\"dr\"\n_riffed_font_start:\n.incbin \"Roboto-Medium.ttf\"\n_riffed_font_end:\n.text\n");
extern "C" const unsigned char riffed_font_start[] __asm__("_riffed_font_start");
extern "C" const unsigned char riffed_font_end[] __asm__("_riffed_font_end");

static void LoadFonts(float px)
{
    ImGuiIO& io = ImGui::GetIO();
    static const ImWchar ranges[] = { 0x0020, 0x00FF, 0x0100, 0x017F, 0x2010, 0x2026, 0 };
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)riffed_font_start, (int)(riffed_font_end - riffed_font_start), px, &cfg, ranges);
    Log("Font: built-in Roboto @ %.0fpx", px);
}

static void InitImGui(IDirect3DDevice9* dev)
{
    D3DDEVICE_CREATION_PARAMETERS cp{};
    dev->GetCreationParameters(&cp);
    g_hwnd = cp.hFocusWindow;
    if (!g_hwnd) {
        IDirect3DSwapChain9* sc = nullptr;
        D3DPRESENT_PARAMETERS pp{};
        if (SUCCEEDED(dev->GetSwapChain(0, &sc))) { sc->GetPresentParameters(&pp); sc->Release(); g_hwnd = pp.hDeviceWindow; }
    }
    RECT rc{}; GetClientRect(g_hwnd, &rc);
    float scale = std::max(1.0f, (rc.bottom - rc.top) / 1080.0f);
    Log("Game window %p, %ldx%ld, UI scale %.2f", g_hwnd, rc.right - rc.left, rc.bottom - rc.top, scale);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    static std::string ini = GameDir() + "RiffedMenu-imgui.ini";
    io.IniFilename = ini.c_str();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    LoadFonts(17.0f * scale);
    Menu::ApplyStyle(scale);
    Features::RiskLoad();
    Config::Init();
    Update::Start();
    g_multiplayer = GetModuleHandleA("samp.dll") != nullptr || GetModuleHandleA("netc.dll") != nullptr;
    if (g_multiplayer) Log("Multiplayer client detected: combat features are disabled");
    Combat::InstallBulletHook();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX9_Init(dev);
    g_origWndProc = (WNDPROC)SetWindowLongPtrW(g_hwnd, GWLP_WNDPROC, (LONG_PTR)WndProc);
    g_device = dev;
    g_imguiReady = true;
    Log("ImGui initialised (backend dx9 + win32)");
}

static void PollToggleKey()
{
    static bool wasDown = false;
    bool down = (GetAsyncKeyState(g_toggles.menuKey) & 0x8000) != 0;
    if (down && !wasDown && GetForegroundWindow() == g_hwnd) {
        Menu::Toggle();
        ImGui::GetIO().MouseDrawCursor = Menu::IsOpen();
        if (Menu::IsOpen()) ClipCursor(nullptr);
        else Config::AutoSave();
    }
    wasDown = down;
}

// One overlay frame. insideScene: called from EndScene, so no BeginScene/EndScene of our own.
static void DrawFrame(IDirect3DDevice9* dev, bool insideScene, const char* via)
{
    if (g_inOverlay) return;
    g_inOverlay = true;
    if (!g_imguiReady) { InitImGui(dev); Log("First overlay frame drawn via %s", via); }
    if (dev == g_device && dev->TestCooperativeLevel() == D3D_OK) {
        InterlockedIncrement(&g_cntFrames);
        // Game tick: runs on the main thread after the frame's world update,
        // so queued actions and per-frame cheats run here (no exe-specific hook needed).
        g_gameFocused = GetForegroundWindow() == g_hwnd && !Captured();
        if (Game::Supported()) Features::Tick();
        PollToggleKey();
        ImGui_ImplDX9_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        Menu::Render(dev);
        ImGui::EndFrame();
        if (insideScene) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        } else if (SUCCEEDED(dev->BeginScene())) {
            ImGui::Render();
            ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
            dev->EndScene();
        }
    }
    g_inOverlay = false;
}

// Vtable-slot (VMT) hooks: we swap entries in the device's function table instead of patching
// d3d9 code. Overlays like RTSS/Discord inline-patch those same functions and undo foreign
// patches; going through the table leaves their hooks (and their Reset bookkeeping) intact.
struct VSlot { void** vtable; int index; void* original; void* detour; const char* name; };
static std::vector<VSlot> g_slots;
static SRWLOCK g_slotLock = SRWLOCK_INIT;

static void* Orig(void* obj, int index)
{
    void** vt = *reinterpret_cast<void***>(obj);
    AcquireSRWLockShared(&g_slotLock);
    void* o = nullptr;
    for (auto& s : g_slots) if (s.vtable == vt && s.index == index) { o = s.original; break; }
    if (!o) for (auto& s : g_slots) if (s.index == index) { o = s.original; break; }   // object switched vtables
    ReleaseSRWLockShared(&g_slotLock);
    return o;
}

static bool SwapSlot(void** vt, int index, void* detour, const char* name)
{
    AcquireSRWLockExclusive(&g_slotLock);
    bool known = false;
    for (auto& s : g_slots) known |= s.vtable == vt && s.index == index;
    if (!known && vt[index] != detour) {
        void* orig = vt[index];
        DWORD old;
        if (VirtualProtect(&vt[index], sizeof(void*), PAGE_READWRITE, &old)) {
            vt[index] = detour;
            VirtualProtect(&vt[index], sizeof(void*), old, &old);
            g_slots.push_back({ vt, index, orig, detour, name });
            Log("Hooked %s via vtable %p[%d], original %s", name, vt, index, ModuleOf(orig).c_str());
            known = true;
        } else {
            Log("VirtualProtect failed for %s vtable %p", name, vt);
        }
    }
    ReleaseSRWLockExclusive(&g_slotLock);
    return known;
}

static HRESULT WINAPI hkPresent(IDirect3DDevice9* dev, const RECT* s, const RECT* d, HWND w, const RGNDATA* r)
{
    auto o = (Present_t)Orig(dev, 17);
    InterlockedIncrement(&g_cntPresent);
    g_lastPresentTick = GetTickCount();
    DrawFrame(dev, false, "IDirect3DDevice9::Present");
    return o(dev, s, d, w, r);
}

static HRESULT WINAPI hkEndScene(IDirect3DDevice9* dev)
{
    auto o = (EndScene_t)Orig(dev, 42);
    InterlockedIncrement(&g_cntEndScene);
    // Fallback only: no Present has come through our slot for a second.
    if (!g_inOverlay && GetTickCount() - g_lastPresentTick > 1000) {
        IDirect3DSurface9 *rt = nullptr, *bb = nullptr;
        dev->GetRenderTarget(0, &rt);
        dev->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb);
        bool toBackBuffer = rt && rt == bb;
        if (rt) rt->Release();
        if (bb) bb->Release();
        if (toBackBuffer) DrawFrame(dev, true, "EndScene fallback");
    }
    return o(dev);
}

using SetTransform_t = HRESULT(WINAPI*)(IDirect3DDevice9*, D3DTRANSFORMSTATETYPE, const D3DMATRIX*);

// Remembers the main 3D camera. Shadow and effect cameras are filtered out by shape: only the
// main camera is a perspective projection with the same aspect ratio as the screen.
static HRESULT WINAPI hkSetTransform(IDirect3DDevice9* dev, D3DTRANSFORMSTATETYPE state, const D3DMATRIX* m)
{
    auto o = (SetTransform_t)Orig(dev, 44);
    static D3DMATRIX view{}, proj{};
    if (m && (state == D3DTS_VIEW || state == D3DTS_PROJECTION)) {
        (state == D3DTS_VIEW ? view : proj) = *m;
        bool perspective = proj._44 == 0.0f && proj._34 != 0.0f && proj._11 > 0.0f;
        if (perspective && g_imguiReady) {
            ImVec2 d = ImGui::GetIO().DisplaySize;
            float ratio = proj._22 / proj._11, want = d.y > 0 ? d.x / d.y : 0;
            if (want > 0 && ratio > want * 0.85f && ratio < want * 1.15f)
                Game::SetRenderCamera((const float*)&view, (const float*)&proj);
        }
    }
    return o(dev, state, m);
}

static HRESULT WINAPI hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp)
{
    auto o = (Reset_t)Orig(dev, 16);
    InterlockedIncrement(&g_cntReset);
    if (g_imguiReady) ImGui_ImplDX9_InvalidateDeviceObjects();
    HRESULT hr = o(dev, pp);
    static LONG failures = 0;
    if (SUCCEEDED(hr) || InterlockedIncrement(&failures) <= 5)   // a lost device retries every frame
        Log("IDirect3DDevice9::Reset -> 0x%08lX (%ux%u)", hr, pp ? pp->BackBufferWidth : 0, pp ? pp->BackBufferHeight : 0);
    if (SUCCEEDED(hr)) failures = 0;
    if (g_imguiReady && SUCCEEDED(hr)) ImGui_ImplDX9_CreateDeviceObjects();
    return hr;
}

static bool HookDevice(IDirect3DDevice9* dev, const char* source)
{
    void** vt = *reinterpret_cast<void***>(dev);
    static void* lastDev = nullptr;
    static void** lastVt = nullptr;
    if (dev != lastDev || vt != lastVt) {
        Log("Hooking %s device %p, vtable %p [%s]", source, dev, vt, ModuleOf(vt).c_str());
        lastDev = dev; lastVt = vt;
    }
    bool ok = SwapSlot(vt, 17, (void*)hkPresent, "Device::Present");
    ok &= SwapSlot(vt, 16, (void*)hkReset, "Device::Reset");
    ok &= SwapSlot(vt, 42, (void*)hkEndScene, "Device::EndScene");
    if (Game::Supported()) SwapSlot(vt, 44, (void*)hkSetTransform, "Device::SetTransform");
    return ok;
}

// Re-hooks if the game creates a new device / switches vtables, reports slots that someone
// else replaced, and logs which render path is live (makes "menu doesn't show" diagnosable).
static DWORD WINAPI Watchdog(LPVOID)
{
    LONG lastFrames = -1;
    for (int i = 0;; ++i) {
        Sleep(i < 30 ? 2000 : 30000);
        if (auto* dev = (IDirect3DDevice9*)Game::D3DDevice()) HookDevice(dev, "game's");

        AcquireSRWLockShared(&g_slotLock);
        for (auto& s : g_slots) {
            static std::vector<void*> reported;
            void* key = &s.vtable[s.index];
            bool seen = false;
            for (void* r : reported) seen |= r == key;
            if (s.vtable[s.index] != s.detour && !seen) {
                reported.push_back(key);
                Log("Watchdog: %s slot was replaced by %s", s.name, ModuleOf(s.vtable[s.index]).c_str());
            }
        }
        ReleaseSRWLockShared(&g_slotLock);

        if (i < 30 || g_cntFrames == lastFrames)
            Log("Watchdog: Present=%ld EndScene=%ld Reset=%ld overlayFrames=%ld",
                g_cntPresent, g_cntEndScene, g_cntReset, g_cntFrames);
        lastFrames = g_cntFrames;
    }
    return 0;
}

bool Hooks::InstallRenderHooks()
{
    Log("d3d9.dll in use: %s", ModuleOf((const void*)GetProcAddress(GetModuleHandleA("d3d9.dll"), "Direct3DCreate9")).c_str());
    LogLoadedModules();

    if (Game::Supported()) {
        // The game's own device (RenderWare global). A throwaway device fails on Windows with
        // D3DERR_DEVICELOST once the game owns an exclusive fullscreen one.
        bool ok = false;
        for (int i = 0; i < 1200 && !ok; ++i) {        // up to 60 s for the game to create it
            if (void* dev = Game::D3DDevice()) ok = HookDevice((IDirect3DDevice9*)dev, "game's");
            else Sleep(50);
        }
        if (!ok) Log("Game never created a D3D9 device?");
        if (HANDLE t = CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr)) CloseHandle(t);
        return ok;
    }

    // Unknown exe: grab the vtable from a throwaway device. All devices from one d3d9.dll share it.
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = g_module;
    wc.lpszClassName = L"RiffedMenuDummy";
    RegisterClassExW(&wc);
    HWND wnd = CreateWindowExW(0, wc.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, g_module, nullptr);

    bool ok = false;
    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (d3d) {
        D3DPRESENT_PARAMETERS pp{};
        pp.Windowed = TRUE;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        pp.BackBufferFormat = D3DFMT_UNKNOWN;
        pp.hDeviceWindow = wnd;
        IDirect3DDevice9* dev = nullptr;
        HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, wnd,
                                       D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_DISABLE_DRIVER_MANAGEMENT, &pp, &dev);
        if (SUCCEEDED(hr) && dev) {
            ok = HookDevice(dev, "dummy");
            dev->Release();
        } else {
            Log("Dummy CreateDevice failed: 0x%08lX", hr);
        }
        d3d->Release();
    } else {
        Log("Direct3DCreate9 failed");
    }
    DestroyWindow(wnd);
    UnregisterClassW(wc.lpszClassName, g_module);
    return ok;
}

// ===========================================================================
// Input: DirectInput8 + cursor
// ===========================================================================
using GetDeviceState_t = HRESULT(WINAPI*)(void*, DWORD, LPVOID);
using GetDeviceData_t  = HRESULT(WINAPI*)(void*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);

// Up to 4 distinct implementations (A/W x keyboard/mouse), each needs its own trampoline.
template <int N> struct DIHook {
    static inline GetDeviceState_t oState = nullptr;
    static inline GetDeviceData_t  oData  = nullptr;
    static HRESULT WINAPI State(void* self, DWORD size, LPVOID data)
    {
        HRESULT hr = oState(self, size, data);
        if (SUCCEEDED(hr) && Captured() && data) memset(data, 0, size);
        return hr;
    }
    static HRESULT WINAPI Data(void* self, DWORD objSize, LPDIDEVICEOBJECTDATA rgdod, LPDWORD inOut, DWORD flags)
    {
        HRESULT hr = oData(self, objSize, rgdod, inOut, flags);
        if (SUCCEEDED(hr) && Captured() && inOut) *inOut = 0;
        return hr;
    }
};

using SetCursorPos_t = BOOL(WINAPI*)(int, int);
using ClipCursor_t   = BOOL(WINAPI*)(const RECT*);
static SetCursorPos_t oSetCursorPos = nullptr;
static ClipCursor_t   oClipCursor   = nullptr;
static BOOL WINAPI hkSetCursorPos(int x, int y) { return Captured() ? TRUE : oSetCursorPos(x, y); }
static BOOL WINAPI hkClipCursor(const RECT* r)  { return oClipCursor(Captured() ? nullptr : r); }

bool Hooks::InstallInputHooks()
{
    void* stateFns[4]; void* dataFns[4]; int n = 0;
    auto collect = [&](void* devIface) {
        void** vt = *reinterpret_cast<void***>(devIface);
        for (int i = 0; i < n; ++i) if (stateFns[i] == vt[9]) return;
        stateFns[n] = vt[9]; dataFns[n] = vt[10]; ++n;
    };

    IDirectInput8A* dia = nullptr;
    IDirectInput8W* diw = nullptr;
    if (SUCCEEDED(DirectInput8Create(g_module, DIRECTINPUT_VERSION, IID_IDirectInput8A, (void**)&dia, nullptr))) {
        for (REFGUID g : { GUID_SysKeyboard, GUID_SysMouse }) {
            IDirectInputDevice8A* d = nullptr;
            if (SUCCEEDED(dia->CreateDevice(g, &d, nullptr))) { collect(d); d->Release(); }
        }
        dia->Release();
    }
    if (SUCCEEDED(DirectInput8Create(g_module, DIRECTINPUT_VERSION, IID_IDirectInput8W, (void**)&diw, nullptr))) {
        for (REFGUID g : { GUID_SysKeyboard, GUID_SysMouse }) {
            IDirectInputDevice8W* d = nullptr;
            if (SUCCEEDED(diw->CreateDevice(g, &d, nullptr))) { collect(d); d->Release(); }
        }
        diw->Release();
    }
    Log("DirectInput: %d distinct device implementation(s)", n);

    bool ok = n > 0;
    if (n > 0) { ok &= Hook(stateFns[0], DIHook<0>::State, &DIHook<0>::oState, "DIDevice::GetDeviceState#0");
                 ok &= Hook(dataFns[0],  DIHook<0>::Data,  &DIHook<0>::oData,  "DIDevice::GetDeviceData#0"); }
    if (n > 1) { ok &= Hook(stateFns[1], DIHook<1>::State, &DIHook<1>::oState, "DIDevice::GetDeviceState#1");
                 ok &= Hook(dataFns[1],  DIHook<1>::Data,  &DIHook<1>::oData,  "DIDevice::GetDeviceData#1"); }
    if (n > 2) { ok &= Hook(stateFns[2], DIHook<2>::State, &DIHook<2>::oState, "DIDevice::GetDeviceState#2");
                 ok &= Hook(dataFns[2],  DIHook<2>::Data,  &DIHook<2>::oData,  "DIDevice::GetDeviceData#2"); }
    if (n > 3) { ok &= Hook(stateFns[3], DIHook<3>::State, &DIHook<3>::oState, "DIDevice::GetDeviceState#3");
                 ok &= Hook(dataFns[3],  DIHook<3>::Data,  &DIHook<3>::oData,  "DIDevice::GetDeviceData#3"); }

    HMODULE user32 = GetModuleHandleA("user32.dll");
    ok &= Hook((void*)GetProcAddress(user32, "SetCursorPos"), hkSetCursorPos, &oSetCursorPos, "user32!SetCursorPos");
    ok &= Hook((void*)GetProcAddress(user32, "ClipCursor"), hkClipCursor, &oClipCursor, "user32!ClipCursor");
    return ok;
}
