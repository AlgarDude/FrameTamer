-- FrameTamer: background clients draw at a low rate while the game itself keeps running.
_addon.name = 'FrameTamer'
_addon.author = 'AlgarDude'
_addon.version = '1.02'
_addon.commands = {'frametamer', 'ft'}

package.cpath = package.cpath .. ';' .. windower.addon_path .. 'libs/?.dll'
local limiter = require('_frametamer')

local defaults = {enabled = true, drawFps = 1, gameFps = 15, cutsceneFps = 0, foregroundGameFps = 180, foregroundCutsceneFps = 180, zoneFps = 60, debug = false}
local settings = nil
local playerName = nil
local inEvent = false
local zoningInEvent = false
local restartNeeded = limiter.version() ~= _addon.version

--- Prints one FrameTamer line to the chat log.
--- @param text string the message
local function say(text)
    windower.add_to_chat(207, "FrameTamer: " .. text)
end

--- Returns the path of a file in the data folder, creating the folder if needed.
--- @param name string the file name
--- @return string the path
local function dataPath(name)
    if not windower.dir_exists(windower.addon_path .. 'data') then
        windower.create_dir(windower.addon_path .. 'data')
    end
    return windower.addon_path .. 'data/' .. name
end

--- Returns true when a value is a real, finite number at or above the floor.
--- @param value any the value to check
--- @param floor number the lowest allowed value
--- @return boolean whether it can be used
local function isFps(value, floor)
    return type(value) == 'number' and value == value and value < math.huge and value >= floor
end

--- Reads a character's settings, using the default for anything missing or invalid.
--- @param name string the character
--- @return table the settings
local function loadSettings(name)
    local path = dataPath(name .. '.lua')
    local saved = {}
    if windower.file_exists(path) then
        local chunk = loadfile(path)
        local ok, result = false, nil
        if chunk then ok, result = pcall(chunk) end
        if ok and type(result) == 'table' then
            saved = result
        else
            say("Settings Unreadable - data/" .. name .. ".lua couldn't be loaded, using defaults.")
        end
    end

    local loaded = {
        enabled = defaults.enabled,
        drawFps = defaults.drawFps,
        gameFps = defaults.gameFps,
        cutsceneFps = defaults.cutsceneFps,
        foregroundGameFps = defaults.foregroundGameFps,
        foregroundCutsceneFps = defaults.foregroundCutsceneFps,
        zoneFps = defaults.zoneFps,
        debug = defaults.debug,
    }
    if type(saved.enabled) == 'boolean' then loaded.enabled = saved.enabled end
    if isFps(saved.drawFps, 0.01) then loaded.drawFps = saved.drawFps end
    if isFps(saved.gameFps, 10) then loaded.gameFps = saved.gameFps end
    if saved.cutsceneFps == 0 or isFps(saved.cutsceneFps, 10) then loaded.cutsceneFps = saved.cutsceneFps end
    if isFps(saved.foregroundGameFps, 10) then loaded.foregroundGameFps = saved.foregroundGameFps end
    if saved.foregroundCutsceneFps == 0 or isFps(saved.foregroundCutsceneFps, 10) then loaded.foregroundCutsceneFps = saved.foregroundCutsceneFps end
    if isFps(saved.zoneFps, 10) then loaded.zoneFps = saved.zoneFps end
    if type(saved.debug) == 'boolean' then loaded.debug = saved.debug end
    return loaded
end

--- Writes this character's settings to its file.
local function saveSettings()
    local file = io.open(dataPath(playerName .. '.lua'), 'w')
    if not file then
        say("Settings Not Saved - data/" .. playerName .. ".lua couldn't be written. Changes last until logout.")
        return
    end
    file:write(('return {enabled = %s, drawFps = %s, gameFps = %s, cutsceneFps = %s, foregroundGameFps = %s, foregroundCutsceneFps = %s, zoneFps = %s, debug = %s}\n')
                   :format(tostring(settings.enabled), tostring(settings.drawFps), tostring(settings.gameFps), tostring(settings.cutsceneFps),
                           tostring(settings.foregroundGameFps), tostring(settings.foregroundCutsceneFps), tostring(settings.zoneFps), tostring(settings.debug)))
    file:close()
end

