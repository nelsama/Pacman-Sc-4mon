/**
 * ============================================================================
 * test_maze.c - PROTOTIPO: laberinto arcade con PAREDES FINAS
 * ============================================================================
 * Los tiles de 8x8 son las celdas del laberinto. En el arcade las PAREDES son
 * lineas finas (~4 px) dibujadas dentro de las celdas de muro, NO bloques de
 * 8x8. Este prototipo prueba paredes finas con GROSOR CONFIGURABLE.
 *
 * COMO FUNCIONA (auto-tiling):
 *   Para cada celda de MURO se miran sus 4 vecinos. Los lados que dan a un
 *   PASILLO se dibujan como una LINEA FINA (grosor GROSOR_PX) pegada a ese lado.
 *   Asi el muro "rodea" el pasillo con una linea fina en vez de un bloque.
 *
 *   Ejemplo (celda de muro con pasillo a la izquierda y abajo):
 *       . . . .        (arriba: vecino muro -> sin linea)
 *       | . . .        (linea vertical fina pegada al borde izquierdo)
 *       | . . .        (el resto del tile queda NEGRO/transparente)
 *       |_____         (linea horizontal fina pegada al borde inferior)
 *
 * GROSOR_PX controla el grosor de la linea (1, 2, 3 o 4). Probar cual se ve
 * mas parecido al arcade.
 *
 * CODIFICACION DEL MAPA (28x28):
 *   '#' pared   '.' punto   'o' power pellet   ' ' vacio
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"

/* ---------------------------------------------------------------------------
 * CONFIGURACION
 * --------------------------------------------------------------------------- */

/* Grosor de la pared en pixeles (1..4). El arcade usa ~4 px en celdas de 8x8.
 * Prueba 4 (grueso fino), 3, 2 (fino) o 1 (muy fino). */
#define GROSOR_PX   2

/* ---------------------------------------------------------------------------
 * GEOMETRIA
 * --------------------------------------------------------------------------- */
#define HUD_ROWS   2
#define MAZE_W     28
#define MAZE_H     28
#define MAZE_X0    6
#define MAZE_Y0    HUD_ROWS

/* ---------------------------------------------------------------------------
 * TILES
 * ---------------------------------------------------------------------------
 * Tiles usados (fuera del rango de la fuente $20-$7F):
 *   0x00 vacio
 *   0x02 punto
 *   0x03 power pellet
 *   0x10.. lineas finas de pared: se generan por codigo segun el grosor.
 *
 * Codigos de borde (bitmask): 1=arriba, 2=abajo, 4=izq, 8=der.
 *   tile = TILE_WALL_BASE + mascara (1..15)
 * --------------------------------------------------------------------------- */
#define TILE_BLANK      0x00
#define TILE_DOT        0x02
#define TILE_POWER      0x03
#define TILE_WALL_BASE  0x10   /* 0x11..0x1F = 15 combinaciones de borde */

/* ---------------------------------------------------------------------------
 * MAPA DEL LABERINTO (28 x 28)
 * --------------------------------------------------------------------------- */
static const char *const maze[MAZE_H] = {
/* 0  */ "############################",
/* 1  */ "#............##............#",
/* 2  */ "#.####.#####.##.#####.####.#",
/* 3  */ "#o####.#####.##.#####.####.#",   /* ojo: corregido abajo en 'o' */
/* 4  */ "#.####.#####.##.#####.####.#",
/* 5  */ "#..........................#",
/* 6  */ "#.####.##.########.##.####.#",
/* 7  */ "#.####.##.########.##.####.#",
/* 8  */ "#......##....##....##......#",
/* 9  */ "######.##### ## #####.######",
/* 10 */ "     #.##### ## #####.#     ",
/* 11 */ "     #.##          ##.#     ",
/* 12 */ "     #.## ###--### ##.#     ",
/* 13 */ "######.## #      # ##.######",
/* 14 */ "      .   #      #   .      ",
/* 15 */ "######.## #      # ##.######",
/* 16 */ "     #.## ######## ##.#     ",
/* 17 */ "     #.##          ##.#     ",
/* 18 */ "     #.## ######## ##.#     ",
/* 19 */ "######.## ######## ##.######",
/* 20 */ "#............##............#",
/* 21 */ "#.####.#####.##.#####.####.#",
/* 22 */ "#.####.#####.##.#####.####.#",
/* 23 */ "#o..##.......  .......##..o#",
/* 24 */ "###.##.##.########.##.##.###",
/* 25 */ "###.##.##.########.##.##.###",
/* 26 */ "#......##....##....##......#",
/* 27 */ "############################"
};

