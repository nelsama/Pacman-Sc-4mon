/* test_gho.c - Prueba la IA en ASM (gho_ai.s) en sim65.
 *
 * Define los datos que gho_ai.s importa, fija estados, llama a gho_target() y
 * gho_choose_dir_i(), e imprime los resultados. Se compara con el C validado.
 */
#include <stdio.h>
#include <stdint.h>

/* Datos importados por gho_ai.s (definidos aqui para el test). */
uint16_t pac_x;
uint8_t  pac_y;
int8_t   pac_dx, pac_dy;
uint16_t gho_x[4];
uint8_t  gho_y[4];
int8_t   gho_dx[4], gho_dy[4];
uint8_t  gho_state[4];

/* Mapa comprimido (mismo formato: 4 celdas/byte, 0=vacio 1=punto 2=power
 * 3=pared). Uso un mapa de prueba: borde de paredes + interior abierto. */
#define MW 28
#define MH 28
uint8_t maze_bits[MH * (MW/4)];

/* Prototipos de las funciones ASM. */
extern void gho_target(uint8_t i, int16_t *tx, int16_t *ty);
extern uint8_t gho_choose_dir_i(uint8_t i, uint8_t col, uint8_t row,
                                uint8_t use_target, int16_t tx, int16_t ty);

static void fill_maze(void) {
    int r, c;
    for (r = 0; r < MH; r++) {
        for (c = 0; c < MW; c++) {
            uint8_t v;
            if (r == 0 || r == MH-1) v = 3;          /* bordes: pared */
            else v = 0;                               /* interior: libre */
            {
                int idx = r * (MW/4) + (c >> 2);
                int sh = (3 - (c & 3)) * 2;
                maze_bits[idx] = (uint8_t)((maze_bits[idx] & ~(3 << sh)) | (v << sh));
            }
        }
    }
}

int main(void) {
    int16_t tx, ty;
    int i;
    fill_maze();

    /* Estado de prueba. */
    pac_x = (6 + 13) * 8;  /* celda 13 */
    pac_y = (2 + 14) * 8;  /* celda 14 */
    pac_dx = 1; pac_dy = 0;
    for (i = 0; i < 4; i++) {
        gho_x[i] = (6 + 10 + i) * 8;
        gho_y[i] = (uint8_t)((2 + 20) * 8);
        gho_dx[i] = 0; gho_dy[i] = 0;
        gho_state[i] = 2;   /* FREE */
    }

    for (i = 0; i < 4; i++) {
        tx = 999; ty = 999;
        gho_target((uint8_t)i, &tx, &ty);
        printf("T %d -> %d %d\n", i, tx, ty);
    }

    /* choose_dir: prueba con objetivo (13,14) sin obstaculos en fila 20. */
    for (i = 0; i < 4; i++) {
        uint8_t r;
        gho_dx[i] = 0; gho_dy[i] = 0;
        r = gho_choose_dir_i((uint8_t)i, (uint8_t)(10+i), 20, 1, 13, 14);
        printf("C %d -> r=%d dx=%d dy=%d\n", i, r, gho_dx[i], gho_dy[i]);
    }
    return 0;
}
