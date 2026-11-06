// FrameTamer: background FFXI clients skip drawing most frames while the game keeps running.
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <tlhelp32.h>
#include <share.h>
#include <string.h>
#include <intrin.h>
#include <string>
#include <unordered_map>
#include <vector>
#include "version.h"

extern "C" {
typedef struct lua_State lua_State;
typedef int (*lua_CFunction)(lua_State *);
__declspec(dllimport) void lua_createtable(lua_State *L, int narr, int nrec);
__declspec(dllimport) void lua_pushcclosure(lua_State *L, lua_CFunction fn, int n);
__declspec(dllimport) void lua_setfield(lua_State *L, int idx, const char *k);
__declspec(dllimport) const char *lua_tolstring(lua_State *L, int idx, size_t *len);
__declspec(dllimport) void lua_pushboolean(lua_State *L, int b);
__declspec(dllimport) void lua_pushstring(lua_State *L, const char *s);
__declspec(dllimport) int lua_toboolean(lua_State *L, int idx);
__declspec(dllimport) double lua_tonumber(lua_State *L, int idx);
}

// The game's frame, roughly:
//
//     device check        <- frame start: decide draw or skip, hold for the game fps cap
//     BeginScene
//     update + 3D scene   <- scene draw flag, model draw, occlusion test
//     game task, UI       <- draw wrappers
//     EndScene
//     Present             <- skipped on dropped frames
//     divisor pacing
//
// The update always runs. A dropped frame only loses its drawing.

// Game Hooks
// The game's functions are thiscall. Declaring them __fastcall with an unused edx puts `this` in ecx.
static bool(__fastcall *frameStartOriginal)(void *self, void *unused) = nullptr;
static void(__fastcall *sceneDrawOriginal)(void *self, void *unused, int draw) = nullptr;
static HRESULT(__fastcall *drawUserPrimitiveOriginal)(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d) = nullptr;
static HRESULT(__fastcall *drawPrimitiveOriginal)(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c) = nullptr;
static void(__fastcall *presentOriginal)(void *self, void *unused) = nullptr;
static int(__fastcall *visibilityTestOriginal)(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g,
                                               uint32_t h, uint32_t i) = nullptr;
static std::unordered_map<uint64_t, int> visibilityAnswers;
static int(__fastcall *modelDrawOriginal)(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t *e, uint32_t *f) = nullptr;

static volatile bool enabled = false;
static volatile double drawIntervalMs = 1000.0;
static volatile double gameIntervalMs = 0.0;
static volatile double cutsceneIntervalMs = 0.0;
static volatile bool inEvent = false;
static LARGE_INTEGER previousFrameStartedAt;
static bool installed = false;
static bool skipFrame = false;
static bool wasForeground = true;
static FILE *logFile = nullptr;
static std::string logPath;
static SRWLOCK logLock = SRWLOCK_INIT;
static volatile bool debugLogging = false;

static LARGE_INTEGER counterFrequency;
static LARGE_INTEGER frameStartedAt;
static LARGE_INTEGER lastDrawnAt;
static LARGE_INTEGER statsStartedAt;
static uint64_t statsCpuStart = 0;
static int statsFrames = 0;
static int statsDrawn = 0;
static double statsWorkMs = 0.0;

/// Writes one timestamped line to the log file, creating the file on first use.
/// @param text the line to write
static void writeLog(const char *text)
{
    AcquireSRWLockExclusive(&logLock);
    if (!logFile && !logPath.empty()) logFile = _fsopen(logPath.c_str(), "a", _SH_DENYNO);
    if (logFile) {
        SYSTEMTIME now;
        GetLocalTime(&now);
        fprintf(logFile, "%02d:%02d:%02d %s\n", now.wHour, now.wMinute, now.wSecond, text);
        fflush(logFile);
    }
    ReleaseSRWLockExclusive(&logLock);
}

/// Writes one timestamped line to the log file when debug logging is on.
/// @param text the line to write
static void writeDebugLog(const char *text)
{
    if (debugLogging) writeLog(text);
}

/// Returns this process's total user plus kernel CPU time in 100 ns units.
static uint64_t processCpuTime()
{
    FILETIME created, exited, kernel, user;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    return ((uint64_t)kernel.dwHighDateTime << 32 | kernel.dwLowDateTime) + ((uint64_t)user.dwHighDateTime << 32 | user.dwLowDateTime);
}

/// Returns true when the foreground window belongs to this game client.
static bool isForeground()
{
    DWORD processId = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &processId);
    return processId == GetCurrentProcessId();
}

