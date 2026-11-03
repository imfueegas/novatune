# NovaTune

Plugin de corrección de tono en tiempo real (autotune) en formato **VST3** (Windows / macOS / Linux), más AU en macOS y versión Standalone. Hecho con [JUCE](https://juce.com). El motor de audio es propio y no depende de JUCE.

## Cómo conseguir el .vst3

### Opción A: sin instalar nada (GitHub Actions)

1. Crea un repositorio en GitHub y sube esta carpeta tal cual.
2. Pestaña **Actions** → se ejecuta *Build NovaTune* solo (o lánzalo con *Run workflow*).
3. Cuando acabe (10-15 min), descarga el artefacto **NovaTune-Windows** (también hay macOS y Linux).
4. Dentro está `VST3/NovaTune.vst3`.

### Opción B: compilar en tu PC

Necesitas CMake 3.22+ y un compilador (Visual Studio 2022 con "Desarrollo para escritorio con C++" en Windows, Xcode en macOS, `build-essential` y las librerías de JUCE en Linux). JUCE se descarga solo la primera vez.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
```

El plugin queda en `build/NovaTune_artefacts/Release/VST3/NovaTune.vst3`.

## Instalarlo en FL Studio (Windows)

1. Copia la carpeta `NovaTune.vst3` a `C:\Program Files\Common Files\VST3\`.
2. En FL Studio: **Options → Manage plugins → Find installed plugins** (o *Start scan*).
3. Cárgalo como efecto en una pista de mezclador, sobre una voz **monofónica** (una sola voz, sin acompañamiento).

## Parámetros

| Control | Qué hace |
|---|---|
| **Key / Scale** | Tonalidad y escala a la que se corrige (cromática, mayor, menor, pentatónicas, blues, modos...). |
| **Teclas de notas** | Muestran las notas permitidas. Si pulsas una, pasa a escala *Custom* y activa/desactiva esa nota. |
| **Retune Speed** | 0 ms = efecto robótico "duro". 20-50 ms = corrección natural. 200+ ms = solo suaviza. |
| **Flex-Tune** | Margen en cents alrededor de la nota donde no se corrige. Conserva el vibrato natural. |
| **Amount** | Intensidad de la corrección (0-100 %). |
| **Transpose** | Sube o baja la voz en semitonos tras la corrección. |
| **Reference A** | Afinación de referencia (440 Hz por defecto). |

El medidor superior muestra la nota detectada (barra blanca) y la nota que se oye (punto turquesa).

## Cómo funciona

1. **Detección:** algoritmo YIN sobre una copia diezmada de la señal (70-1100 Hz), con puerta de energía y rechazo de saltos de octava.
2. **Decisión:** nota más cercana de la escala, con histéresis para que no oscile en los límites entre notas. La corrección se suaviza con la constante de tiempo de *Retune Speed*.
3. **Cambio de tono:** línea de retardo con dos cabezas y fundido cruzado. La ventana se ajusta a 2k periodos del tono detectado para que las dos cabezas estén en fase y no haya filtro peine en notas sostenidas.
4. **Sin voz:** consonantes y silencios no se corrigen (solo se aplica la transposición), para evitar artefactos.
5. Con corrección ≈ 0 la salida es una copia retardada limpia de la entrada.

Latencia: ≈ 14 ms, informada al anfitrión (FL Studio la compensa).

## Tests del motor

```
g++ -O2 -std=c++17 -ISource Tests/EngineTest.cpp -o engine_test && ./engine_test
```

Generan voces sintéticas desafinadas y comprueban afinación final, escalas, vibrato, ruido, silencios y estéreo. También con `cmake -B build -DNOVATUNE_BUILD_TESTS=ON -DNOVATUNE_BUILD_PLUGIN=OFF`.

## Limitaciones

- **Monofónico:** una sola voz o instrumento melódico. No sirve para acordes.
- **Sin preservación de formantes:** con correcciones grandes (más de ~4-5 semitonos) la voz suena más "ardilla" o más "grave". Para correcciones normales no se nota.
- No es un clon de Auto-Tune ni usa código de Antares. Los plugins comerciales llevan años de ajuste fino (formantes, detección más robusta, modo gráfico); este es un punto de partida sólido y modificable.

## Estructura

```
Source/DSP/PitchDetector.h    detector YIN
Source/DSP/PitchShifter.h     cambiador de tono
Source/DSP/AutoTuneEngine.h   lógica de notas, retune, flex, unvoiced
Source/DSP/Scales.h           escalas
Source/PluginProcessor.*      parámetros y conexión con el anfitrión
Source/PluginEditor.*         interfaz
Tests/EngineTest.cpp          tests offline
```

## Licencia

JUCE se distribuye con licencia dual (AGPLv3 o comercial). Si vas a distribuir o vender el plugin, revisa las condiciones en juce.com/juce-licence.
