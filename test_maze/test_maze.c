/**
 * ============================================================================
 * test_compare.c - PROTOTIPO: personajes 8x8 vs 16x16 en el mismo laberinto
 * ============================================================================
 * Compara EN PANTALLA dos tamanos de personaje dentro del laberinto del arcade:
 *
 *   - 8x8 nativo  : 1 slot de OAM, cabe 1 celda. Sin parpadeo.
 *   - 16x16 nativo: 2x2 slots por linea. 5 juntos -> parpadeo.
 *
 * Se dibujan AMBOS grupos a la vez:
 *   - Grupo A (arriba):  personajes 8x8
 *   - Grupo B (abajo) :  personajes 16x16 (F4 alterna)
 *
 * Asi se juzga: tamano, detalle y coherencia con el laberinto.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"

/* ---------------------------------------------------------------------------
 * GEOMETRIA
 * --------------------------------------------------------------------------- */
#define HUD_ROWS   2
#define MAZE_W     28
#define MAZE_H     28
#define MAZE_X0    6
#define MAZE_Y0    HUD_ROWS

#define GROSOR_PX  2   /* grosor de las paredes finas */

/* Frames entre cambios de animacion. A 60 Hz: 8 -> ~7.5 Hz (ritmo arcade).
 * Sube el numero para animacion mas lenta. */
#define ANIM_FRAMES  8

/* Tiles */
#define TILE_BLANK      0x00
#define TILE_DOT        0x02
#define TILE_POWER      0x03
#define TILE_WALL_BASE  0x10

/* ---------------------------------------------------------------------------
 * MAPA (28x28) - laberinto del arcade
 * --------------------------------------------------------------------------- */