--- Sends this character's settings to the limiter.
local function applySettings()
    limiter.setDebug(settings.debug)
    limiter.setDrawFps(settings.drawFps)
    limiter.setGameFps(settings.gameFps)
    limiter.setCutsceneFps(settings.cutsceneFps)
    limiter.setForegroundGameFps(settings.foregroundGameFps)
    limiter.setForegroundCutsceneFps(settings.foregroundCutsceneFps)
    limiter.setZoneFps(settings.zoneFps)
    limiter.setEnabled(settings.enabled)
end

--- Tells the limiter whether this character is in an event.
--- @param value boolean true in a cutscene or NPC menu
local function setInEvent(value)
    inEvent = value
    limiter.setInEvent(value)
end

--- Explains in chat that the hooks didn't go in.
local function sayStandingDown()
    say("Standing Down - Game code didn't match (game update or another addon). See data/" .. playerName .. ".log.")
end

--- Prints this character's limits, or why it isn't limiting.
local function showStatus()
    if not settings.enabled then
        say("Limiting Disabled")
    elseif not limiter.isInstalled() then
        sayStandingDown()
    else
        local cutsceneText = settings.cutsceneFps > 0 and (settings.cutsceneFps .. " fps") or "Unlimited"
        say(("Background - Draw: %s fps | Game: %s fps | Cutscene: %s | Zone: %s fps"):format(settings.drawFps, settings.gameFps, cutsceneText, settings.zoneFps) ..
                (settings.debug and " | Debug: On" or ""))
        local foregroundCutsceneText = settings.foregroundCutsceneFps > 0 and (settings.foregroundCutsceneFps .. " fps") or "Unlimited"
        say(("Foreground - Game: %s fps | Cutscene: %s"):format(settings.foregroundGameFps, foregroundCutsceneText))
    end
end

--- Prints the command list.
local function showHelp()
    say("Commands")
    windower.add_to_chat(207, "  //ft - Show current limits")
    windower.add_to_chat(207, "  //ft on | off - Turn limiting on or off (also enable | disable)")
    windower.add_to_chat(207, "  //ft draw <fps> - Background redraw rate (Default: 1)")
    windower.add_to_chat(207, "  //ft game <fps> [bg | fg] - Game speed, capped by frame rate divisor (Default: 15 bg, 180 fg)")
    windower.add_to_chat(207, "  //ft cutscene <fps> | off [bg | fg] - Game speed in cutscenes and menus (Default: Off bg, 180 fg)")
    windower.add_to_chat(207, "  //ft zone <fps> - Game speed while changing zones (Default: 60)")
    windower.add_to_chat(207, "  //ft debug on | off - Log a summary each minute to data/<name>.log (Default: Off)")
    windower.add_to_chat(207, "  //ft reset - Return this character to defaults")
    windower.add_to_chat(207, "  //ft all <command> - Send a command to every logged-in character")
end

--- Checks a typed fps value against its floor, saying what's wrong when it can't be used.
--- @param name string the setting, for the message
--- @param number number|nil the typed value
--- @param floor number the lowest allowed value
--- @param allowOff boolean whether the setting also takes off
--- @return boolean whether it can be used
local function checkFps(name, number, floor, allowOff)
    if not isFps(number, floor) then
        say("Invalid Value - " .. name .. " must be a number, " .. floor .. " fps or higher" .. (allowOff and ", or Off." or "."))
        return false
    end
    return true
end

