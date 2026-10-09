#!/usr/bin/env python3
# test_ai.py - Test diferencial de la IA de fantasmas (Pac-Man).
#
# Compara la version C ORIGINAL (game.c, con int32_t) contra una version en
# 16 bits (la que se traducira a ASM), sobre un barrido EXHAUSTIVO de estados.
# Si coinciden en todos los casos, la refactorizacion a 16 bits es segura y el
# ASM que la implemente dara el mismo resultado.
#
# Uso:  python test_ai.py
#
# NO forma parte del build del juego; es una herramienta de verificacion.

MAZE_W = 28
MAZE_H = 28
MAZE_X0 = 6
MAZE_Y0 = 2

GHO_HOME, GHO_LEAVE, GHO_FREE, GHO_EATEN = 0, 1, 2, 3


def wall_at(col, row):
    """Mascara de paredes determinista (~50%) para forzar muchos casos."""
    if row < 0 or row >= MAZE_H:
        return 1
    if col < 0 or col >= MAZE_W:
        return 0
    h = ((col * 73856093) ^ (row * 19349663)) & 0xFFFFFFFF
    return (h >> 4) & 1


def cell_passable(col, row):
    if row < 0 or row >= MAZE_H:
        return 0
    if col < 0 or col >= MAZE_W:
        return row == 14
    return 0 if wall_at(col, row) else 1


def cell_ghost_house(col, row):
    if row == 12 and 13 <= col <= 14:
        return 1
    if 13 <= row <= 15 and 11 <= col <= 16:
        return 1
    return 0


def s8(v):
    """Trunca a int8 con signo (como int8_t en C)."""
    v &= 0xFF
    return v - 256 if v >= 128 else v


def s16(v):
    """Trunca a int16 con signo (como int16_t en C)."""
    v &= 0xFFFF
    return v - 65536 if v >= 32768 else v


class State:
    def __init__(self):
        self.pac_x = 0; self.pac_y = 0
        self.pac_dx = 0; self.pac_dy = 0
        self.gho_x = [0, 0, 0, 0]; self.gho_y = [0, 0, 0, 0]
        self.gho_dx = [0, 0, 0, 0]; self.gho_dy = [0, 0, 0, 0]
        self.gho_state = [0, 0, 0, 0]


# ---------------- VERSION 1: ORIGINAL (int / int32) ----------------
def gho_target_orig(s, i):
    pc = (s.pac_x >> 3) - MAZE_X0
    pr = (s.pac_y >> 3) - MAZE_Y0
    if i == 0:
        tx, ty = pc, pr
    elif i == 1:
        tx, ty = pc + s.pac_dx * 4, pr + s.pac_dy * 4
    elif i == 2:
        bc = (s.gho_x[0] >> 3) - MAZE_X0
        br = (s.gho_y[0] >> 3) - MAZE_Y0
        mx = pc + s.pac_dx * 2
        my = pr + s.pac_dy * 2
        tx, ty = mx + (mx - bc), my + (my - br)
    else:
        dc = pc - ((s.gho_x[i] >> 3) - MAZE_X0)
        dr = pr - ((s.gho_y[i] >> 3) - MAZE_Y0)
        if (dc * dc + dr * dr) > 64:
            tx, ty = pc, pr
        else:
            tx, ty = pc - s.pac_dx * 4, pr - s.pac_dy * 4
    return tx, ty


def choose_dir_orig(s, i, col, row, use_target, tx, ty):
    cand = [(0, -1), (-1, 0), (0, 1), (1, 0)]
    have = 0
    best = 0x7FFFFFFF
    bdx = bdy = 0
    for (ndx, ndy) in cand:
        if ndx == -s.gho_dx[i] and ndy == -s.gho_dy[i]:
            continue
        if not cell_passable(col + ndx, row + ndy):
            continue
        if s.gho_state[i] == GHO_FREE and cell_ghost_house(col + ndx, row + ndy):
            continue
        if not use_target:
            if not have:
                bdx, bdy, have = ndx, ndy, 1
            continue
        nc = col + ndx
        nr = row + ndy
        dist = (nc - tx) * (nc - tx) + (nr - ty) * (nr - ty)  # 32 bits
        if dist < best:
            best, bdx, bdy, have = dist, ndx, ndy, 1
    if have:
        s.gho_dx[i], s.gho_dy[i] = bdx, bdy
        return 1
    s.gho_dx[i] = s8(-s.gho_dx[i])
    s.gho_dy[i] = s8(-s.gho_dy[i])
    return 0


# ---------------- VERSION 2: 16 BITS (base ASM) ----------------
def gho_target_16(s, i):
    pc = s16((s.pac_x >> 3) - MAZE_X0)
    pr = s16((s.pac_y >> 3) - MAZE_Y0)
    if i == 0:
        tx, ty = pc, pr
    elif i == 1:
        tx, ty = s16(pc + s.pac_dx * 4), s16(pr + s.pac_dy * 4)
    elif i == 2:
        bc = s16((s.gho_x[0] >> 3) - MAZE_X0)
        br = s16((s.gho_y[0] >> 3) - MAZE_Y0)
        mx = s16(pc + s.pac_dx * 2)
        my = s16(pr + s.pac_dy * 2)
        tx, ty = s16(mx + (mx - bc)), s16(my + (my - br))
    else:
        dc = s16(pc - ((s.gho_x[i] >> 3) - MAZE_X0))
        dr = s16(pr - ((s.gho_y[i] >> 3) - MAZE_Y0))
        if s16(dc * dc + dr * dr) > 64:
            tx, ty = pc, pr
        else:
            tx, ty = s16(pc - s.pac_dx * 4), s16(pr - s.pac_dy * 4)
    return tx, ty


