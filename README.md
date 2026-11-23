# FrameTamer

A frame limiting addon for **Final Fantasy XI** on **Windower 4** and **Ashita v4**.

Unlike traditional frame limiters, FrameTamer works by stopping draws from happening, while the game engine itself still runs at a proper speed.

FrameTamer can also handle traditional frame limiting, replacing other frame limiters such as RTSS or the NVIDIA cap (they are ineffective on "tamed" background clients anyway).

Fully compatible with FastCS; FrameTamer does not adjust the frame rate divisor (and is largely indifferent to it). Other addons that manipulate the divisor, such as bkgthrottle, are unnecessary and should be disabled.

By default, the draws happen at 1 fps, while the game client continues to operate and process at 15 fps, which maintains full function. These values are configurable (see below).

## Settings

| Setting | What it does | Default | Lowest |
|:--|:--|:--|:--|
| **Draw** | How often a background window redraws. | 1 fps | 0.01 |
| **Game** | Game speed cap outside cutscenes. Can only slow the game below what Windower's frame rate divisor allows. | 15 fps background, 180 fps foreground | 10 |
| **Cutscene** | Game speed cap in cutscenes and NPC menus. | Unlimited background, 180 fps foreground | 10 |
| **Zone** | Game speed cap for every window while changing zones. Too high a value can cause disconnects. | 60 fps | 10 |
| **Debug** | Writes a summary every minute (game rate, draws, CPU), focus changes and setting changes to `data/<name>.log`. | Off | |

Settings are per character, saved to `data/<name>.lua` the first time you change one.

10 fps is set as the floor to help assure proper client performance. A special thanks to Rahvin for providing low-fps scenario testing results to the community.

## Commands

```
//ft                                 show this character's current limits
//ft on | off                        turn limiting on or off (also enable | disable)
//ft draw <fps>                      background redraw rate
//ft game <fps> [bg | fg]            game speed cap
//ft cutscene <fps> | off [bg | fg]  game speed cap in cutscenes and menus
//ft zone <fps>                      game speed cap while changing zones
//ft debug on | off                  per-minute summary in data/<name>.log
//ft reset                           put this character back on the defaults
//ft all <command>                   run a command on every logged-in client
//ft help                            list the commands
```

Add `fg` (or `foreground`) to set the foreground value. Leave it off, or use `bg` (or `background`), for the background value.

`//frametamer` works anywhere `//ft` does.

To keep one character drawing at full speed in the background, type `//ft off` on it.

## How it works

FrameTamer comes with a small DLL that hooks seven spots in the game's frame loop at login. A background window still runs the full game update every frame but skips drawing it. It draws at the draw rate so the window and its Win+Tab thumbnail stay current.

The caps limit how fast the game runs in the background and the foreground. The cutscene cap is used in cutscenes and NPC menus. The zone cap is used while changing zones.

Windower overlays in a background window only update when the window draws. Tested with dgVoodoo.

**Author's note:** A zone cap above 60 may load zones a little faster but can cause disconnects. I run six clients at 180 with no issues.

## Game updates

Each spot is found by a byte pattern at every load, and each must match exactly once. If an update changes one, or another addon already hooks the same spot, FrameTamer hooks nothing and says so:

```
FrameTamer: Standing Down - Game code didn't match (game update or another addon). See data/<name>.log.
```

## Install instructions

1. Download the newest `windower` zip from [releases](../../releases).
2. Extract it into `Windower/addons`, so it lands as `addons/FrameTamer/`.
3. Load it with `//lua load frametamer`, or add it to your autoload.
4. Log in. It turns on at login, on the defaults above.
5. Check with `//ft`:

```
FrameTamer: Background - Draw: 1 fps | Game: 15 fps | Cutscene: Unlimited | Zone: 60 fps
FrameTamer: Foreground - Game: 180 fps | Cutscene: 180 fps
```

## Updating

Once loaded, the DLL stays in the game until the client closes. Close every client before copying in a new version. If a client is still running an older DLL after an update, FrameTamer tells you at login:

```
FrameTamer: Restart Needed - Updated to <new>, the game is still running <old>.
```

## Ashita

Differences on Ashita:

- Commands start with one slash: `/ft`, `/ft draw 2`, `/ft all off`.
- Settings and the debug log go to `config/addons/frametamer/<name>.lua` and `<name>.log`.

To install:

1. Download the newest `ashita` zip from [releases](../../releases).
2. Extract it into `Ashita/addons`, so it lands as `addons/frametamer/`.
3. Load it with `/addon load frametamer`, or add that line to your boot script.
4. Log in, then check with `/ft`.

## Building

Open `src/FrameTamer.sln` in Visual Studio with the C++ desktop workload and build **Release x86**. It writes `libs/_frametamer.dll`, linking against Windower's `LuaCore.dll` through `src/LuaCore.def`, and `ashita/addons/frametamer/libs/_frametamer.dll`.

## License

GPL-3.0. See [LICENSE](LICENSE).

## Special Thanks

Thanks to the [MacroQuest](https://github.com/macroquest/macroquest) developers, whose frame limiter inspired FrameTamer.