static double elapsedMs(const LARGE_INTEGER &from, const LARGE_INTEGER &to)
{
    return (double)(to.QuadPart - from.QuadPart) * 1000.0 / (double)counterFrequency.QuadPart;
}

// This hooks the device check at the top of every frame. Everything after it in the frame
// reads skipFrame, so this is the one place that decides whether the frame draws.
static bool __fastcall frameStartDetour(void *self, void *unused)
{
    QueryPerformanceCounter(&frameStartedAt);
    bool foreground = isForeground();
    if (foreground != wasForeground) {
        writeDebugLog(foreground ? "focus gained" : "focus lost");
        wasForeground = foreground;
    }
    double capIntervalMs = inEvent ? cutsceneIntervalMs : gameIntervalMs;
    if (enabled && !foreground && capIntervalMs > 0.0) {
        double remaining = capIntervalMs - elapsedMs(previousFrameStartedAt, frameStartedAt);
        while (remaining >= 1.0) {
            Sleep((DWORD)remaining);
            QueryPerformanceCounter(&frameStartedAt);
            remaining = capIntervalMs - elapsedMs(previousFrameStartedAt, frameStartedAt);
        }
    }
    previousFrameStartedAt = frameStartedAt;
    skipFrame = enabled && !foreground && elapsedMs(lastDrawnAt, frameStartedAt) < drawIntervalMs;
    if (!skipFrame) lastDrawnAt = frameStartedAt;
    return frameStartOriginal(self, unused);
}

// The 3D scene draw already takes a draw flag. Passing 0 lets the game skip it on its own.
static void __fastcall sceneDrawDetour(void *self, void *unused, int draw)
{
    sceneDrawOriginal(self, unused, skipFrame ? 0 : draw);
}

// All UI and text drawing goes through these two DrawPrimitive wrappers. They only touch
// device state, so a dropped frame can return S_OK without calling them.
static HRESULT __fastcall drawUserPrimitiveDetour(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    if (skipFrame) return S_OK;
    return drawUserPrimitiveOriginal(self, unused, a, b, c, d);
}

static HRESULT __fastcall drawPrimitiveDetour(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c)
{
    if (skipFrame) return S_OK;
    return drawPrimitiveOriginal(self, unused, a, b, c);
}

// The occlusion test draws a quad and reads pixels back, which stalls on the GPU. Dropped frames
// reuse the last real answer for the same caller and object, or call it fully visible.
static int __fastcall visibilityTestDetour(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e, uint32_t f, uint32_t g,
                                           uint32_t h, uint32_t i)
{
    uint64_t key = (uint64_t)(uint32_t)_ReturnAddress() << 32 | (uint32_t)self;
    if (skipFrame) {
        auto answer = visibilityAnswers.find(key);
        return answer != visibilityAnswers.end() ? answer->second : 256;
    }
    int answer = visibilityTestOriginal(self, unused, a, b, c, d, e, f, g, h, i);
    if (visibilityAnswers.size() > 8192) visibilityAnswers.clear();
    visibilityAnswers[key] = answer;
    return answer;
}

// Character model draw, where the game skins vertices on the CPU. Its only outputs are the two
// counters it zeroes on entry, so a dropped frame zeroes them and returns.
static int __fastcall modelDrawDetour(void *self, void *unused, uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t *e, uint32_t *f)
{
    if (skipFrame) {
        *e = 0;
        *f = 0;
        return 0;
    }
    return modelDrawOriginal(self, unused, a, b, c, d, e, f);
}

// Skipping Present leaves the last drawn frame on screen. The debug summary is taken here too.
static void __fastcall presentDetour(void *self, void *unused)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    statsFrames++;
    statsWorkMs += elapsedMs(frameStartedAt, now);
    if (!skipFrame) statsDrawn++;

    double windowMs = elapsedMs(statsStartedAt, now);
    if (windowMs >= 60000.0) {
        uint64_t cpu = processCpuTime();
        char line[200];
        sprintf_s(line, "frames/s %.1f drawn/s %.2f work %.2f ms/frame cpu %.1f%% %s %s", statsFrames * 1000.0 / windowMs, statsDrawn * 1000.0 / windowMs,
                  statsWorkMs / statsFrames, (double)(cpu - statsCpuStart) / 10000.0 / windowMs * 100.0, wasForeground ? "fg" : "bg", enabled ? "on" : "off");
        writeDebugLog(line);
        statsStartedAt = now;
        statsCpuStart = cpu;
        statsFrames = 0;
        statsDrawn = 0;
        statsWorkMs = 0.0;
    }

    if (skipFrame) return;
    presentOriginal(self, unused);
}

