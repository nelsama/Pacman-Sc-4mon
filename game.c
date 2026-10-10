/**
 * ============================================================================
 * game.c - PAC-MAN para el Core de Video (vc) / Monitor 6502
 * ============================================================================
 * FASE 1 - CIMIENTOS:
 *   - Setup de video: BG negro, paletas del juego (pared/puntos/personajes).
 *   - Laberinto del arcade 28x28 (celdas 8x8), con paredes FINAS por
 *     auto-tiling (probado en test_maze).
 *   - Puntos y power pellets como TILES del tilemap (0 sprites).
 *   - Pac-Man (sprite 8x8) colocado en el laberinto, estatico.
 *   - Fase de "match" del HUD superior (2 filas).
 *
 * DISENO (fijado en doc/ARQUITECTURA-PACMAN.md):
 *   - Laberinto 28x28, celdas 8x8 cuadradas, centrado (col 6..33).
 *   - Personajes 8x8 (1 celda), sin flicker, 5 colores via indice de paleta.
 *   - Puntos/pildoras = tiles.  Paredes = tiles finos (auto-tiling).
 *   - Motor: ASM para lo critico, C para orquestacion (esta fase en C).
 * ============================================================================
 */

#include <stdint.h>
#include "video.h"
#include "romapi.h"
#include "sound.h"
#include "joy.h"

/* Escritura rapida de numeros en (col,row) [text.s]. */
extern void txt_put_u16_5(uint8_t col, uint8_t row, uint16_t valor);
/* Decena de millar del ULTIMO numero dibujado con txt_put_u16_5 (0..6).
 * Lo fija text.s; el juego lo usa para la vida extra sin aritmetica de 16 bits. */
extern uint8_t txt_top_digit;

/* ===========================================================================
 * GEOMETRIA
 * ===========================================================================
 * Pantalla 40x30. HUD en las filas 0-1. Laberinto 28x28 centrado (col 6..33).
 * =========================================================================== */
#define HUD_ROWS   2
#define MAZE_W     28
#define MAZE_H     28
#define MAZE_X0    6                 /* (40-28)/2 */
#define MAZE_Y0    HUD_ROWS

#define GROSOR_PX  2                 /* grosor de las paredes finas */

/* ===========================================================================
 * PALETAS (indices RGB444)
 * ===========================================================================
 * FONDO:
 *   paleta 1: color 3 = blanco      -> puntos (tile de punto)
 *   paleta 2: color 3 = azul        -> paredes
 * SPRITE (reparto de indices para sacar 5 colores de 2 paletas):
 *   paleta 0: color 3 = rojo   (F1),  color 2 = rosa (F2), color 1 = cian (F3)
 *   paleta 1: color 3 = amarillo (Pac), color 2 = naranja (F4)
 * =========================================================================== */
#define PAL_DOT    VC_BGPAL_1
#define PAL_WALL   VC_BGPAL_2
#define PAL_GHOST  VC_SPPAL_0        /* banco 0: F1/F2/F3 por indice */
#define PAL_PAC    VC_SPPAL_1        /* banco 1: Pac (idx3) y F4 (idx2) */

/* ===========================================================================
 * TILES
 * =========================================================================== */
#define TILE_BLANK      0x00
#define TILE_DOT        0x02
#define TILE_POWER      0x03
#define TILE_WALL_BASE  0x10         /* 0x11..0x1F = 15 combinaciones de borde */

