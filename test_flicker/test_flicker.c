/**
 * ============================================================================
 * test_flicker.c - PROTOTIPO de prueba: parpadeo de sprites 16x16
 * ============================================================================
 * Objetivo: medir VISUALMENTE como se ve el "flicker" cuando hay que meter
 * 5 personajes de 16x16 en una misma fila y el core solo admite 8 sprites por
 * linea (OVERFLOW si se pasa).
 *
 * Con 16x16 NATIVO (vc_sprite16_set = 4 sprites de 8x8, 2 por linea):
 *     5 personajes x 2 slots/linea = 10 slots  ->  2 sobran
 *     -> hay que OCULTAR 1 personaje cada frame, alternando cual.
 *
 * DISENO DE LA PRUEBA (igual que la idea para Pac-Man):
 *   - 5 "personajes" en la MISMA fila (2x2 celdas = 16x16 px cada uno).
 *   - PRIORIDAD FIJA: Pac-Man y 2 fantasmas NUNCA parpadean.
 *   - Los 2 ultimos (Inky/Clyde) se ALTERNAN cada frame (30 Hz cada uno):
 *         frame par   -> visibles: Pac, F1, F2, F3   (F4 oculto)
 *         frame impar -> visibles: Pac, F1, F2, F4   (F3 oculto)
 *   - Los personajes se MUEVEN en horizontal para juzgar el parpadeo en
 *     movimiento (en estatico se percibe distinto).
 *
 * MODO CONMUTABLE (recompilar cambiando una linea):
 *   #define DRAW_MODE  MODE_NATIVE    -> 16x16 nativo (con parpadeo)
 *   #define DRAW_MODE  MODE_SCALE2X   -> 8x8 al doble (SIN parpadeo)
 * Asi se comparan los MISMOS 5 personajes en ambos modos.
 *
 * Este archivo es SOLO una prueba: no forma parte del motor del juego.
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"

/* ---------------------------------------------------------------------------
 * MODO DE DIBUJADO
 * --------------------------------------------------------------------------- */
#define MODE_NATIVE   0    /* 16x16 nativo: 4 sprites (2 por linea) */
#define MODE_SCALE2X  1    /* 8x8 al doble: 1 sprite               */

#define DRAW_MODE  MODE_NATIVE   /* <-- CAMBIA AQUI para comparar */

/* ---------------------------------------------------------------------------
 * PATRONES (8x8). Se cargan como 4 patrones consecutivos por personaje.
 * Cada personaje 16x16 nativo = 2x2 = 4 patrones:
 *      P+0 = arriba-izquierda   P+1 = arriba-derecha
 *      P+2 = abajo-izquierda    P+3 = abajo-derecha
 *
 * Para la prueba usamos FORMAS SIMPLES (no el arte final) pero del tamano
 * real, para poder juzgar el parpadeo. Un cuadrado con hueco (estilo "fantasma"
 * abstracto) y un circulo con cuna (estilo "Pac-Man" abstracto).
 *
 * En sprites (color 0 = transparente): plano0 = plano1 = dibujo -> color 3.
 * --------------------------------------------------------------------------- */

/* --- Forma "Pac-Man" abstracta 16x16 (dividida en 4 cuadrantes 8x8) --- */
/* Filas 0..7 = mitad superior; filas 8..15 = mitad inferior.
 * Circulo puro desde 8x8: la silueta se ve bien al componer los 4 cuadrantes. */
static const uint8_t pac_tl_p0[8] = { 0x0F,0x3F,0x7F,0x7F,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_tr_p0[8] = { 0xF0,0xFC,0xFE,0xFE,0xFF,0xFF,0xFF,0xFF };
static const uint8_t pac_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0x7F,0x3F,0x0F };
static const uint8_t pac_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFE,0xFE,0xFC,0xF0 };

