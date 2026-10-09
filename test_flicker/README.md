# TEST FLICKER — prototipo de prueba de parpadeo (5 × 16×16)

Prototipo **aislado** para decidir, **con los ojos**, si el parpadeo (*flicker*)
de sprites 16×16 es tolerable o si hay que ir a Scale2x.

> No forma parte del juego. Es un banco de pruebas. No toca el juego Pac-Man.

---

## 1. Qué prueba exactamente

En una **misma fila** coloca **5 personajes de 16×16**:

| Personaje | Prioridad | ¿Parpadea? |
|---|---|---|
| Pac-Man  | 1 (fija) | no |
| F1       | 2 (fija) | no |
| F2       | 3 (fija) | no |
| F3       | 4 (alterna) | **sí, 30 Hz** |
| F4       | 5 (alterna) | **sí, 30 Hz** |

F3 y F4 se **alternan cada frame** (uno visible, el otro oculto). Es la idea
que se barajó: *"dejar un fantasma como última prioridad y ponerlo a parpadear"*.

Los 5 se **mueven en horizontal** rebotando en los márgenes: el parpadeo se
percibe distinto **en movimiento** que estático, y en movimiento es como se
verá en el juego real.

---

## 2. Los dos modos (la clave del experimento)

En `test_flicker.c` hay una sola línea que cambia TODO el dibujado:

```c
#define DRAW_MODE  MODE_NATIVE   /* <-- CAMBIA AQUI */
```

| Modo | Cómo dibuja | Slots por línea | Parpadeo |
|---|---|---|---|
| `MODE_NATIVE`  | 16×16 nativo (`vc_sprite16_set`, 4 sprites) | **10** (2 sobran) | **sí** |
| `MODE_SCALE2X` | 8×8 al doble (`VC_SPR_SCALE2X`, 1 sprite) | **5** | **no** |

**Recompila cambiando esa constante** y compara los MISMOS 5 personajes en
ambos modos. Ese es el experimento: no hay que rehacer nada, solo cambiar una
constante.

---

## 3. Compilar y cargar

Desde **Git Bash / cmd** (NO desde WSL: cc65 es un `.exe` de Windows):

```bash
make CC65_HOME=D:/cc65
```

Genera `output/test.bin`. Cárgalo en el monitor:

```
LOAD TEST 0800
R 0800
```

Para probar el otro modo: edita `DRAW_MODE` en `test_flicker.c`, `make clean`,
y `make` otra vez.

---

## 4. Qué mirar

1. **Modo NATIVO**: ¿el parpadeo de F3/F4 es tolerable o molesto? Fíjate sobre
   todo **cuando los personajes están juntos** (es cuando el problema ocurre de
   verdad).
2. **Modo SCALE2X**: ¿se ve aceptable el personaje "al doble"? ¿El píxel gordo
   es un precio razonable a cambio de cero parpadeo?
3. **Compara en movimiento**, no parado.

---

## 5. Estructura

```
test_flicker/
├── makefile
├── test_flicker.c     <- el prototipo (DRAW_MODE conmutable)
├── startup.s          <- copia del arranque cc65
├── programa.cfg       <- config del linker (copia)
├── include/           <- video.h, romapi.h (copias)
└── lib/vc.lib         <- biblioteca de vídeo (copia)
```

Las copias de `include/`, `lib/` y `programa.cfg` son para que el prototipo sea
**autónomo** (no depende de rutas del proyecto principal).

---

## 6. Si el parpadeo se ve feo

Entonces se fija `DRAW_MODE = MODE_SCALE2X` y **no hay que cambiar nada más**:
la capa de dibujado está encapsulada. Esa es la razón de hacer la prueba así:
la decisión nativo/Scale2x queda en **un solo punto** del código.
