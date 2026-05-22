# kmon

polls `EnumDeviceDrivers` in a loop and yells at you when something changes. that's it.

wrote this because i needed to know what drivers were getting loaded and i didn't want to open procmon for the 40th time this week.

![platform](https://img.shields.io/badge/platform-windows-blue)
![language](https://img.shields.io/badge/language-C%2B%2B-f34b7d)
![license](https://img.shields.io/badge/license-MIT)
![elevation](https://img.shields.io/badge/run%20as-administrator-red)

---

## what it does

- detects drivers being loaded/unloaded in real time
- shows name, base address, file path, timestamp
- green = loaded, red = unloaded, pretty straightforward
- optional log file if you actually want a record of it
- warns you if you forgot to run as admin (you will forget)

no kernel module. no hooking. just diffing snapshots. if a driver loads and unloads between two polls you won't see it, but honestly if that's your threat model you have bigger problems.

---

## building

needs `psapi.lib`. MSVC:

```bat
cl kmon.cpp /O2 /W3 /EHsc /link psapi.lib
```

MinGW also works:

```bat
g++ kmon.cpp -O2 -o kmon.exe -lpsapi
```

no cmake. no vcpkg. no 47-step setup. one file.

---

## usage

```
kmon [options]

  -a, --all           dump all currently loaded drivers on start
  -l, --log           write events to kmon.log
  -i, --interval <ms> how often to poll (default: 500ms, min: 50, max: 5000)
  -h, --help          you're reading the readme, you'll be fine
```

run as administrator or half the drivers won't show up and you'll think nothing is happening.

```bat
kmon -a -l -i 250
```

---

## output

```
  12:34:56.789  LOAD    somedriver.sys                        0xFFFFF80012340000  \SystemRoot\System32\Drivers\somedriver.sys
  12:34:57.123  UNLOAD  sketchything.sys                      0xFFFFF80099810000
```

on exit it prints how many load/unload events happened. good for when you're staring at it at 2am trying to figure out what that executor just dropped.

log file appends, doesn't overwrite. each session gets a `=== session started ===` header so you can tell them apart.

---

## caveats

- max 4096 drivers tracked. if you somehow have more than that loaded you have other issues
- poll-based diffing means very short-lived drivers *can* slip through at high intervals. lower `-i` if you care
- tested on windows 10/11. probably works on older versions. probably.

---

## license

idc, do whatever you want with it
