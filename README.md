# PAC-MAN — Core de Vídeo (vc) para 6502 / Tang Nano 9K

Proyecto para crear un **Pac-Man** para el computador **6502** con el **Core de
Vídeo** (`videocore-6502-cc65`) y el **monitor 6502**. Motor en **ensamblador**
(lo crítico por frame) con orquestación en **C** (cc65).

---

## 📐 Diseño de Pac-Man (decisiones fijadas)

El **plano completo** (geometría, sprites, colores, IA, reparto C/ASM, plan por
fases) está en:

> **[`doc/ARQUITECTURA-PACMAN.md`](doc/ARQUITECTURA-PACMAN.md)**

Resumen:

| Tema | Decisión |
|---|---|
| Laberinto | **28×28 celdas**, celdas 8×8 cuadradas, paredes finas |
| HUD | 2 filas arriba (filas 0–1) |
| Personajes | **sprites 8×8** (1 celda), 5 colores, **sin flicker** |
| Puntos / píldoras | **tiles** del tilemap |
| Motor | **ASM** para lo crítico; C para orquestación |

### Prototipos de validación

| Carpeta | Qué valida |
|---|---|
| `test_flicker/` | Parpadeo de 5 sprites 16×16 |
| `test_maze/`   | Laberinto y comparación 8×8 vs 16×16 |

---

## Compilar y cargar

```bash
make CC65_HOME=D:/cc65
```

Genera `output/game.bin`. Cárgalo en el monitor:

```
LOAD GAME 0800
R 0800
```

> **Nota sobre `CC65_HOME`:** en este entorno cc65 es un ejecutable de Windows
> (`D:/cc65`). Compila desde **Git Bash / cmd** (`CC65_HOME=D:/cc65`), no desde
> WSL (WSL no puede lanzar los `.exe`).

---

## Mapa de memoria (hardware real)

Del fuente del monitor (`monitor_6502_TN-9k_16k`):

| Rango | Contenido |
|-------|-----------|
| `$0000–$3FFF` | **RAM (16 KB)** |
| `$4000–$7FFF` | VRAM |
| `$8000–$BFFF` | ROM (monitor) |
| `$C000–$C0xx` | I/O (GPIO, UART, SPI, I2C, timer) |
| `$D400–$D41F` | SID |
| `$D800–$D87F` | Vídeo |

**Dentro de la RAM:**
- `$0000–$00FF`: Zero Page (el monitor usa `$0002–$00FF`; el juego `$20–$7x`).
- `$0100–$01FF`: pila de hardware del 6502.
- **`$0800–$3F9F`**: programa (código + datos + BSS).
- **`$3FA0–$3FFD`**: pila software de cc65.
- ⚠️ **`$3FFE–$3FFF`**: flag de **AUTOBOOT del monitor**. No usar.

> El programa se carga en **`$0800`**. Cargarlo en `$0200` **NO funciona** en este
> hardware (algo del monitor usa esa zona), aunque la RAM llegue a `$3FFF`.

---

## Sonido (SID 6581, compatible C64)

El juego usa el **SID** en `$D400-$D41F` (idéntico al C64). Driver en `sound.s`
(ASM). **Reparto de voces** (importante, están muy justas):

| Voz | Uso |
|-----|-----|
| **1** | *waka-waka* continuo al moverse/comer (pulso) |
| **2** | efectos: power pellet, comer fantasma, muerte (saw/noise) y game over |
| **3** | fondo continuo: **sirena** en juego normal; **sonido de fantasmas azules** con power pellet |

> ⚠️ **Reparto delicado:** la sirena de fondo comparte voz 3 con el sonido de
> fantasmas azules (nunca a la vez: con power pellet suena el de azules). El
> *waka* tiene la voz 1 para él solo; los efectos cortos van en la voz 2.

> **Nota:** la frecuencia de cada voz se lleva en RAM (`snd_w_freq`, `snd_v2_freq`,
> `snd_bg_freq`), **NO** se lee del SID (este hardware no devuelve los registros
> de frecuencia).

Volumen master a máximo (`$D418 = 0x0F`, filtro desactivado). `snd_update()` se
llama una vez por frame para gestionar envolventes y el fondo continuo.

---

## Diseño del juego (decisiones fijadas)

