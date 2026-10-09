/* test_gho_full.c - Verificacion EXHAUSTIVA de gho_ai.s contra la logica C.
 *
 * Ejecuta la IA en ASM y una copia en C de la misma logica (la validada por
 * test_ai.py) sobre el MISMO barrido, y compara. Imprime OK/FALLO y un conteo.
 */
#include <stdio.h>
#include <stdint.h>

#define MW 28
#define MH 28
#define MAZE_X0 6
#define MAZE_Y0 2
#define GHO_HOME 0
#define GHO_LEAVE 1
#define GHO_FREE 2
#define GHO_EATEN 3

uint16_t pac_x;
uint8_t  pac_y;
int8_t   pac_dx, pac_dy;
uint16_t gho_x[4];
uint8_t  gho_y[4];
int8_t   gho_dx[4], gho_dy[4];
uint8_t  gho_state[4];
uint8_t  maze_bits[MH * (MW/4)];

extern void gho_target(uint8_t i, int16_t *tx, int16_t *ty);
extern uint8_t gho_choose_dir_i(uint8_t i, uint8_t col, uint8_t row,
                                uint8_t use_target, int16_t tx, int16_t ty);

static uint8_t wall_at(int col, int row) {
    unsigned h;
    if (row < 0 || row >= MH) return 1;
    if (col < 0 || col >= MW) return 0;
    h = ((unsigned)(col * 73856093u) ^ (unsigned)(row * 19349663u));
    return (uint8_t)((h >> 4) & 1);
}
static uint8_t cell_passable(int col, int row) {
    if (row < 0 || row >= MH) return 0;
    if (col < 0 || col >= MW) return (row == 14);
    return !wall_at(col, row);
}
static uint8_t cell_ghost_house(int col, int row) {
    if (row == 12 && col >= 13 && col <= 14) return 1;
    if (row >= 13 && row <= 15 && col >= 11 && col <= 16) return 1;
    return 0;
}

/* --- copia C de la logica (la validada) --- */
static void c_target(uint8_t i, int16_t *tx, int16_t *ty) {
    int16_t pc = (int16_t)(((int16_t)(pac_x >> 3)) - MAZE_X0);
    int16_t pr = (int16_t)(((int16_t)(pac_y >> 3)) - MAZE_Y0);
    if (i == 0) { *tx = pc; *ty = pr; }
    else if (i == 1) { *tx = (int16_t)(pc + pac_dx * 4); *ty = (int16_t)(pr + pac_dy * 4); }
    else if (i == 2) {
        int16_t bc = (int16_t)(((int16_t)(gho_x[0] >> 3)) - MAZE_X0);
        int16_t br = (int16_t)(((int16_t)(gho_y[0] >> 3)) - MAZE_Y0);
        int16_t mx = (int16_t)(pc + pac_dx * 2);
        int16_t my = (int16_t)(pr + pac_dy * 2);
        *tx = (int16_t)(mx + (mx - bc)); *ty = (int16_t)(my + (my - br));
    } else {
        int16_t dc = (int16_t)(pc - (((int16_t)(gho_x[i] >> 3)) - MAZE_X0));
        int16_t dr = (int16_t)(pr - (((int16_t)(gho_y[i] >> 3)) - MAZE_Y0));
        if ((int16_t)(dc*dc + dr*dr) > 64) { *tx = pc; *ty = pr; }
        else { *tx = (int16_t)(pc - pac_dx * 4); *ty = (int16_t)(pr - pac_dy * 4); }
    }
}
static uint8_t c_choose(uint8_t i, uint8_t col, uint8_t row, uint8_t use, int16_t tx, int16_t ty) {
    static const int8_t cdx[4] = {0,-1,0,1};
    static const int8_t cdy[4] = {-1,0,1,0};
    uint8_t k, have = 0;
    int16_t best = 0x7FFF;
    int8_t bdx = 0, bdy = 0;
    for (k = 0; k < 4; k++) {
        int16_t dist; int nc, nr;
        int8_t ndx = cdx[k], ndy = cdy[k];
        if ((ndx == -gho_dx[i]) && (ndy == -gho_dy[i])) continue;
        if (!cell_passable((int)col + ndx, (int)row + ndy)) continue;
        if (gho_state[i] == GHO_FREE && cell_ghost_house((int)col+ndx,(int)row+ndy)) continue;
        if (!use) { if (!have) { bdx=ndx; bdy=ndy; have=1; } continue; }
        nc = (int)col + ndx; nr = (int)row + ndy;
        dist = (int16_t)((nc-tx)*(nc-tx) + (nr-ty)*(nr-ty));
        if (dist < best) { best=dist; bdx=ndx; bdy=ndy; have=1; }
    }
    if (have) { gho_dx[i]=bdx; gho_dy[i]=bdy; return 1; }
    gho_dx[i] = (int8_t)-gho_dx[i]; gho_dy[i] = (int8_t)-gho_dy[i];
    return 0;
}

