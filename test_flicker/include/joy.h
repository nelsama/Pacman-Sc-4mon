/**
 * ============================================================================
 * joy.h - Joystick Atari (DB9) via Puerto GPIO 1 del FPGA
 * ============================================================================
 * Puerto 1 = 8 bits. Los 3 bits bajos los usa el TM1638, asi que el joystick
 * usa los 5 bits ALTOS (3-7), que en el orden del puerto son los pines:
 *
 *   bit  pin  senal
 *   ---  ---  -----
 *    3    40   right
 *    4    34   left
 *    5    33   down
 *    6    30   up
 *    7    29   fire
 *
 * Registros:
 *   $C000  Puerto 1 datos  (leer: entradas)
 *   $C002  Config Puerto 1 (por bit: 0=salida, 1=entrada)
 *
 * IMPORTANTE (TM1638): el registro de configuracion $C002 es de BYTE completo.
 * Para no romper la config de los bits 0-2 (TM1638) se hace UN READ-MODIFY-WRITE:
 * se leen los bits actuales, se ponen a ENTRADA solo los 3-7, y se reescribe.
 * Nunca se escribe $C002 con un valor fijo, o se pisaria el TM1638.
 * ============================================================================
 */

#ifndef JOY_H
#define JOY_H

#include <stdint.h>

/* Bits del joystick en el Puerto 1 (ver tabla de arriba). */
#define JOY_RIGHT   0x08   /* bit 3, pin 40 */
#define JOY_LEFT    0x10   /* bit 4, pin 34 */
#define JOY_DOWN    0x20   /* bit 5, pin 33 */
#define JOY_UP      0x40   /* bit 6, pin 30 */
#define JOY_FIRE    0x80   /* bit 7, pin 29 */
#define JOY_MASK    0xF8   /* los 5 bits del joystick (ignora el TM1638) */

/* Polaridad: el joystick Atari es ACTIVO POR NIVEL BAJO (0 = pulsado) si el
 * hardware tiene pull-ups. Si tu placa lee lo contrario (1 = pulsado), pon
 * JOY_ACTIVE_LOW a 0. */
#define JOY_ACTIVE_LOW  1

/* Acciones devueltas por joy_read() (bitmask). */
#define JOY_A_LEFT   0x01
#define JOY_A_RIGHT  0x02
#define JOY_A_UP     0x04
#define JOY_A_DOWN   0x08
#define JOY_A_FIRE   0x10

/* Configura los bits 3-7 del Puerto 1 como ENTRADA (sin tocar los 0-2). */
void joy_init(void);

/* Lee el joystick y devuelve un bitmask con las acciones (JOY_A_*).
 * Las direcciones son de movimiento (izq/der); arriba/abajo se incluyen por
 * completitud aunque el juego solo use izq/der/fuego. */
uint8_t joy_read(void);

#endif /* JOY_H */
