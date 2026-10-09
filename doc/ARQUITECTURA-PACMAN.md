# Arquitectura — PAC-MAN para el Core de Vídeo (6502 / Tang Nano 9K)

> Documento de diseño. Recoge las **decisiones ya validadas en hardware** con los
> prototipos (`test_flicker/`, `test_maze/`) y sirve de plano para la implementación.

---

## 0. Decisiones fijadas (resumen)

| Tema | Decisión | Validado en |
|---|---|---|
| Pantalla / laberinto | **28×28 celdas**, celdas 8×8 cuadradas | `test_maze` |
| HUD | **2 filas arriba** (filas 0–1) | `test_maze` |
| Personajes | **Sprites 8×8 nativos** (1 celda) | `test_compare` |
| Colores | 5 distintos **sin flicker**, vía índice de color | `test_compare` |
| Puntos / píldoras | **Tiles** del tilemap | `test_maze` |
| Paredes | **Tiles finos** (auto-tiling, ~2 px) | `test_maze` |
| Motor | **ASM** para lo crítico, C para orquestación | — |
| Assets | En ROM; **SD después** si aprieta | — |

---

## 1. Geometría

```
Pantalla:            40 × 30 celdas   (320 × 240 px)
┌──────────────────────────────────────┐   fila 0 ┐
│  HUD  (score, vidas, nivel)          │   fila 1 ┘ (2 filas)
├──────────────────────────────────────┤
│                                      │
│   LABERINTO  28 × 28 celdas          │   filas 2..29
│   celdas 8×8 CUADRADAS               │
│   centrado: columnas 6..33           │
│   (6 columnas de margen a cada lado) │
│                                      │
└──────────────────────────────────────┘
```

- **Celdas de 8×8 cuadradas**: igual que el arcade → **sin deformación**.
- El laberinto del arcade es 28×31; las 3 filas superiores eran su marcador, que
  aquí cubre el **HUD de 2 filas**. Quedan **28 filas jugables** que encajan exactas.
- **Filas seguras 1..28** (fila 0 desplazada por pipeline, fila 29 por overscan).

---

## 2. Personajes: sprites 8×8 (decisión clave)

### Por qué 8×8 y no 16×16

| | **8×8 (elegido)** | 16×16 (descartado) |
|---|---|---|
| Celdas por personaje | **1×1** | 2×2 |
| Slots OAM por línea (5 personajes) | **5** (límite 8) | 10 (excede 8) |
| Flicker | **ninguno** | F3/F4 alternan |
| Patrones por frame/personaje | **1** | 4 |
| Coherencia con el laberinto | ✅ 1 celda | 2×2 |

**El límite de 8 sprites por línea es FIJO** (manual del core §13). Con 16×16, los
5 personajes no caben y hay que recurrir a parpadeo. Con **8×8 nada de eso ocurre**.

### Presupuesto de sprites

| Elemento | Slots |
|---|---|
| Pac-Man | 1 |
| 4 fantasmas | 4 |
| Fruta (temporal) | 1 |
| **Total** | **6 de 32** |

Por línea: **máximo 5** (los personajes) — muy por debajo de 8.

---

## 3. Color: índice + paleta (la técnica que da los 5 colores)

Un **patrón** guarda **índices 0-3**; la **paleta** los traduce a RGB.

```
índice del píxel = (plano1 << 1) | plano0        índices 0..3
índice 0 = TRANSPARENTE  →  3 colores usables por paleta (1, 2, 3)
```

**Consecuencia:** una misma paleta sirve para **hasta 3 sprites** con 3 colores
distintos, cambiando **solo el índice del dibujo**:

| Sprite | plano0 | plano1 | índice | color |
|---|---|---|---|---|
| A | dibujo | 0x00 | 1 | color 1 |
| B | 0x00 | dibujo | 2 | color 2 |
| C | dibujo | dibujo | 3 | color 3 |

### Reparto de paletas (5 personajes con 2 paletas)

| Paleta | Índice | Personaje | Color (RGB444) |
|---|---|---|---|
| 0 | 3 | F1 Blinky | rojo |
| 0 | 2 | F2 Pinky | rosa |
| 0 | 1 | F3 Inky | cian |
| 1 | 3 | Pac-Man | amarillo |
| 1 | 2 | F4 Clyde | naranja |

