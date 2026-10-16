// SPDX-License-Identifier: MIT
// Code patching helpers: byte checks, raw writes, vtable/call redirection and "callouts":
// runtime-generated trampolines that hand the full register state to a C handler.
// Every patch first checks the exact original bytes (Expect) and is skipped if they differ, so
// an unknown game build is left untouched.
#include "hexfps.h"
#include <stdio.h>

bool Expect(uintptr_t va, const char* hex)
{
    const uint8_t* p = (const uint8_t*)VA(va);
    for (int i = 0; hex[0] && hex[1]; i++) {
        unsigned v;
        sscanf_s(hex, "%2x", &v);
        if (p[i] != (uint8_t)v) {
            Log("signature mismatch at %08X+%d (got %02X want %02X)", (unsigned)va, i, p[i], v);
            return false;
        }
        hex += 2;
        while (*hex == ' ') hex++;
    }
    return true;
}

void WriteMem(uintptr_t va, const void* data, size_t n)
{
    void* p = (void*)VA(va);
    DWORD old;
    VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old);
    memcpy(p, data, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
}

void WriteJmp(uintptr_t va, void* target, size_t len)
{
    uint8_t buf[16];
    buf[0] = 0xE9;
    *(int32_t*)(buf + 1) = (int32_t)((uintptr_t)target - (VA(va) + 5));
    for (size_t i = 5; i < len; i++) buf[i] = 0x90;
    WriteMem(va, buf, len);
}

void* HookVtable(uintptr_t slotVa, uintptr_t expectVa, void* hook)
{
    void** slot = (void**)VA(slotVa);
    if ((uintptr_t)*slot != VA(expectVa)) {
        Log("vtable slot %08X unexpected (%p)", (unsigned)slotVa, *slot);
        return nullptr;
    }
    void* orig = *slot;
    WriteMem(slotVa, &hook, sizeof(hook));
    return orig;
}

void* RedirectCall(uintptr_t callVa, uintptr_t expectTargetVa, void* hook)
{
    uint8_t* p = (uint8_t*)VA(callVa);
    uintptr_t target = VA(callVa) + 5 + *(int32_t*)(p + 1);
    if (p[0] != 0xE8 || target != VA(expectTargetVa)) {
        Log("call at %08X unexpected", (unsigned)callVa);
        return nullptr;
    }
    uint8_t buf[5] = { 0xE8 };
    *(int32_t*)(buf + 1) = (int32_t)((uintptr_t)hook - (VA(callVa) + 5));
    WriteMem(callVa, buf, 5);
    return (void*)target;
}

// ---- callouts -------------------------------------------------------------------------------
// Trampoline layout:
//   pushad; sub esp,128; movdqu [esp+16n],xmmN      save everything
//   push site; lea eax,[esp+4]; push eax; call handler; add esp,8
//   test eax,eax; jnz 1f; mov eax,<va+len>; 1: mov [cont],eax
//   movdqu xmmN,[esp+16n]; add esp,128; popad       restore (handler may have edited ctx)
//   <bytes [va+keep, va+len) re-executed>; jmp [cont]
static uint8_t* g_code;
static size_t g_used;

static void Emit(const void* p, size_t n) { memcpy(g_code + g_used, p, n); g_used += n; }
static void Emit8(uint8_t b) { Emit(&b, 1); }
static void Emit32(uint32_t v) { Emit(&v, 4); }

bool Callout(uintptr_t va, size_t len, size_t keep, CalloutHandler handler, int site, const char* expectHex)
{
    if (!g_code) {
        g_code = (uint8_t*)VirtualAlloc(nullptr, 16384, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!g_code) return false;
    }
    if (!Expect(va, expectHex) || len < 5 || keep > len) return false;

    uint32_t* cont = (uint32_t*)(g_code + g_used);   // per-trampoline continuation slot
    Emit32(0);
    uint8_t* tramp = g_code + g_used;
    Emit8(0x60);                                                        // pushad
    static const uint8_t subEsp[] = { 0x81, 0xEC, 0x80, 0, 0, 0 };      // sub esp, 128
    Emit(subEsp, sizeof(subEsp));
    for (int n = 0; n < 8; n++) {                                       // movdqu [esp+16*n], xmmN
        const uint8_t op[] = { 0xF3, 0x0F, 0x7F, (uint8_t)(0x44 | (n << 3)), 0x24, (uint8_t)(16 * n) };
        Emit(op, sizeof(op));
    }
    Emit8(0x68); Emit32((uint32_t)site);                                // push site
    static const uint8_t leaCtx[] = { 0x8D, 0x44, 0x24, 0x04, 0x50 };   // lea eax,[esp+4]; push eax
    Emit(leaCtx, sizeof(leaCtx));
    Emit8(0xE8); Emit32((uint32_t)((uintptr_t)handler - ((uintptr_t)g_code + g_used + 4)));  // call handler
    static const uint8_t post[] = { 0x83, 0xC4, 0x08, 0x85, 0xC0, 0x75, 0x05 };  // add esp,8; test eax,eax; jnz +5
    Emit(post, sizeof(post));
    Emit8(0xB8); Emit32((uint32_t)VA(va + len));                        // mov eax, default
    Emit8(0xA3); Emit32((uint32_t)(uintptr_t)cont);                     // mov [cont], eax
    for (int n = 0; n < 8; n++) {                                       // movdqu xmmN, [esp+16*n]
        const uint8_t op[] = { 0xF3, 0x0F, 0x6F, (uint8_t)(0x44 | (n << 3)), 0x24, (uint8_t)(16 * n) };
        Emit(op, sizeof(op));
    }
    static const uint8_t addEsp[] = { 0x81, 0xC4, 0x80, 0, 0, 0, 0x61 };  // add esp,128; popad
    Emit(addEsp, sizeof(addEsp));
    Emit((const void*)VA(va + keep), len - keep);                       // displaced instructions
    Emit8(0xFF); Emit8(0x25); Emit32((uint32_t)(uintptr_t)cont);        // jmp [cont]
    WriteJmp(va, tramp, len);
    return true;
}
