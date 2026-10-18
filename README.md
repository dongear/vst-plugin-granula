# Granula

Гранулятор реального времени для DAW: режет входящий звук на «зёрна» и собирает из них облака, текстуры, глитчи и дроны. Внутри — кассетная сатурация, дилей и реверб.

**Форматы:** VST3 (Windows / macOS / Linux), AU (macOS), Standalone.

## Скачать

Готовые сборки — на странице **[Releases](../../releases/latest)**. Внутри архива файл `INSTALL.txt` с инструкцией по установке.

## Возможности

**Grain**
- XY-пэд: размер зерна × плотность
- Size, Density (или синхронизация с темпом DAW), Position, Spray
- Pitch ±24 st, случайный питч с квантованием по ладу (Chromatic, Major, Minor, Pentatonic, Fifths, Octaves), Detune
- Reverse (вероятность обратного зерна), Spread (стерео), Shape (форма огибающей)
- Feedback — зёрна возвращаются в буфер; Freeze — бесконечная текстура из того, что уже записано
- Живая визуализация облака зёрен

**Cassette** — Drive, Wow, Flutter, Age (потеря верхов), Hiss
**Delay** — Time / Sync, Feedback, Tone, Ping-Pong, Mix
**Reverb** — 8-линейный FDN: Size, Decay до 20 с, Damping, Pre-Delay, Width, Mix

8 заводских пресетов: Init, Cloud Pad, Shimmer Octaves, Glitch Stutter, Lo-Fi Cassette, Reverse Dream, Drone Machine, Grain Delay.

## Сборка из исходников

Нужны CMake ≥ 3.22 и C++17-компилятор. JUCE скачается автоматически.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Результат — в `build/Granula_artefacts/Release/`.

На Linux дополнительно нужны пакеты: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libgl1-mesa-dev`.

Проверка DSP без хоста (рендерит WAV-файлы через всю цепочку):

```bash
cmake -B build -DGRANULA_BUILD_TESTS=ON && cmake --build build --target granula_render
./build/granula_render out/
```

## Релизы

Каждый пуш в `main` собирается под все три ОС (GitHub Actions) и проверяется [pluginval](https://github.com/Tracktion/pluginval). Чтобы выпустить версию для всех:

```bash
git tag v0.1.0
git push origin v0.1.0
```

Через ~10 минут на странице Releases появятся архивы для Windows, macOS и Linux.

## Лицензия

Плагин построен на [JUCE 8](https://juce.com), который бесплатен для открытых проектов под AGPLv3 (или по коммерческой лицензии JUCE). Поэтому исходный код Granula распространяется под AGPLv3.
