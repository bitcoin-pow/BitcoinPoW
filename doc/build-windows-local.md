# Build BitcoinPoW 31.x on your Windows PC

Use **Visual Studio 2022** (17) with the "Desktop development with C++" workload.
You do **not** need Visual Studio 2026. Official Core 31 docs mention VS 2026
presets; those are optional.

GPU mining is not available on Windows in this tree. The node, wallet, CLI, and
Qt GUI still build.

## 1. Install tools

```powershell
winget install --id Microsoft.VisualStudio.2022.Community --override "--wait --quiet --add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended"
winget install Git.Git
winget install Kitware.CMake
```

Open **Developer PowerShell for VS 2022** for the rest.

If you do not already have vcpkg:

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
$env:VCPKG_ROOT = "C:\vcpkg"
```

Visual Studio's bundled vcpkg also works; then `VCPKG_INSTALLATION_ROOT` is already set.

## 2. Get the source

```powershell
git clone https://github.com/btcw-space/BitcoinPoW.git
cd BitcoinPoW
git checkout btcw_31.x
```

Use a path **without spaces** (for example `C:\src\BitcoinPoW`).

## 3. Fast first build (no GUI)

This skips Qt. First vcpkg run still takes a few minutes (Boost, libevent, SQLite).

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVCPKG_MANIFEST_FEATURES=wallet `
  -DVCPKG_INSTALLED_DIR="C:\btcw-vcpkg" `
  -DVCPKG_INSTALL_OPTIONS="--x-buildtrees-root=C:\vcpkg-bt" `
  -DBUILD_GUI=OFF `
  -DENABLE_WALLET=ON `
  -DENABLE_IPC=OFF `
  -DBUILD_TESTS=OFF `
  -DBUILD_BENCH=OFF `
  -DWITH_ZMQ=OFF

cmake --build build --config Release -j
```

Binaries land in `build\bin\Release\`:

- `bitcoind.exe`
- `bitcoin-cli.exe`
- `bitcoin-tx.exe`
- `bitcoin-util.exe`
- `bitcoin-wallet.exe`

## 4. GUI build (optional)

Install Qt 6.7+ MSVC 64-bit from https://www.qt.io/download-qt-installer
or with [aqtinstall](https://github.com/miurahr/aqtinstall):

```powershell
pip install aqtinstall
aqt install-qt windows desktop 6.7.3 win64_msvc2019_64 --outputdir C:\Qt
```

Then configure with GUI on and `CMAKE_PREFIX_PATH` pointing at Qt:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET=x64-windows `
  -DVCPKG_MANIFEST_NO_DEFAULT_FEATURES=ON `
  -DVCPKG_MANIFEST_FEATURES=wallet `
  -DVCPKG_INSTALLED_DIR="C:\btcw-vcpkg" `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.7.3\msvc2019_64" `
  -DBUILD_GUI=ON `
  -DENABLE_WALLET=ON `
  -DENABLE_IPC=OFF `
  -DBUILD_TESTS=OFF `
  -DWITH_ZMQ=OFF

cmake --build build --config Release -j
C:\Qt\6.7.3\msvc2019_64\bin\windeployqt.exe build\bin\Release\bitcoin-qt.exe
```

Do **not** use `-DVCPKG_MANIFEST_FEATURES=qt` unless you want vcpkg to compile
Qt from source. That is what made GitHub Actions sit on Configure for an hour.

## 5. If configure fails

- Path too long: keep `--x-buildtrees-root=C:\vcpkg-bt`.
- Spaces in the repo path: set `-DVCPKG_INSTALLED_DIR=C:\btcw-vcpkg`.
- Windows Defender: exclude the repo folder and `C:\vcpkg-bt`.
- Close other heavy apps; the first CMake/vcpkg pass is CPU-heavy.