// Hook Installation
struct Hook {
    const char *name;
    const char *pattern;
    uint32_t length;
    void *detour;
    void **original;
    uint8_t *target;
};

// Each pattern has to match exactly once. length is how many bytes the jump replaces, always
// ending on an instruction boundary.
static Hook hooks[] = {
    {"frame start", "8B 41 0C 50 8B 08 FF 51 0C 85 C0 0F", 6, (void *)frameStartDetour, (void **)&frameStartOriginal, nullptr},
    {"scene draw", "F6 41 04 04 74 19 8B 44 24 04 C6 05", 6, (void *)sceneDrawDetour, (void **)&sceneDrawOriginal, nullptr},
    {"draw user primitive",
     "83 EC 08 8D 44 24 00 56 8B F1 8D 4C 24 08 50 51 8B CE E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 53 55 57 8B BE 10 08 00 00 8B CE C7 86 10 08 00 00 00 00 "
     "00 00 E8 ?? ?? ?? ?? 8B 5C 24 28 8B 6C 24",
     7, (void *)drawUserPrimitiveDetour, (void **)&drawUserPrimitiveOriginal, nullptr},
    {"draw primitive",
     "83 EC 08 8D 44 24 00 56 8B F1 8D 4C 24 08 50 51 8B CE E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 53 55 57 8B BE 10 08 00 00 8B CE C7 86 10 08 00 00 00 00 "
     "00 00 E8 ?? ?? ?? ?? 8B 5C 24 24 8B 46 0C",
     7, (void *)drawPrimitiveDetour, (void **)&drawPrimitiveOriginal, nullptr},
    {"present", "56 57 8B F9 BE 64 00 00 00 8B 47 08", 9, (void *)presentDetour, (void **)&presentOriginal, nullptr},
    {"visibility test", "A1 ?? ?? ?? ?? 53 56 8B F1 8A 98 28", 5, (void *)visibilityTestDetour, (void **)&visibilityTestOriginal, nullptr},
    {"model draw", "83 EC 50 53 55 56 57 8B D9 6A 00 89", 5, (void *)modelDrawDetour, (void **)&modelDrawOriginal, nullptr},
};

/// Finds FFXiMain's code section from its in-memory headers.
/// @param base the module's load address
/// @param start receives the section's first byte
/// @param end receives one past the section's last byte
static void findCodeSection(uint8_t *base, uint8_t **start, uint8_t **end)
{
    IMAGE_NT_HEADERS32 *headers = (IMAGE_NT_HEADERS32 *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(headers);
    for (WORD i = 0; i < headers->FileHeader.NumberOfSections; i++, section++) {
        if (memcmp(section->Name, ".text", 5) == 0) {
            *start = base + section->VirtualAddress;
            *end = *start + section->Misc.VirtualSize;
            return;
        }
    }
    *start = *end = nullptr;
}

/// Searches a byte range for a pattern written as hex bytes with ?? wildcards.
/// @param start first byte to search
/// @param end one past the last byte to search
/// @param pattern the pattern text
/// @param matchCount receives how many places matched
/// @return the first match, or nullptr when none matched
static uint8_t *findPattern(uint8_t *start, uint8_t *end, const char *pattern, int *matchCount)
{
    uint8_t bytes[128];
    bool wildcard[128];
    size_t length = 0;
    for (const char *cursor = pattern; *cursor && length < 128;) {
        if (*cursor == ' ') {
            cursor++;
            continue;
        }
        wildcard[length] = cursor[0] == '?';
        bytes[length] = wildcard[length] ? 0 : (uint8_t)strtoul(std::string(cursor, 2).c_str(), nullptr, 16);
        length++;
        cursor += 2;
    }

    uint8_t *found = nullptr;
    *matchCount = 0;
    for (uint8_t *candidate = start; candidate + length <= end; candidate++) {
        size_t i = 0;
        while (i < length && (wildcard[i] || candidate[i] == bytes[i])) i++;
        if (i == length) {
            if (!found) found = candidate;
            (*matchCount)++;
        }
    }
    return found;
}

static void writeJump(uint8_t *from, const uint8_t *to)
{
    from[0] = 0xE9;
    *(int32_t *)(from + 1) = (int32_t)(to - (from + 5));
}

/// Opens every other thread in this process for suspending.
/// @return the opened thread handles
static std::vector<HANDLE> openOtherThreads()
{
    std::vector<HANDLE> threads;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != GetCurrentProcessId() || entry.th32ThreadID == GetCurrentThreadId()) continue;
        HANDLE thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (thread) threads.push_back(thread);
    }
    CloseHandle(snapshot);
    return threads;
}

