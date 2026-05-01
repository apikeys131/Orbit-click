# Orbit Click

A precise, configurable auto clicker for Windows. Hotkey toggle, adjustable intervals, multi-point support.

## Features

- Configurable click intervals with millisecond precision
- Multi-point click sequences — record and replay click paths
- Global hotkey start / stop toggle — works even when the app is in the background
- Click count limits and patterns
- Left, right, and middle click support
- Randomized interval option to avoid detection
- Lightweight — minimal CPU usage while running

## Download

Grab the latest installer from [Releases](https://github.com/apikeys131/Orbit-click/releases).

Run `OrbitClick-Setup-v1.0.0.exe` and follow the installer. Windows may show a SmartScreen warning — click **More info → Run anyway**.

## Requirements

- Windows 10 or 11 (64-bit)

## Building

```bash
# 1. Clone the repository
git clone https://github.com/apikeys131/Orbit-click.git
cd Orbit-click

# 2. Configure
cmake -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

# 3. Build
cmake --build build --config Release --parallel

# 4. Run
.\build\Release\OrbitClick.exe
```

## Architecture

```
src/
├── main.cpp                Entry point
├── ui/
│   ├── MainWindow.h/.cpp   Main window and controls
│   └── SequenceEditor.h    Multi-point sequence recorder and editor
├── input/
│   ├── Clicker.h/.cpp      Core click dispatch loop
│   ├── HotkeyHook.h/.cpp   Global low-level keyboard hook
│   └── MouseInput.h        SendInput wrapper for click injection
└── utils/
    ├── Interval.h          Interval and randomization logic
    └── Config.h/.cpp       Persistent settings storage
```

## Source

Source is available for review. See the repository for build instructions.

## License

Source-available. Free to use. Do not redistribute modified binaries without permission.
