# lua-profiler
A simple Lua profiler. Requires Lua 5.3.

* Supports recursive calls.
* Supports tail calls.
* Minimalistic.
* Pretty report output format.

## Obsolete
This library is no longer maintained. See
[this repository](https://github.com/Fingercomp/lprofile-rs) for a
reimplementation in Rust.

## Installation
Compile the hook, and put `profile_hook.so` in Lua's `package.cpath`
(e.g. `/usr/lib/lua/5.3`).

```
$ gcc -O3 -Wall -Wextra -pedantic -fPIC -shared c/profile_hook.c -o profile_hook.so
```

Put `lua/profile.lua` in Lua's `package.path` (e.g. `/usr/share/lua/5.3`).

## Usage
`require("profile")` returns a function with the following parameters:

1. `f` (`function`): a function to profile.
2. `path` (`string`; default `./profiler-report.txt`): a path to put
   the report to.
3. `obscureAnonymous` (`boolean`; default `false`): if `true`, the hook will
   use the address as a name of anonymous functions, otherwise, `<anon>`
   is used.

```lua
local function factorial(n, acc)
  acc = acc or 1

  if n <= 1 then
    return acc
  else
    return factorial(n - 1, n * acc)
  end
end

require("profile")(function()
  return factorial(10)
end)
```
