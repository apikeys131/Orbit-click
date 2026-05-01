# OrbitClick — Build Instructions

## Compile with MinGW (recommended)

```bash
g++ OrbitClick.cpp -o OrbitClick.exe -luser32 -lgdi32 -lshell32 -lwinmm -mwindows -std=c++17 -O2
```

## Compile with MSVC

```bash
cl OrbitClick.cpp /std:c++17 /O2 /link user32.lib gdi32.lib shell32.lib winmm.lib /SUBSYSTEM:WINDOWS /OUT:OrbitClick.exe
```

## Requirements
- Windows 10 or 11
- MinGW-w64 or MSVC 2019+

## Microsoft Store (MSIX packaging)
1. Compile the .exe above
2. Install "Windows Application Packaging Project" in Visual Studio
3. Create a new Packaging Project, add OrbitClick.exe as a reference
4. Fill out Package.appxmanifest (name, publisher, capabilities: none needed)
5. Right-click project → Publish → Create App Packages
6. Submit to Partner Center at partner.microsoft.com ($19 one-time dev fee)

> NOTE: The Store may reject autoclickers under their "apps that enable cheating" policy.
> Frame it as an accessibility/productivity tool in your Store description.