/* Correccion de power pellets en las esquinas superiores (fila 3). */
static uint8_t cell_is_wall(int r, int c) {
    if (r < 0 || r >= MAZE_H || c < 0 || c >= MAZE_W) return 1;  /* fuera = muro */
    return maze[r][c] == '#';
}

/* ---------------------------------------------------------------------------
 * GENERACION DE TILES DE BORDE FINO (auto-tiling)
 * ---------------------------------------------------------------------------
 * mascara: bit0=linea ARRIBA, bit1=ABAJO, bit2=IZQ, bit3=DER.
 * Devuelve los 2 planos del tile correspondiente.
 * --------------------------------------------------------------------------- */
static void build_wall_tile(uint8_t mascara, uint8_t *p0, uint8_t *p1) {
    uint8_t i;
    /* Empezamos con todo negro (color 0 = transparente; sobre BG negro). */
    for (i = 0; i < 8; i++) { p0[i] = 0; p1[i] = 0; }

    /* Color 3 (plano0=plano1=1) -> usa el color de la paleta del muro. */
    if (mascara & 1) {   /* linea ARRIBA: filas 0..GROSOR-1 */
        for (i = 0; i < GROSOR_PX; i++) { p0[i] = 0xFF; p1[i] = 0xFF; }
    }
    if (mascara & 2) {   /* linea ABAJO: filas 8-GROSOR..7 */
        for (i = (uint8_t)(8 - GROSOR_PX); i < 8; i++) { p0[i] = 0xFF; p1[i] = 0xFF; }
    }
    {
        /* Columna de bits para IZQ (bit 7 = izquierda) y DER (bit 0 = derecha). */
        static const uint8_t col_izq[4] = { 0xF0, 0xC0, 0xE0, 0x80 }; /* izquierda */
        uint8_t cizq, cder;
        if (GROSOR_PX == 4) { cizq = 0xF0; cder = 0x0F; }
        else if (GROSOR_PX == 3) { cizq = 0xE0; cder = 0x07; }
        else if (GROSOR_PX == 2) { cizq = 0xC0; cder = 0x03; }
        else { cizq = 0x80; cder = 0x01; }
        (void)col_izq;

        if (mascara & 4) {   /* linea IZQUIERDA */
            for (i = 0; i < 8; i++) { p0[i] |= cizq; p1[i] |= cizq; }
        }
        if (mascara & 8) {   /* linea DERECHA */
            for (i = 0; i < 8; i++) { p0[i] |= cder; p1[i] |= cder; }
        }
    }
}

static void load_wall_tiles(void) {
    uint8_t m;
    uint8_t p0[8], p1[8];
    for (m = 1; m <= 15; m++) {
        build_wall_tile(m, p0, p1);
        vc_load_bg_pattern((uint8_t)(TILE_WALL_BASE + m), p0, p1);
    }
}

/* ---------------------------------------------------------------------------
 * TILES DE PUNTO Y POWER
 * --------------------------------------------------------------------------- */
