#include "log.h"
#include <cstdio>
#include <cstdarg>
#include <dbghelp.h>

static FILE*            g_log = nullptr;
static std::string      g_dir;
static CRITICAL_SECTION g_lock;
static HMODULE          g_self = nullptr;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;

const std::string& GameDir() { return g_dir; }

void LogInit(HMODULE self)
{
    g_self = self;
    InitializeCriticalSection(&g_lock);

    char path[MAX_PATH];
    GetModuleFileNameA(self, path, MAX_PATH);
    g_dir = path;
    g_dir = g_dir.substr(0, g_dir.find_last_of("\\/") + 1);

    g_log = fopen((g_dir + "RiffedMenu.log").c_str(), "w");
    SYSTEMTIME t; GetLocalTime(&t);
    Log("RiffedMenu log started %04d-%02d-%02d %02d:%02d:%02d", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    Log("Module: %s", path);
}

void Log(const char* fmt, ...)
{
    char buf[2048];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    EnterCriticalSection(&g_lock);
    DWORD ms = GetTickCount();
    if (g_log) { fprintf(g_log, "[%8lu.%03lu] %s\n", ms / 1000, ms % 1000, buf); fflush(g_log); }
    LeaveCriticalSection(&g_lock);
    OutputDebugStringA(buf);   // also visible via WINEDEBUG=+debugstr
}

// ---------------------------------------------------------------------------
// Crash handling
// ---------------------------------------------------------------------------

static void DescribeAddress(DWORD addr, char* out, size_t n)
{
    HMODULE mod = nullptr;
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCSTR)addr, &mod) && mod) {
        char name[MAX_PATH];
        GetModuleFileNameA(mod, name, MAX_PATH);
        const char* base = strrchr(name, '\\');
        snprintf(out, n, "%s+0x%lX", base ? base + 1 : name, addr - (DWORD)mod);
    } else {
        snprintf(out, n, "0x%08lX", addr);
    }
}

static void LogContext(EXCEPTION_POINTERS* ep)
{
    EXCEPTION_RECORD* er = ep->ExceptionRecord;
    CONTEXT* c = ep->ContextRecord;
    char where[MAX_PATH + 32];
    DescribeAddress((DWORD)er->ExceptionAddress, where, sizeof(where));
    Log("  code=0x%08lX at %s", er->ExceptionCode, where);
    if (er->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && er->NumberParameters >= 2)
        Log("  %s address 0x%08lX", er->ExceptionInformation[0] == 0 ? "read" : er->ExceptionInformation[0] == 1 ? "write" : "execute",
            (DWORD)er->ExceptionInformation[1]);
    Log("  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX", c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi);
    Log("  EBP=%08lX ESP=%08lX EIP=%08lX", c->Ebp, c->Esp, c->Eip);

    // Raw stack scan: print anything on the stack that points into a loaded module.
    DWORD* sp = (DWORD*)c->Esp;
    int found = 0;
    for (int i = 0; i < 1024 && found < 24; ++i) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(sp + i, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) break;
        DWORD v = sp[i];
        HMODULE m;
        if (v > 0x10000 && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)v, &m)) {
            DescribeAddress(v, where, sizeof(where));
            Log("  stack[%03d] %s", i, where);
            ++found;
        }
    }
}

static bool IsFatal(DWORD code)
{
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:   case EXCEPTION_INT_DIVIDE_BY_ZERO:  case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return true;
    }
    return false;
}

// First-chance: log only (some of these get handled by the game/Wine), capped to avoid spam.
static LONG CALLBACK VectoredHandler(EXCEPTION_POINTERS* ep)
{
    static volatile LONG count = 0;
    if (IsFatal(ep->ExceptionRecord->ExceptionCode) && InterlockedIncrement(&count) <= 8) {
        Log("First-chance exception #%ld:", count);
        LogContext(ep);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* ep)
{
    Log("==== UNHANDLED EXCEPTION - game is going down ====");
    LogContext(ep);

    typedef BOOL(WINAPI* MiniDumpWriteDump_t)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION,
                                              PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION);
    if (HMODULE dbg = LoadLibraryA("dbghelp.dll")) {
        auto dump = (MiniDumpWriteDump_t)GetProcAddress(dbg, "MiniDumpWriteDump");
        std::string path = g_dir + "RiffedMenu-crash.dmp";
        HANDLE f = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump && f != INVALID_HANDLE_VALUE) {
            MINIDUMP_EXCEPTION_INFORMATION mei{ GetCurrentThreadId(), ep, FALSE };
            BOOL ok = dump(GetCurrentProcess(), GetCurrentProcessId(), f,
                           (MINIDUMP_TYPE)(MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory), &mei, nullptr, nullptr);
            Log("Minidump %s: %s", ok ? "written" : "FAILED", path.c_str());
        }
        if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    }
    return g_prevFilter ? g_prevFilter(ep) : EXCEPTION_CONTINUE_SEARCH;
}

void InstallCrashHandlers()
{
    AddVectoredExceptionHandler(1, VectoredHandler);
    g_prevFilter = SetUnhandledExceptionFilter(UnhandledFilter);
    Log("Crash handlers installed");
}
