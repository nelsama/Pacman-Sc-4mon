# Mini-manual: integración del joystick Atari (DB9) en un juego 6502

> **Destinatario:** otra IA (o desarrollador) que deba añadir soporte de joystick
> Atari a un juego sobre el **computador 6502** con el **Core de Vídeo (`vc`)** y
> los **puertos GPIO** del FPGA. Basado en una integración real y funcionando.

---

## 1. Resumen (qué se va a hacer)

Añadir entrada de **joystick Atari (DB9)** leyendo un **puerto GPIO** del FPGA, en
**paralelo** al teclado UART. El juego podrá usar o el joystick o el teclado.

Se entrega como **dos archivos nuevos** (`joy.c` + `include/joy.h`) que se enganchan
al bucle de entrada del juego. **No hay que tocar la lógica del juego.**

---

## 2. Hardware asumido (AJUSTAR si tu placa difiere)

| Recurso | Valor |
|---|---|
| Puerto GPIO usado | **Puerto 1** (8 bits) |
| Registro de datos | `$C000` (leer = entradas) |
| Registro de configuración | `$C002` (por bit: **0 = salida, 1 = entrada**) |
| Bits libres para el joystick | **3-7** (los bits 0-2 los ocupa el TM1638) |

**Mapeo de señal → bit** (pines del Puerto 1 en orden bit 0..7: `42,41,35,40,34,33,30,29`):

| Señal | Pin | Bit | Máscara |
|-------|-----|-----|---------|
| right | 40 | 3 | `0x08` |
| left | 34 | 4 | `0x10` |
| down | 33 | 5 | `0x20` |
| up | 30 | 6 | `0x40` |
| fire | 29 | 7 | `0x80` |

> ⚠️ **Lo único que hay que personalizar** si tu placa cambia es: la dirección de
> los registros y las máscaras de bits. El resto del código no cambia.

### El joystick Atari estándar (DB9)

5 entradas activas por **nivel bajo** (0 = pulsado), con pull-ups:
`UP, DOWN, LEFT, RIGHT, FIRE`. Encajan perfecto en los 5 bits libres.

---

## 3. El punto CRÍTICO: no romper el TM1638

El **registro de configuración es de byte completo** (`$C002`). Si escribes un valor
fijo, **pisas la dirección de los 3 pines del TM1638** y deja de funcionar.

**Regla de oro: READ-MODIFY-WRITE.**

```c
/* MAL: pisa los bits del TM1638 */
JOY_CFG = 0xF8;

/* BIEN: preserva los bits 0-2 y solo cambia los 3-7 */
JOY_CFG = (JOY_CFG & 0x07) | 0xF8;
```

Lo mismo al leer: **enmascarar con `0xF8`** para ignorar los bits del TM1638.

---

## 4. Código a crear

### `include/joy.h`

```c
#ifndef JOY_H
#define JOY_H
#include <stdint.h>

/* Bits del joystick en el Puerto 1 (Ajustar si tu mapeo cambia). */
#define JOY_RIGHT   0x08   /* bit 3 */
#define JOY_LEFT    0x10   /* bit 4 */
#define JOY_DOWN    0x20   /* bit 5 */
#define JOY_UP      0x40   /* bit 6 */
#define JOY_FIRE    0x80   /* bit 7 */
#define JOY_MASK    0xF8   /* los 5 bits del joystick (ignora el TM1638) */

/* El joystick Atari es activo por nivel bajo. Pon 0 si tu placa lee al reves. */
#define JOY_ACTIVE_LOW  1

/* Acciones devueltas por joy_read() (bitmask). */
#define JOY_A_LEFT   0x01
#define JOY_A_RIGHT  0x02
#define JOY_A_UP     0x04
#define JOY_A_DOWN   0x08
#define JOY_A_FIRE   0x10

void    joy_init(void);
uint8_t joy_read(void);
#endif
```

### `joy.c`

```c
#include <stdint.h>
#include "joy.h"

#define JOY_PORT  (*(volatile uint8_t *)0xC000)   /* datos puerto 1  */
#define JOY_CFG   (*(volatile uint8_t *)0xC002)   /* config puerto 1 */

void joy_init(void) {
    /* Poner los bits 3-7 como ENTRADA preservando los 0-2 (TM1638).
     * READ-MODIFY-WRITE: nunca escribir un valor fijo. */
    JOY_CFG = (uint8_t)((JOY_CFG & 0x07) | JOY_MASK);
}

uint8_t joy_read(void) {
    uint8_t raw = (uint8_t)(JOY_PORT & JOY_MASK);  /* ignora bits TM1638 */
    uint8_t act = 0;

#if JOY_ACTIVE_LOW
    raw = (uint8_t)(~raw);      /* pulsado = 0 -> invertir a 1 = pulsado */
#endif

    if (raw & JOY_LEFT)  act |= JOY_A_LEFT;
    if (raw & JOY_RIGHT) act |= JOY_A_RIGHT;
    if (raw & JOY_UP)    act |= JOY_A_UP;
    if (raw & JOY_DOWN)  act |= JOY_A_DOWN;
    if (raw & JOY_FIRE)  act |= JOY_A_FIRE;
    return act;
}
```

