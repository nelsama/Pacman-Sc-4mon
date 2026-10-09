/**
 * ============================================================================
 * joy.c - Joystick Atari (DB9) via Puerto GPIO 1 del FPGA
 * ============================================================================
 * Ver include/joy.h para el mapeo de bits y el aviso sobre el TM1638.
 * ============================================================================
 */

#include <stdint.h>
#include "joy.h"

/* Registros del Puerto 1 (GPIO). */
#define JOY_PORT  (*(volatile uint8_t *)0xC000)   /* datos del puerto 1   */
#define JOY_CFG   (*(volatile uint8_t *)0xC002)   /* config puerto 1      */

void joy_init(void) {
    /* Poner los bits 3-7 como ENTRADA preservando los 0-2 (TM1638).
     * READ-MODIFY-WRITE: leer la config, dejar los 3 bits bajos como estaban y
     * forzar los 5 altos a 1 (entrada). Nunca se escribe un valor fijo, para no
     * pisar la direccion de los pines del TM1638. */
    uint8_t cfg = (uint8_t)((JOY_CFG & 0x07) | JOY_MASK);
    JOY_CFG = cfg;
}

uint8_t joy_read(void) {
    uint8_t raw = (uint8_t)(JOY_PORT & JOY_MASK);   /* ignora bits del TM1638 */
    uint8_t act = 0;

#if JOY_ACTIVE_LOW
    /* Pulsado = 0 -> invertimos para que 1 = pulsado. */
    raw = (uint8_t)(~raw);
#endif

    if (raw & JOY_LEFT)  act |= JOY_A_LEFT;
    if (raw & JOY_RIGHT) act |= JOY_A_RIGHT;
    if (raw & JOY_UP)    act |= JOY_A_UP;
    if (raw & JOY_DOWN)  act |= JOY_A_DOWN;
    if (raw & JOY_FIRE)  act |= JOY_A_FIRE;

    return act;
}