--- Runs one FrameTamer command for this character.
--- @param command string|nil the command word
--- @param value string|nil its value
--- @param side string|nil bg, background, fg, foreground, or nil for background
local function runCommand(command, value, side)
    command = (command or ''):lower()
    value = (value or ''):lower()
    side = (side or ''):lower()
    local number = tonumber(value)
    local foreground = side == 'fg' or side == 'foreground'
    if not foreground and side ~= '' and side ~= 'bg' and side ~= 'background' then
        say("Invalid Value - Side must be BG or FG.")
        return
    end
    if command == 'on' or command == 'off' or command == 'enable' or command == 'disable' then
        settings.enabled = command == 'on' or command == 'enable'
        limiter.setEnabled(settings.enabled)
    elseif command == 'draw' then
        if foreground then
            say("Invalid Value - Draw only applies to the background.")
            return
        end
        if not checkFps("Draw", number, 0.01, false) then return end
        settings.drawFps = number
        limiter.setDrawFps(number)
    elseif command == 'game' then
        -- The floor of 10 here and on cutscene: below it the game risks slowdowns and missed packets.
        if not checkFps("Game", number, 10, false) then return end
        if foreground then
            settings.foregroundGameFps = number
            limiter.setForegroundGameFps(number)
        else
            settings.gameFps = number
            limiter.setGameFps(number)
        end
    elseif command == 'cutscene' then
        if value ~= 'off' and not checkFps("Cutscene", number, 10, true) then return end
        if foreground then
            settings.foregroundCutsceneFps = value == 'off' and 0 or number
            limiter.setForegroundCutsceneFps(settings.foregroundCutsceneFps)
        else
            settings.cutsceneFps = value == 'off' and 0 or number
            limiter.setCutsceneFps(settings.cutsceneFps)
        end
    elseif command == 'zone' then
        if side ~= '' then
            say("Invalid Value - Zone applies to both background and foreground.")
            return
        end
        if not checkFps("Zone", number, 10, false) then return end
        settings.zoneFps = number
        limiter.setZoneFps(number)
    elseif command == 'debug' then
        if value ~= 'on' and value ~= 'off' then
            say("Invalid Value - Debug must be On or Off.")
            return
        end
        settings.debug = value == 'on'
        limiter.setDebug(settings.debug)
    elseif command == 'reset' then
        os.remove(dataPath(playerName .. '.lua'))
        settings = loadSettings(playerName)
        applySettings()
        showStatus()
        return
    elseif command == '' or command == 'status' then
        showStatus()
        return
    elseif command == 'help' then
        showHelp()
        return
    else
        say("Unknown Command - '" .. command .. "'. Type //ft help for commands.")
        return
    end
    saveSettings()
    showStatus()
end

--- Loads this character's settings, hooks the game if it isn't already, and applies them.
--- @param name string the logged-in character
local function startLimiter(name)
    -- The DLL outlives the addon, so every login sends the whole state, on or off included.
    playerName = name
    settings = loadSettings(name)
    limiter.setLogPath(dataPath(name .. '.log'))
    limiter.setDebug(settings.debug)
    if not limiter.install() then
        sayStandingDown()
    end
    if restartNeeded then
        say("Restart Needed - Updated to " .. _addon.version .. ", the game is still running " .. limiter.version() .. ".")
        return
    end
    applySettings()
    limiter.setZoning(false)
    setInEvent((windower.ffxi.get_player() or {}).status == 4)
end

windower.register_event('login', startLimiter)

-- Status 4 is an event: a cutscene or an NPC menu.
windower.register_event('status change', function(new, old)
    if new == 4 or old == 4 then
        setInEvent(new == 4)
    end
end)

-- A zone change runs at the zone cap until the new zone is loaded (0x00C). A cutscene that carries
-- on through the zone goes back to the cutscene cap after that.
windower.register_event('outgoing chunk', function(id)
    if restartNeeded then return end
    if id == 0x00D then
        limiter.setZoning(true)
        if inEvent then zoningInEvent = true end
    elseif id == 0x00C then
        limiter.setZoning(false)
    end
end)

windower.register_event('incoming chunk', function(id, original, modified, injected)
    if id == 0x00A and not injected and zoningInEvent then
        zoningInEvent = false
        setInEvent(true)
    end
end)

windower.register_event('addon command', function(...)
    if not settings or not windower.ffxi.get_info().logged_in then
        say("This command only functions after character login.")
        return
    end
    local args = {...}
    if (args[1] or ''):lower() == 'all' then
        -- Other clients get it over Windower IPC, which only reaches this same addon.
        windower.send_ipc_message(table.concat(args, ' ', 2))
        table.remove(args, 1)
    end
    if restartNeeded then
        say("Restart Needed - Updated to " .. _addon.version .. ", the game is still running " .. limiter.version() .. ".")
        return
    end
    runCommand(args[1], args[2], args[3])
end)

windower.register_event('ipc message', function(message)
    if settings and not restartNeeded and windower.ffxi.get_info().logged_in then
        runCommand(message:match('^(%S*)%s*(%S*)%s*(%S*)'))
    end
end)

windower.register_event('unload', function()
    limiter.setEnabled(false)
    limiter.setDebug(false)
end)

local player = windower.ffxi.get_player()
if windower.ffxi.get_info().logged_in and player then
    startLimiter(player.name)
end