/* --- Forma "fantasma" abstracta 16x16: cupula + borde inferior ondulado --- */
static const uint8_t gho_tl_p0[8] = { 0x3F,0x7F,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_tr_p0[8] = { 0xFC,0xFE,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF };
static const uint8_t gho_bl_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xF3,0xF7 };
static const uint8_t gho_br_p0[8] = { 0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xEF,0xDF };

/* Lista de los 4 patrones de cada personaje, para cargarlos en bucle. */
static const uint8_t *const pac_pats[4] = { pac_tl_p0, pac_tr_p0, pac_bl_p0, pac_br_p0 };
static const uint8_t *const gho_pats[4] = { gho_tl_p0, gho_tr_p0, gho_bl_p0, gho_br_p0 };

/* Plano 1 a cero: al cargar un patron con p1 = este array, el indice de color
 * queda en 1 (plano0=dibujo, plano1=0) -> usa el COLOR 1 de la paleta.
 * Se usa para dibujar F4 en naranja (color 1 del banco 3) y distinguirlo de F3
 * (cian = color 3 del mismo banco). */
static const uint8_t gho_p1_cero[8] = { 0,0,0,0,0,0,0,0 };

/* ---------------------------------------------------------------------------
 * BANCO DE PATRONES EN VRAM (patrones de sprite 0..63)
 * ---------------------------------------------------------------------------
 * Reservamos 4 patrones por personaje:
 *   0..3   -> Pac-Man
 *   4..7   -> Fantasma 1 (Blinky)
 *   8..11  -> Fantasma 2 (Pinky)
 *   12..15 -> Fantasma 3 (Inky)
 *   16..19 -> Fantasma 4 (Clyde)
 * (Se podrian compartir los de los fantasmas por color de paleta, pero para la
 *  PRUEBA de parpadeo cada uno tiene su copia: nos interesa el coste en slots
 *  de OAM y el efecto visual, no ahorrar patrones todavia.) */
#define PAT_PAC   0
#define PAT_F1    4
#define PAT_F2    8
#define PAT_F3    12
#define PAT_F4    16

/* ---------------------------------------------------------------------------
 * SLOTS DE OAM
 * ---------------------------------------------------------------------------
 * 5 personajes x 4 sprites = 20 slots de 32. Cabe en el total.
 * PERO por LINEA solo se ven 2 por personaje -> el limite de 8 es el problema.
 *
 * Slots usados por personaje (2x2):
 *   personaje i -> slots base+0 (sup-izq), base+1 (sup-der),
 *                  base+2 (inf-izq), base+3 (inf-der)
 * --------------------------------------------------------------------------- */
#define SLOT_PAC   0    /* 0..3  */
#define SLOT_F1    4    /* 4..7  */
#define SLOT_F2    8    /* 8..11 */
#define SLOT_F3    12   /* 12..15 */
#define SLOT_F4    16   /* 16..19 */

/* ---------------------------------------------------------------------------
 * POSICION Y MOVIMIENTO
 * --------------------------------------------------------------------------- */
#define N_PERSONAJES 5
#define SEPARACION   44      /* px entre personajes */
#define X0           16      /* X inicial */
#define Y_FILA       112     /* Y de la fila (todos a la misma altura = peor caso) */
#define VELOCIDAD    1       /* px por frame (lento, para juzgar el parpadeo) */

static uint16_t pos_x[N_PERSONAJES];   /* X actual de cada personaje */
static int8_t   dir;                   /* +1 derecha / -1 izquierda */
static uint8_t  frame;                 /* contador de frames (par/impar) */

/* ---------------------------------------------------------------------------
 * CARGA DE PATRONES
 * --------------------------------------------------------------------------- */
