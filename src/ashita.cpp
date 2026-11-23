// FrameTamer for Ashita: C exports frametamer.lua calls through LuaJIT FFI.
#include <windows.h>
#include <string.h>
#include "limiter.h"
#include "version.h"

// Shared between clients for /ft all.
struct IpcChannel {
    volatile LONG sequence;
    struct {
        DWORD senderProcess;
        char text[120];
    } messages[16];
};

static IpcChannel *ipc = nullptr;
static HANDLE ipcLock = nullptr;
static LONG ipcSeen = 0;

/// Opens the shared channel on first use.
/// @return true when it's open
static bool openIpc()
{
    if (ipc) return true;
    HANDLE mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(IpcChannel), "Local\\FrameTamerAshitaIpc");
    ipcLock = CreateMutexA(nullptr, FALSE, "Local\\FrameTamerAshitaIpcLock");
    if (!mapping || !ipcLock) return false;
    ipc = (IpcChannel *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(IpcChannel));
    if (ipc) ipcSeen = ipc->sequence;
    return ipc != nullptr;
}

extern "C" {
__declspec(dllexport) void setLogPath(const char *path)
{
    limiter::setLogPath(path);
}

__declspec(dllexport) bool install(void *device)
{
    // Pinned: the hooks jump into this DLL, so it can never unload while the game runs.
    HMODULE self;
    GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, (LPCSTR)&install, &self);
    return limiter::install(device);
}

__declspec(dllexport) bool isInstalled()
{
    return limiter::isInstalled();
}

__declspec(dllexport) void setEnabled(bool value)
{
    limiter::setEnabled(value);
}

__declspec(dllexport) void setDrawFps(double fps)
{
    limiter::setDrawFps(fps);
}

__declspec(dllexport) void setGameFps(double fps)
{
    limiter::setGameFps(fps);
}

__declspec(dllexport) void setCutsceneFps(double fps)
{
    limiter::setCutsceneFps(fps);
}

__declspec(dllexport) void setForegroundGameFps(double fps)
{
    limiter::setForegroundGameFps(fps);
}

__declspec(dllexport) void setForegroundCutsceneFps(double fps)
{
    limiter::setForegroundCutsceneFps(fps);
}

__declspec(dllexport) void setZoneFps(double fps)
{
    limiter::setZoneFps(fps);
}

__declspec(dllexport) void setZoning(bool value)
{
    limiter::setZoning(value);
}

__declspec(dllexport) void setInEvent(bool value)
{
    limiter::setInEvent(value);
}

__declspec(dllexport) void setDebug(bool value)
{
    limiter::setDebug(value);
}

__declspec(dllexport) const char *version()
{
    return FRAMETAMER_VERSION;
}

/// Sends a command to the other clients.
__declspec(dllexport) void sendCommand(const char *text)
{
    if (!openIpc()) return;
    WaitForSingleObject(ipcLock, INFINITE);
    LONG sequence = ipc->sequence + 1;
    auto &message = ipc->messages[sequence % _countof(ipc->messages)];
    message.senderProcess = GetCurrentProcessId();
    strncpy_s(message.text, text, _TRUNCATE);
    ipc->sequence = sequence;
    ReleaseMutex(ipcLock);
}

/// Copies the next command another client sent into buffer.
/// @return false when there's none
__declspec(dllexport) bool receiveCommand(char *buffer, int size)
{
    if (!openIpc() || ipc->sequence == ipcSeen) return false;
    bool found = false;
    WaitForSingleObject(ipcLock, INFINITE);
    LONG oldest = ipc->sequence - (LONG)_countof(ipc->messages) + 1;
    if (ipcSeen < oldest - 1) ipcSeen = oldest - 1;
    while (!found && ipcSeen < ipc->sequence) {
        ipcSeen++;
        auto &message = ipc->messages[ipcSeen % _countof(ipc->messages)];
        if (message.senderProcess != GetCurrentProcessId()) {
            strncpy_s(buffer, size, message.text, _TRUNCATE);
            found = true;
        }
    }
    ReleaseMutex(ipcLock);
    return found;
}
}