static const uint8_t dot_p0[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
static const uint8_t dot_p1[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
static const uint8_t pow_p0[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };
static const uint8_t pow_p1[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };

/* ---------------------------------------------------------------------------
 * PERSONAJES
 * --------------------------------------------------------------------------- */
static const uint8_t pac_tl_p0[8] = { 0x0F,0x3F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_tr_p0[8] = { 0xF0,0xFC,0xFE,0xFE,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x3F,0x0F };
static const uint8_t pac_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFE,0xFE,0xFC,0xF0 };

static const uint8_t gho_tl_p0[8] = { 0x3F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_tr_p0[8] = { 0xFC,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF3,0xF7 };
static const uint8_t gho_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xEF,0xDF };

static const uint8_t *const pac_pats[4] = { pac_tl_p0, pac_tr_p0, pac_bl_p0, pac_br_p0 };
static const uint8_t *const gho_pats[4] = { gho_tl_p0, gho_tr_p0, gho_bl_p0, gho_br_p0 };
static const uint8_t gho_p1_cero[8] = { 0,0,0,0,0,0,0,0 };

#define PAT_PAC   0
#define PAT_F1    4
#define PAT_F2    8
#define PAT_F3    12
#define PAT_F4    16

#define SLOT_PAC   0
#define SLOT_F1    4
#define SLOT_F2    8
#define SLOT_F3    12
#define SLOT_F4    16

/* ---------------------------------------------------------------------------
 * CARGA DE PATRONES
 * --------------------------------------------------------------------------- */
static void load_personajes(void) {
    uint8_t i;
    for (i = 0; i < 4; i++) {
        vc_load_spr_pattern((uint8_t)(PAT_PAC + i), pac_pats[i], pac_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F1 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F2 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F3 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F4 + i), gho_pats[i], gho_p1_cero);
    }
}

/* ---------------------------------------------------------------------------
 * MASCARA DE BORDE de una celda de MURO: que lados dan a pasillo.
 * --------------------------------------------------------------------------- */
static uint8_t wall_mask(int r, int c) {
    uint8_t m = 0;
    if (!cell_is_wall(r - 1, c)) m |= 1;   /* arriba da a pasillo */
    if (!cell_is_wall(r + 1, c)) m |= 2;   /* abajo  da a pasillo */
    if (!cell_is_wall(r, c - 1)) m |= 4;   /* izq    da a pasillo */
    if (!cell_is_wall(r, c + 1)) m |= 8;   /* der    da a pasillo */
    return m;
}

/* ---------------------------------------------------------------------------
 * DIBUJADO DEL LABERINTO
 * --------------------------------------------------------------------------- */
static void draw_maze(void) {
    uint8_t r, c;

    for (r = 0; r < MAZE_H; r++) {
        for (c = 0; c < MAZE_W; c++) {
            uint8_t col = (uint8_t)(MAZE_X0 + c);
            uint8_t rr  = (uint8_t)(MAZE_Y0 + r);
            char ch = maze[r][c];

            if (ch == '#') {
                uint8_t m = wall_mask(r, c);
                if (m == 0) {
                    /* Muro interior rodeado de muros: nada que dibujar. */
                    vc_put_cell(col, rr, TILE_BLANK);
                    vc_set_cell_attr(col, rr, VC_BGPAL_2, 0);
                } else {
                    vc_put_cell(col, rr, (uint8_t)(TILE_WALL_BASE + m));
                    vc_set_cell_attr(col, rr, VC_BGPAL_2, VC_ATTR_SOLID);
                }
            } else if (ch == 'o') {
                vc_put_cell(col, rr, TILE_POWER);
                vc_set_cell_attr(col, rr, VC_BGPAL_1, 0);
            } else if (ch == '.') {
                vc_put_cell(col, rr, TILE_DOT);
                vc_set_cell_attr(col, rr, VC_BGPAL_1, 0);
            } else {
                vc_put_cell(col, rr, TILE_BLANK);
                vc_set_cell_attr(col, rr, VC_BGPAL_2, 0);
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * PERSONAJE (16x16 = 2x2 sprites)
 * --------------------------------------------------------------------------- */
static void draw_char(uint8_t base_slot, uint8_t base_pat,
                      uint16_t x, uint8_t y, uint8_t pal) {
    vc_sprite_t s;
    s.flags = pal;
    s.coll  = VC_COLL_CENTER;
    s.y = y;
    s.tile = (uint8_t)(base_pat + 0); s.x_lo = (uint8_t)x;       vc_sprite_set((uint8_t)(base_slot + 0), &s);
    s.tile = (uint8_t)(base_pat + 1); s.x_lo = (uint8_t)(x + 8); vc_sprite_set((uint8_t)(base_slot + 1), &s);
    s.tile = (uint8_t)(base_pat + 2); s.x_lo = (uint8_t)x;       s.y = (uint8_t)(y + 8); vc_sprite_set((uint8_t)(base_slot + 2), &s);
    s.tile = (uint8_t)(base_pat + 3); s.x_lo = (uint8_t)(x + 8); vc_sprite_set((uint8_t)(base_slot + 3), &s);
}

static void hide_char(uint8_t base_slot) {
    vc_sprite_disable((uint8_t)(base_slot + 0));
    vc_sprite_disable((uint8_t)(base_slot + 1));
    vc_sprite_disable((uint8_t)(base_slot + 2));
    vc_sprite_disable((uint8_t)(base_slot + 3));
}

/* ---------------------------------------------------------------------------
 * SETUP
 * --------------------------------------------------------------------------- */
static void setup_video(void) {
    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));

    /* Fondo: 0 = no usado, 1 = puntos (beige), 2 = pared (azul). */
    vc_pal_set_bg(1, 3, VC_RGB444(0xF, 0xC, 0x8));   /* puntos */
    vc_pal_set_bg(2, 3, VC_RGB444(0x0, 0x4, 0xF));   /* pared azul */

    /* Sprites. */
    vc_pal_set_spr(0, 3, VC_RGB444(0xF, 0xF, 0x0));
    vc_pal_set_spr(1, 3, VC_RGB444(0xF, 0x0, 0x0));
    vc_pal_set_spr(2, 3, VC_RGB444(0xF, 0x8, 0xC));
    vc_pal_set_spr(3, 3, VC_RGB444(0x0, 0xF, 0xF));
    vc_pal_set_spr(3, 1, VC_RGB444(0xF, 0x8, 0x0));

    vc_load_bg_pattern(TILE_DOT,   dot_p0, dot_p1);
    vc_load_bg_pattern(TILE_POWER, pow_p0, pow_p1);
    load_wall_tiles();
    load_personajes();

    /* HUD. */
    vc_put_str(1, 0, "PAREDES FINAS - test de grosor");
#if GROSOR_PX == 1
    vc_put_str(1, 1, "GROSOR_PX=1 (muy fino)");
#elif GROSOR_PX == 2
    vc_put_str(1, 1, "GROSOR_PX=2 (fino)");
#elif GROSOR_PX == 3
    vc_put_str(1, 1, "GROSOR_PX=3");
#else
    vc_put_str(1, 1, "GROSOR_PX=4 (grueso, como arcade)");
#endif
}

/* ---------------------------------------------------------------------------
 * MAIN
 * --------------------------------------------------------------------------- */
int main(void) {
    uint8_t pac_y, frame;

    rom_uart_puts("\r\nTEST MAZE - paredes finas\r\n");

    setup_video();
    draw_maze();

    /* Personajes en un pasillo horizontal: fila 5 (pasillo abierto). */
    pac_y = (uint8_t)((MAZE_Y0 + 5) * 8);

    frame = 0;
    for (;;) {
        vc_wait_vblank();
        {
            uint16_t px = (uint16_t)((MAZE_X0 + 9) * 8);
            draw_char(SLOT_PAC, PAT_PAC, px, pac_y, VC_SPPAL_0);
            draw_char(SLOT_F1, PAT_F1, (uint16_t)(px + 32), pac_y, VC_SPPAL_1);
            draw_char(SLOT_F2, PAT_F2, (uint16_t)(px + 64), pac_y, VC_SPPAL_2);
            if ((frame & 1) == 0) {
                draw_char(SLOT_F3, PAT_F3, (uint16_t)(px + 96), pac_y, VC_SPPAL_3);
                hide_char(SLOT_F4);
            } else {
                hide_char(SLOT_F3);
                draw_char(SLOT_F4, PAT_F4, (uint16_t)(px + 96), pac_y, VC_SPPAL_3);
            }
        }
        frame++;
        vc_wait_vblank_end();
    }

    return 0;
}