static void load_mis_patrones(void) {
    uint8_t i;

    /* Pac-Man */
    for (i = 0; i < 4; i++) {
        vc_load_spr_pattern((uint8_t)(PAT_PAC + i), pac_pats[i], pac_pats[i]);
    }
    /* Los 4 fantasmas comparten FORMA; para la prueba cargamos la misma en
     * sus 4 bancos (realmente en el juego bastaria 1 y cambiaria la paleta).
     * F3 se dibuja en color 3 (plano1 = dibujo) y F4 en color 1 (plano1 = 0),
     * para que tengan COLORES DISTINTOS al alternarse. */
    for (i = 0; i < 4; i++) {
        vc_load_spr_pattern((uint8_t)(PAT_F1 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F2 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F3 + i), gho_pats[i], gho_pats[i]);
        vc_load_spr_pattern((uint8_t)(PAT_F4 + i), gho_pats[i], gho_p1_cero); /* color 1 */
    }
}

/* ---------------------------------------------------------------------------
 * SETUP DE VIDEO
 * --------------------------------------------------------------------------- */
static void setup_video(void) {
    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* Fondo negro. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));

    /* Colores de sprite (bancos 0..3). Para la prueba:
     *   banco 0: amarillo (Pac-Man)
     *   banco 1: rojo     (F1)
     *   banco 2: rosa     (F2)
     *   banco 3: cian     (F3)   - color 3
     *   banco 3: naranja  (F4)   - color 1 (mismo banco, otro indice)
     * El arte del fantasma se dibuja en DOS indices de color: F3 en el 3 (cian)
     * y F4 en el 1 (naranja). Asi cada uno tirita con un color DISTINTO y se ve
     * sin ambiguedad quien esta en pantalla cada frame. */
    vc_pal_set_spr(0, 3, VC_RGB444(0xF, 0xF, 0x0));   /* banco 0, color 3 = amarillo */
    vc_pal_set_spr(1, 3, VC_RGB444(0xF, 0x0, 0x0));   /* banco 1, color 3 = rojo     */
    vc_pal_set_spr(2, 3, VC_RGB444(0xF, 0x8, 0xC));   /* banco 2, color 3 = rosa     */
    vc_pal_set_spr(3, 3, VC_RGB444(0x0, 0xF, 0xF));   /* banco 3, color 3 = cian     */
    vc_pal_set_spr(3, 1, VC_RGB444(0xF, 0x8, 0x0));   /* banco 3, color 1 = naranja  */

    load_mis_patrones();

    /* Texto de referencia (reutilizamos la fuente del sistema). */
    vc_put_str(1, 1, "TEST FLICKER 5x 16x16");
#if DRAW_MODE == MODE_NATIVE
    vc_put_str(1, 2, "MODO: NATIVO (2 slots/linea, parpadeo)");
#else
    vc_put_str(1, 2, "MODO: SCALE2X (1 slot, sin parpadeo)");
#endif
    vc_put_str(1, 27, "F3=cian  F4=naranja (alternan)");
}

/* ---------------------------------------------------------------------------
 * DIBUJADO - MODO NATIVO: 16x16 partido en 4 sprites (2 por linea)
 * ---------------------------------------------------------------------------
 *   base+0 = sup-izq (tile+0)   base+1 = sup-der (tile+1)
 *   base+2 = inf-izq (tile+2)   base+3 = inf-der (tile+3)
 * --------------------------------------------------------------------------- */
static void draw_native(uint8_t base_slot, uint8_t base_pat,
                        uint16_t x, uint8_t y, uint8_t pal) {
    vc_sprite_t s;

    /* sup-izq */
    s.x_lo = (uint8_t)x;      s.y = y;      s.tile = (uint8_t)(base_pat + 0);
    s.flags = pal;            s.coll = VC_COLL_CENTER;  vc_sprite_set((uint8_t)(base_slot + 0), &s);
    /* sup-der */
    s.x_lo = (uint8_t)(x + 8);                          s.tile = (uint8_t)(base_pat + 1);
    vc_sprite_set((uint8_t)(base_slot + 1), &s);
    /* inf-izq */
    s.x_lo = (uint8_t)x;      s.y = (uint8_t)(y + 8);   s.tile = (uint8_t)(base_pat + 2);
    vc_sprite_set((uint8_t)(base_slot + 2), &s);
    /* inf-der */
    s.x_lo = (uint8_t)(x + 8);                          s.tile = (uint8_t)(base_pat + 3);
    vc_sprite_set((uint8_t)(base_slot + 3), &s);
}