### Laberinto = **tiles** del tilemap, personajes = **sprites**

El hardware limita la OAM a **32 sprites** y **8 sprites por línea**. Como solo
hay 5 personajes (Pac-Man + 4 fantasmas) más la fruta, todo cabe como sprites:

- **Laberinto, puntos y power pellets → tiles** del tilemap.
- **Pac-Man, fantasmas y fruta → sprites** (OAM).

### Movimiento (píxel a píxel, giro en el centro de la celda)

Pac-Man y los fantasmas avanzan **píxel a píxel**, pero **solo giran en el centro
de la celda**:

```
cada frame:
  avanzar según la dirección actual (px)
  si el personaje está alineado con el centro de una celda:
     - si hay dirección pedida (buffer) y no es pared -> girar
     - si toca pared -> parar
```

Estado por personaje: `x`, `y` (posición en px), `dir` (actual), `next_dir`
(pedida). El **túnel** (fila central) hace *wraparound*: al salir por un lado se
reaparece por el otro.

### IA de fantasmas (ASM, `gho_ai.s`)

Los 4 fantasmas con los modos clásicos del arcade:

- **Scatter**: cada fantasma va a su esquina.
- **Chase**: Blinky directo; Pinky delante de Pac-Man; Inky combina; Clyde se
  acerca/aleja según la distancia.
- **Frightened**: huyen (dirección opuesta) y se pueden comer.
- **Eaten**: vuelven a la casa como "ojos" y renacen.

En cada cruce eligen la dirección que **minimiza la distancia** al objetivo (sin
invertir). Es la pieza que más ciclos consume, por eso está en **ASM**.

### Colisiones (software)

- **Personaje ↔ pared:** convertir su X/Y a celda (`col = x/8`, `row = y/8`) y
  consultar el mapa (`maze_bits`). El flag `VC_ATTR_SOLID` del hardware es global,
  así que la celda destino se deduce en software.
- **Pac-Man ↔ fantasma:** cajas AABB (`vc_box_overlap`); con power pellet y
  fantasmas azules, el fantasma se come (+200 y ojos a casa).
- **Pac-Man ↔ punto / power pellet:** al centro de la celda se borra el tile y se
  suma puntaje (10 / 50).
- **Pac-Man ↔ fruta:** aparece 2× por nivel (a 120 y 240 puntos); al comerla suma
  los puntos de la fruta del nivel.

### Entrada: joystick Atari + UART

Se puede jugar con **joystick Atari (DB9)** y/o con el teclado del terminal.

**Joystick** (`joy.c` / `include/joy.h`): **Puerto GPIO 1**, bits **3-7** (los
bits 0-2 los ocupa el TM1638, que **no se toca**).

| Señal | Pin | Bit |
|-------|-----|-----|
| right | 40 | 3 |
| left  | 34 | 4 |
| down  | 33 | 5 |
| up    | 30 | 6 |
| fire  | 29 | 7 |

Registros: `$C000` (datos), `$C002` (config: 1 = entrada). `joy_init()` hace
**read-modify-write** de `$C002` para no pisar los bits del TM1638. Ver
`doc/JOYSTICK-INTEGRACION.md` para el mini-manual.

> **Polaridad:** el joystick Atari es activo en bajo (0 = pulsado).

**Teclado (UART):** en paralelo al joystick.

| Tecla | Acción |
|-------|--------|
| `a` / `j` / flecha izq | mover a la izquierda |
| `d` / `l` / flecha der | mover a la derecha |
| `w` / `i` | mover hacia arriba |
| `s` / `k` | mover hacia abajo |
| `q` | salir al monitor |

(En GAME OVER, `FIRE` del joystick empieza otra partida; `q` sale.)

### Reglas

- **3 vidas.** Al ser atrapado por un fantasma, Pae-Man muere (animación) y
  reaparece en la posición inicial.
- **Puntos:** 10 por punto, 50 por power pellet, 200 por fantasma comido,
  fruta según el nivel (100..5000).
- **Power pellets:** los fantasmas se vuelven azules y se pueden comer; el
  efecto dura menos según el nivel.
- **Niveles:** al comer todos los puntos sube el nivel; los fantasmas van más
  rápidos y el modo asustado dura menos.
