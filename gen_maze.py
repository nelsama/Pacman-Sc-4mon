maze = [
"############################",
"#............##............#",
"#.####.#####.##.#####.####.#",
"#o####.#####.##.#####.####o#",
"#.####.#####.##.#####.####.#",
"#..........................#",
"#.####.##.########.##.####.#",
"#.####.##.########.##.####.#",
"#......##....##....##......#",
"######.##### ## #####.######",
"     #.##### ## #####.#     ",
"     #.##          ##.#     ",
"     #.## ###--### ##.#     ",
"######.## #      # ##.######",
"      .   #      #   .      ",
"######.## #      # ##.######",
"     #.## ######## ##.#     ",
"     #.##          ##.#     ",
"     #.## ######## ##.#     ",
"######.## ######## ##.######",
"#............##............#",
"#.####.#####.##.#####.####.#",
"#.####.#####.##.#####.####.#",
"#o..##.......  .......##..o#",
"###.##.##.########.##.##.###",
"###.##.##.########.##.##.###",
"#......##....##....##......#",
"############################",
]

# Mapeo:  0=vacio(' '), 1=punto('.'), 2=power('o'), 3=pared('#')
# La puerta '--' se trata como VACIO (transitable), igual que antes.
def code(ch):
    if ch == '#': return 3
    if ch == '.': return 1
    if ch == 'o': return 2
    return 0   # ' ' y '-' y cualquier otro = vacio

W = 28
H = 28
assert len(maze) == H
for r,row in enumerate(maze):
    assert len(row) == W, (r, len(row))

# Empaquetar 4 celdas por byte: la celda de mas a la IZQUIERDA en los bits altos.
bytes_out = []
for r in range(H):
    for c in range(0, W, 4):
        b = 0
        for k in range(4):
            b = (b << 2) | code(maze[r][c+k])
        bytes_out.append(b)

# Verificacion: descomprimir y comparar.
def decode(byte, k):   # k=0..3 (0 = celda mas a la izquierda)
    shift = (3-k)*2
    return (byte >> shift) & 3
ok = True
for r in range(H):
    for c in range(W):
        b = bytes_out[r*(W//4) + c//4]
        t = decode(b, c % 4)
        orig = code(maze[r][c])
        if t != orig:
            print("MISMATCH", r, c); ok = False
print("Verificacion:", "OK" if ok else "FALLA")
print("Bytes totales:", len(bytes_out), "(28x28=784 celdas ->", len(bytes_out), "bytes, antes ~840)")

# Emitir como array C, 7 bytes por fila.
print("\nstatic const uint8_t maze_bits[MAZE_H * (MAZE_W/4)] = {")
for r in range(H):
    fila = bytes_out[r*7:(r+1)*7]
    s = ", ".join("0x%02X" % b for b in fila)
    print("    /* %2d */ %s," % (r, s))
print("};")