/* Oculta los 4 sprites de un personaje (modo nativo). */
static void hide_native(uint8_t base_slot) {
    vc_sprite_disable((uint8_t)(base_slot + 0));
    vc_sprite_disable((uint8_t)(base_slot + 1));
    vc_sprite_disable((uint8_t)(base_slot + 2));
    vc_sprite_disable((uint8_t)(base_slot + 3));
}

/* ---------------------------------------------------------------------------
 * DIBUJADO - MODO SCALE2X: un solo sprite 8x8 al doble -> 16x16
 * ---------------------------------------------------------------------------
 * Usamos el cuadrante sup-izq como "miniatura" 8x8 al doble. (Es solo la
 * prueba; en el juego se dibujaria el arte 8x8 definitivo.) */
static void draw_scale2x(uint8_t base_slot, uint8_t base_pat,
                         uint16_t x, uint8_t y, uint8_t pal) {
    vc_sprite_t s;

    s.x_lo = (uint8_t)x;
    s.y    = y;
    s.tile = base_pat;                       /* solo 1 patron */
    s.flags = (uint8_t)(pal | VC_SPR_SCALE2X);
    s.coll = VC_COLL_CENTER;
    vc_sprite_set(base_slot, &s);

    /* Solo usamos el slot base; los otros 3 del bloque quedan ocultos para
     * que la comparacion sea justa (1 sprite por personaje, no 4). */
    vc_sprite_disable((uint8_t)(base_slot + 1));
    vc_sprite_disable((uint8_t)(base_slot + 2));
    vc_sprite_disable((uint8_t)(base_slot + 3));
}

/* ---------------------------------------------------------------------------
 * MOVIMIENTO DEL GRUPO
 * ---------------------------------------------------------------------------
 * Los 5 avanzan juntos en horizontal (como una cuadrilla) y rebotan en los
 * margenes. El rebote se decide con el personaje EXTREMO para que el grupo
 * cambie de direccion de forma coherente (sin desmontarse). */
#define X_MIN   4
#define X_MAX   (VC_SCREEN_W - 8 - 16)   /* el personaje mas a la derecha */

static void move_group(void) {
    uint16_t left_x  = pos_x[0];
    uint16_t right_x = pos_x[N_PERSONAJES - 1];
    int16_t  step    = (int16_t)dir;

    /* Rebote si el grupo toca un margen. */
    if ((left_x <= X_MIN && dir < 0) || (right_x >= X_MAX && dir > 0)) {
        dir = (int8_t)-dir;
        step = (int16_t)dir;
    }

    {
        uint8_t i;
        for (i = 0; i < N_PERSONAJES; i++) {
            pos_x[i] = (uint16_t)((int16_t)pos_x[i] + step);
        }
    }
}

/* ---------------------------------------------------------------------------
 * INDICADOR DE OVERFLOW
 * ---------------------------------------------------------------------------
 * vc_status() tiene el bit VC_STATUS_OVERFLOW. Si el core descarta sprites por
 * pasar de 8 por linea, el bit se marca. Lo mostramos en pantalla: es la prueba
 * OBJETIVA de que el modo NATIVO desborda.
 *
 * DEL MANUAL DEL CORE (07-MANUAL-PROGRAMACION.md §10):
 *   - OVERFLOW refleja el frame EN CURSO y NO se limpia al leer.
 *   - "Se dibujan los PRIMEROS y se descartan los demas de esa linea."
 *   -> Con 10 slots (5 personajes x 2/linea) SIEMPRE se descartan 2, asi que el
 *      indicador se quedara encendido mientras esten en la misma fila.
 *
 * Leemos STATUS UNA VEZ por frame (recomendacion del manual: es un registro de
 * varios bits, se guarda en RAM una vez por frame si se consulta mas de uno).
 * Solo redibujamos el texto cuando CAMBIA (evita escrituras VRAM cada frame). */