- **Fin de partida:** al perder la última vida se limpia la pantalla y se
  muestra `GAME OVER` / `FIRE: OTRA PARTIDA`.

---

## Presupuesto de recursos

| Recurso | Límite | Uso |
|---|---|---|
| Sprites (OAM) | 32 | Pac 1 + 4 fantasmas + fruta 1 + fruta HUD 1 = **7** |
| Sprites por línea | **8** | máx. ~6 (Pac + 4 fantasmas + fruta) → OK |
| Patrones de sprite | 64 | Pac 8 (dir/muerte) + fantasmas 12 + asustado 4 + ojos 1 + frutas 8 |
| Patrones de fondo | 256 (fuente `$20`-`$7F`) | paredes 15 + punto + power + fuente |
| RAM | `$0800-$3F7E` | **muy justo** — ver `doc/ARQUITECTURA-PACMAN.md` §17 |

> **Límite de 8 sprites por línea (⚠️ importante):** el core dibuja como máximo
> **8 sprites por línea**. Con Pac + 4 fantasmas + fruta el peor caso es **6**,
> por debajo del límite (por eso se eligieron sprites 8×8).

> **RAM justa:** el binario está **pegado al límite** (`$3F7E`). Features ya
> implementadas pero desactivadas por falta de bytes: el puntaje flotante al
> comer fruta y las esquinas redondeadas del laberinto.

---

## Módulos

| Archivo | Lenguaje | Qué hace |
|---|---|---|
| `game.c` | C | lógica del juego (orquestación, frutas, niveles, colisiones) |
| `sound.s` | ASM | driver SID 6581 (waka, power, fantasma, muerte, sirena, azules) |
| `gho_ai.s` | ASM | IA de los 4 fantasmas (target + elección de dirección) |
| `joy.c` | C | lectura joystick Atari |
| `text.s` | ASM | texto y HUD (escritura VRAM directa, números por restas) |
| `math.s` | ASM | `u8_mod`, `u16_div_u8` (sin división de hardware) |
| `walltile.s` | ASM | patrón de tile de pared según máscara de borde |
| `startup.s` | ASM | arranque cc65 |

## Presupuesto OAM (slots usados)

```
0     Pac-Man
1-4   Blinky / Pinky / Inky / Clyde
5     fruta (del juego, temporal)
6     fruta del nivel (HUD)
```

---

## Estructura

```
Pacman/
├── makefile            build autónomo
├── game.c              lógica del juego
├── sound.s             driver SID (ASM)
├── gho_ai.s            IA de fantasmas (ASM)
├── walltile.s          tiles de pared (ASM)
├── joy.c               joystick Atari
├── text.s              texto/HUD en ASM
├── math.s              aritmética sin división (ASM)
├── startup.s           arranque cc65
├── tools/              utilidades de desarrollo (NO en el build):
│   ├── gen_fruits.py      genera los sprites de fruta
│   ├── test_ai.py         test diferencial de la IA (C 32 vs 16 bits)
│   └── test_gho*.c        test de la IA ASM con sim65
├── include/
│   ├── video.h         API del Core de Vídeo (copia de la lib)
│   ├── romapi.h        ROM API del monitor
│   ├── sound.h         API del driver SID
│   └── joy.h           API del joystick
├── config/
│   └── programa.cfg    config del linker (juego)
├── doc/
│   ├── VIDEO-LIB.md            manual de la biblioteca
│   ├── ARQUITECTURA-PACMAN.md  plano de diseño y decisiones
│   ├── JOYSTICK-INTEGRACION.md mini-manual del joystick
│   ├── TIPS-Y-TRAMPA.md        trampas y lecciones para el proximo juego
│   └── text.s.md               diseño del módulo de texto ASM
├── lib/
│   └── vc.lib          copia de la biblioteca
├── build/              objetos
└── output/
    ├── game.bin        binario final
    └── game.map        mapa de memoria
```

### Actualizar la biblioteca

```bash
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/config/programa.cfg config/programa.cfg
```

> **Versión de la API:** el juego está alineado con la biblioteca `vc`
> correspondiente al **Manual del Core v2.7** (constantes neutras
> `VC_BGPAL_0..3`, `VC_SPPAL_0..3`, `VC_TINTA(paleta)`).
