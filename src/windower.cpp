// FrameTamer for Windower: Lua bindings.
#include <windows.h>
#include "limiter.h"
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

// Lua Interface
static int luaSetLogPath(lua_State *L)
{
    const char *path = lua_tolstring(L, 1, nullptr);
    if (!path) return 0;
    limiter::setLogPath(path);
    return 0;
}

static int luaInstall(lua_State *L)
{
    lua_pushboolean(L, limiter::install(nullptr));
    return 1;
}

static int luaSetEnabled(lua_State *L)
{
    limiter::setEnabled(lua_toboolean(L, 1) != 0);
    return 0;
}

static int luaSetDrawFps(lua_State *L)
{
    limiter::setDrawFps(lua_tonumber(L, 1));
    return 0;
}

static int luaSetGameFps(lua_State *L)
{
    limiter::setGameFps(lua_tonumber(L, 1));
    return 0;
}

static int luaSetCutsceneFps(lua_State *L)
{
    limiter::setCutsceneFps(lua_tonumber(L, 1));
    return 0;
}

static int luaSetInEvent(lua_State *L)
{
    limiter::setInEvent(lua_toboolean(L, 1) != 0);
    return 0;
}

static int luaSetDebug(lua_State *L)
{
    limiter::setDebug(lua_toboolean(L, 1) != 0);
    return 0;
}

static int luaIsInstalled(lua_State *L)
{
    lua_pushboolean(L, limiter::isInstalled());
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
