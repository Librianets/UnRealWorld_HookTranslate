# UnReal World Translation Hook

Source for the common translation bootstrap `URWTrace.exe` and x86
presentation hook `URWTraceHook.dll`, version 0.22, for the original English
UnReal World 3.89.1 executable. Russian and Finnish share this runtime.

Required game SHA256:
`c6ed73642c1b1cb1f86b818c7586224398c2f0554f6114ced31bc40ff071806d`.
Hook 0.19 targets3.88; do not mix those versions.

## Build and test

With Visual Studio 2022 Desktop development with C++ and the Windows SDK,
run `build.cmd` in an **x86 Native Tools Command Prompt**. This builds the
bootstrap, DLL and native test tools. The separate GUI launcher is not built.
`test.cmd` runs catalogue fixtures and synthetic DLL self-tests without
launching an installed game. Real gameplay remains a separate verification step.

Install the bootstrap and DLL alongside the launcher and original game.
Language data, editor and shared INI come from **[UnRealWorld_MultiTranslate](https://github.com/Librianets/UnRealWorld_MultiTranslate)**.
The GUI source is in **[UnRealWorld_Launcher](https://github.com/Librianets/UnRealWorld_Launcher)**. Use matching component versions.

## How it works

`src/Launcher.cpp` is the bootstrap, not the GUI. It verifies the game SHA,
starts its own child process under the debugger, stops at the game entry,
loads the hook DLL, initializes it, and resumes the game. MinHook redirects
selected functions in process memory. English game identifiers and recipes
remain in use; the hook provides translated text and fonts for presentation.

The loader uses cross-process memory operations and DLL injection. Security
software may flag these operations; publishing source does not guarantee that
every scanner will accept the binaries. Reports should identify the exact
file, SHA256 and detection name.

## Version addresses

There are 188 hook profiles and 1118 verified direct call sites. Catalogue
function IDs retain3.88 identities; `VersionAddresses.h` maps them to3.89.1
addresses. These IDs must not be applied directly to another game version.
`docs/addresses_3.89.1.json` records the mapping. A newer game needs a new audit.

## Licensing

MinHook and its bundled HDE retain their notices under `third_party/minhook`.
Copy `THIRD_PARTY_LICENSES.txt` with distributed binaries, including Lite.
See `LICENSE_SCOPE.txt` for the project code. Original game EXEs, decompiler
dumps and user session logs are not part of this repository.
See [Russian instructions](README_RU.md).

Hook0.20 remains available for game3.89; Hook0.19 targets3.88.

## Steam entry installation in0.22

`SteamInstall.h` is shared with the GUI. In Steam mode the master loader is
copied to `urw.exe` and launches the preserved `urw-original.exe`. English
runs directly without the hook. The bootstrap inherits the Steam environment,
forwards arguments and waits for its child. Direct bootstrap launches are
refused while Steam mode is installed; use the game's Steam entry instead.
`test_steam_022.cmd` exercises supported updates, rollback, interrupted writes,
backup collisions and exclusive operation locks using temporary fixtures.
The shipped DLL and language data are byte-identical to0.21; only the GUI and
bootstrap changed. The DLL diagnostic version therefore remains0.21.
Real EN/RU/FI renamed-game startup and normal close were tested. Steam client
integration (overlay/playtime/Cloud) remains subject to player testing.