static const char *const maze[MAZE_H] = {
/* 0  */ "############################",
/* 1  */ "#............##............#",
/* 2  */ "#.####.#####.##.#####.####.#",
/* 3  */ "#o####.#####.##.#####.####.#",
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

static uint8_t cell_is_wall(int r, int c) {
    if (r < 0 || r >= MAZE_H || c < 0 || c >= MAZE_W) return 1;
    return maze[r][c] == '#';
}

static void build_wall_tile(uint8_t mascara, uint8_t *p0, uint8_t *p1) {
    uint8_t i;
    for (i = 0; i < 8; i++) { p0[i] = 0; p1[i] = 0; }
    if (mascara & 1) { for (i = 0; i < GROSOR_PX; i++) { p0[i] = 0xFF; p1[i] = 0xFF; } }
    if (mascara & 2) { for (i = (uint8_t)(8 - GROSOR_PX); i < 8; i++) { p0[i] = 0xFF; p1[i] = 0xFF; } }
    {
        uint8_t cizq, cder;
        if (GROSOR_PX == 4) { cizq = 0xF0; cder = 0x0F; }
        else if (GROSOR_PX == 3) { cizq = 0xE0; cder = 0x07; }
        else if (GROSOR_PX == 2) { cizq = 0xC0; cder = 0x03; }
        else { cizq = 0x80; cder = 0x01; }
        if (mascara & 4) { for (i = 0; i < 8; i++) { p0[i] |= cizq; p1[i] |= cizq; } }
        if (mascara & 8) { for (i = 0; i < 8; i++) { p0[i] |= cder; p1[i] |= cder; } }
    }
}

static void load_wall_tiles(void) {
    uint8_t m, p0[8], p1[8];
    for (m = 1; m <= 15; m++) { build_wall_tile(m, p0, p1); vc_load_bg_pattern((uint8_t)(TILE_WALL_BASE + m), p0, p1); }
}

static uint8_t wall_mask(int r, int c) {
    uint8_t m = 0;
    if (!cell_is_wall(r - 1, c)) m |= 1;
    if (!cell_is_wall(r + 1, c)) m |= 2;
    if (!cell_is_wall(r, c - 1)) m |= 4;
    if (!cell_is_wall(r, c + 1)) m |= 8;
    return m;
}

static const uint8_t dot_p0[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
static const uint8_t dot_p1[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
static const uint8_t pow_p0[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };
static const uint8_t pow_p1[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };

/* ===========================================================================
 * ARTE 8x8 DE PERSONAJES (color 3: plano0 = plano1 = dibujo)
 * ===========================================================================
 * Pac-Man 8x8 mirando a la derecha, boca abierta (2 frames).
 * Fantasma 8x8: cupula + ojos + base ondulada.
 * =========================================================================== */

/* Pac-Man boca cerrada (circulo). */
static const uint8_t pac8_closed[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
/* Pac-Man boca abierta (cuna a la derecha). */
static const uint8_t pac8_open[8]   = { 0x3C,0x7E,0xFE,0xF8,0xF8,0xFE,0x7E,0x3C };

/* Fantasma: cupula, ojos blancos, base ondulada. */
static const uint8_t ghost8_a[8] = { 0x3C,0x7E,0xDB,0xFF,0xFF,0xFF,0xFF,0xA5 };
static const uint8_t ghost8_b[8] = { 0x3C,0x7E,0xDB,0xFF,0xFF,0xFF,0xFF,0x5A };
/* Plano1 a cero para F4: patron en COLOR 1 (naranja) en vez de color 3 (cian). */
static const uint8_t cero8_pat[8] = { 0,0,0,0,0,0,0,0 };

/* ===========================================================================
 * ARTE 16x16 DE PERSONAJES (2x2 de patrones 8x8)
 * =========================================================================== */
static const uint8_t pac_tl_p0[8] = { 0x0F,0x3F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_tr_p0[8] = { 0xF0,0xFC,0xFE,0xFE,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x3F,0x0F };
static const uint8_t pac_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFE,0xFE,0xFC,0xF0 };

static const uint8_t gho_tl_p0[8] = { 0x3F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_tr_p0[8] = { 0xFC,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF3,0xF7 };
static const uint8_t gho_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xEF,0xDF };

static const uint8_t *const pac16[4] = { pac_tl_p0, pac_tr_p0, pac_bl_p0, pac_br_p0 };
static const uint8_t *const gho16[4] = { gho_tl_p0, gho_tr_p0, gho_bl_p0, gho_br_p0 };
static const uint8_t cero8[8] = { 0,0,0,0,0,0,0,0 };

/* ---------------------------------------------------------------------------
 * PATRONES DE SPRITE: reservamos
 *   0,1   : Pac 8x8 (2 frames)
 *   2,3   : fantasma 8x8 (2 frames)
 *   8..11 : Pac 16x16
 *   12..15: fantasma 16x16 (color 3)
 *   16..19: fantasma 16x16 (color 1, para F4)
 * --------------------------------------------------------------------------- */
#define PAT_PAC8_A   0
#define PAT_PAC8_B   1
#define PAT_GHO8_A   2
#define PAT_GHO8_B   3
#define PAT_GHO8_A_C1 4   /* fantasma 8x8 en color 1 (para F4, naranja) */
#define PAT_GHO8_B_C1 5
#define PAT_PAC16    8
#define PAT_GHO16    12
#define PAT_GHO16_C1 16

/* Slots OAM */
#define SLOT_PAC8   0
#define SLOT_F1_8   1
#define SLOT_F2_8   2
#define SLOT_F3_8   3
#define SLOT_F4_8   4
#define SLOT_PAC16  8   /* 8..11 */
#define SLOT_F1_16  12  /* 12..15 */
#define SLOT_F2_16  16  /* 16..19 */
#define SLOT_F3_16  20  /* 20..23 */
#define SLOT_F4_16  24  /* 24..27 */

static void load_personajes(void) {
    uint8_t i;
    /* 8x8 */
    vc_load_spr_pattern(PAT_PAC8_A, pac8_closed, pac8_closed);
    vc_load_spr_pattern(PAT_PAC8_B, pac8_open,   pac8_open);
    vc_load_spr_pattern(PAT_GHO8_A, ghost8_a,   ghost8_a);
    vc_load_spr_pattern(PAT_GHO8_B, ghost8_b,   ghost8_b);
    /* Fantasma 8x8 en COLOR 1 (para F4 naranja): plano0=dibujo, plano1=0. */
    vc_load_spr_pattern(PAT_GHO8_A_C1, ghost8_a, cero8_pat);
    vc_load_spr_pattern(PAT_GHO8_B_C1, ghost8_b, cero8_pat);
    /* 16x16 */
    for (i = 0; i < 4; i++) {
        vc_load_spr_pattern((uint8_t)(PAT_PAC16 + i), pac16[i], pac16[i]);
        vc_load_spr_pattern((uint8_t)(PAT_GHO16 + i), gho16[i], gho16[i]);
        vc_load_spr_pattern((uint8_t)(PAT_GHO16_C1 + i), gho16[i], cero8); /* color 1 */
    }
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
                if (m == 0) { vc_put_cell(col, rr, TILE_BLANK); vc_set_cell_attr(col, rr, VC_BGPAL_2, 0); }
                else { vc_put_cell(col, rr, (uint8_t)(TILE_WALL_BASE + m)); vc_set_cell_attr(col, rr, VC_BGPAL_2, VC_ATTR_SOLID); }
            } else if (ch == 'o') { vc_put_cell(col, rr, TILE_POWER); vc_set_cell_attr(col, rr, VC_BGPAL_1, 0); }
            else if (ch == '.') { vc_put_cell(col, rr, TILE_DOT); vc_set_cell_attr(col, rr, VC_BGPAL_1, 0); }
            else { vc_put_cell(col, rr, TILE_BLANK); vc_set_cell_attr(col, rr, VC_BGPAL_2, 0); }
        }
    }
}

/* ---------------------------------------------------------------------------
 * DIBUJADO DE SPRITES
 * --------------------------------------------------------------------------- */
static void draw8(uint8_t slot, uint8_t pat, uint16_t x, uint8_t y, uint8_t pal) {
    vc_sprite_t s;
    s.x_lo = (uint8_t)x; s.y = y; s.tile = pat; s.flags = pal; s.coll = VC_COLL_CENTER;
    vc_sprite_set(slot, &s);
}

static void draw16(uint8_t base_slot, uint8_t base_pat, uint16_t x, uint8_t y, uint8_t pal) {
    vc_sprite_t s;
    s.flags = pal; s.coll = VC_COLL_CENTER; s.y = y;
    s.tile = (uint8_t)(base_pat + 0); s.x_lo = (uint8_t)x;       vc_sprite_set((uint8_t)(base_slot + 0), &s);
    s.tile = (uint8_t)(base_pat + 1); s.x_lo = (uint8_t)(x + 8); vc_sprite_set((uint8_t)(base_slot + 1), &s);
    s.tile = (uint8_t)(base_pat + 2); s.x_lo = (uint8_t)x;       s.y = (uint8_t)(y + 8); vc_sprite_set((uint8_t)(base_slot + 2), &s);
    s.tile = (uint8_t)(base_pat + 3); s.x_lo = (uint8_t)(x + 8); vc_sprite_set((uint8_t)(base_slot + 3), &s);
}

static void hide16(uint8_t base_slot) {
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

    vc_pal_set_bg(1, 3, VC_RGB444(0xF, 0xC, 0x8));
    vc_pal_set_bg(2, 3, VC_RGB444(0x0, 0x4, 0xF));

    vc_pal_set_spr(0, 3, VC_RGB444(0xF, 0xF, 0x0));   /* Pac amarillo */
    vc_pal_set_spr(1, 3, VC_RGB444(0xF, 0x0, 0x0));   /* rojo */
    vc_pal_set_spr(2, 3, VC_RGB444(0xF, 0x8, 0xC));   /* rosa */
    vc_pal_set_spr(3, 3, VC_RGB444(0x0, 0xF, 0xF));   /* cian */
    vc_pal_set_spr(3, 1, VC_RGB444(0xF, 0x8, 0x0));   /* naranja (color 1) */

    vc_load_bg_pattern(TILE_DOT,   dot_p0, dot_p1);
    vc_load_bg_pattern(TILE_POWER, pow_p0, pow_p1);
    load_wall_tiles();
    load_personajes();

    vc_put_str(1, 0, "COMPARA 8x8  vs  16x16");
    vc_put_str(1, 1, "opcion 8x8 = 1 celda, sin parpadeo");
}

/* ---------------------------------------------------------------------------
 * MAIN
 * --------------------------------------------------------------------------- */
int main(void) {
    uint8_t y8, y16, frame;
    uint8_t anim;          /* frame de animacion (0/1), cambia lento */

    rom_uart_puts("\r\nTEST COMPARE - 8x8 vs 16x16\r\n");

    setup_video();
    draw_maze();

    /* Todo en la fila 5 (pasillo completo del laberinto): mitad izquierda = 8x8,
     * mitad derecha = 16x16. Asi se comparan en el MISMO contexto, a la vez. */
    y8  = (uint8_t)((MAZE_Y0 + 5) * 8);
    y16 = (uint8_t)((MAZE_Y0 + 4) * 8);   /* 16x16 sube 1 celda para centrarse */

    frame = 0;
    anim  = 0;
    for (;;) {
        vc_wait_vblank();

        /* --- Grupo 8x8 (izquierda): 1 slot cada uno, los 5 a la vez --- */
        {
            uint16_t x8 = (uint16_t)((MAZE_X0 + 1) * 8);
            draw8(SLOT_PAC8, anim ? PAT_PAC8_B : PAT_PAC8_A, x8,                  y8,  VC_SPPAL_0);
            draw8(SLOT_F1_8, anim ? PAT_GHO8_B : PAT_GHO8_A, (uint16_t)(x8 + 16), y8, VC_SPPAL_1);
            draw8(SLOT_F2_8, anim ? PAT_GHO8_B : PAT_GHO8_A, (uint16_t)(x8 + 32), y8, VC_SPPAL_2);
            draw8(SLOT_F3_8, anim ? PAT_GHO8_B : PAT_GHO8_A, (uint16_t)(x8 + 48), y8, VC_SPPAL_3);
            /* F4: patron en COLOR 1 (naranja), mismo banco de paleta (3). */
            draw8(SLOT_F4_8, anim ? PAT_GHO8_B_C1 : PAT_GHO8_A_C1, (uint16_t)(x8 + 64), y8, VC_SPPAL_3);
        }

        /* --- Grupo 16x16 (derecha): 4 slots c/u, F4 alterna --- */
        {
            uint16_t x16 = (uint16_t)((MAZE_X0 + 12) * 8);
            draw16(SLOT_PAC16, PAT_PAC16, x16, y16, VC_SPPAL_0);
            draw16(SLOT_F1_16, PAT_GHO16, (uint16_t)(x16 + 32), y16, VC_SPPAL_1);
            draw16(SLOT_F2_16, PAT_GHO16, (uint16_t)(x16 + 64), y16, VC_SPPAL_2);
            if (anim == 0) {
                draw16(SLOT_F3_16, PAT_GHO16, (uint16_t)(x16 + 96), y16, VC_SPPAL_3);
                hide16(SLOT_F4_16);
            } else {
                hide16(SLOT_F3_16);
                draw16(SLOT_F4_16, PAT_GHO16_C1, (uint16_t)(x16 + 96), y16, VC_SPPAL_3);
            }
        }

        /* Animacion LENTA: cambia de frame cada ANIM_FRAMES frames (no cada
         * frame). A 60 Hz, 8 frames = ~7.5 cambios/seg (ritmo del arcade). */
        if (++frame >= ANIM_FRAMES) {
            frame = 0;
            anim ^= 1;
        }
        vc_wait_vblank_end();
    }

    return 0;
}
