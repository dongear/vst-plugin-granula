# artempidor

Гранулятор для DAW: режет звук на «зёрна» и собирает из них облака, текстуры, глитчи и дроны. Внутри — кассетная сатурация, дилей и реверб.

Один плагин-инструмент с двумя режимами, переключаются прямо в окне:
- **SAMPLE** — закиньте в окно сэмпл (wav, aiff, flac, mp3, ogg) и играйте по нему с MIDI: 8 голосов, C3 = исходная высота, сустейн, velocity.
- **INPUT** — гранулирует живой звук с другой дорожки, поданный во вход Sidechain (Ableton: Sidechain → Audio From).

**Форматы:** VST3 (Windows / macOS / Linux), AU (macOS), Standalone.

## Interference

Второй плагин в комплекте — эффект «harmonic sweetener»: подтягивает любой звук в выбранную тональность, делает грязные и диссонирующие звуки музыкальными.

- **Тональность**: тоника + лад (мажор, минор, моды, пентатоники, аккорды) или свои ноты кликом по клавиатуре
- **MIDI KEY**: тональность следует за аккордом, который вы играете
- **COLOR** — сила эффекта; выше 100% ноты тональности звенят хвостом
- **SNAP** (HQ) — переносит ноты вне тональности на ближайшие ноты в тональности
- **FOCUS**, **GATE** (спектральный гейт), **TRANSIENTS** (сохранение атаки), **LOW/HIGH**, **MIX**
- Режимы **ZERO LATENCY** (банк фильтров) и **HQ** (спектральный, ~85 мс задержки, компенсируется DAW)
- Живой спектр с нотами тональности, 10 пресетов + свои пресеты (`.interference`)

## Скачать

Готовые сборки — на странице **[Releases](../../releases/latest)**. Внутри архива файл `INSTALL.txt` с инструкцией по установке.

## Возможности

**Режим SAMPLE**
- Перетаскивание сэмпла прямо в окно, волна с позициями зёрен и плейхедами голосов
- Position (клик/драг по волне), Spray, **Scan** (движение по сэмплу, от −2x до 2x — таймстретч), Attack, Release
- Кнопка «Hold to play» — прослушать без MIDI

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

**Пресеты**
- 16 заводских пресетов (8 для SAMPLE, 8 для INPUT)
- Свои пресеты: кнопка **SAVE**, файлы `.artempidor` в `Документы/artempidor/Presets/`
- Делиться пресетами: перетащите `.artempidor` на окно плагина — он скопируется в ваш список

## Сборка из исходников

Нужны CMake ≥ 3.22 и C++17-компилятор. JUCE скачается автоматически.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

Результат — в `build/Artempidor_artefacts/Release/`.

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

Плагин построен на [JUCE 8](https://juce.com), который бесплатен для открытых проектов под AGPLv3 (или по коммерческой лицензии JUCE). Поэтому исходный код artempidor распространяется под AGPLv3.
