# Diseño: módulo `text.s` (texto y HUD en ensamblador)

> Objetivo: reemplazar las rutinas de texto/HUD de `game.c` (hoy en C, muy
> verbosas) por un módulo ASM (`text.s`) más pequeño y rápido. Además elimina
> las **divisiones de 16 bits** que arrastran `div.o`/`udiv.o`/`umod.o` de
> `none.lib`.

---

## 1. Rutinas C que se reemplazan

| C actual | Bytes C | ¿Qué hace? |
|---|---:|---|
| `put_str_at(col,row,s)` | 100 | escribe cadena (tile ASCII + attr paleta 2) |
| `put_str_center(row,s)` | 60 | centra y llama a `put_str_at` |
| `str_len(s)` | 40 | longitud de cadena |
| `clear_str_at(col,row,len)` | 76 | escribe `len` espacios |
| `fmt_score(buf,v)` | 167 | número de 4 dígitos a un buffer |
| `put_score(col,row)` | 141 | HUD score (usa `fmt_score`+`put_str_at`) |
| `put_lives(col,row)` | 55 | HUD vidas (1 dígito) |
| `put_level(col,row)` | 55 | HUD nivel (1 dígito) |
| `put_hiscore(col,row)` | 44 | HUD high score |
| **Total** | **~738** | |

---

## 2. API del nuevo módulo `text.s`

Convención **`__fastcall__`** (parámetros en A/X; nada por stack de cc65 → sin
`pusha`/`popa`). El módulo define su **propia Zero Page** (segmento ZEROPAGE).

### Funciones públicas (exportadas)

```c
/* Escribe una cadena (tile ASCII + atributo paleta) en (col,row), con
 * salto automatico a la columna 0 al pasar de 40 columnas.
 *   col  : __fastcall__ -> A
 *   row  :                -> X
 *   s    :                -> ptr1 (ZP del modulo, se fija con txt_str())  */
void txt_put_at(void);                 /* usa txt_col/txt_row/txt_ptr */

/* Escribe UN digito ASCII '0'..'9' en (col,row). Uso interno/HUD. */
void txt_put_digit(uint8_t col, uint8_t row, uint8_t digito);

/* Escribe un numero de 4 digitos (0000-9999) desde (col,row). */
void txt_put_u16_4(uint8_t col, uint8_t row, uint16_t valor);

/* Escribe un numero de 1 digito (0-9) desde (col,row). */
void txt_put_u8_1(uint8_t col, uint8_t row, uint8_t valor);

/* Cadena CENTRADA en la fila 'row' (40 columnas). */
void txt_put_center(uint8_t row, const char *s);

/* Borra 'len' celdas con espacios desde (col,row). */
void txt_clear_at(uint8_t col, uint8_t row, uint8_t len);
```

> Nota: cc65 pasa argumentos por stack si la firma no es `__fastcall__`. Para
> que el ASM lea argumentos con `popa`/`popax` al estilo de `video.s`, se puede
> también. Se elige lo más corto en bytes por función (ver §4).

---

## 3. Detalles de implementación

### 3.1 Color / paleta
La fuente pinta **siempre color 3**. El color = **paleta de la celda**. Hoy es
`VC_BGPAL_2` (=2). El atributo de celda = `paleta & 0x0F` = `0x02` (sin flags).

### 3.2 Escritura a VRAM (sin `vc_put_cell`/`vc_set_cell_attr`)
Se hace **directo**, como la macro `VC_WRITE`:

```asm
; escribir tile T en celda (col,row):  addr = row*64 + col
;   $D800 = addr & $FF
;   $D801 = AREA_TILEMAP($00) | ((addr>>8) & $07)
;   $D802 = T
; para el ATRIBUTO, AREA_ATTR = $40.
```

El cálculo `row*64 + col` es barato en ASM (`row` se desplaza 6 bits; `col` se
suma). **Ventaja**: evita las dos llamadas `jsr` por celda y el `shlax*` de cc65.

> **Integración con `video.o`:** el núcleo `video.s` **exporta** `ptr` (ZP) y las
> constantes de área, pero **`calc_cell` es interno** (no exportado). Por eso
> `text.s` implementa **su propio cálculo de celda** (duplicar las ~12 lineas de
> `calc_cell` es mas barato que llamar a `vc_put_cell`, que ademas escribe por
> `jsr`). Se importa `ptr` para no gastar ZP nueva.
>
> Registros usados (absolutos, definidos en `video.s`):
>   `VID_ADDR_LO=$D800`, `VID_ADDR_HI=$D801`, `VID_DATA=$D802`,
>   `AREA_TILEMAP=$00`, `AREA_ATTR=$40`.

### 3.3 Número de 4 dígitos SIN división
En vez de `/1000, /100, /10`, se usan **restas sucesivas** (o un bucle BCD). Para
valores 0..9999 cabe un método simple por restas; alternativamente desplazar por
tablas de potencias. Esto **elimina `div.o`/`udiv.o`/`umod.o`**.

### 3.4 Estado interno (ZP del módulo)
```
txt_col   : .res 1     columna actual
txt_row   : .res 1     fila actual
txt_ptr   : .res 2     puntero a la cadena
```
(`txt_ptr` puede reutilizar `ptr1` de la libreria, ya en ZP.)

---

## 4. Estimación de tamano

| Rutina ASM | Estimado |
|---|---:|
| `txt_put_at` (bucle + escritura directa) | ~50 |
| `txt_put_digit` | ~25 |
| `txt_put_u16_4` (restas) | ~45 |
| `txt_put_u8_1` | ~20 |
| `txt_put_center` | ~30 |
| `txt_clear_at` | ~35 |
| **Total ASM** | **~205** |

**Ahorro en `game.o`:** 738 − 205 ≈ **530 B**.
**Bonus `none.lib`:** −(`div.o`+`udiv.o`+`umod.o`) ≈ **−100 B** (si no quedan
otras divisiones; hay usos en `fleet_step_delay` y `enemy_fire` → hay que
convertir esos también para que se caigan del todo).

---

## 5. Plan de pasos (incremental, verificable)

1. Crear `text.s` con `txt_put_at` + `txt_put_digit` + `txt_put_u8_1`.
2. Cambiar `game.c` para usarlas; **compilar y probar** en hardware.
3. Añadir `txt_put_u16_4` (restas) y migrar `put_score`/`fmt_score`/`put_hiscore`.
4. Añadir `txt_put_center` y `txt_clear_at`; migrar los banners.
5. **Verificar** que `div.o`/`udiv.o`/`umod.o` ya no se enlazan (si no, convertir
   `fleet_step_delay`/`enemy_fire` como paso siguiente).

> Cada paso deja el juego **jugable** y compilando. Nada se hace de golpe.

---

## 6. Riesgos y mitigaciones

| Riesgo | Mitigación |
|---|---|
| Cálculo de dirección VRAM equivocado | Reusar la misma fórmula que `video.s` (`VC_CELL`), probar el texto primero |
| Convención fastcall mal aplicada | Empezar con funciones de 1-2 args simples |
| Nombres/segmentos de ZP chocan | Segmento `ZEROPAGE` propio, sin solapar la ZP de `video.o` |
| `div.o` no se cae (otra división) | Buscar `tosudiv` tras compilar; convertir los restantes |
