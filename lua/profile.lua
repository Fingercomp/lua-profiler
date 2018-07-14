local profileHook = require("profile_hook")

local function formatSeconds(sec)
  local secs = math.floor(sec)
  local millis = (math.floor(sec * 1000) -
                  secs * 1000)
  local micros = (math.floor(sec * 1000000) -
                  secs * 1000000 -
                  millis * 1000)
  local nanos = (math.floor(sec * 1000000000) -
                 secs * 1000000000 -
                 millis * 1000000 -
                 micros * 1000)

  local out = {}

  if secs > 0 then
    table.insert(out, secs .. " s")
  end

  if millis > 0 then
    table.insert(out, millis .. " ms")
  end

  if micros > 0 then
    table.insert(out, micros .. " μs")
  end

  if nanos > 0 then
    table.insert(out, nanos .. " ns")
  end

  local res = #out > 0 and table.concat(out, " ") or "0 s"
  return res, #res - (micros > 0 and 1 or 0)
end

local function padLeft(s, l, sl)
  return (" "):rep(l - (sl or #s)) .. s
end

local function padRight(s, l, sl)
  return s .. (" "):rep(l - (sl or #s))
end

local function report(path, timesum, profile)
  path = path or "./profiler-report.txt"

  local f = io.open(path, "w")

  local entries = {}

  local keypad = #"function"
  local countpad = #"calls"
  local meanpad = #"mean time"
  local timepad = #"total time"
  local meansum = 0

  for k, v in pairs(profile) do
    local t = {key = k, count = v.calls, mean = v.time / v.calls, time = v.time}

    keypad = math.max(keypad, #k)
    countpad = math.max(countpad, #tostring(t.count))
    meanpad = math.max(meanpad, select(2, formatSeconds(t.mean)))
    timepad = math.max(timepad, select(2, formatSeconds(t.time)))
    meansum = meansum + t.mean

    table.insert(entries, t)
  end

  local dynWidth = keypad + countpad + meanpad + timepad

  keypad = (math.max(dynWidth + 21,
                     #"total time: " + select(2, formatSeconds(timesum))) -
            (dynWidth - keypad) - 21)

  table.sort(entries, function(a, b)
    return a.mean > b.mean
  end)

  local tsm, tsml = formatSeconds(timesum)

  f:write("┌─" .. ("─"):rep(dynWidth + 21) .. "─┐\n")
  f:write("│ total time: " .. padRight(tsm, dynWidth + 9, tsml) .. " │\n")

  f:write("├─" .. ("─"):rep(keypad) ..
          "─┬─" .. ("─"):rep(countpad) ..
          "─┬─" .. ("─"):rep(meanpad) ..
          "─┬─" .. ("─"):rep(timepad) ..
          "─┬─────┬─────┤\n")

  f:write("│ " .. padRight("function", keypad) ..
          " │ " .. padLeft("calls", countpad) ..
          " │ " .. padLeft("mean time", meanpad) ..
          " │ " .. padLeft("total time", timepad) ..
          " │ me% │ to% │\n")

  f:write("├─" .. ("─"):rep(keypad) ..
          "─┼─" .. ("─"):rep(countpad) ..
          "─┼─" .. ("─"):rep(meanpad) ..
          "─┼─" .. ("─"):rep(timepad) ..
          "─┼─────┼─────┤\n")

  for i, entry in ipairs(entries) do
    local s, sl = formatSeconds(entry.mean)
    local ts, tsl = formatSeconds(entry.time)
    local pc = math.floor(entry.mean / meansum * 100 + 0.5)
    local tpc = math.floor(entry.time / timesum * 100 + 0.5)

    f:write("│ " .. padRight(entry.key, keypad) ..
            " │ " .. padLeft(tostring(entry.count), countpad) ..
            " │ " .. padLeft(s, meanpad, sl) ..
            " │ " .. padLeft(ts, timepad, tsl) ..
            " │ " .. padLeft(tostring(pc), 3) ..
            " │ " .. padLeft(tostring(tpc), 3) ..
            " │\n")
  end

  f:write("└─" .. ("─"):rep(keypad) ..
          "─┴─" .. ("─"):rep(countpad) ..
          "─┴─" .. ("─"):rep(meanpad) ..
          "─┴─" .. ("─"):rep(timepad) ..
          "─┴─────┴─────┘\n")
  f:close()
end

return function(f, path, obscureAnonymous)
  assert(type(f) == "function", "bad argument #1 (function expected)")
  assert(not path or type(path) == "string",
         "bad argument #2 (string or nil expected)")

  profileHook.setProfileHook(obscureAnonymous)
  f()
  local totalTime = profileHook.unsetProfileHook()
  local profile = profileHook.wipeProfileData()

  report(path, totalTime, profile)
end