/* Punto (pixel central). */
static const uint8_t dot_p0[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
static const uint8_t dot_p1[8] = { 0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00 };
/* Power pellet (bloque 4x4). */
static const uint8_t pow_p0[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };
static const uint8_t pow_p1[8] = { 0x00,0x00,0x3C,0x3C,0x3C,0x3C,0x00,0x00 };

/* ===========================================================================
 * MAPA DEL LABERINTO (28x28) - forma del arcade
 *   '#' pared   '.' punto   'o' power pellet   ' ' vacio
 * =========================================================================== */
/* ===========================================================================
 * MAPA DEL LABERINTO (28x28) - COMPRIMIDO a 2 bits/celda
 * ===========================================================================
 * Antes eran 28 strings de 28 chars + 28 punteros (~840 B en RAM). Ahora 196 B.
 *
 * Codigos (2 bits, 4 celdas por byte; la celda mas a la IZQUIERDA en los bits
 * mas altos):
 *   0 = vacio (' ')     1 = punto ('.')     2 = power ('o')     3 = pared ('#')
 * La puerta de la casa ('--' del original) va como VACIO (transitable).
 *
 * Para leer una celda:  maze_cell(col,row)  (ver helper mas abajo).
 * ===========================================================================
 */
#define MAZE_PACK    (MAZE_W / 4)   /* bytes por fila = 7 */
/* NO static: lo importa gho_ai.s para su cell_passable. */
const uint8_t maze_bits[MAZE_H * MAZE_PACK] = {
    /*  0 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
    /*  1 */ 0xD5, 0x55, 0x55, 0x7D, 0x55, 0x55, 0x57,
    /*  2 */ 0xDF, 0xF7, 0xFF, 0x7D, 0xFF, 0xDF, 0xF7,
    /*  3 */ 0xEF, 0xF7, 0xFF, 0x7D, 0xFF, 0xDF, 0xFB,
    /*  4 */ 0xDF, 0xF7, 0xFF, 0x7D, 0xFF, 0xDF, 0xF7,
    /*  5 */ 0xD5, 0x55, 0x55, 0x55, 0x55, 0x55, 0x57,
    /*  6 */ 0xDF, 0xF7, 0xDF, 0xFF, 0xF7, 0xDF, 0xF7,
    /*  7 */ 0xDF, 0xF7, 0xDF, 0xFF, 0xF7, 0xDF, 0xF7,
    /*  8 */ 0xD5, 0x57, 0xD5, 0x7D, 0x57, 0xD5, 0x57,
    /*  9 */ 0xFF, 0xF7, 0xFF, 0x3C, 0xFF, 0xDF, 0xFF,
    /* 10 */ 0x00, 0x37, 0xFF, 0x3C, 0xFF, 0xDC, 0x00,
    /* 11 */ 0x00, 0x37, 0xC0, 0x00, 0x03, 0xDC, 0x00,
    /* 12 */ 0x00, 0x37, 0xCF, 0xC3, 0xF3, 0xDC, 0x00,
    /* 13 */ 0xFF, 0xF7, 0xCC, 0x00, 0x33, 0xDF, 0xFF,
    /* 14 */ 0x00, 0x04, 0x0C, 0x00, 0x30, 0x10, 0x00,
    /* 15 */ 0xFF, 0xF7, 0xCC, 0x00, 0x33, 0xDF, 0xFF,
    /* 16 */ 0x00, 0x37, 0xCF, 0xFF, 0xF3, 0xDC, 0x00,
    /* 17 */ 0x00, 0x37, 0xC0, 0x00, 0x03, 0xDC, 0x00,
    /* 18 */ 0x00, 0x37, 0xCF, 0xFF, 0xF3, 0xDC, 0x00,
    /* 19 */ 0xFF, 0xF7, 0xCF, 0xFF, 0xF3, 0xDF, 0xFF,
    /* 20 */ 0xD5, 0x55, 0x55, 0x7D, 0x55, 0x55, 0x57,
    /* 21 */ 0xDF, 0xF7, 0xFF, 0x7D, 0xFF, 0xDF, 0xF7,
    /* 22 */ 0xDF, 0xF7, 0xFF, 0x7D, 0xFF, 0xDF, 0xF7,
    /* 23 */ 0xE5, 0xF5, 0x55, 0x41, 0x55, 0x5F, 0x5B,
    /* 24 */ 0xFD, 0xF7, 0xDF, 0xFF, 0xF7, 0xDF, 0x7F,
    /* 25 */ 0xFD, 0xF7, 0xDF, 0xFF, 0xF7, 0xDF, 0x7F,
    /* 26 */ 0xD5, 0x57, 0xD5, 0x7D, 0x57, 0xD5, 0x57,
    /* 27 */ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

/* Codigos de celda del mapa comprimido. */
#define MC_EMPTY  0
#define MC_DOT    1
#define MC_POWER  2
#define MC_WALL   3

/* Lee el codigo (0..3) de la celda (col,row) del laberinto.
 * row*7 con shifts ((row<<3)-row) en vez de multiplicar. */
static uint8_t maze_cell(uint8_t col, uint8_t row) {
    uint8_t idx = (uint8_t)(((uint8_t)(row << 3) - row) + (col >> 2));
    uint8_t byte = maze_bits[idx];
    return (uint8_t)((byte >> ((3 - (col & 3)) * 2)) & 3);
}

/* ===========================================================================
 * ESTADO
 * =========================================================================== */
/* Posicion de Pac-Man: NO static para que gho_ai.s (ASM) pueda leerlas. */
uint16_t pac_x;      /* posicion en pixeles (16 bits: caben los 9 de X) */
uint8_t  pac_y;
int8_t   pac_dx;     /* direccion actual: -1, 0, +1 */
int8_t   pac_dy;     /* -1, 0, +1 */
static int8_t   pac_next_dx;/* direccion PEDIDA (buffer), se aplica en el centro */
static int8_t   pac_next_dy;
static int8_t   pac_face_dx;/* direccion para ORIENTAR el sprite (aunque este quieto) */
static int8_t   pac_face_dy;
static uint8_t  pac_anim;   /* contador para la animacion de boca */
static uint8_t  quit_flag;

/* Declaraciones adelantadas (se definen mas abajo). */
static void gho_setup(void);
static void fruit_hide(void);
static void fruit_spawn(void);

/* --- Puntos (pellets) ---------------------------------------------------
 * Para no usar un byte por celda, un BITMAP de 1 bit por celda (28x28 bits =
 * 98 B). 1 = aun hay punto (o power) sin comer. La FILA y COLUMNA se indexan
 * dentro del laberinto (0..27). El tipo (punto/power) se consulta a maze[]. */
#define DOTS_BYTES  ((MAZE_W * MAZE_H + 7) / 8)   /* 98 */
static uint8_t  dots[DOTS_BYTES];
static uint16_t dots_left;   /* puntos que quedan por comer */
static uint16_t score;       /* puntuacion */

/* --- Vidas y estado de la partida ----------------------------------------*/
#define PAC_LIVES0   3       /* vidas iniciales */
#define EXTRA_LIFE_PTS 10000 /* vida extra al alcanzar esta puntuacion (arcade) */
#define DEATH_PAUSE  90      /* frames de pausa al morir (~1.5 s) */
#define READY_FRAMES 120     /* frames de la pausa READY! antes de cada ronda */
#define READY_COL    17      /* columna del texto READY! (centrado: 20 - 3) */
#define READY_ROW    (MAZE_Y0 + 17)  /* fila: pasillo central, junto a Pac */

static uint8_t  lives;
static uint8_t  death_timer;  /* >0: pausa de muerte en curso */
static uint8_t  game_over;    /* 1 = sin vidas */
static uint8_t  go_drawn;     /* 1 = ya se dibujo la pantalla de GAME OVER */
static uint8_t  fire_held;    /* 1 = FIRE aun pulsado (evita reinicio repetido) */
static uint8_t  ready_timer;  /* >0: pausa de READY! (nadie se mueve) */
static uint8_t  ready_drawn;  /* 1 = el texto READY! esta en pantalla */
static uint8_t  ready_wait;   /* 1 = READY! espera FIRE/direccion (no expira solo) */
static uint8_t  ready_rel;    /* 1 = aun hay que SOLTAR la entrada antes de empezar */
static uint8_t  extra_life;   /* 1 = ya se dio la vida extra de los 10000 */

/* Velocidad: px por frame. Pac-Man avanza 1 px SIEMPRE (60 px/s).
 * Los fantasmas son MAS LENTOS: se saltan 1 de cada GHO_SKIP frames, asi su
 * velocidad media es 1/(GHO_SKIP+1) px/frame. GHO_SKIP=1 -> avanzan 1 de cada
 * 2 frames = ~la mitad (primer nivel, como el arcade). OJO: el movimiento sigue
 * siendo entero (1 px) para no romper la alineacion al centro de celda. */
#define PAC_SPEED   1
#define GHO_SKIP    1        /* valor por defecto (el nivel puede subirlo/bajarlo) */

/* --- Fantasmas (los 4 clasicos) ----------------------------------------
 * Estado en ARRAYS paralelos. Misma mecanica de
 * movimiento sub-pixel que Pac-Man, con IA simple: en cada centro de celda
 * eligue una direccion valida (no pared) que no sea la reversa.
 *
 * Colores (bancos/indices de paleta):
 *   F0 Blinky rojo    -> paleta 0, indice 3
 *   F1 Pinky  rosa    -> paleta 0, indice 2
 *   F2 Inky   cian    -> paleta 0, indice 1
 *   F3 Clyde  naranja -> paleta 1, indice 2 */
#define NGHOSTS    4
#define GHO_SPEED  1

/* --- Modo ASUSTADO (power pellet) ----------------------------------------
 * Cuando Pac-Man come un power pellet, los fantasmas se vuelven AZULES y huyen
 * durante FRIGHT_FRAMES. En los ultimos FRIGHT_BLINK frames PARPADEAN (azul/
 * blanco) avisando de que estan a punto de volver a la normalidad. Si Pac-Man
 * toca a un fantasma asustado, se lo COME (+puntos) y el fantasma vuelve a la
 * casa. */
#define FRIGHT_FRAMES   300   /* duracion por defecto (el nivel la acorta) */
#define FRIGHT_BLINK    90    /* ultimos frames: parpadeo de aviso */
#define GHOST_EAT_PTS   200   /* puntos por comerse un fantasma */

static uint16_t fright_timer;   /* >0: fantasmas asustados */

/* ===========================================================================
 * NIVELES / DIFICULTAD
 * ===========================================================================
 * Empieza en el nivel 1. Al comerse todos los puntos se sube de nivel: los
 * fantasmas van mas rapidos (menos GHO_SKIP) y el modo asustado dura menos.
 * Las tablas tienen NIVEL_MAX entradas; del ultimo nivel en adelante se repite.
 * =========================================================================== */
#define NIVEL_MAX   8
static uint8_t  level;          /* nivel actual (1..NIVEL_MAX) */
static uint8_t  gho_skip_cur;   /* GHO_SKIP efectivo del nivel (menor = mas rapido) */
static uint16_t fright_frames;  /* duracion del fright del nivel */

/* Velocidad de los fantasmas por nivel (menor = mas rapido):
 *   skip 2 = 1/3 de la velocidad de Pac | skip 1 = 1/2 | skip 0 = igual que Pac.
 * Progresion por PAREJAS de niveles para que el salto se NOTE:
 *   niv 1-2 -> 1/3,  niv 3-4 -> 1/2,  niv 5-8 -> 1/1. */
static const uint8_t level_gho_skip[NIVEL_MAX] = { 2, 2, 1, 1, 0, 0, 0, 0 };
/* Duracion del modo asustado por nivel (frames), cada vez mas corta. */
static const uint16_t level_fright[NIVEL_MAX] = {
    300, 260, 220, 180, 140, 110, 80, 60
};

/* ===========================================================================
 * FRUTAS (bonus)
 * ===========================================================================
 * Aparecen DOS VECES por nivel (como el arcade): al comer 70 y 170 puntos, en
 * la celda que hay DEBAJO de la casa (pasillo central). Duran FRUIT_FRAMES; si
 * Pac-Man las come, +puntos. El tipo depende del nivel (tabla del arcade). */
#define FRUIT_FRAMES    600   /* ~10 s en pantalla */
#define FRUIT_COL       13    /* columna (maze) debajo de la casa */
#define FRUIT_ROW       17    /* fila (maze) del pasillo central */

static uint8_t  fruit_on;      /* 1 = fruta visible */
static uint16_t fruit_timer;   /* frames que le quedan */
static uint16_t dots_eaten;    /* puntos comidos en el nivel */
static uint8_t  fruit_spawned; /* cuantas frutas han aparecido ya en el nivel */

/* Puntos comidos a los que aparece cada fruta del nivel. */
static const uint16_t fruit_at_dots[2] = { 120, 240 };

/* Tipo de fruta por nivel (1..). Del 13 en adelante: llave. Codigos:
 * 0=cereza 1=fresa 2=naranja 3=manzana 4=melon 5=galaxian 6=campana 7=llave */
static const uint8_t fruit_type_by_level[13] = {
    0,                /* nivel 1  */
    1,                /* nivel 2  */
    2, 2,             /* 3,4  naranja  */
    3, 3,             /* 5,6  manzana  */
    4, 4,             /* 7,8  melon    */
    5, 5,             /* 9,10 galaxian */
    6, 6,             /* 11,12 campana */
    7                 /* 13+  llave    */
};
/* Puntos que da cada fruta (indice 0..7). */
static const uint16_t fruit_pts[8] = { 100, 300, 500, 700, 1000, 2000, 3000, 5000 };

/* Devuelve el tipo de fruta del nivel actual (0..7). */
static uint8_t fruit_type_now(void) {
    uint8_t idx = (uint8_t)(level - 1);
    if (idx > 12) idx = 12;
    return fruit_type_by_level[idx];
}

/* Velocidad de los OJOS (fantasma comido) al volver a casa, en px/frame.
 * 1 = misma que Pac-Man (los fantasmas normales van a ~0.5 por el salto).
 * 2 = el doble (mas parecido al arcade). */
#define EYES_SPEED   1

/* Arrays de fantasmas: NO static para que gho_ai.s (ASM) pueda importarlos. */
uint16_t gho_x[NGHOSTS];
uint8_t  gho_y[NGHOSTS];
int8_t   gho_dx[NGHOSTS];
int8_t   gho_dy[NGHOSTS];
static uint8_t  gho_anim[NGHOSTS];
static uint8_t  gho_skip[NGHOSTS];   /* contador del "salto" de velocidad */

/* --- Estado de cada fantasma (para la salida de la casa y la IA) ---------
 *   GHO_HOME  : dentro de la casa, esperando su turno para salir.
 *   GHO_LEAVE : saliendo de la casa (va hacia la puerta).
 *   GHO_FREE  : fuera, en el laberinto (IA de persecucion).
 *   GHO_EATEN : comido -> solo OJOS, vuelve volando a la casa. */
#define GHO_HOME   0
#define GHO_LEAVE  1
#define GHO_FREE   2
#define GHO_EATEN  3
uint8_t gho_state[NGHOSTS];   /* NO static: lo lee gho_ai.s */
static uint8_t gho_wait[NGHOSTS];   /* frames que aun le quedan en la casa */
/* 1 = este fantasma participa del modo asustado (azul y comestible). Al comer
 * un power pellet se pone a 1 en los fantasmas vivos; al ser comido y revivir
 * se pone a 0, para que salga NORMAL aunque el fright siga activo (como el
 * arcade). */
static uint8_t gho_scared[NGHOSTS];

/* Punto de SALIDA (celda justo encima de la puerta de la casa). La puerta esta
 * en la fila 12 (cols 13-14); la celda de arriba (fila 11) es pasillo. */
#define GHO_GATE_COL  13
#define GHO_GATE_ROW  12
#define GHO_GATE_Y    ((uint8_t)((MAZE_Y0 + GHO_GATE_ROW) * 8))

/* Tiempos de espera iniciales (frames) para escalonar la salida. */
static const uint8_t gho_home_wait0[NGHOSTS] = { 0, 60, 150, 240 };

/* Flags de color de cada fantasma (paleta + indice fijados en el PATRON, pero
 * la PALETA se elige en flags). Los indices 1/2/3 se consiguen con el distinto
 * plano1 del patron (ver load: gho_pat_* por color). Aqui solo la paleta. */
static const uint8_t gho_pal[NGHOSTS] = {
    VC_SPPAL_0,            /* F0 Blinky: paleta 0 (rojo   = idx3) */
    VC_SPPAL_0,            /* F1 Pinky : paleta 0 (rosa   = idx2) */
    VC_SPPAL_0,            /* F2 Inky  : paleta 0 (cian   = idx1) */
    VC_SPPAL_1             /* F3 Clyde : paleta 1 (naranja= idx2) */
};

/* Patron (8x8) de cada fantasma: 2 frames, y color segun el indice.
 * Van en los patrones 16..23 (Pac-Man usa 0..7) para no solaparse. */
static const uint8_t gho_pat_a[NGHOSTS] = { 16, 18, 20, 22 };
static const uint8_t gho_pat_b[NGHOSTS] = { 17, 19, 21, 23 };

/* Patrones del modo ASUSTADO: mismo dibujo, en AZUL (24/25) y BLANCO (26/27).
 * Se eligen por el INDICE de color del patron (igual que los colores normales).
 *   asustado azul  -> paleta 2 (color 3 = azul)  pat 24/25
 *   asustado blanco-> paleta 2 (color 3 = blanco) pat 26/27 */
#define PAT_FRIGHT_A  24
#define PAT_FRIGHT_B  25
#define PAL_FRIGHT    VC_SPPAL_2

/* (La IA de fantasmas usa direcciones deterministas; el RNG de eleccion
 * aleatoria ya no se usa: la IA esta en gho_ai.s.) */

/* Puntos por tipo de pellet. */
#define POINTS_DOT    10
#define POINTS_POWER  50

/* ===========================================================================
 * HELPERS DEL LABERINTO
 * =========================================================================== */
static uint8_t cell_is_wall(int r, int c) {
    if (r < 0 || r >= MAZE_H || c < 0 || c >= MAZE_W) return 1;
    return maze_cell((uint8_t)c, (uint8_t)r) == MC_WALL;
}

/* Mascara de borde de una celda de muro (que lados dan a pasillo). */
static uint8_t wall_mask(int r, int c) {
    uint8_t m = 0;
    if (!cell_is_wall(r - 1, c)) m |= 1;
    if (!cell_is_wall(r + 1, c)) m |= 2;
    if (!cell_is_wall(r, c - 1)) m |= 4;
    if (!cell_is_wall(r, c + 1)) m |= 8;
    return m;
}

/* Construye el patron de un tile de borde fino segun la mascara.
 * IMPLEMENTADA EN ASM (walltile.s). */
extern void build_wall_tile(uint8_t mascara, uint8_t *p0, uint8_t *p1);

/* ===========================================================================
 * SPRITE DE PAC-MAN (8x8). Indice 3 de su paleta -> amarillo.
 * Direcciones: boca a la DERECHA (base), IZQUIERDA = FLIP_X, ARRIBA y ABAJO
 * tienen su propio dibujo (boca vertical). Animacion de boca (2 frames).
 * =========================================================================== */
#define SPR_PAC    0
/* Patrones: 0-1 horizontal (cerrada/abierta), 2-3 vertical. */
#define PAT_PAC_H0  0   /* boca derecha, cerrada */
#define PAT_PAC_H1  1   /* boca derecha, abierta */
#define PAT_PAC_V0  2   /* boca arriba,  cerrada */
#define PAT_PAC_V1  3   /* boca arriba,  abierta */

/* Boca horizontal (base, mira a la DERECHA): la cuna abre a la derecha.
 * bit 7 = pixel IZQUIERDO. */
static const uint8_t pac_h0_p0[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t pac_h1_p0[8] = { 0x3C,0x7E,0xFE,0xF8,0xF8,0xFE,0x7E,0x3C };

/* Boca vertical (mira hacia ARRIBA): circulo con la cuña abriendo arriba.
 * Es el MISMO Pac-Man girado 90, dibujado a mano: la abertura esta en el tope
 * (columnas centrales de las primeras filas). bit 7 = pixel IZQUIERDO.
 *   fila0: ........   0x00
 *   fila1: .##..##.   0x66   <- abertura
 *   fila2: ###..###   0xE7
 *   fila3: ########   0xFF
 *   fila4: ########   0xFF
 *   fila5: ########   0xFF
 *   fila6: .######.   0x7E
 *   fila7: ..####..   0x3C */
static const uint8_t pac_v0_p0[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t pac_v1_p0[8] = { 0x00,0x66,0xE7,0xFF,0xFF,0xFF,0x7E,0x3C };

/* Animacion de MUERTE: 4 frames, Pac-Man se abre hasta desaparecer. */
#define PAT_DIE_BASE  4     /* 4..7 */
static const uint8_t pac_die0_p0[8] = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t pac_die1_p0[8] = { 0x7E,0xFF,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t pac_die2_p0[8] = { 0x7E,0xFF,0xFF,0x7E,0x3C,0x3C,0x18,0x00 };
static const uint8_t pac_die3_p0[8] = { 0x18,0x3C,0x3C,0x18,0x00,0x00,0x00,0x00 };

/* ===========================================================================
 * FRUTAS (bonus) - patrones 40..47 (una por nivel).
 * Cada fruta es un sprite 8x8 de HASTA 3 colores, todos de la MISMA paleta
 * (paleta spr 3): indice 1 = verde, 2 = rojo, 3 = naranja. Los dos planos
 * codifican el indice: (plan1_bit << 1) | plan0_bit.
 * =========================================================================== */
#define PAT_FRUIT_BASE  40
#define PAL_FRUIT       VC_SPPAL_3
/* Colores de la paleta de frutas (indices 1..3), en RGB444.
 * Formato de origen AABBGGRR: verde=0xff0bbc00, rojo=0xff0000ff,
 * naranja=0xff07b3ff. Se toman los nibbles altos de R,G,B. */
#define FRUIT_G   VC_RGB444(0x0, 0xB, 0x0)   /* verde   (tallo/hoja) */
#define FRUIT_R   VC_RGB444(0xF, 0x0, 0x0)   /* rojo    */
#define FRUIT_O   VC_RGB444(0xF, 0xB, 0x0)   /* naranja */

/* Cereza */
static const uint8_t fr_cherry_p0[8] = { 0x04,0x0C,0x14,0x24,0x00,0x00,0x00,0x00 };
static const uint8_t fr_cherry_p1[8] = { 0x00,0x00,0x00,0x00,0x6C,0xDE,0xDE,0x6C };
/* Fresa */
static const uint8_t fr_straw_p0[8]  = { 0x3C,0x18,0x1C,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t fr_straw_p1[8]  = { 0x00,0x66,0xC3,0xB5,0xFF,0x5A,0x74,0x18 };
/* Naranja */
static const uint8_t fr_orange_p0[8] = { 0x08,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t fr_orange_p1[8] = { 0x00,0x76,0xEF,0xFF,0xFF,0xFF,0x7E,0x3C };
/* Manzana */
static const uint8_t fr_apple_p0[8]  = { 0x08,0x08,0x10,0x00,0x00,0x00,0x00,0x00 };
static const uint8_t fr_apple_p1[8]  = { 0x00,0x76,0xEF,0xFF,0xFF,0xFF,0x7E,0x24 };
/* Melon */
static const uint8_t fr_melon_p0[8]  = { 0x3C,0x7E,0xFF,0xFF,0xFF,0xFF,0x7E,0x3C };
static const uint8_t fr_melon_p1[8]  = { 0x00,0x66,0x95,0xAB,0xD5,0xA9,0x52,0x3C };
/* Galaxian */
static const uint8_t fr_galax_p0[8]  = { 0x00,0x81,0xA5,0xE7,0xFF,0x7E,0x3C,0x18 };
static const uint8_t fr_galax_p1[8]  = { 0x18,0x3C,0x7E,0x7E,0x7E,0x3C,0x18,0x18 };
/* Campana */
static const uint8_t fr_bell_p0[8]   = { 0x18,0x34,0x2C,0x5E,0xBF,0xFF,0xEF,0x00 };
static const uint8_t fr_bell_p1[8]   = { 0x18,0x34,0x2C,0x5E,0xBF,0xFF,0xFF,0x30 };
/* Llave */
static const uint8_t fr_key_p0[8]    = { 0x00,0x00,0x00,0x18,0x1C,0x18,0x1C,0x18 };
static const uint8_t fr_key_p1[8]    = { 0x18,0x66,0x7E,0x18,0x1C,0x18,0x1C,0x18 };

/* ===========================================================================
 * SPRITE DE FANTASMA (8x8). Cupula + ojos + base ondulada (2 frames).
 * 4 fantasmas, cada uno con su color via INDICE de paleta (patrones 4..11).
 * =========================================================================== */
static const uint8_t gho_a_p0[8] = { 0x3C,0x7E,0xDB,0xFF,0xFF,0xFF,0xFF,0xA5 };
static const uint8_t gho_b_p0[8] = { 0x3C,0x7E,0xDB,0xFF,0xFF,0xFF,0xFF,0x5A };
/* Plano0 a cero: para el INDICE 2 (plano0=0, plano1=dibujo). */
static const uint8_t gho_cero[8]  = { 0,0,0,0,0,0,0,0 };
/* Ojos (fantasma comido): dos pupilas blancas sobre fondo transparente. */
static const uint8_t gho_eyes_p0[8] = { 0x00,0x00,0x00,0x66,0x66,0x00,0x00,0x00 };

/* ===========================================================================
 * SETUP DE VIDEO
 * =========================================================================== */
/* Dibuja SOLO las paredes del laberinto en el tilemap (los pellets los pone
 * level_reset()). Se llama del setup inicial y al reiniciar tras un game over,
 * para restaurar las celdas que tapo el texto de GAME OVER. */
static void draw_maze(void) {
    uint8_t x, y;
    for (y = 0; y < MAZE_H; y++) {
        for (x = 0; x < MAZE_W; x++) {
            uint8_t col = (uint8_t)(MAZE_X0 + x);
            uint8_t rr  = (uint8_t)(MAZE_Y0 + y);
            uint8_t ch = maze_cell(x, y);

            if (ch == MC_WALL) {
                uint8_t m = wall_mask(y, x);
                if (m == 0) {
                    vc_put_cell(col, rr, TILE_BLANK);
                    vc_set_cell_attr(col, rr, PAL_WALL, 0);
                } else {
                    vc_put_cell(col, rr, (uint8_t)(TILE_WALL_BASE + m));
                    vc_set_cell_attr(col, rr, PAL_WALL, VC_ATTR_SOLID);
                }
            } else {
                vc_put_cell(col, rr, TILE_BLANK);
                vc_set_cell_attr(col, rr, PAL_WALL, 0);
            }
        }
    }
}

static void setup_video(void) {
    vc_wait_ready();
    vc_wait_vblank();
    vc_clear_vram();

    /* Fondo negro. */
    vc_set_bgcolor(VC_RGB444(0x0, 0x0, 0x0));

    /* --- Paletas de FONDO --- */
    vc_pal_set_bg(1, 3, VC_RGB444(0xF, 0xC, 0x8));   /* puntos: beige claro */
    vc_pal_set_bg(2, 3, VC_RGB444(0x0, 0x4, 0xF));   /* paredes: azul */

    /* --- Paletas de SPRITE (5 colores con 2 paletas) --- */
    vc_pal_set_spr(0, 3, VC_RGB444(0xF, 0x0, 0x0));  /* paleta 0, color 3: rojo  (F1) */
    vc_pal_set_spr(0, 2, VC_RGB444(0xF, 0x8, 0xC));  /* paleta 0, color 2: rosa  (F2) */
    vc_pal_set_spr(0, 1, VC_RGB444(0x0, 0xF, 0xF));  /* paleta 0, color 1: cian  (F3) */
    vc_pal_set_spr(1, 3, VC_RGB444(0xF, 0xF, 0x0));  /* paleta 1, color 3: amari (Pac) */
    vc_pal_set_spr(1, 2, VC_RGB444(0xF, 0x8, 0x0));  /* paleta 1, color 2: naran (F4) */
    /* Paleta 2: reservada al modo ASUSTADO. Color 3 = azul. (El blanco del
     * parpadeo se consigue cargando el patron con el plano del indice 2, cuyo
     * color fijamos tambien a blanco.) */
    vc_pal_set_spr(2, 3, VC_RGB444(0x0, 0x0, 0xF));  /* paleta 2, color 3: azul   */
    vc_pal_set_spr(2, 2, VC_RGB444(0xF, 0xF, 0xF));  /* paleta 2, color 2: blanco */

    /* Paleta 3: FRUTAS. Una sola paleta para las 8 frutas (verde/rojo/naranja). */
    vc_pal_set_spr(3, 1, FRUIT_G);                   /* paleta 3, color 1: verde   */
    vc_pal_set_spr(3, 2, FRUIT_R);                   /* paleta 3, color 2: rojo    */
    vc_pal_set_spr(3, 3, FRUIT_O);                   /* paleta 3, color 3: naranja */

    /* --- Patrones de tiles --- */
    vc_load_bg_pattern(TILE_DOT,   dot_p0, dot_p1);
    vc_load_bg_pattern(TILE_POWER, pow_p0, pow_p1);
    {
        uint8_t m, p0[8], p1[8];
        for (m = 1; m <= 15; m++) {
            build_wall_tile(m, p0, p1);
            vc_load_bg_pattern((uint8_t)(TILE_WALL_BASE + m), p0, p1);
        }
    }

    /* --- Patron del sprite de Pac-Man (horizontal + vertical + muerte) --- */
    vc_load_spr_pattern(PAT_PAC_H0, pac_h0_p0, pac_h0_p0);
    vc_load_spr_pattern(PAT_PAC_H1, pac_h1_p0, pac_h1_p0);
    vc_load_spr_pattern(PAT_PAC_V0, pac_v0_p0, pac_v0_p0);
    vc_load_spr_pattern(PAT_PAC_V1, pac_v1_p0, pac_v1_p0);
    vc_load_spr_pattern((uint8_t)(PAT_DIE_BASE + 0), pac_die0_p0, pac_die0_p0);
    vc_load_spr_pattern((uint8_t)(PAT_DIE_BASE + 1), pac_die1_p0, pac_die1_p0);
    vc_load_spr_pattern((uint8_t)(PAT_DIE_BASE + 2), pac_die2_p0, pac_die2_p0);
    vc_load_spr_pattern((uint8_t)(PAT_DIE_BASE + 3), pac_die3_p0, pac_die3_p0);

    /* --- Patron del fantasma: 4 fantasmas x 2 frames, cada uno con su color.
     * El INDICE de color se elige con los planos:
     *   idx3 = p0=dib, p1=dib   idx2 = p0=0,   p1=dib   idx1 = p0=dib, p1=0
     *   F0 Blinky (pal.0): idx3 (rojo)    -> pat 16/17
     *   F1 Pinky  (pal.0): idx2 (rosa)    -> pat 18/19
     *   F2 Inky   (pal.0): idx1 (cian)    -> pat 20/21
     *   F3 Clyde  (pal.1): idx2 (naranja) -> pat 22/23
     * ASUSTADO (pal.2): idx3 = azul (24/25), idx2 = blanco (26/27). */
    vc_load_spr_pattern(16, gho_a_p0, gho_a_p0);      /* F0 A idx3 */
    vc_load_spr_pattern(17, gho_b_p0, gho_b_p0);      /* F0 B idx3 */
    vc_load_spr_pattern(18, gho_cero, gho_a_p0);      /* F1 A idx2 */
    vc_load_spr_pattern(19, gho_cero, gho_b_p0);      /* F1 B idx2 */
    vc_load_spr_pattern(20, gho_a_p0, gho_cero);      /* F2 A idx1 */
    vc_load_spr_pattern(21, gho_b_p0, gho_cero);      /* F2 B idx1 */
    vc_load_spr_pattern(22, gho_cero, gho_a_p0);      /* F3 A idx2 (pal.1) */
    vc_load_spr_pattern(23, gho_cero, gho_b_p0);      /* F3 B idx2 (pal.1) */
    vc_load_spr_pattern(24, gho_a_p0, gho_a_p0);      /* asustado A azul   (idx3) */
    vc_load_spr_pattern(25, gho_b_p0, gho_b_p0);      /* asustado B azul   (idx3) */
    vc_load_spr_pattern(26, gho_cero, gho_a_p0);      /* asustado A blanco (idx2) */
    vc_load_spr_pattern(27, gho_cero, gho_b_p0);      /* asustado B blanco (idx2) */
    vc_load_spr_pattern(28, gho_eyes_p0, gho_eyes_p0);/* OJOS (comido) idx3 */

    /* --- Patrones de las FRUTAS (40..47, una por nivel) ---
     * Se cargan en BUCLE desde una tabla de punteros para ahorrar CODE. */
    {
        static const uint8_t *const fr_pat[8][2] = {
            { fr_cherry_p0, fr_cherry_p1 }, { fr_straw_p0, fr_straw_p1 },
            { fr_orange_p0, fr_orange_p1 }, { fr_apple_p0, fr_apple_p1 },
            { fr_melon_p0,  fr_melon_p1  }, { fr_galax_p0, fr_galax_p1 },
            { fr_bell_p0,   fr_bell_p1   }, { fr_key_p0,   fr_key_p1   }
        };
        uint8_t k;
        for (k = 0; k < 8; k++)
            vc_load_spr_pattern((uint8_t)(PAT_FRUIT_BASE + k), fr_pat[k][0], fr_pat[k][1]);
    }

    /* --- Dibujar el laberinto (SOLO las paredes; los pellets los pone
     *     level_reset(), que ademas inicializa el bitmap de puntos). --- */
    draw_maze();

    /* --- HUD (fila 1: la fila 0 se ve desplazada por el pipeline/overscan) ---
     * El HUD completo lo dibujan put_lives()/put_level()/put_score() al
     * arrancar la partida (new_game). Aqui no pintamos texto para no solapar. */
}

/* ===========================================================================
 * PAC-MAN: posicion y movimiento
 * ===========================================================================
 * Movimiento con SUB-PIXEL: avanza px a px, pero el cambio de direccion solo se
 * aplica cuando el personaje esta ALINEADO con el centro de una celda (multiplo
 * de 8). Asi el giro siempre ocurre en el centro de la celda, como el arcade.
 * =========================================================================== */

/* ===========================================================================
 * PUNTOS (pellets)
 * =========================================================================== */

/* Indice de bit de la celda (col,row) del laberinto en dots[]. */
#define DOT_BIT(col, row) ((uint16_t)((row) * MAZE_W + (col)))

static uint8_t dot_alive(uint8_t col, uint8_t row) {
    uint16_t b = DOT_BIT(col, row);
    return (uint8_t)((dots[b >> 3] >> (b & 7)) & 1u);
}

static void dot_clear(uint8_t col, uint8_t row) {
    uint16_t b = DOT_BIT(col, row);
    dots[b >> 3] &= (uint8_t)~(1u << (b & 7));
}

/* Inicializa el bitmap de puntos desde el mapa: marca '.' y 'o'. */
static void dots_init(void) {
    uint8_t x, y;
    for (y = 0; y < MAZE_H; y++) {
        for (x = 0; x < MAZE_W; x++) {
            uint8_t ch = maze_cell(x, y);
            if (ch == MC_DOT || ch == MC_POWER) {
                uint16_t b = DOT_BIT(x, y);
                dots[b >> 3] |= (uint8_t)(1u << (b & 7));
                dots_left++;
            }
        }
    }
}

/* Muestra el score (5 digitos, 00000-65535) en el HUD.
 * Reutiliza txt_put_u16_5 (ASM, en text.o): escribe los digitos por restas
 * SIN gastar CODE en C. */
static void put_score(void) {
    txt_put_u16_5(28, 1, score);   /* col 28, fila 1 (28..32) */
}

/* Come el pellet de la celda (col,row) si lo hay. Devuelve 1 si comio. */
static uint8_t pac_eat(uint8_t col, uint8_t row) {
    uint8_t ch;

    if (col >= MAZE_W || row >= MAZE_H) return 0;
    if (!dot_alive(col, row)) return 0;

    ch = maze_cell(col, row);
    dot_clear(col, row);
    if (dots_left) dots_left--;

    /* Borra la celda en el tilemap (deja el pasillo vacio). */
    vc_put_cell((uint8_t)(MAZE_X0 + col), (uint8_t)(MAZE_Y0 + row), TILE_BLANK);
    vc_set_cell_attr((uint8_t)(MAZE_X0 + col), (uint8_t)(MAZE_Y0 + row), PAL_WALL, 0);

    score = (uint16_t)(score + ((ch == MC_POWER) ? POINTS_POWER : POINTS_DOT));
    dots_eaten += (uint16_t)((ch == MC_POWER) ? POINTS_POWER : POINTS_DOT);

    /* FRUTA: aparece a los puntos del arcade (2 por nivel), una sola vez cada
     * umbral. El contador fruit_spawned evita que reaparezca en bucle. */
    if (fruit_spawned < 2 && dots_eaten >= fruit_at_dots[fruit_spawned]) {
        fruit_spawned++;
        fruit_spawn();
    }

    /* POWER PELLET: activa el modo asustado (fantasmas azules y huidizos). */
    if (ch == MC_POWER) {
        snd_power();
        snd_fright_start();   /* arranca el zumbido de fantasmas asustados */
        fright_timer = fright_frames;
        {
            uint8_t gi;
            for (gi = 0; gi < NGHOSTS; gi++) {
                if (gho_state[gi] != GHO_EATEN) gho_scared[gi] = 1;
            }
        }
        /* Al asustarse, los fantasmas LIBRES invierten su direccion (como el
         * arcade). Los que estan en la casa NO (se quedarian mirando a la
         * pared y atascados). */
        {
            uint8_t gi;
            for (gi = 0; gi < NGHOSTS; gi++) {
                if (gho_state[gi] == GHO_FREE) {
                    gho_dx[gi] = (int8_t)-gho_dx[gi];
                    gho_dy[gi] = (int8_t)-gho_dy[gi];
                }
            }
        }
    } else {
        snd_waka();           /* come un punto: (re)arranca el waka-waka */
    }

    put_score();
    return 1;
}

/* Coloca el sprite en la posicion actual (lo llama cada frame), eligiendo el
 * patron y el FLIP segun la direccion de Pac-Man. Si esta QUIETO (dx=dy=0),
 * usa la direccion PEDIDA (buffer) solo para ORIENTARSE (no lo mueve). */
static void pac_draw(void) {
    uint8_t flags = PAL_PAC;
    uint8_t pat;
    int8_t ddx = pac_face_dx;
    int8_t ddy = pac_face_dy;

    if (ddx == 0 && ddy == 0) { ddx = -1; }   /* por defecto: izquierda */

    if (ddx != 0) {
        /* Horizontal: base "mira a la derecha"; a la izquierda = FLIP_X. */
        pat = ((pac_anim & 4) ? PAT_PAC_H1 : PAT_PAC_H0);
        if (ddx < 0) flags |= VC_SPR_FLIP_X;
    } else {
        /* Vertical: base "mira arriba"; hacia abajo = FLIP_Y. */
        pat = ((pac_anim & 4) ? PAT_PAC_V1 : PAT_PAC_V0);
        if (ddy > 0) flags |= VC_SPR_FLIP_Y;
    }

    vc_sprite_move(SPR_PAC, pac_x, pac_y, flags);
    vc_oam_put(SPR_PAC, VC_OAM_TILE, pat);
}

/* Coloca un frame de la animacion de MUERTE (0..3). */
static void pac_draw_die(uint8_t frame) {
    if (frame > 3) frame = 3;
    vc_sprite_move(SPR_PAC, pac_x, pac_y, PAL_PAC);
    vc_oam_put(SPR_PAC, VC_OAM_TILE, (uint8_t)(PAT_DIE_BASE + frame));
}

static void setup_pac(void) {
    /* Posicion inicial como el ARCADE: justo DEBAJO de la casa de los fantasmas,
     * en el pasillo central (fila 17 de maze), centrado (col 13), mirando a la
     * IZQUIERDA pero QUIETO hasta que el jugador lo mueva. Coordenadas en px. */
    pac_x = (uint16_t)((MAZE_X0 + 13) * 8);
    pac_y = (uint8_t)((MAZE_Y0 + 17) * 8);
    pac_dx = 0;  pac_dy = 0;          /* quieto al empezar */
    pac_next_dx = 0; pac_next_dy = 0; /* sin direccion pedida: no se mueve */
    pac_face_dx = -1; pac_face_dy = 0;/* mira a la izquierda */
    pac_anim = 0;
    pac_draw();
}

/* Aplica la dificultad del nivel actual: velocidad de fantasmas y duracion
 * del modo asustado (del ultimo nivel en adelante, se repiten los valores). */
static void level_apply(void) {
    uint8_t idx = (uint8_t)(level - 1);
    if (idx >= NIVEL_MAX) idx = (uint8_t)(NIVEL_MAX - 1);
    gho_skip_cur  = level_gho_skip[idx];
    fright_frames = level_fright[idx];
}

/* Muestra las vidas en el HUD (fila 1, a la izquierda) como "VIDAS" + numero. */
static void put_lives(void) {
    uint8_t d = lives;
    if (d > 9) d = 9;            /* un solo digito */
    vc_put_str(1, 1, "VIDAS");
    vc_put_char(7, 1, (char)('0' + d));   /* col 7: un espacio tras VIDAS */
}

/* Muestra el nivel actual en el HUD (fila 1). */
static void put_level(void) {
    uint8_t d = level;
    if (d > 9) d = 9;            /* un solo digito por ahora */
    vc_put_str(12, 1, "NIV");
    vc_put_char(16, 1, (char)('0' + d));
}

/* Recoloca a Pac-Man y a los fantasmas (tras morir) sin tocar los puntos. */
static void reset_positions(void) {
    setup_pac();
    gho_setup();
    ready_timer = READY_FRAMES;   /* pausa READY! antes de reanudar */
    ready_drawn = 0;
    ready_wait = 0;               /* tras morir: READY! automatico (temporizado) */
    /* La sirena NO se arranca aqui: lo hace el fin del READY! (al pulsar FIRE
     * o al terminar la pausa), para que suene exactamente al empezar a jugar. */
}

/* Pac-Man ha sido atrapado: resta vida, pausa y respawn, o game over. */
static void pac_die(void) {
    if (lives) lives--;
    put_lives();
    fright_timer = 0;      /* al morir termina el modo asustado */
    fruit_hide();          /* la fruta desaparece al morir (no se hereda) */
    snd_bg_off();          /* durante la muerte: fondo en silencio */
    snd_eyes_stop();       /* por si volvia un ojo */
    snd_waka_stop();
    snd_die();
    if (lives == 0) {
        game_over = 1;
        snd_game_over();
        return;
    }
    death_timer = DEATH_PAUSE;   /* pausa: en el bucle se cuenta y luego resetea */
}

/* ===========================================================================
 * NIVEL
 * =========================================================================== */

/* Reset del nivel: recoloca Pac-Man y reinicia los puntos del mapa. */
static void level_reset(void) {
    uint16_t i;

    /* Limpiar el bitmap de puntos. */
    for (i = 0; i < DOTS_BYTES; i++) dots[i] = 0;
    dots_left = 0;
    dots_eaten = 0;       /* reinicia el contador de la fruta */
    fruit_spawned = 0;    /* sin frutas aparecidas aun */
    fruit_hide();         /* sin fruta al empezar el nivel */

    /* Redibujar los pellets en el tilemap. */
    {
        uint8_t x, y;
        for (y = 0; y < MAZE_H; y++) {
            for (x = 0; x < MAZE_W; x++) {
                uint8_t ch = maze_cell(x, y);
                uint8_t col = (uint8_t)(MAZE_X0 + x);
                uint8_t rr  = (uint8_t)(MAZE_Y0 + y);
                if (ch == MC_DOT) {
                    vc_put_cell(col, rr, TILE_DOT);
                    vc_set_cell_attr(col, rr, PAL_DOT, 0);
                } else if (ch == MC_POWER) {
                    vc_put_cell(col, rr, TILE_POWER);
                    vc_set_cell_attr(col, rr, PAL_DOT, 0);
                }
            }
        }
    }

    /* Marcar los pellets vivos y contarlos. */
    dots_init();

    fright_timer = 0;    /* sin modo asustado al empezar el nivel */
    snd_eyes_stop();     /* sin ojos volviendo */
    setup_pac();
    put_score();
}

/* Titileo de los POWER PELLETS (como el arcade).
 * SOLO hay 4 power pellets (las esquinas), asi que se recorre una TABLA fija de
 * 4 posiciones en vez de las 784 celdas del mapa (esto ultimo era MUY caro y
 * hacia que el juego fuera a saltos cada pocos frames). */
static const uint8_t power_pos[4][2] = {
    { 1,  3}, {26,  3},   /* {col,row} esquinas superiores */
    { 1, 23}, {26, 23}    /* esquinas inferiores */
};

static void power_blink(uint8_t on) {
    uint8_t k;
    for (k = 0; k < 4; k++) {
        uint8_t x = power_pos[k][0];
        uint8_t y = power_pos[k][1];
        if (!dot_alive(x, y)) continue;    /* ya comido */
        vc_put_cell((uint8_t)(MAZE_X0 + x), (uint8_t)(MAZE_Y0 + y),
                    on ? TILE_POWER : TILE_BLANK);
        vc_set_cell_attr((uint8_t)(MAZE_X0 + x), (uint8_t)(MAZE_Y0 + y), PAL_DOT, 0);
    }
}

/* ===========================================================================
 * FRUTA (bonus) - un solo sprite, patron segun el nivel.
 * ===========================================================================
 * Slot de OAM dedicado (SPR_FRUIT). Aparece al alcanzar FRUIT_AT_DOTS puntos
 * comidos, en la celda FRUIT_COL/FRUIT_ROW (debajo de la casa). */
#define SPR_FRUIT   5   /* slots 0=Pac, 1..4=fantasmas, 5=fruta */
#define SPR_HUD_FRUIT 6 /* fruta del nivel mostrada en el HUD */

/* Coloca el sprite de la fruta en pantalla. */
static void fruit_draw(void) {
    uint8_t pat = (uint8_t)(PAT_FRUIT_BASE + fruit_type_now());
    vc_sprite_move(SPR_FRUIT, (uint16_t)((MAZE_X0 + FRUIT_COL) * 8),
                   (uint8_t)((MAZE_Y0 + FRUIT_ROW) * 8), PAL_FRUIT);
    vc_oam_put(SPR_FRUIT, VC_OAM_TILE, pat);
}

/* Dibuja la fruta del nivel en el HUD (junto a las vidas), columna HUD_FRUIT_COL
 * de la fila 1. Es fija: no depende de si la fruta del juego esta visible. */
#define HUD_FRUIT_COL  20
static void fruit_draw_hud(void) {
    uint8_t pat = (uint8_t)(PAT_FRUIT_BASE + fruit_type_now());
    vc_sprite_move(SPR_HUD_FRUIT, (uint16_t)(HUD_FRUIT_COL * 8),
                   (uint8_t)(1 * 8), PAL_FRUIT);
    vc_oam_put(SPR_HUD_FRUIT, VC_OAM_TILE, pat);
}

/* Oculta la fruta. */
static void fruit_hide(void) {
    vc_sprite_disable(SPR_FRUIT);
    fruit_on = 0;
    fruit_timer = 0;
}

/* Aparece la fruta (al alcanzar los puntos). */
static void fruit_spawn(void) {
    fruit_on = 1;
    fruit_timer = FRUIT_FRAMES;
    fruit_draw();
}

/* Tick por frame: gestiona temporizador y colision de Pac-Man con la fruta. */
static void fruit_update(void) {
    if (!fruit_on) return;

    /* Colision: Pac-Man ocupa la celda FRUIT_COL/FRUIT_ROW. */
    {
        uint8_t pc = (uint8_t)((pac_x >> 3) - MAZE_X0);
        uint8_t pr = (uint8_t)((pac_y >> 3) - MAZE_Y0);
        if (pc == FRUIT_COL && pr == FRUIT_ROW) {
            score = (uint16_t)(score + fruit_pts[fruit_type_now()]);
            put_score();
            fruit_hide();
            snd_eat_ghost();       /* efecto de "coger bonus" (reutiliza el glup) */
            return;
        }
    }

    /* Temporizador: al agotarse, desaparece. */
    if (fruit_timer) {
        fruit_timer--;
        if (fruit_timer == 0) fruit_hide();
    }
}

/* 1 si la celda del laberinto (col,row) es transitable (no pared y dentro del
 * area jugable). Las celdas FUERA del laberinto (los margenes) se consideran
 * transitable para el tunel lateral... salvo arriba/abajo. */
static uint8_t cell_passable(int col, int row) {
    /* Fuera del laberinto en vertical -> no. */
    if (row < 0 || row >= MAZE_H) return 0;
    /* Tunel: la fila central sale por los lados (col fuera del laberinto). */
    if (col < 0 || col >= MAZE_W) {
        return (row == 14);   /* fila del tunel */
    }
    return maze_cell((uint8_t)col, (uint8_t)row) != MC_WALL;
}

/* Celdas de la CASA de los fantasmas: la puerta (fila 12, cols 13-14) y el
 * interior (filas 13-15, cols 11-16). Pac-Man NO puede entrar ahi; los
 * fantasmas y los ojos SI. */
static uint8_t cell_ghost_house(int col, int row) {
    if (row == 12 && col >= 13 && col <= 14) return 1;        /* puerta */
    if (row >= 13 && row <= 15 && col >= 11 && col <= 16) return 1; /* interior */
    return 0;
}

/* Transitable para PAC-MAN: como cell_passable, pero sin poder entrar a la casa. */
static uint8_t cell_pac_passable(int col, int row) {
    if (cell_ghost_house(col, row)) return 0;
    return cell_passable(col, row);
}

/* Mueve a Pac-Man 1 frame. */
static void pac_step(void) {
    /* Celda actual (pixel -> celda del laberinto). */
    uint8_t col = (uint8_t)((pac_x >> 3) - MAZE_X0);
    uint8_t row = (uint8_t)((pac_y >> 3) - MAZE_Y0);

    /* Alineado con el centro de la celda? (las posiciones son multiplos de 8
     * porque arrancamos alineados y la velocidad es 1). */
    if (((pac_x & 7) == 0) && ((pac_y & 7) == 0)) {
        /* COMER: si hay pellet en la celda actual, comerselo. */
        pac_eat(col, row);

        /* Intentar el giro PEDIDO si la celda destino es transitable. */
        if (pac_next_dx || pac_next_dy) {
            int nc = (int)col + pac_next_dx;
            int nr = (int)row + pac_next_dy;
            if ((pac_next_dx == 0 && pac_next_dy == 0) ||
                cell_pac_passable(nc, nr)) {
                pac_dx = pac_next_dx;
                pac_dy = pac_next_dy;
            }
        }
        /* Si la direccion ACTUAL choca con pared (o la casa), parar. */
        if (!cell_pac_passable((int)col + pac_dx, (int)row + pac_dy)) {
            return;   /* quieto: no avanza */
        }
    }

    /* Avanzar segun la direccion actual. */
    pac_x = (uint16_t)((int16_t)pac_x + pac_dx * PAC_SPEED);
    pac_y = (uint8_t)((int8_t)pac_y + pac_dy * PAC_SPEED);

    /* Actualizar la orientacion del sprite con la direccion en la que va. */
    if (pac_dx || pac_dy) { pac_face_dx = pac_dx; pac_face_dy = pac_dy; }

    /* Tunel lateral: al SALIR por un borde del laberinto, reaparecer por el
     * otro. El muñeco recorre como mucho 1 celda fuera del laberinto (para que
     * se vea "entrar") y reaparece 1 celda antes del borde opuesto. */
    {
        int16_t left_px  = (int16_t)(MAZE_X0 * 8);              /* 48 */
        int16_t right_px = (int16_t)((MAZE_X0 + MAZE_W) * 8);   /* 272 */
        if (pac_x < (uint16_t)(left_px - 8)) {
            pac_x = (uint16_t)(right_px - 1);   /* entra por la derecha */
        } else if (pac_x > (uint16_t)(right_px + 7)) {
            pac_x = (uint16_t)left_px;           /* entra por la izquierda */
        }
    }
}

/* ===========================================================================
 * FANTASMAS (los 4 clasicos) - Fase 4b
 * ===========================================================================
 * Slots de OAM: 1..4 (Pac-Man usa el 0). Posiciones iniciales: dentro de la
 * casa central (filas 13-15, columnas 10-17), ligeramente separados entre si. */
static const uint8_t gho_start_col[NGHOSTS] = { 13, 14, 12, 15 };
static const uint8_t gho_start_row[NGHOSTS] = { 14, 14, 15, 15 };

static void gho_draw(uint8_t i) {
    uint8_t slot = (uint8_t)(1 + i);

    /* COMIDO: solo los OJOS, volviendo a la casa. */
    if (gho_state[i] == GHO_EATEN) {
        vc_sprite_move(slot, gho_x[i], gho_y[i], gho_pal[i]);
        vc_oam_put(slot, VC_OAM_TILE, 28);
        return;
    }

    if (fright_timer && gho_scared[i] && gho_state[i] == GHO_FREE) {
        uint8_t blink = (uint8_t)((fright_timer < FRIGHT_BLINK) &&
                                  ((fright_timer >> 3) & 1));
        uint8_t pat = blink
            ? (uint8_t)((gho_anim[i] & 4) ? 27 : 26)   /* blanco */
            : (uint8_t)((gho_anim[i] & 4) ? PAT_FRIGHT_B : PAT_FRIGHT_A); /* azul */
        vc_sprite_move(slot, gho_x[i], gho_y[i], PAL_FRIGHT);
        vc_oam_put(slot, VC_OAM_TILE, pat);
        return;
    }

    {
        uint8_t pal = gho_pal[i];
        uint8_t pat = (uint8_t)((gho_anim[i] & 4) ? gho_pat_b[i] : gho_pat_a[i]);
        vc_sprite_move(slot, gho_x[i], gho_y[i], pal);
        vc_oam_put(slot, VC_OAM_TILE, pat);
    }
}

static void gho_setup(void) {
    uint8_t i;
    for (i = 0; i < NGHOSTS; i++) {
        gho_x[i] = (uint16_t)((MAZE_X0 + gho_start_col[i]) * 8);
        gho_y[i] = (uint8_t)((MAZE_Y0 + gho_start_row[i]) * 8);
        gho_dx[i] = 0;
        gho_dy[i] = -1;          /* mirando arriba (a salir de la casa) */
        gho_anim[i] = (uint8_t)(i * 2);   /* desfase de animacion */
        gho_state[i] = GHO_HOME;
        gho_wait[i] = gho_home_wait0[i];
        gho_skip[i] = 0;
        gho_scared[i] = 0;
        gho_draw(i);
    }
}

/* ---------------------------------------------------------------------------
 * IA: objetivo (target) de cada fantasma, como el arcade.
 * ---------------------------------------------------------------------------
 *   Blinky (F0): persigue DIRECTAMENTE a Pac-Man.
 *   Pinky  (F1): apunta 4 celdas DELANTE de Pac-Man (emboscada).
 *   Inky  (F2): combina Pac-Man y Blinky (el clasico "2 celdas delante de Pac,
 *               el vector desde Blinky doblado").
 *   Clyde (F3): persigue, pero si esta CERCA de Pac se aleja a su esquina.
 *
 * Devuelve el objetivo en *tx,*ty (celdas del laberinto, pueden salirse de
 * rango: se usa la distancia, no se accede al mapa con el).
 * IMPLEMENTADA EN ASM (gho_ai.s), verificada con sim65 contra esta misma
 * logica en C (0 discrepancias en 409.600 casos). */
extern void gho_target(uint8_t i, int16_t *tx, int16_t *ty);

/* Elige la direccion que MAS ACERCA al objetivo (o la primera valida si
 * use_target=0), sin invertir la direccion actual. Actualiza gho_dx/gho_dy[i].
 * IMPLEMENTADA EN ASM (gho_ai.s). */
extern uint8_t gho_choose_dir_i(uint8_t i, uint8_t col, uint8_t row,
                                uint8_t use_target, int16_t tx, int16_t ty);

/* Mueve el fantasma i un frame. */
static void gho_step(uint8_t i) {
    uint8_t col = (uint8_t)((gho_x[i] >> 3) - MAZE_X0);
    uint8_t row = (uint8_t)((gho_y[i] >> 3) - MAZE_Y0);
    uint8_t centered = (uint8_t)(((gho_x[i] & 7) == 0) && ((gho_y[i] & 7) == 0));

    /* --- COMIDO: solo ojos, volviendo a la casa --- */
    if (gho_state[i] == GHO_EATEN) {
        /* Renace solo cuando esta DENTRO del interior de la casa
         * (filas 13-15, cols 12-15), no antes. */
        if (row >= 13 && row <= 15 && col >= 12 && col <= 15) {
            gho_state[i] = GHO_HOME;
            gho_scared[i] = 0;    /* revive NORMAL (aunque el fright siga) */
            gho_wait[i] = 20;
            gho_dx[i] = 0;
            gho_dy[i] = -1;
            return;
        }
        /* Navega hacia el INTERIOR de la casa (fila 14 = centro). */
        if (centered) {
            gho_choose_dir_i(i, col, row, 1, 13, 14);
        } else if (gho_dx[i] == 0 && gho_dy[i] == 0) {
            gho_dy[i] = 1;   /* seguridad: que algo avance (hacia abajo) */
        }
        /* Los ojos vuelan de vuelta: 1 px/frame (ya es rapido, pues los
         * fantasmas normales avanzan 1 de cada 2 frames). Configurable. */
        gho_x[i] = (uint16_t)((int16_t)gho_x[i] + gho_dx[i] * EYES_SPEED);
        gho_y[i] = (uint8_t)((int8_t)gho_y[i] + gho_dy[i] * EYES_SPEED);
        return;
    }

    /* --- Estado de la casa --- */
    if (gho_state[i] == GHO_HOME) {
        /* Quieto dentro de la casa mientras cuenta el temporizador. */
        if (gho_wait[i]) { gho_wait[i]--; return; }
        gho_state[i] = GHO_LEAVE;
    }

    if (gho_state[i] == GHO_LEAVE) {
        /* Va hacia la puerta (salir): objetivo = celda sobre la puerta. */
        if (centered) {
            int gtx = GHO_GATE_COL;
            int gty = GHO_GATE_ROW - 1;          /* una celda encima de la puerta */
            gho_choose_dir_i(i, col, row, 1, gtx, gty);
        }
        /* Al llegar arriba (a la altura de la celda sobre la puerta), LIBRE. */
        if (row <= (uint8_t)(GHO_GATE_ROW - 1)) {
            gho_state[i] = GHO_FREE;
        }
    }

    if (gho_state[i] == GHO_FREE) {
        if (centered) {
            int tx, ty;
            /* Decidir en el CENTRO de cada celda: elegir la direccion que mas
             * acerca al objetivo, sin invertir. Al no poder invertir, no
             * titubea en pasillos rectos (seguira recto si es lo que acerca).
             * Esto evita el conteo de salidas (que era caro y rompia el VBLANK). */
            gho_target(i, &tx, &ty);
            gho_choose_dir_i(i, col, row, 1, tx, ty);

            /* ASUSTADO: HUIR = alejarse de Pac-Man (esquina opuesta lejana). */
            if (fright_timer) {
                int fx = ((int)((pac_x >> 3) - MAZE_X0) < 14) ? 27 : 0;
                int fy = ((int)((pac_y >> 3) - MAZE_Y0) < 14) ? 27 : 0;
                gho_choose_dir_i(i, col, row, 1, fx, fy);
            }
        }
    }

    /* --- Avance de posicion con velocidad reducida ---
     * Los fantasmas avanzan 1 px, pero se saltan GHO_SKIP frames de cada
     * (GHO_SKIP+1). La decision de direccion se hace igual todos los frames
     * (barata) para no perder los cruces por el salto. */
    if (gho_skip[i] == 0) {
        gho_x[i] = (uint16_t)((int16_t)gho_x[i] + gho_dx[i] * GHO_SPEED);
        gho_y[i] = (uint8_t)((int8_t)gho_y[i] + gho_dy[i] * GHO_SPEED);
        gho_skip[i] = gho_skip_cur;
    } else {
        gho_skip[i]--;
    }

    {
        int16_t left_px  = (int16_t)(MAZE_X0 * 8);
        int16_t right_px = (int16_t)((MAZE_X0 + MAZE_W) * 8);
        if (gho_x[i] < (uint16_t)(left_px - 8)) {
            gho_x[i] = (uint16_t)(right_px - 1);
        } else if (gho_x[i] > (uint16_t)(right_px + 7)) {
            gho_x[i] = (uint16_t)left_px;
        }
    }
}

/* ===========================================================================
 * COLISION Pac-Man <-> fantasma
 * ===========================================================================
 * Colision por caja (AABB): dos sprites 8x8 chocan si sus rectangulos se
 * solapan. Solo cuentan los fantasmas LIBRES (los de la casa no).
 * Si el fantasma esta ASUSTADO, Pac-Man se lo come; si no, Pac-Man muere. */
/* Busca un fantasma que solape con Pac-Man. Devuelve el INDICE (0..3) o 0xFF
 * si ninguno. Solo cuentan los fantasmas LIBRES (los de la casa no). */
#define NO_GHOST 0xFF
static uint8_t pac_hits_ghost(void) {
    uint8_t i;
    for (i = 0; i < NGHOSTS; i++) {
        if (gho_state[i] != GHO_FREE) continue;
        {
            uint16_t gx = gho_x[i];
            uint8_t  gy = gho_y[i];
            if (pac_x + 8 <= gx) continue;
            if (pac_x >= gx + 8) continue;
            if ((uint8_t)(pac_y + 8) <= gy) continue;
            if (pac_y >= (uint8_t)(gy + 8)) continue;
            return i;   /* solapan */
        }
    }
    return NO_GHOST;
}

/* Pac-Man se COME un fantasma asustado: suma puntos y lo manda a casa. */
static void gho_eaten(uint8_t i) {
    score = (uint16_t)(score + GHOST_EAT_PTS);
    put_score();
    snd_eat_ghost();
    /* Se convierte en OJOS y vuelve a la casa. CONSERVA su direccion actual
     * (no la pone a 0): asi sigue avanzando hasta alinearse con una celda y
     * poder decidir. Si la pusieramos a 0 no se moveria nunca. */
    gho_state[i] = GHO_EATEN;
    if (gho_dx[i] == 0 && gho_dy[i] == 0) {
        /* Caso raro: sin direccion -> forzar hacia arriba para arrancar. */
        gho_dy[i] = -1;
    }
}

/* ===========================================================================
 * ENTRADA
 * ===========================================================================
 * Joystick Atari + teclado UART en paralelo. 'q' sale al monitor.
 * La direccion pedida se guarda en el buffer (pac_next_*) y se aplica en el
 * centro de la celda (durante el proximo paso). */
static void set_dir(int8_t dx, int8_t dy) {
    pac_next_dx = dx;
    pac_next_dy = dy;
}

static void read_input(void) {
    uint8_t j = joy_read();

    /* El joystick mantiene la direccion mientras se empuja. Se da prioridad al
     * eje horizontal (como en Snake) para que las diagonales sean predecibles. */
    if (j & JOY_A_LEFT)       set_dir(-1, 0);
    else if (j & JOY_A_RIGHT) set_dir( 1, 0);
    else if (j & JOY_A_UP)    set_dir( 0, -1);
    else if (j & JOY_A_DOWN)  set_dir( 0,  1);

    while (rom_uart_rx_ready()) {
        char c = rom_uart_getc();
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);   /* a MAYUSCULA */
        switch (c) {
            case 'A': case 'J': set_dir(-1, 0); break;
            case 'D': case 'L': set_dir( 1, 0); break;
            case 'W': case 'I': set_dir( 0,-1); break;
            case 'S': case 'K': set_dir( 0, 1); break;
            case 'Q': quit_flag = 1; break;
            default: break;
        }
    }
}

/* 1 si el jugador quiere EMPEZAR la partida desde el READY!: pulsa FIRE o
 * empuja el joystick en cualquier direccion. */
#define START_MASK (JOY_A_FIRE | JOY_A_LEFT | JOY_A_RIGHT | JOY_A_UP | JOY_A_DOWN)

/* Vacia cualquier byte pendiente en el buffer UART. Se llama AL ARRANCAR, antes
 * del bucle: al cargar el .bin por el monitor quedan bytes en el buffer de
 * recepcion (basura), y si uno fuese 'q' haria salir el juego de inmediato
 * (main retorna -> jmp $8000 -> vuelve el monitor). Ver doc/TIPS-Y-TRAMPA.md. */
static void flush_uart(void) {
    while (rom_uart_rx_ready()) {
        (void)rom_uart_getc();
    }
}

/* ===========================================================================
 * MAIN
 * =========================================================================== */

/* Arranca una partida nueva: puntuacion, vidas, nivel y fantasmas.
 * Reutilizable desde el game over (FIRE para volver a jugar). */
static void new_game(void) {
    score = 0;
    lives = PAC_LIVES0;
    extra_life = 0;            /* la vida extra de los 10000 aun no se ha dado */
    level = 1;                  /* empieza en el nivel 1 */
    game_over = 0;
    go_drawn = 0;
    fire_held = 0;
    death_timer = 0;
    ready_timer = READY_FRAMES;   /* pausa READY! antes de jugar */
    ready_drawn = 0;
    ready_wait = 1;               /* al empezar partida: espera FIRE/direccion */
    ready_rel = 1;                /* y que la entrada este suelta primero */
    fright_timer = 0;
    snd_silence();
    level_apply();              /* velocidad y fright del nivel 1 */

    draw_maze();        /* redibuja paredes (borra el texto de GAME OVER) */
    level_reset();      /* dibuja pellets, inicializa el bitmap y coloca a Pac */
    gho_setup();        /* coloca los fantasmas en la casa central */
    put_lives();
    put_level();
    fruit_draw_hud();   /* fruta del nivel en el HUD */
    put_score();
    flush_uart();
}

int main(void) {
    rom_uart_puts("\r\nPAC-MAN\r\n");
    rom_uart_puts("joystick o WASD/flechas; q = salir.\r\n");

    setup_video();
    joy_init();
    snd_init();

    new_game();
    quit_flag = 0;

    for (;;) {
        vc_wait_vblank();
        pac_anim++;
        snd_update();

        if (game_over) {
            /* Pantalla de GAME OVER: se dibuja una sola vez. FIRE empieza otra
             * partida; 'q' por UART vuelve al monitor. */
            if (!go_drawn) {
                go_drawn = 1;
                snd_silence();
                vc_clear_oam();          /* oculta Pac y fantasmas */
                /* Limpia el tilemap (laberinto oculto) para que el texto se
                 * lea limpio sobre fondo negro, sin solaparse con las paredes. */
                vc_fill_tilemap(TILE_BLANK);
                vc_clear_attr();         /* atributos a 0 (negro) */
                vc_put_str(15, 12, "GAME OVER");
                txt_put_u16_5(18, 14, score);   /* marcador final, centrado */
                vc_put_str(11, 16, "FIRE: OTRA PARTIDA");
            }
            /* Esperamos a que SUELTEN el boton y luego lo pulsen, para no
             * reiniciar de inmediato con el mismo pulsado de FIRE. */
            if (fire_held) {
                if (!(joy_read() & JOY_A_FIRE)) fire_held = 0;
            } else {
                read_input();
                if (quit_flag) break;
                if (joy_read() & JOY_A_FIRE) {
                    fire_held = 1;
                    new_game();   /* redibuja el laberinto desde cero */
                }
            }
            vc_wait_vblank_end();
            continue;
        }

        if (ready_timer) {
            /* Pausa READY!: nadie se mueve y los personajes estan OCULTOS
             * (como el arcade: Pac y fantasmas aparecen al quitarse el aviso).
             * El aviso se dibuja al empezar y se borra al terminar; la fila
             * usada esta vacia, asi que debajo no hay puntos que tapar.
             * Si ready_wait: NO expira solo, espera FIRE/direccion del jugador. */
            if (!ready_drawn) {
                uint8_t i;
                vc_sprite_disable(SPR_PAC);
                for (i = 0; i < NGHOSTS; i++) vc_sprite_disable((uint8_t)(1 + i));
                snd_bg_off();   /* sin sirena durante el READY! */
                vc_put_str(READY_COL, READY_ROW, "READY!");
                ready_drawn = 1;
            }

            read_input();
            if (quit_flag) break;

            /* Condicion de fin: contar el temporizador (muerte/nivel) o, al
             * empezar partida, esperar a que el jugador pulse FIRE o mueva. */
            if (ready_wait) {
                /* Si la entrada ya venia pulsada (p.ej. FIRE que arranco el game
                 * over), exigir soltarla primero para no saltar el READY. */
                if (ready_rel) {
                    if (!(joy_read() & START_MASK)) ready_rel = 0;
                    vc_wait_vblank_end();
                    continue;
                }
                if (!(joy_read() & START_MASK)) { vc_wait_vblank_end(); continue; }
            } else {
                ready_timer--;
                if (ready_timer) { vc_wait_vblank_end(); continue; }
            }

            /* Se acaba la pausa: quita el texto, muestra a los personajes y
             * arranca el sonido de fondo (sirena) justo al empezar a jugar. */
            {
                uint8_t i;
                vc_put_str(READY_COL, READY_ROW, "      ");
                pac_draw();
                for (i = 0; i < NGHOSTS; i++) gho_draw(i);
            }
            snd_bg_siren();
            ready_timer = 0;
            vc_wait_vblank_end();
            continue;
        }

        if (death_timer) {
            /* Pausa de MUERTE: Pac-Man se abre/desaparece (4 frames), luego
             * nadie se mueve. La animacion ocupa los primeros 4*(DEATH_PAUSE/4)
             * frames; se mapea el tiempo a un frame de dibujo. */
            {
                uint8_t df;
                if (death_timer > (DEATH_PAUSE - 40)) {
                    df = (uint8_t)((DEATH_PAUSE - death_timer) / 10);  /* 0..3 */
                    pac_draw_die(df);
                } else {
                    vc_sprite_disable(SPR_PAC);
                }
            }
            death_timer--;
            if (death_timer == 0) {
                reset_positions();   /* revivir en la posicion inicial */
            }
            read_input();
            vc_wait_vblank_end();
            if (quit_flag) break;
            continue;
        }

        read_input();
        pac_step();
        pac_draw();
        {
            uint8_t i;
            for (i = 0; i < NGHOSTS; i++) {
                gho_step(i);
                gho_anim[i]++;
                gho_draw(i);
            }
        }

        /* Titileo de los power pellets: alterna visible/oculto. Solo redibuja
         * cuando cambia el estado (cada 15 frames), para no gastar VRAM. */
        {
            uint8_t blink_on = (uint8_t)((pac_anim / 15) & 1);
            static uint8_t blink_prev = 2;   /* 2 = forzar primer dibujado */
            if (blink_on != blink_prev) {
                blink_prev = blink_on;
                power_blink(blink_on);
            }
        }

        /* Fruta: temporizador + colision (si esta visible). */
        fruit_update();

        /* Temporizador del modo asustado. */
        if (fright_timer) {
            fright_timer--;
            if (fright_timer == 0) snd_fright_stop();   /* fin del modo asustado */
        }

        /* Colision Pac-Man <-> fantasma: si el fantasma esta asustado, se lo
         * come; si no, muere. */
        {
            uint8_t gi = pac_hits_ghost();
            if (gi != NO_GHOST) {
                if (fright_timer && gho_scared[gi]) {
                    gho_eaten(gi);     /* comido: +puntos, vuelve a casa */
                } else {
                    pac_die();         /* muerte: -vida, pausa */
                }
            }
        }

        /* Sonido de los OJOS volviendo a casa: suena mientras haya algun
         * fantasma en estado EATEN. */
        {
            uint8_t i, hay_ojos = 0;
            for (i = 0; i < NGHOSTS; i++) {
                if (gho_state[i] == GHO_EATEN) { hay_ojos = 1; break; }
            }
            if (hay_ojos) snd_eyes_start();
            else          snd_eyes_stop();
        }

        /* Vida extra: UNA SOLA VEZ al alcanzar los 10000 puntos (como el arcade).
         * Se mira la decena de millar que dejo put_score en txt_top_digit: asi
         * NO se usa la aritmetica de 16 bits del runtime C (RAM muy justa). */
        if (!extra_life && txt_top_digit) {
            extra_life = 1;
            lives++;
            put_lives();
        }
        /* Nivel completado: se comieron todos los puntos -> SUBIR de nivel
         * (fantasmas mas rapidos y fright mas corto) y reiniciar el nivel. */
        if (dots_left == 0) {
            vc_wait_vblank_end();
            if (level < 250) level++;   /* tope de seguridad */
            level_apply();
            put_level();
            fruit_draw_hud();   /* nueva fruta del nivel en el HUD */
            level_reset();
            gho_setup();
            ready_timer = READY_FRAMES;   /* pausa READY! antes del nuevo nivel */
            ready_drawn = 0;
            ready_wait = 0;               /* nivel nuevo: READY! automatico */
            continue;
        }

        vc_wait_vblank_end();
        if (quit_flag) break;
    }

    /* Salida limpia al monitor: silenciar, apagar los sprites (OAM) y limpiar
     * la VRAM, para que no queden restos del juego en la pantalla del monitor. */
    snd_silence();
    vc_wait_vblank();
    vc_clear_oam();          /* deshabilita los 32 sprites (Pac + fantasmas) */
    vc_set_scroll_x(0);
    vc_set_scroll_y(0);
    vc_clear_vram();         /* limpia tilemap + atributos + patrones */
    vc_wait_vblank_end();
    return 0;
}