**4 paletas totales**: quedan **2 libres** (2 y 3) para fruta, estados
(frightened azul), parpadeos, etc.

> ⚠️ **Esto NO es flicker.** Son sprites siempre visibles con color fijo. El color
> se elige por el índice del patrón, no por alternar visibilidad.

### Estados de color (reprogramación dinámica)

Los colores se **reescriben en caliente** (`vc_pal_set_spr`, RGB444). Sirve para:
- Fantasmas "frightened" (azul) → reprogramar los colores de la paleta 0.
- Parpadeo azul↔blanco antes de volver a la normalidad.
- Fade in/out en transiciones.

---

## 4. Laberinto

### Estructura

- Mapa **28×28** codificado como datos (`#` pared, `.` punto, `o` power pellet, ` ` vacío).
- Se dibuja **una vez** al empezar el nivel (no por frame).
- Puntos/píldoras = **tiles** (0 sprites). Al comerlos, se borra la celda (tile 0).

### Paredes finas (auto-tiling)

Las paredes del arcade son **líneas finas (~2-4 px)**, no bloques de 8×8. Se
implementan con **auto-tiling**: para cada celda de muro se miran los 4 vecinos y
se dibuja una línea fina **solo en los lados que dan a pasillo**.

```
mascara (bitmask):  bit0=arriba  bit1=abajo  bit2=izq  bit3=der
tile = TILE_WALL_BASE + mascara     (15 combinaciones)
```

**Pendiente de ajuste**: probar si la línea va **pegada al borde** (versión actual)
o **centrada** en la celda (como el arcade), y el grosor (1-4 px).

### Colisión con paredes

- Cada celda de muro lleva el atributo **`VC_ATTR_SOLID`**.
- La colisión sprite↔tile la detecta el hardware (`vc_solid_hit()`), pero el flag
  es **global** (no dice cuál sprite). Se deduce en software comparando la celda
  destino del personaje con el mapa.

---

## 5. Movimiento

Pac-Man y los fantasmas **avanzan píxel a píxel**, pero **solo giran en el centro
de la celda**:

```
cada frame:
  avanzar según la dirección actual (px)
  si el personaje está alineado con el centro de una celda:
     - si hay dirección pedida (buffer) y no es pared -> girar
     - si toca pared -> parar
```

Estado por personaje: `x`, `y` (posición en px), `dir` (actual), `next_dir` (pedida).

**Túnel**: la fila central (14) es el túnel; al salir por un lado, se reaparece por
el otro (wraparound con módulo del ancho). `u8_mod` de `math.s` ayuda.

---

## 6. IA de fantasmas (la parte más costosa — ASM)

Los 4 fantasmas con modos clásicos:
- **Scatter**: cada fantasma va a su esquina.
- **Chase**: cada uno persigue según su regla (Blinky directo; Pinky delante;
  Inky combina; Clyde se acerca/aleja).
- **Frightened**: huyen (dirección opuesta).
- **Eaten**: vuelven a la casa como "ojos".

Decisión de movimiento por celda: en cada cruce, elegir la dirección que **minimiza
la distancia** al objetivo (sin invertir). Es la pieza que más ciclos consume y
donde el **ASM da ventaja**. Es también la parte **sin precedente** en los
prototipos previos (no había IA de pathing).

---

## 7. Reparto C / ASM

| Capa | Lenguaje | Motivo |
|---|---|---|
| Setup de vídeo, tiles, paletas | ASM | una vez; tablas directas |
| Escritura VRAM/OAM | ASM | ya en `vc.lib` |
| **Movimiento** | **ASM** | por frame; muchos `>>3`, `&7`, 16 bits |
| **IA de fantasmas** | **ASM** | el núcleo pesado |
| Colisiones | ASM | comparaciones de 16 bits |
| Puntos / comer | ASM | por frame |
| Orquestación (niveles, vidas, score, pantallas) | C | no crítico en ciclos, muy cambiante |
| Entrada | C (`joy.c`) | ya hecho |
| Sonido | ASM (`sound.s`) | driver SID, barridos por software |

## Reparto de voces del SID (sound.s)