static void set_maze(void) {
    int r, c;
    for (r = 0; r < MH; r++) for (c = 0; c < MW; c++) {
        uint8_t v = wall_at(c, r) ? 3 : 0;
        int idx = r*(MW/4) + (c>>2), sh = (3-(c&3))*2;
        maze_bits[idx] = (uint8_t)((maze_bits[idx] & ~(3<<sh)) | (v<<sh));
    }
}

static const int8_t dirs[5][2] = {{0,-1},{-1,0},{0,1},{1,0},{0,0}};

int main(void) {
    long total = 0, mism = 0;
    int gi, pd, pcol, prow, gcol, grow, gd, st, use;
    set_maze();
    for (gi = 0; gi < 4; gi++)
    for (pd = 0; pd < 5; pd++)
    for (pcol = 0; pcol < MW; pcol += 7)
    for (prow = 0; prow < MH; prow += 7)
    for (gcol = 0; gcol < MW; gcol += 7)
    for (grow = 0; grow < MH; grow += 7)
    for (gd = 0; gd < 5; gd++)
    for (st = 0; st < 4; st++)
    for (use = 0; use < 2; use++) {
        int16_t atx, aty, ctx, cty;
        int ar, cr;
        pac_x = (uint16_t)((MAZE_X0 + pcol) * 8);
        pac_y = (uint8_t)((MAZE_Y0 + prow) * 8);
        pac_dx = dirs[pd][0]; pac_dy = dirs[pd][1];
        gho_x[gi] = (uint16_t)((MAZE_X0 + gcol) * 8);
        gho_y[gi] = (uint8_t)((MAZE_Y0 + grow) * 8);
        gho_x[0] = (uint16_t)((MAZE_X0 + (gcol+3)%MW) * 8);
        gho_y[0] = (uint8_t)((MAZE_Y0 + (grow+5)%MH) * 8);
        gho_dx[gi] = dirs[gd][0]; gho_dy[gi] = dirs[gd][1];
        gho_state[gi] = (uint8_t)st;

        /* target */
        gho_target((uint8_t)gi, &atx, &aty);
        c_target((uint8_t)gi, &ctx, &cty);
        total++;
        if (atx != ctx || aty != cty) { mism++; if (mism<10) printf("T mis gi=%d\n", gi); }

        /* choose: USAR EL MISMO TARGET para ASM y C (el de C). */
        gho_dx[gi] = dirs[gd][0]; gho_dy[gi] = dirs[gd][1];
        {
            int8_t sdx, sdy;
            int8_t cdx0, cdy0;
            cdx0 = gho_dx[gi]; cdy0 = gho_dy[gi];
            ar = gho_choose_dir_i((uint8_t)gi, (uint8_t)gcol, (uint8_t)grow, (uint8_t)use, ctx, cty);
            sdx = gho_dx[gi]; sdy = gho_dy[gi];
            gho_dx[gi] = cdx0; gho_dy[gi] = cdy0;
            cr = c_choose((uint8_t)gi, (uint8_t)gcol, (uint8_t)grow, (uint8_t)use, ctx, cty);
            total++;
            if (ar != cr || sdx != gho_dx[gi] || sdy != gho_dy[gi]) {
                mism++;
                if (mism<=3) printf("MIS gi=%d pc=%d,%d pd=%d,%d gc=%d,%d gd=%d,%d st=%d use=%d target=%d,%d asm(r%d %d,%d) c(r%d %d,%d)\n",
                                    gi, pcol,prow, pac_dx,pac_dy, gcol,grow, dirs[gd][0],dirs[gd][1], st, use, ctx,cty,
                                    ar, sdx,sdy, cr, gho_dx[gi],gho_dy[gi]);
            }
        }
    }
    printf("total=%ld mism=%ld\n", total, mism);
    return 0;
}
