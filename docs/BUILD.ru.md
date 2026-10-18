# Сборка и упаковка

[English](BUILD.md) · [Главная](../README.ru.md)

## Требования

- Windows x64, PowerShell 5.1 или новее, Git, CMake 3.20 или новее, Python 3 для конфигурации OpenXR SDK через CMake.
- Visual Studio 2022 либо Build Tools 2022 с компонентами **Разработка классических приложений на C++**, инструментами MSVC x86/x64 и Windows SDK. Релиз собирался MSVC 14.44.35207 и SDK 10.0.22621.0.
- Обязательна 32-битная сборка: `swkotor.exe` — x86. Скрипт использует x86-компилятор с хостом x64 и NMake. Сборка мода для x64 не подходит.
- Для компиляции и математических тестов игра и шлем не нужны. Для проверки в игре нужны поддерживаемый EXE и рабочий 32-битный OpenXR runtime.

## Обычная сборка

```powershell
git clone https://github.com/megabars/kotor-vr.git
Set-Location kotor-vr
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1
.\bin\kotor-vr-launcher.exe --version
```

Скрипт находит Visual Studio через `vswhere`, выбирает установленный Windows SDK, конфигурирует каталог `build/`, собирает **Release** и запускает CTest. OpenXR SDK и MinHook уже включены в исходники: скачивание зависимостей и инициализация подмодулей не требуются. Файл `VERSION` задаёт версию проекта, генерируемого заголовка и ресурсов Windows. Для этого релиза ожидается `0.0.1`.

Результат в `bin/`:

| Файл | Назначение |
| --- | --- |
| `kotor_vr.dll` | Внедряемый модуль отрисовки игры |
| `kotor-vr-launcher.exe` | Проверка EXE, выбор runtime и загрузка DLL |
| `kotor-vr-probe.exe` | Независимая проверка шлема сценой с кубом |
| `kotor-vr-math-test.exe` | Проверки проекций и курсора без шлема |

OpenXR loader и MinHook линкуются статически. Среда Microsoft C/C++ используется динамически (`/MD`), поэтому пользователям нужен VC++ Redistributable **x86**. Сам OpenXR runtime от производителя устанавливается отдельно.

## Нестандартные пути и параметры

`-ToolchainRoot` указывает на **каталог версии MSVC**, а не корень Visual Studio. В `-WindowsSdkRoot` должны находиться `Include`, `Lib` и `bin`:

```powershell
.\build.ps1 -ToolchainRoot 'D:\BuildTools\VC\Tools\MSVC\14.44.35207' -WindowsSdkRoot 'D:\Windows Kits\10' -WindowsSdkVersion '10.0.22621.0'
.\build.ps1 -Configuration RelWithDebInfo -BuildDirectory "$PWD\build-debug-info" -OutputDirectory "$PWD\bin-debug-info"
.\build.ps1 -ConfigureOnly
.\build.ps1 -ProbeOnly -BuildDirectory "$PWD\build-probe" -OutputDirectory "$PWD\bin-probe"
```

После выполнения восстанавливаются `PATH`, `INCLUDE`, `LIB` и `VSLANG` текущего процесса. Глобальные переменные среды не меняются. После смены компилятора или генератора используйте новый `-BuildDirectory`, чтобы не конфликтовать со старым кешем CMake. `-ProbeOnly` исключает игровую DLL, поэтому полноценный установочный архив из такой сборки не получится. Нестандартные папки вывода не игнорируются автоматически: не добавляйте бинарники в Git.

Эквивалентные команды в **среде разработчика MSVC x86**:

```powershell
cmake -S . -B build-manual -G 'NMake Makefiles' -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_RC_COMPILER=rc
cmake --build build-manual
ctest --test-dir build-manual --output-on-failure
```

## Проверка и установка своей сборки

```powershell
ctest --test-dir build --output-on-failure
.\bin\kotor-vr-math-test.exe
.\bin\kotor-vr-launcher.exe --version
.\bin\kotor-vr-launcher.exe --help
.\install.ps1 -GameDirectory 'D:\Games\Star Wars - KotOR'
```

Тесты проверяют рабочие стереопроекции, смещения головы/глаз и обратную проекцию курсора. Их успех не подтверждает видимость в шлеме или совместимость игры. Для живой проверки подключите Virtual Desktop, запустите тест с кубом, затем игру и проверьте объём, центрирование, интерфейс и щелчки в сохранении. Подробности — в [руководстве по установке](INSTALL.ru.md).

## Подготовка релиза

Соберите полный Release в стандартный `bin/`, затем выполните:

```powershell
.\package.ps1
Get-FileHash .\dist\kotor-vr-0.0.1-windows-x86.zip -Algorithm SHA256
```

Скрипт проверяет ресурс версии всех четырёх бинарников, копирует только разрешённый набор файлов, документацию, установщик и лицензии, затем создаёт:

- `dist/kotor-vr-0.0.1-windows-x86.zip` с верхним каталогом `kotor-vr/`;
- `dist/SHA256SUMS.txt` с SHA-256 архива;
- отдельный список контрольных сумм бинарников **внутри** архива.

Исходники зависимостей, файлы игры, PDB/ILK, журналы, личные INI, сохранения и снимки экрана в архив не включаются. Скрипт не переиспользует каталог подготовки и не перезаписывает существующий архив. Для повторной попытки задайте `-OutputDirectory "$PWD\dist\attempt-2"`. Временные метки архивов могут отличаться: хеш идентифицирует опубликованный архив, побайтовая воспроизводимость сборок не обещается.

В Git коммитятся исходники и документация. Готовые ZIP и контрольная сумма публикуются вложениями GitHub Release для тега `v0.0.1`. Бинарники в историю Git не добавляются.

## Типичные ошибки сборки

| Ошибка | Решение |
| --- | --- |
| MSVC/vswhere не найден | Установите C++ workload или укажите точный `-ToolchainRoot` |
| Нет `rc.exe`, `kernel32.lib`, заголовков | Установите полный Windows SDK и укажите корень/версию |
| `KOTOR requires an x86 build` | Используйте x86-компилятор и новый каталог сборки |
| Конфигурация SDK не находит Python | Установите Python 3 и добавьте в PATH текущего терминала |
| Команда `cmake` не найдена | Установите CMake и заново откройте терминал |
| DLL нельзя перезаписать | Закройте игру, использующую эту DLL, перед пересборкой |
| Debug не запускается на другом ПК | Распространяйте Release; отладочный CRT не предназначен для такого развёртывания |
