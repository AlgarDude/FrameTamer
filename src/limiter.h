// FrameTamer: hooks into the game's frame loop.
#pragma once

namespace limiter
{
    void setLogPath(const char *path);
    /// Hooks the game.
    /// @param device the real Direct3D 8 device, or nullptr on Windower
    /// @return true when all hooks are installed
    bool install(void *device);
    bool isInstalled();
    void setEnabled(bool value);
    void setDrawFps(double fps);
    /// @param fps 0 for uncapped
    void setGameFps(double fps);
    /// @param fps 0 for uncapped
    void setCutsceneFps(double fps);
    /// @param fps 0 for uncapped
    void setForegroundGameFps(double fps);
    /// @param fps 0 for uncapped
    void setForegroundCutsceneFps(double fps);
    /// @param fps 0 for uncapped
    void setZoneFps(double fps);
    void setInEvent(bool value);
    /// Holds the client at the zone cap from leaving a zone until the next one is loaded.
    void setZoning(bool value);
    void setDebug(bool value);
} // namespace limiter