| Voz | Direccion | Sonido |
|---|---|---|
| 1 | `$D400` | Waka-waka continuo (comer/moverse). Pulso, `hi $08-$18`, 2/frame. |
| 2 | `$D407` | Power pellet (pulso asc.), comer fantasma (saw asc.), muerte (saw desc.), game over (noise desc.). |
| 3 | `$D40E` | FONDO continuo: sirena (triangular, `$10-$20`, ritmo del waka) en juego normal; sonido de fantasmas azules (pulso, `$20-$3C`) con power pellet. |

> **Nota:** la frecuencia de cada voz se lleva en RAM (`snd_w_freq`, `snd_v2_freq`,
> `snd_bg_freq`), NO se lee del SID (este hardware no devuelve los registros de
> frecuencia).
| Texto / HUD | ASM (`text.s`) | ya hecho |

**Por qué ASM**: en este hardware el C de cc65 arrastra `none.lib` (pegamento,
aritmética de 16 bits, `shlax`/`aslax`) y no se puede permitir en el bucle de juego,
que debe caber en el **VBLANK** (si no, parpadeo).

---

## 8. Presupuesto de recursos

| Recurso | Límite | Uso previsto |
|---|---|---|
| Sprites (OAM) | 32 | **6** (Pac 1 + 4 fantasmas + fruta) |
| Sprites por línea | **8** | **máx 5** (los 5 personajes) |
| Patrones de sprite | 64 | Pac (2 frames × 4 dir) + fantasmas (2 × 4 dir × 4... con flip) + estados |
| Patrones de fondo | 256 (`$20-$7F` = fuente) | laberinto (paredes 15 + punto + power) + fuente |
| RAM | `$0800-$3F7F` | ver §9 |

**Patrones de fantasmas** (aprovechando `FLIP_X`/`FLIP_Y` para las direcciones
opuestas): cada fantasma = **2 frames × 2 orientaciones base** = 4 patrones (más
variantes de color). Pac-Man = 2-3 frames × 2 orientaciones = ~4-6.

---

## 9. Memoria y arranque

- Cargar en **`$0800`**, `LOAD GAME 0800` / `R 0800`.
- **Ampliar el stack** antes de programar: eliminar el segmento `STACK`
  desaprovechado, extender `RAM` hasta `$3F7F` y ajustar `c_sp` en `startup.s`.
  La presión de RAM es real (IA + estado de fantasmas).
- **Assets en ROM**; si aprieta, mover sprites a **SD** (ROM API `mfs_load_file`)
  con fallback en ROM.

---

## 10. Reutilización de los proyectos existentes

| Módulo | De | Estado |
|---|---|---|
| `vc.lib` + `video.h` | videocore | ✅ tal cual |
| `romapi.h` | videocore | ✅ tal cual |
| `joy.c` / `joy.h` | propio | ✅ (da 4 direcciones) |
| `sound.s` | propio | ✅ driver SID en ASM |
| `text.s` | propio | ✅ HUD/titulo; `txt_put_u16_4` reutilizado por la fruta |
| `math.s` | propio | ✅ `u8_mod`, `u16_div_u8` |
| `startup.s` | — | ✅ base del stack ajustada |
| `gho_ai.s` | propio | ✅ IA de fantasmas en ASM (ver §16) |
| `tools/` | propio | utilidades dev (gen de frutas, tests de IA con sim65) |

---

## 11. Prototipos (validación previa)

| Prototipo | Qué validó | Resultado |
|---|---|---|
| `test_flicker/` | Flicker de 5×16×16 | Parpadeo tolerable, pero innecesario con 8×8 |
| `test_maze/` | Laberinto, paredes finas, proporción | Laberinto 28×28 celdas 8×8 funciona |
| `test_maze/test_compare.c` | 8×8 vs 16×16, 5 colores | **8×8 sin flicker con 5 colores** |

---

## 12. Plan de implementación (por fases)

Cada fase deja el juego **compilable y jugable**.

1. **Cimientos**: ampliar stack; esqueleto ASM-first; cargar laberinto y paletas.
2. **Laberinto**: mapa 28×28, auto-tiling de paredes, puntos/power como tiles.
3. **Pac-Man**: sprite 8×8, movimiento con sub-píxel, giro en centro de celda,
   túnel, buffer de dirección.