static uint8_t over_prev = 0xFF;   /* 0xFF = forzar el primer dibujado */

static void update_overflow_indicator(uint8_t status_byte) {
    uint8_t over = (status_byte & VC_STATUS_OVERFLOW) ? 1 : 0;

    if (over == over_prev) return;
    over_prev = over;

    if (over) {
        vc_put_str(1, 4, "OVERFLOW: el core DESCARTA sprites (>8/linea)");
    } else {
        /* Borrar la linea (40 espacios). */
        uint8_t c;
        for (c = 0; c < 40; c++) vc_put_char(c, 4, ' ');
    }
}

/* ---------------------------------------------------------------------------
 * BUCLE PRINCIPAL
 * --------------------------------------------------------------------------- */
int main(void) {
    uint8_t i;

    rom_uart_puts("\r\nTEST FLICKER - 5 personajes 16x16\r\n");

    setup_video();

    /* Posiciones iniciales: una fila horizontal. */
    for (i = 0; i < N_PERSONAJES; i++) {
        pos_x[i] = (uint16_t)(X0 + i * SEPARACION);
    }
    dir   = 1;
    frame = 0;

    for (;;) {
        uint8_t alive_visible;

        vc_wait_vblank();

        alive_visible = (uint8_t)(frame & 1);   /* 0 o 1: quien alterna */

        /* --- Pac-Man (nunca parpadea) --- */
        {
            uint16_t x = pos_x[0];
#if DRAW_MODE == MODE_NATIVE
            draw_native(SLOT_PAC, PAT_PAC, x, Y_FILA, VC_SPPAL_0);
#else
            draw_scale2x(SLOT_PAC, PAT_PAC, x, Y_FILA, VC_SPPAL_0);
#endif
        }
        /* --- F1 y F2 (nunca parpadean) --- */
        {
            uint16_t x1 = pos_x[1];
            uint16_t x2 = pos_x[2];
#if DRAW_MODE == MODE_NATIVE
            draw_native(SLOT_F1, PAT_F1, x1, Y_FILA, VC_SPPAL_1);
            draw_native(SLOT_F2, PAT_F2, x2, Y_FILA, VC_SPPAL_2);
#else
            draw_scale2x(SLOT_F1, PAT_F1, x1, Y_FILA, VC_SPPAL_1);
            draw_scale2x(SLOT_F2, PAT_F2, x2, Y_FILA, VC_SPPAL_2);
#endif
        }
        /* --- F3 y F4: ALTERNAN cada frame --- */
        {
            uint16_t x3 = pos_x[3];
            uint16_t x4 = pos_x[4];

            if (alive_visible == 0) {
                /* frame par: F3 visible, F4 oculto */
#if DRAW_MODE == MODE_NATIVE
                draw_native(SLOT_F3, PAT_F3, x3, Y_FILA, VC_SPPAL_3);
#else
                draw_scale2x(SLOT_F3, PAT_F3, x3, Y_FILA, VC_SPPAL_3);
#endif
                hide_native(SLOT_F4);
            } else {
                /* frame impar: F4 visible, F3 oculto */
                hide_native(SLOT_F3);
#if DRAW_MODE == MODE_NATIVE
                draw_native(SLOT_F4, PAT_F4, x4, Y_FILA, VC_SPPAL_3);
#else
                draw_scale2x(SLOT_F4, PAT_F4, x4, Y_FILA, VC_SPPAL_3);
#endif
            }
        }

        /* --- Avanzar el grupo (comun a todos, rebote coherente) --- */
        move_group();
        frame++;

        /* Indicador de overflow (prueba objetiva de que NATIVO desborda). */
        update_overflow_indicator(vc_status());

        vc_wait_vblank_end();
    }

    return 0;
}