---

## 5. Integración en el juego

### 5.1 Inicializar una vez

En el arranque, junto a la inicialización de vídeo/sonido:

```c
#include "joy.h"
...
joy_init();     /* bits 3-7 del Puerto 1 como entrada */
```

### 5.2 Leer en el bucle de entrada

Junto a la lectura del teclado UART (ambos funcionan a la vez):

```c
static void read_input(void) {
    int8_t move = 0;
    uint8_t j = joy_read();

    if (j & JOY_A_LEFT)  move = -SPEED;
    if (j & JOY_A_RIGHT) move =  SPEED;
    if (j & JOY_A_FIRE)  fire();       /* o la accion de disparo */

    /* Teclado UART (en paralelo) */
    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        /* ... mapear teclas ... */
    }

    /* aplicar 'move' a la posicion del jugador */
}
```

### 5.3 Makefile

Añadir `joy.c` a las fuentes y su `.o` al enlazado:

```make
APP_C   = game.c sound.c joy.c
APP_OBJECTS = $(BUILD_DIR)/game.o $(BUILD_DIR)/sound.o $(BUILD_DIR)/joy.o $(BUILD_DIR)/startup.o

$(BUILD_DIR)/joy.o: joy.c include/joy.h
	$(CC) -c $(CFLAGS) -o $@ joy.c
```

---

## 6. Patrón: disparo "auto" vs "un tiro por pulsación"

Según el juego, hay dos comportamientos. Ambos se construyen sobre `joy_read()`:

**Auto-disparo** (mientras se mantiene el boton, dispara en cuanto puede):

```c
if (j & JOY_A_FIRE) fire();      /* el juego decide si hay bala libre */
```

**Un tiro por pulsacion** (hay que soltar y volver a pulsar): detectar el flanco:

```c
static uint8_t fire_prev;
uint8_t fire_now = (j & JOY_A_FIRE) ? 1 : 0;
if (fire_now && !fire_prev) fire();   /* solo en el flanco de subida */
fire_prev = fire_now;
```

---

## 7. Checklist de verificación (IMPRESCINDIBLE en hardware)

1. **`joy_init()` se llama ANTES de leer** el joystick.
2. **`$C002` se escribe con read-modify-write** (bits 0-2 intactos).
3. **Polaridad correcta:** probar `JOY_ACTIVE_LOW` 1 y 0. Si el joystick responde
   invertido (se mueve solo con el joystick quieto), cambiar esta constante.
4. **Pull-ups:** si los pines flotan (lecturas erráticas con el joystick quieto),
   el FPGA debe tener **pull-ups** en esos pines. Sin ellos, los bits no son
   fiables.
5. **El TM1638 sigue funcionando** tras el `joy_init()` (verificar su display).
6. **Direcciones de registros** coinciden con tu mapa de memoria del FPGA.

---

## 8. Errores típicos y su causa

| Sintoma | Causa probable |
|---|---|
| El TM1638 deja de funcionar | Se escribio `$C002` con un valor fijo (se pisó la config de los bits 0-2) |
| El joystick responde invertido | Polaridad: ajustar `JOY_ACTIVE_LOW` |
| Lecturas erráticas con el joystick quieto | Faltan pull-ups en el FPGA, o los bits se leen sin enmascarar (`& JOY_MASK`) |
| Una dirección se mueve sola | Bit mal mapeado (revisar la tabla señal→bit) |
| No responde nada | `joy_init()` no se llamó, o las direcciones `$C000`/`$C002` no son las correctas |

---

## 9. Nota de diseño

El driver está **aislado** del juego: `joy_read()` devuelve un **bitmask de acciones**
(`JOY_A_*`) y el juego decide qué hacer. Para cambiar de fuente de entrada
(joystick, teclado, otro mando) **solo se reescribe `read_input()`**, sin tocar la
lógica del juego. Mantén esta separación si adaptas el código a otra IA o proyecto.