/// Suspends the threads, retrying until none of them is stopped partway into a hook target.
/// @param threads the threads to suspend
static void suspendOutsideHooks(const std::vector<HANDLE> &threads)
{
    // Nothing in here may allocate: a suspended thread could be holding the heap lock.
    for (;;) {
        bool clear = true;
        for (HANDLE thread : threads) SuspendThread(thread);
        for (HANDLE thread : threads) {
            CONTEXT context;
            context.ContextFlags = CONTEXT_CONTROL;
            if (!GetThreadContext(thread, &context)) continue;
            for (const Hook &hook : hooks) {
                if (context.Eip > (DWORD)hook.target && context.Eip < (DWORD)(hook.target + hook.length)) clear = false;
            }
        }
        if (clear) return;
        for (HANDLE thread : threads) ResumeThread(thread);
        Sleep(1);
    }
}

/// Finds every target by its pattern, then builds trampolines and patches the jumps.
/// @return true when all hooks were installed, false when any pattern didn't match exactly once
static bool installHooks()
{
    uint8_t *base = (uint8_t *)GetModuleHandleA("FFXiMain.dll");
    if (!base) {
        writeLog("refused: FFXiMain.dll not loaded");
        return false;
    }
    uint8_t *textStart, *textEnd;
    findCodeSection(base, &textStart, &textEnd);
    if (!textStart) {
        writeLog("refused: FFXiMain code section not found");
        return false;
    }

    bool matched = true;
    for (Hook &hook : hooks) {
        int matchCount = 0;
        hook.target = findPattern(textStart, textEnd, hook.pattern, &matchCount);
        char line[160];
        if (matchCount == 1) {
            sprintf_s(line, "found %s at FFXiMain+0x%X", hook.name, (uint32_t)(hook.target - base));
            writeDebugLog(line);
        }
        else {
            sprintf_s(line, "refused: %s pattern matched %d places", hook.name, matchCount);
            writeLog(line);
            matched = false;
        }
    }
    if (!matched) return false;

    uint8_t *trampolines = (uint8_t *)VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!trampolines) {
        writeLog("refused: trampoline allocation failed");
        return false;
    }

    uint8_t *cursor = trampolines;
    for (Hook &hook : hooks) {
        uint8_t *target = hook.target;
        *hook.original = cursor;
        if (strcmp(hook.name, "scene draw") == 0) {
            // The stolen bytes end in a short jz, which can't reach its target from here. Rebuild it as a near jz.
            memcpy(cursor, target, 4);
            cursor += 4;
            cursor[0] = 0x0F;
            cursor[1] = 0x84;
            *(int32_t *)(cursor + 2) = (int32_t)((target + 0x1F) - (cursor + 6));
            cursor += 6;
        }
        else {
            memcpy(cursor, target, hook.length);
            cursor += hook.length;
        }
        writeJump(cursor, target + hook.length);
        cursor += 5;
    }

    // Patch with the rest of the process suspended, the way Detours does, so no thread runs a half-written jump.
    // Every target is unlocked before any is written, so a refusal leaves the game untouched.
    std::vector<HANDLE> threads = openOtherThreads();
    suspendOutsideHooks(threads);
    DWORD oldProtect[_countof(hooks)];
    size_t unlocked = 0;
    while (unlocked < _countof(hooks) && VirtualProtect(hooks[unlocked].target, hooks[unlocked].length, PAGE_EXECUTE_READWRITE, &oldProtect[unlocked])) {
        unlocked++;
    }
    if (unlocked == _countof(hooks)) {
        for (Hook &hook : hooks) {
            writeJump(hook.target, (const uint8_t *)hook.detour);
            for (uint32_t i = 5; i < hook.length; i++) hook.target[i] = 0x90;
        }
    }
    // Restore in reverse, since two targets can share a page.
    for (size_t i = unlocked; i-- > 0;) {
        DWORD unused;
        VirtualProtect(hooks[i].target, hooks[i].length, oldProtect[i], &unused);
        FlushInstructionCache(GetCurrentProcess(), hooks[i].target, hooks[i].length);
    }
    for (HANDLE thread : threads) {
        ResumeThread(thread);
        CloseHandle(thread);
    }

    if (unlocked < _countof(hooks)) {
        writeLog("refused: game code could not be unlocked for writing");
        return false;
    }
    return true;
}

