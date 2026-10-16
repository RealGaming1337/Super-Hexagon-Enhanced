// SPDX-License-Identifier: MIT
// version.dll proxy: SDL2.dll imports VERSION.dll, so Windows loads this file from the
// game folder first. Every export lazily forwards to the real System32 version.dll.
#include <windows.h>

static const char* const kNames[17] = {
    "GetFileVersionInfoA",     "GetFileVersionInfoByHandle", "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",   "GetFileVersionInfoSizeA",    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW", "GetFileVersionInfoSizeW",  "GetFileVersionInfoW",
    "VerFindFileA",            "VerFindFileW",               "VerInstallFileA",
    "VerInstallFileW",         "VerLanguageNameA",           "VerLanguageNameW",
    "VerQueryValueA",          "VerQueryValueW",
};

extern "C" FARPROC g_real[17] = {};
static volatile LONG g_resolved = 0;

extern "C" void __cdecl ResolveReal()
{
    if (g_resolved) return;
    char path[MAX_PATH];
    UINT n = GetSystemDirectoryA(path, MAX_PATH);  // WOW64 redirects this to SysWOW64
    lstrcpyA(path + n, "\\version.dll");
    HMODULE real = LoadLibraryA(path);
    if (real)
        for (int i = 0; i < 17; i++) g_real[i] = GetProcAddress(real, kNames[i]);
    InterlockedExchange(&g_resolved, 1);
}

#define STUB(i)                                                  \
    extern "C" __declspec(naked) void Proxy_##i()                \
    {                                                            \
        __asm pushad                                             \
        __asm call ResolveReal                                   \
        __asm popad                                              \
        __asm jmp dword ptr [g_real + i * 4]                     \
    }

STUB(0) STUB(1) STUB(2) STUB(3) STUB(4) STUB(5) STUB(6) STUB(7) STUB(8)
STUB(9) STUB(10) STUB(11) STUB(12) STUB(13) STUB(14) STUB(15) STUB(16)
