# Перехват перевода UnReal World

Общий загрузчик `URWTrace.exe` и DLL `URWTraceHook.dll` версии0.22 для чистой
английской3.89.1. Игра, готовые каталоги перевода и GUI сюда не входят.

Из x86 Native Tools Command Prompt Visual Studio 2022 запустите `build.cmd`.
`test.cmd` проверяет синтетические примеры и сам hook, не запуская игру.
Требуются Desktop development with C++ и Windows SDK. GUI собирается отдельно
в UnRealWorld_Launcher; языковые данные и INI — в UnRealWorld_MultiTranslate.

`src/Launcher.cpp` — технический загрузчик DLL. `src/URW_Launcher.cpp` во втором
репозитории — оконный GUI. Загрузчик сверяет SHA оригинального3.89.1,
запускает собственный процесс игры и подключает нашу DLL. MinHook меняет
инструкции в памяти для перехватов отображения. Игровые английские ключи,
рецепты и файл EXE на диске не заменяются. Возможны срабатывания защитных
систем на сам механизм DLL injection; исходники позволяют изучить его работу.

188 профилей,1118 известных прямых вызовов. Адресные ID каталогов исторически
относятся к3.88, физические адреса3.89.1 — в VersionAddresses.h и docs.
На новую версию игры эти адреса автоматически переносить нельзя.

Сохраняйте лицензии MinHook/HDE и вкладывайте THIRD_PARTY_LICENSES.txt
в бинарные комплекты. Лицензия собственного кода — LICENSE_SCOPE.txt.
Оригинальные EXE, ASM/C из IDA и пользовательские журналы не публикуются.

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
