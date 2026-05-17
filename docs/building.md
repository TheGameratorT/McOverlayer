# Building MCOverlayer

## Prerequisites

- **CMake** 3.20 or higher
- **C++20** compatible compiler
- **Qt 6** with modules: Core, Gui, Widgets, Concurrent
- **Ninja** (recommended) or another CMake generator

## Linux

```bash
# Ubuntu / Debian
sudo apt-get install cmake ninja-build \
    qt6-base-dev libqt6concurrent6-dev

# Arch Linux
sudo pacman -S cmake ninja qt6-base
```

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## macOS

```bash
brew install cmake ninja qt@6
export CMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(sysctl -n hw.ncpu)
```

The GUI and region editor are built as `.app` bundles inside `build/apps/`.

## Windows (MSYS2 / MinGW64)

Open an **MSYS2 MinGW64** shell and install dependencies:

```bash
pacman -S mingw-w64-x86_64-{gcc,cmake,ninja,qt6-base,qt6-tools}
```

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Build outputs

After a successful build the `build/dist/` folder is populated automatically by the `deploy` target:

| Platform | Contents |
|----------|----------|
| Linux | `mcoverlayer-core.so`, three executables, `entity_regions/` |
| macOS | `mcoverlayer-cli`, two `.app` bundles (Qt-deployed), `entity_regions/` |
| Windows | `mcoverlayer-core.dll`, three `.exe` files, Qt DLLs, MinGW DLLs, `entity_regions/` |

## Installing to a prefix

```bash
cmake --install build --prefix /usr/local
```

## CMake options

| Option | Default | Description |
|--------|---------|-------------|
| `BUILD_SHARED_LIBS` | `ON` | Build `mcoverlayer-core` as a shared library |
| `CMAKE_BUILD_TYPE` | — | `Release`, `Debug`, or `RelWithDebInfo` |
