# UnReal World Translation Hook

Source for the common translation bootstrap `URWTrace.exe` and x86
presentation hook `URWTraceHook.dll`, version 0.20, for the original English
UnReal World 3.89 executable. Russian and Finnish share this runtime.

Required game SHA256:
`e0b15bbbe16ffcd95d4ab61f08fe95dea4b95ebe1b3de428dae1d9477a9710d9`.
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
function IDs retain3.88 identities; `VersionAddresses.h` maps them to3.89
addresses. These IDs must not be applied directly to another game version.
`docs/addresses_3.89.json` records the mapping. A newer game needs a new audit.

## Licensing

MinHook and its bundled HDE retain their notices under `third_party/minhook`.
Copy `THIRD_PARTY_LICENSES.txt` with distributed binaries, including Lite.
See `LICENSE_SCOPE.txt` for the project code. Original game EXEs, decompiler
dumps and user session logs are not part of this repository.
See [Russian instructions](README_RU.md).