4. **Comer**: colisión con puntos, borrar celda, score, power pellet.
5. **Un fantasma**: movimiento simple por el laberinto (sin IA).
6. **4 fantasmas**: estado por fantasma, colores, modo básico.
7. **IA**: scatter/chase/frightened/eaten; objetivo por fantasma.
8. **Reglas**: vidas, colisión Pac↔fantasma, fruta, niveles.
9. **Sonido + HUD + pantallas** (inicio, game over, récords).
10. **Pulido**: paredes centradas vs borde, ritmo de animación, balance.

---

## 13. Incógnitas a resolver en implementación

- **Paredes**: ¿línea pegada al borde (actual) o centrada (más como el arcade)?
- **Grosor de pared**: 1, 2, 3 o 4 px.
- **Frames de animación** de Pac-Man y fantasmas (ritmo).
- **IA**: cómo de fiel al arcade vs simplificada (por presupuesto de ciclos).
- **RAM final**: medir tras la fase 1 y decidir si hace falta SD.

---

# Estado implementado (actualizado)

## 14. Frutas (bonus)

- **8 frutas**, una por rango de nivel, en el orden del arcade:
  cereza, fresa, naranja (3-4), manzana (5-6), melon (7-8), galaxian (9-10),
  campana (11-12), llave (13+). Puntos: 100/300/500/700/1000/2000/3000/5000.
- Sprites **8×8** (patrones 40..47), dibujados con **2 planos** (`plano0`/`plano1`)
  y UNA sola paleta de sprite (paleta 3): indice 1=verde, 2=rojo, 3=naranja.
  Los datos de origen venian en color ARGB con formato **AABBGGRR**.
- **Aparicion**: 2 veces por nivel, a los **120 y 240** puntos comidos (2 umbrales
  en `fruit_at_dots[]`). Celda fija debajo de la casa (`FRUIT_COL/ROW`).
  Dura `FRUIT_FRAMES` (~10 s). Se come al pasar Pac-Man por encima.
- **No se hereda**: al morir, la fruta desaparece (`fruit_hide()` en `pac_die`).
- **HUD**: se dibuja la fruta del nivel junto a las vidas (slot OAM 6).
- Los sprites se generaron con `tools/gen_fruits.py`.

## 15. Dificultad por niveles

- `level` empieza en 1; al comer todos los puntos sube.
- Dos tablas por nivel (`NIVEL_MAX=8`, del ultimo en adelante se repite):
  - `level_gho_skip[]`: velocidad de fantasmas (menor = mas rapido).
  - `level_fright[]`: duracion del modo asustado (acortandose).
- HUD: `VIDAS n   NIVn   ...   score`.

## 16. IA de fantasmas en ASM (`gho_ai.s`)

- `gho_target()` y `gho_choose_dir_i()` migradas de C a **ASM** (patrones `gho_ai.s`).
- Reimplementa `cell_passable()` y `cell_ghost_house()` leyendo el mapa comprimido.
- **Verificacion (critica)**: test diferencial con **`sim65`** (el emulador de 6502
  que trae cc65): `tools/test_gho_full.c` compara el ASM real contra la misma logica
  en C sobre 409.600 casos -> **0 discrepancias**. Asi se detecto (y corrigio) un bug
  de sign-extension en el calculo de distancia ANTES de probar en hardware.
- **Ahorro medido**: CODE de la IA 1450 B (C) -> 1322 B (ASM) = -128 B; pero BSS +43
  y ZP +4 -> **neto ~-81 B**.

## 17. Limitacion de RAM (importante)

El juego esta **al limite** de RAM (`$0800-$3F7E`). Caracteristicas implementadas
pero **DESACTIVADAS** por falta de bytes (listas para activar con un `#define`):

| Feature | Coste | Estado |
|---|---|---|
| Puntaje flotante al comer fruta | ~26 B | `FRUIT_SHOW_SCORE 0` en `game.c` |
| Esquinas redondeadas del laberinto | ~30 B | no integrado (revertido) |

**Antes de anadir features, considerar reducir la base**: cargar graficos bajo
 demanda (hay espacio en VRAM `$4000-$7FFF`) o comprimir los mapas de sprites.
