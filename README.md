# FrameTamer

A frame limiting addon for **Final Fantasy XI** on **Windower 4**.

Unlike traditional frame limiters, FrameTamer works by stopping draws from happening, while the game engine itself still runs at a proper speed.

Fully compatible with FastCS; FrameTamer does not adjust the frame rate divisor (and is largely indifferent to it). Other addons that manipulate the divisor, such as bkgthrottle, are unnecessary and should be disabled.

By default, the draws happen at 1 fps, while the game client continues to operate and process at 15 fps, which maintains full function. These values are configurable (see below).

## Settings

| Setting | What it does | Default | Lowest |
|:--|:--|:--|:--|
| **Draw** | How often a background window redraws. | 1 fps | 0.01 |
| **Game** | Background game speed cap outside cutscenes. Can only slow the game below what Windower's frame rate divisor allows. | 15 fps | 10 |
| **Cutscene** | Background game speed cap in cutscenes and NPC menus. | Unlimited | 10 |
| **Debug** | Writes a summary every minute (game rate, draws, CPU), focus changes and setting changes to `data/<name>.log`. | Off | |

Settings are per character, saved to `data/<name>.lua` the first time you change one.

10 fps is set as the floor to help assure proper client performance. A special thanks to Rahvin for providing low-fps scenario testing results to the community.

## Commands

```
//ft                        show this character's current limits
//ft on | off               turn limiting on or off (also enable | disable)
//ft draw <fps>             background redraw rate
//ft game <fps>             background game speed cap
//ft cutscene <fps> | off   background game speed cap in cutscenes and menus
//ft debug on | off         per-minute summary in data/<name>.log
//ft reset                  put this character back on the defaults
//ft all <command>          run a command on every logged-in client
//ft help                   list the commands
```

`//frametamer` works anywhere `//ft` does.

Want one character to keep drawing at full speed in the background? Log in on it and type `//ft off`. It stays off for that character.

## How it works

FrameTamer ships a small native DLL beside the Lua. At login it finds seven spots in the game's own frame loop and hooks them. On a frame a background window doesn't need, the game still runs its full update, but the 3D scene, character models, UI and the final present to the screen are skipped. Every so often a frame draws normally, so the window and its Win+Tab thumbnail stay current.

The game cap holds the background loop to its set rate between frames. In a cutscene or NPC menu it switches to the cutscene cap, so cutscenes keep their pace.

Windower overlays (text and images from other addons) in a background window only update when that window draws. Tested with dgVoodoo.

## Game updates

Each spot is found by a byte pattern at every load, and each must match exactly once. If an update changes one, or another addon already hooks the same spot, FrameTamer hooks nothing and says so:

```
FrameTamer: Standing Down - Game code didn't match (game update or another addon). See data/<name>.log.
```

The game then runs exactly as it would without FrameTamer.

## Install instructions

1. Download the zip from the [latest release](../../releases/latest).
2. Extract it into `Windower/addons`, so it lands as `addons/FrameTamer/`.
3. Load it with `//lua load frametamer`, or add it to your autoload.
4. Log in. It turns on at login, on the defaults above.
5. Check with `//ft`:

```
FrameTamer: Current Limits - Draw: 1 fps | Game: 15 fps | Cutscene: Unlimited
```

## Updating

Once loaded, the DLL stays in the game until the client closes. Close every client before copying in a new version. If a client is still running an older DLL after an update, FrameTamer tells you at login:

```
FrameTamer: Restart Needed - Updated to <new>, the game is still running <old>.
```

## Building

Open `src/FrameTamer.sln` in Visual Studio with the C++ desktop workload and build **Release x86**. It writes `libs/_frametamer.dll`, linking against Windower's `LuaCore.dll` through `src/LuaCore.def`.

## License

GPL-3.0. See [LICENSE](LICENSE).

## Special Thanks

Thanks to the [MacroQuest](https://github.com/macroquest/macroquest) developers, whose frame limiter inspired FrameTamer.