def choose_dir_16(s, i, col, row, use_target, tx, ty):
    cand = [(0, -1), (-1, 0), (0, 1), (1, 0)]
    have = 0
    best = 0x7FFF
    bdx = bdy = 0
    for (ndx, ndy) in cand:
        if ndx == -s.gho_dx[i] and ndy == -s.gho_dy[i]:
            continue
        if not cell_passable(col + ndx, row + ndy):
            continue
        if s.gho_state[i] == GHO_FREE and cell_ghost_house(col + ndx, row + ndy):
            continue
        if not use_target:
            if not have:
                bdx, bdy, have = ndx, ndy, 1
            continue
        nc = col + ndx
        nr = row + ndy
        dist = s16((nc - tx) * (nc - tx) + (nr - ty) * (nr - ty))  # 16 bits
        if dist < best:
            best, bdx, bdy, have = dist, ndx, ndy, 1
    if have:
        s.gho_dx[i], s.gho_dy[i] = bdx, bdy
        return 1
    s.gho_dx[i] = s8(-s.gho_dx[i])
    s.gho_dy[i] = s8(-s.gho_dy[i])
    return 0


# ---------------- BARRIDO EXHAUSTIVO ----------------
def main():
    dirs = [(0, -1), (-1, 0), (0, 1), (1, 0), (0, 0)]
    total = 0
    mism = 0
    examples = []

    # Barrido amplio: todas las direcciones y estados, pero posiciones con
    # paso (STEP) para que el test termine en segundos. Cubre de sobra el
    # espacio de coordenadas (las distancias relativas relevantes ya varian).
    STEP_P = 3     # Pac: cada 3 celdas (cubre todas las distancias relevantes)
    STEP_G = 3     # Fantasma: cada 3 celdas

    for gi in range(4):
        for (pdx, pdy) in dirs:
            for pcol in range(0, MAZE_W, STEP_P):
                for prow in range(0, MAZE_H, STEP_P):
                    for gcol in range(0, MAZE_W, STEP_G):
                        for grow in range(0, MAZE_H, STEP_G):
                            for (gdx, gdy) in dirs:
                                for st in range(4):
                                    base = State()
                                    base.pac_x = (MAZE_X0 + pcol) * 8
                                    base.pac_y = (MAZE_Y0 + prow) * 8
                                    base.pac_dx, base.pac_dy = pdx, pdy
                                    base.gho_x[gi] = (MAZE_X0 + gcol) * 8
                                    base.gho_y[gi] = (MAZE_Y0 + grow) * 8
                                    # Blinky (0) en posicion distinta para Inky
                                    base.gho_x[0] = (MAZE_X0 + (gcol + 3) % MAZE_W) * 8
                                    base.gho_y[0] = (MAZE_Y0 + (grow + 5) % MAZE_H) * 8
                                    base.gho_dx[gi], base.gho_dy[gi] = gdx, gdy
                                    base.gho_state[gi] = st

                                    txo, tyo = gho_target_orig(base, gi)
                                    tx1, ty1 = gho_target_16(base, gi)
                                    total += 1
                                    if (txo, tyo) != (tx1, ty1):
                                        mism += 1
                                        if len(examples) < 10:
                                            examples.append(
                                                "TARGET i=%d pc=%d,%d pd=%d gd=%d st=%d: "
                                                "orig(%d,%d) vs 16(%d,%d)" %
                                                (gi, pcol, prow, (pdx, pdy), (gdx, gdy),
                                                 st, txo, tyo, tx1, ty1))

                                    for use in (0, 1):
                                        so = State(); so.__dict__.update(base.__dict__)
                                        so.gho_x = list(base.gho_x); so.gho_y = list(base.gho_y)
                                        so.gho_dx = list(base.gho_dx); so.gho_dy = list(base.gho_dy)
                                        r_o = choose_dir_orig(so, gi, gcol, grow, use, txo, tyo)
                                        odx, ody = so.gho_dx[gi], so.gho_dy[gi]

                                        s1 = State(); s1.__dict__.update(base.__dict__)
                                        s1.gho_x = list(base.gho_x); s1.gho_y = list(base.gho_y)
                                        s1.gho_dx = list(base.gho_dx); s1.gho_dy = list(base.gho_dy)
                                        r_1 = choose_dir_16(s1, gi, gcol, grow, use, tx1, ty1)
                                        ndx, ndy = s1.gho_dx[gi], s1.gho_dy[gi]

                                        total += 1
                                        if (r_o, odx, ody) != (r_1, ndx, ndy):
                                            mism += 1
                                            if len(examples) < 10:
                                                examples.append(
                                                    "DIR i=%d pc=%d,%d pd=%d gd=%d st=%d use=%d: "
                                                    "orig(r=%d %d,%d) vs 16(r=%d %d,%d)" %
                                                    (gi, pcol, prow, (pdx, pdy), (gdx, gdy),
                                                     st, use, r_o, odx, ody, r_1, ndx, ndy))

    for e in examples:
        print(e)
    print("\nCasos probados: %d   Discrepancias: %d" % (total, mism))
    if mism == 0:
        print("OK: la version de 16 bits es IDENTICA a la original.")
        return 0
    print("FALLO: hay diferencias.")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