// Lua Interface
static int luaSetLogPath(lua_State *L)
{
    const char *path = lua_tolstring(L, 1, nullptr);
    if (!path) return 0;
    AcquireSRWLockExclusive(&logLock);
    if (logPath != path) {
        if (logFile) fclose(logFile);
        logFile = nullptr;
        logPath = path;
    }
    ReleaseSRWLockExclusive(&logLock);
    return 0;
}

static int luaInstall(lua_State *L)
{
    if (!installed) {
        QueryPerformanceFrequency(&counterFrequency);
        QueryPerformanceCounter(&statsStartedAt);
        lastDrawnAt = statsStartedAt;
        frameStartedAt = statsStartedAt;
        previousFrameStartedAt = statsStartedAt;
        statsCpuStart = processCpuTime();
        installed = installHooks();
        if (installed) {
            writeDebugLog("hooks installed");
        }
        else {
            writeLog("hooks not installed");
        }
    }
    lua_pushboolean(L, installed);
    return 1;
}

static int luaSetEnabled(lua_State *L)
{
    enabled = installed && lua_toboolean(L, 1) != 0;
    writeDebugLog(enabled ? "limiter on" : "limiter off");
    return 0;
}

static int luaSetDrawFps(lua_State *L)
{
    double fps = lua_tonumber(L, 1);
    if (fps <= 0.0) return 0;
    drawIntervalMs = 1000.0 / fps;
    char line[80];
    sprintf_s(line, "draw fps %.2f", fps);
    writeDebugLog(line);
    return 0;
}

/// Sets one of the game fps caps from Lua and logs it.
/// @param L the Lua state, holding the fps (0 or less for uncapped)
/// @param interval the cap to set
/// @param name the cap's name for the log
static void setCap(lua_State *L, volatile double &interval, const char *name)
{
    double fps = lua_tonumber(L, 1);
    interval = fps > 0.0 ? 1000.0 / fps : 0.0;
    char line[80];
    if (fps > 0.0) {
        sprintf_s(line, "%s fps cap %.2f", name, fps);
    }
    else {
        sprintf_s(line, "%s fps cap off", name);
    }
    writeDebugLog(line);
}

static int luaSetGameFps(lua_State *L)
{
    setCap(L, gameIntervalMs, "game");
    return 0;
}

static int luaSetCutsceneFps(lua_State *L)
{
    setCap(L, cutsceneIntervalMs, "cutscene");
    return 0;
}

static int luaSetInEvent(lua_State *L)
{
    inEvent = lua_toboolean(L, 1) != 0;
    writeDebugLog(inEvent ? "event started" : "event ended");
    return 0;
}

static int luaSetDebug(lua_State *L)
{
    bool wasLogging = debugLogging;
    debugLogging = lua_toboolean(L, 1) != 0;
    if (debugLogging && !wasLogging) writeLog("debug logging on, DLL " FRAMETAMER_VERSION);
    return 0;
}

static int luaIsInstalled(lua_State *L)
{
    lua_pushboolean(L, installed);
    return 1;
}

static int luaVersion(lua_State *L)
{
    lua_pushstring(L, FRAMETAMER_VERSION);
    return 1;
}

extern "C" __declspec(dllexport) int luaopen__frametamer(lua_State *L)
{
    // Pinned: the hooks jump into this DLL, so it can never unload while the game runs.
    HMODULE self;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCWSTR)&luaopen__frametamer, &self);
    lua_createtable(L, 0, 10);
    lua_pushcclosure(L, luaSetLogPath, 0);
    lua_setfield(L, -2, "setLogPath");
    lua_pushcclosure(L, luaInstall, 0);
    lua_setfield(L, -2, "install");
    lua_pushcclosure(L, luaSetEnabled, 0);
    lua_setfield(L, -2, "setEnabled");
    lua_pushcclosure(L, luaSetDrawFps, 0);
    lua_setfield(L, -2, "setDrawFps");
    lua_pushcclosure(L, luaSetGameFps, 0);
    lua_setfield(L, -2, "setGameFps");
    lua_pushcclosure(L, luaSetCutsceneFps, 0);
    lua_setfield(L, -2, "setCutsceneFps");
    lua_pushcclosure(L, luaSetInEvent, 0);
    lua_setfield(L, -2, "setInEvent");
    lua_pushcclosure(L, luaSetDebug, 0);
    lua_setfield(L, -2, "setDebug");
    lua_pushcclosure(L, luaIsInstalled, 0);
    lua_setfield(L, -2, "isInstalled");
    lua_pushcclosure(L, luaVersion, 0);
    lua_setfield(L, -2, "version");
    return 1;
}
