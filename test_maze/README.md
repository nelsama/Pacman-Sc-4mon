# TEST MAZE — laberinto + comparación 8×8 vs 16×16

Prototipo de validación del laberinto y del tamaño de personaje. **Decisiones ya
tomadas aquí** (ver `../doc/ARQUITECTURA-PACMAN.md`).

---

## Qué hay aquí

| Archivo | Qué es |
|---|---|
| `test_maze.c` | **Prototipo vigente**: laberinto arcade + comparación 8×8 vs 16×16 |
| `legacy_laberinto_16x16.c` | Versión anterior (laberinto solo con personajes 16×16) |

Compila por defecto `test_maze.c` (el vigente).

---

## Qué muestra (`test_maze.c`)

El **laberinto del arcade** (28×28, celdas 8×8 cuadradas, paredes finas por
auto-tiling) con **dos grupos de personajes** para comparar:

| Zona | Personajes | Slots/línea | Flicker |
|---|---|---|---|
| Izquierda | 5 × **8×8** | 5 | **ninguno** |
| Derecha | 5 × **16×16** | 10 | F3/F4 alternan |

Los 5 personajes de cada grupo tienen **colores distintos** (amarillo, rojo, rosa,
cian, naranja), logrados con el truco del **índice de color** (misma paleta, distinto
índice) — no con flicker.

---

## Resultado de la validación

- **8×8 elegido**: 1 slot por personaje, sin flicker, 5 colores, coherente con el
  laberinto (1 celda por personaje).
- **16×16 descartado**: 2 slots/línea → 10 > 8, necesita parpadeo.

---

## Configuración ajustable

En `test_maze.c`:

```c
#define GROSOR_PX    2    /* grosor de las paredes finas (1..4) */
#define ANIM_FRAMES  8    /* frames entre cambios de animación (sube = más lento) */
```

---

## Compilar y cargar

```bash
make CC65_HOME=D:/cc65
```
```
LOAD TEST 0800
R 0800
```
