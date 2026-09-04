# Build Instructions

PlusLib uses CMake "superbuild" method for building. All required libraries and toolkits are automatically downloaded, configured, and built via the PlusBuild repository.

## Prerequisites

### Windows

- **Visual Studio 2019 or newer** (Community, Professional, or Enterprise)
- **CMake 3.20 or newer** - [Download](https://cmake.org/download/)
- **Git** - [Download](https://git-scm.com/downloads)
- **Qt 5.15+** (optional, required for PlusApp)

### Linux (Ubuntu/Debian)

```bash
sudo apt-get update
sudo apt-get install build-essential cmake ninja-build git \
  libxt-dev libgl1-mesa-dev libglu1-mesa-dev
```

For Qt support (needed by PlusApp and by the PlusLib widgets):

```bash
sudo apt-get install qtbase5-dev qttools5-dev libqt5xmlpatterns5-dev \
  libqt5x11extras5-dev
```

The `qt5-default` package named by older instructions was removed in Ubuntu
22.04; `qtbase5-dev` replaces it.

To run the tests on a machine with no display, install a software OpenGL
implementation and an X server:

```bash
sudo apt-get install libgl1-mesa-dri xvfb
xvfb-run -a ctest
```

### macOS

```bash
brew install cmake ninja git
```

Qt 5.15 is not in Homebrew in a form that can be redistributed: its libraries
record absolute paths under `/opt/homebrew`, so a package built against it
will not run anywhere else. Install Qt 5.15 from the Qt installer or with
`aqtinstall` instead. Those builds are x86_64 only, so on an Apple silicon
machine build Plus for x86_64 as well:

```bash
cmake -S PlusBuild -B PlusBuild-bin -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=x86_64
```

## Building PlusLib

PlusLib is typically built as part of the PlusBuild superbuild, which handles all dependencies automatically.

### Step 1: Clone PlusBuild

```bash
git clone https://github.com/PlusToolkit/PlusBuild.git
cd PlusBuild
```

### Step 2: Create Build Directory

```bash
mkdir PlusBuild-bin
cd PlusBuild-bin
```

### Step 3: Configure with CMake

**Windows (using Visual Studio):**
```bash
cmake -G "Visual Studio 16 2019" -A x64 ../PlusBuild
```

**Linux/macOS:**
```bash
cmake -DCMAKE_BUILD_TYPE=Release ../PlusBuild
```

### Step 4: Build

**Windows:**
```bash
cmake --build . --config Release
```

Or open `PlusBuild.sln` in Visual Studio and build the `ALL_BUILD` project.

**Linux/macOS:**
```bash
make -j$(nproc)
```

This will take significant time (30 minutes to several hours) as it downloads and builds all dependencies.

## Build Options

Key CMake configuration options:

### Application Options
- `PLUSBUILD_BUILD_PLUSAPP` - Build PlusApp GUI applications (default: OFF)
- `PLUSBUILD_BUILD_PLUSLIB_WIDGETS` - Build Qt widgets (default: OFF)

### Device Support
- `PLUS_USE_OpenIGTLink` - Enable OpenIGTLink support (default: ON)
- `PLUS_USE_NDI` - NDI tracking devices
- `PLUS_USE_EPIPHAN` - Epiphan video capture cards
- `PLUS_USE_ICCAPTURING_VIDEO` - IC Imaging Source cameras
- `PLUS_USE_MMF_VIDEO` - Microsoft Media Foundation video
- `PLUS_USE_TELEMED_VIDEO` - Telemed ultrasound devices
- `PLUS_USE_THORLABS_VIDEO` - ThorLabs cameras
- `PLUS_USE_ULTRASONIX_VIDEO` - Ultrasonix/Analogic ultrasound

### Advanced Options
- `PLUS_USE_INTEL_MKL` - Use Intel Math Kernel Library for performance
- `PLUSBUILD_OFFLINE_BUILD` - Build without internet access (requires cached dependencies)

## Building PlusLib Standalone

If you need to build only PlusLib (without PlusBuild superbuild):

1. Build or install all dependencies (VTK, ITK, etc.)
2. Clone PlusLib:
   ```bash
   git clone https://github.com/PlusToolkit/PlusLib.git
   ```
3. Configure and build:
   ```bash
   mkdir PlusLib-bin
   cd PlusLib-bin
   cmake -DVTK_DIR=/path/to/VTK-build ../PlusLib
   cmake --build . --config Release
   ```

!!! warning
    Building PlusLib standalone is complex due to numerous dependencies. Using PlusBuild is strongly recommended.

## Running Tests

After building, run the automatic tests:

**Windows:**
```bash
cd PlusLib-bin
BuildAndTest.bat
```

**Linux/macOS:**
```bash
cd PlusLib-bin
./BuildAndTest.sh
```

Tests will run and submit results to the CDash dashboard.

## Build Output

After successful build:

- **Binaries**: `PlusLib-bin/bin/Release/` (Windows) or `PlusLib-bin/bin/` (Linux/macOS)
- **Libraries**: `PlusLib-bin/lib/Release/` (Windows) or `PlusLib-bin/lib/` (Linux/macOS)
- **Test data**: Downloaded to `PlusLibData` directory

## Troubleshooting

### Build Fails with "Cannot find VTK"

Ensure you're using PlusBuild superbuild, which handles VTK automatically.

### Long Build Times

The first build downloads and compiles all dependencies (VTK, ITK, etc.). Subsequent builds are much faster.

### CMake Version Too Old

Update CMake to version 3.15 or newer.

### Git SSL Certificate Errors

If behind a corporate firewall:
```bash
git config --global http.sslVerify false
```

### Windows: MSBuild Not Found

Ensure Visual Studio is properly installed and run cmake from "Developer Command Prompt for VS".

## Next Steps

- [Quick Start Guide](quick-start.md)
- [Configuration Guide](../user-guide/configuration.md)
- [Contributing](../developer-guide/contributing.md)
