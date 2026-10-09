# Tips y trampas — desarrollo de juegos en el Core de Vídeo

Guía práctica para **arrancar un juego nuevo** sobre el **6502** (Tang Nano 9K) con el
**Core de Vídeo (`vc`)**, **cc65** y el **monitor 6502**. Recoge problemas que aparecen
una y otra vez, con **síntoma → causa → solución**.

> Documento **genérico y portable**: los ejemplos concretos (colores, nombres, valores)
> son solo ilustraciones *(ej.)*; adáptalos a tu juego.

---

## 0. Resumen ejecutivo (lo que más duele)

1. **La RAM es el recurso escaso.** El programa y el stack de cc65 salen del mismo bloque.
   Cualquier añadido en C puede desbordar por pocos bytes. **Mide en cada paso.**
2. **Los artefactos pueden ir por detrás del fuente.** La `.lib` y, sobre todo, **el
   bitstream grabado en el FPGA** pueden ser más viejos que el código. Antes de culpar a
   tu lógica, **comprueba versiones** (§6).
3. **La VRAM/OAM y las paletas se escriben SOLO en VBLANK**, o aparecen franjas.
4. **"Tilemap" no es "patrón".** Confundir el *índice* con el *dibujo* es el error de
   dibujo más común.
5. **Un test mínimo vale más que diez hipótesis.** Un binario de 100 bytes que aísla UNA
   cosa ahorra horas.
6. **`main` que retorna = volver al monitor.** Y la **basura del UART** al cargar puede
   hacer que `main` retorne solo (tecla fantasma) → parece un reinicio. Vacía el UART al
   arrancar (§1.4.1).

---

## 1. Puesta en marcha del proyecto

### 1.1 Compila desde el shell nativo del toolchain
cc65 suele ser **`.exe` de Windows** (`D:/cc65/bin/cl65.exe`). Desde un entorno Linux/WSL
no se pueden lanzar (`Error 127: No such file or directory`). Compila desde **Git Bash /
cmd**.

```bash
make CC65_HOME=D:/cc65        # BIEN
```

### 1.2 Decide cómo referenciar la librería
Dos estrategias, elige conscientemente:

- **Proyecto autónomo (recomendado):** copia `vc.lib`, `video.h` y `programa.cfg` dentro
  de tu proyecto y enlaza contra las copias locales. No dependes de rutas relativas.
  **Costo:** al actualizar la lib, recopiar a mano.
- **Referencia directa** al repo de la librería: cómodo para actualizar, frágil si mueves
  carpetas.

Verifica que tu copia local es la correcta y **cómo actualizarla**:
```bash
cmp lib/vc.lib ../videocore-6502-cc65/output/vc.lib && echo identico
cp ../videocore-6502-cc65/output/vc.lib   lib/vc.lib
cp ../videocore-6502-cc65/src/video.h     include/video.h
cp ../videocore-6502-cc65/docs/VIDEO-LIB.md doc/VIDEO-LIB.md
```

### 1.3 Los scripts de build pueden no detectar cambios de configuración
Es habitual que el linker script (`programa.cfg`) **no** figure como dependencia del
makefile. Al editarlo, el link no se rehace y te crees que "no cambió nada".

```bash
rm -f output/juego.bin && make CC65_HOME=D:/cc65   # forzar relink
```

> **Regla general:** ante un "edité y no pasa nada", sospecha de la caché del build.

### 1.4 `main()` retorna → salta al monitor
El `startup.s` típico hace `jsr _main` y, al volver, salta al monitor. **Antes de salir,
deja el estado limpio:** apaga sprites, scroll a 0, silencia el sonido. Si tu programa
corre bajo un launcher/menú, ajusta la dirección de salto a la de tu sistema.

### 1.4.1 ⚠️ Basura en el UART al arrancar → `main` sale solo (reinicio aparente)
**Síntoma:** el juego arranca, se ve un par de segundos, y **vuelve solo al monitor /
launcher** (parece un reinicio del dispositivo, pero no lo es).

**Causa:** al cargar el `.bin` por el monitor, quedan **bytes pendientes en el buffer de
recepción UART** (basura de la carga). Si tu `read_input()` lee la UART y actúa sobre
esos bytes, una basura que coincida con la tecla de salida (p. ej. `'q'`) pone el flag
de salida; el bucle termina, `main` retorna y **`jmp $8000` vuelve al monitor**.

**Solución:** vaciar el UART **antes** de entrar al bucle de juego:

```c
static void flush_uart(void) {
    while (rom_uart_rx_ready()) { (void)rom_uart_getc(); }
}
/* ... tras setup, ANTES del bucle: */
flush_uart();
```

> **Pista de diagnóstico:** si un bucle **sin** salida (ni `read_input`, ni `break`)
> corre estable y el mismo juego con salida se "reinicia", el culpable es la ruta de
> salida (`quit_flag` por UART), no el vídeo ni el stack.

### 1.5 La RAM es de sesión
Los datos en RAM se pierden al salir al monitor. Para persistencia real, usa
almacenamiento externo (p. ej. SD con `mfs_create` / `mfs_write`).

---

## 2. Memoria: el problema recurrente

### 2.1 Conoce tu mapa de memoria ANTES de programar
Localiza la tabla de rangos válidos y **respetados por el monitor/loader**, y las zonas
**reservadas** que NO se deben usar (p. ej. un flag de autoboot en los últimos bytes de la
RAM, o la ZP del monitor).

> **Trampa típica:** cargar el programa en una dirección que "parece libre" pero que el
> monitor usa internamente → **no arranca**. Prueba direcciones alternativas antes de
> volverte loco con la lógica.

### 2.2 Mide el tamaño (y el fin del BSS) en cada iteración
```bash
grep -A9 "^Segment list:" output/juego.map
```
Si el fin del BSS supera el fin de la RAM → `ld65: Segment 'BSS' overflows`.
**Deja siempre varios bytes de margen**: ir a 0 significa que el próximo cambio rompe.

### 2.3 Dónde recortar cuando desborda (por orden de preferencia)
1. **Ajustar el stack de cc65** en el linker script. Suele venir **sobredimensionado**.
   La profundidad real de llamadas de un juego típico es pequeña (< 20-30 bytes).
   *(ej.)* bajar el stack de 56 a 32 B liberó 24 B de RAM. Baja con cuidado y **verifica
   con el listado** que sigues teniendo margen.
2. **Pasar a ASM la función más grande** en C. Mide con el listado (`.proc`/`.endproc`)
   o el mapa.
3. **Evitar que cc65 arrastre `div`/`mod`.** Usar `/` o `%` puede traer
   `div.o`/`udiv.o`/`umod.o`. Sustituye por rutinas por **restas/sumas** donde se pueda.
4. **Reutilizar buffers** solapando tablas que no se usan a la vez.
5. **Cuidado con recortar la lib**: los objetos se enlazan enteros; si usas una función de
   un módulo, las demás del módulo también ocupan ROM. La lib es API pública.

### 2.4 Sospecha SIEMPRE de la RAM antes que del "bug lógico"
Muchos "no arranca" / "se cuelga" / "comportamiento raro" = BSS desbordado o zona de RAM
no disponible.

---

## 3. Gráficos: tiles, patrones, paletas

### 3.1 Rangos de tiles: no confundas la fuente con tus dibujos
La **fuente de texto** ocupa el rango **`$20`-`$7F`** (el índice del tile es el ASCII del
carácter). Tus **tiles propios** van **fuera** de ese rango — usa el espacio libre bajo la
fuente, p. ej. **`$01`-`$1F`**.

**Trampa típica:** escribir un "carácter" con el tile equivocado. `'A'` es `0x41`, no
`0x01`; `'O'` es `0x4F`, no `0x0F`. Usar los bajos equivale a patrones vacíos → **no se ve
nada** y parece un bug del motor.

### 3.2 Codificación del patrón: 2 planos, índices 0-3
Cada patrón son **dos arrays `uint8_t[8]`** (plano 0 y plano 1). Cada byte es una **fila de
8 píxeles**, con **bit 7 = píxel izquierdo**. El índice de color es:

```
índice = (bit_plano1 << 1) | bit_plano0      0..3      (0 = transparente)
```

- Dibujo de **un color** (índice 3): `plano0 = plano1 = dibujo`.
- Dibujo en el **índice 1**: `plano0 = dibujo`, `plano1 = 0x00`.

### 3.3 El color lo da la PALETA, no el patrón
El patrón solo guarda índices 0-3; el RGB lo pone la **paleta**, que es **reprogramable**.
Por eso **no** cambies el patrón para recolorear: cambia la paleta.

```c
vc_pal_set_bg(paleta, indice, VC_RGB444(r,g,b));   /* tiles de fondo */
vc_pal_set_spr(paleta, indice, VC_RGB444(r,g,b));  /* sprites        */
```

Hay 4 paletas de fondo y 4 de sprite. Dos celdas con el **mismo tile** pueden verse de
colores distintos si usan paletas distintas — patrón muy útil.

### 3.4 El texto de la fuente usa SIEMPRE el color 3
La fuente del sistema pinta **solo el color 3** de la paleta de la celda. Para colorear
texto, cambia la **paleta de esa celda**:

```c
vc_set_cell_attr(col, row, paleta, 0);
/* o el helper: */
vc_put_str_pal(col, row, "TEXTO", VC_TINTA(paleta));
```

**Corolario:** un mismo tile (una letra) en paletas distintas = colores distintos.

### 3.5 `BG_COLOR` es global y COMPARTE entrada con la paleta 3
`BG_COLOR` es la **entrada 15** (= "paleta 3, color 3"). Es el color que se ve donde el
tile es **transparente** (índice 0) —p. ej. los **huecos de las letras**— y en el
**margen** de pantalla.

- Para fondo uniforme: `vc_set_bgcolor(rgb444)` **dentro del VBLANK**.
- **Truco:** reserva las paletas **0/1/2** para tu contenido y deja la **3** como
  "fondo/margen".
- Los **sprites no se ven afectados** (usan banco de paleta aparte).
- ⚠️ **Pitfall de versión:** en bitstreams antiguos esto no existía como entrada de
  paleta. Si "no reacciona", sospecha del bitstream (§6), no de tu código.

### 3.6 `vc_clear_vram()` borra los PATRONES (recarga la fuente)
`vc_clear_vram()` usa el **setup por hardware**: limpia tilemap, atributos y OAM, **borra
los patrones** y **vuelve a expandir la fuente** `$20-$7F` (~120-275 µs).

- Carga tus tiles propios **después** de limpiar.
- Si dibujaste en `$20-$7F`, se pierden (ese rango vuelve a ser la fuente).
- Mientras corre (`BUSY`), las escrituras del CPU a VRAM/OAM **se ignoran**; la función
  espera a que termine, así que puedes dibujar justo después.

### 3.7 Orden de arranque del vídeo
```c
vc_wait_ready();        /* esperar VIDEO_READY */
vc_wait_vblank();
vc_clear_vram();        /* limpiar (setup HW) */
vc_set_bgcolor(...);    /* fondo */
/* cargar paletas, cargar patrones, dibujar el mundo/UI */
vc_wait_vblank_end();
```

### 3.8 No heredes estado sucio de otro programa
Al entrar desde un launcher/monitor puede haber **scroll, bandas de raster y paletas** con
valores raros (p. ej. una "línea" horizontal = corte de banda activo). **Fija
explícitamente** al arrancar todo lo que uses: scroll, bandas/raster, paletas.

### 3.9 Respetar el "pipeline" de la fila 0
La **fila 0** del tilemap se ve desplazada por el pipeline. Resérvala para margen/HUD y
empieza el contenido útil una fila más abajo.

---

## 4. Sprites (OAM)

### 4.1 Presupuesto: 32 sprites, máximo 8 por línea
Si pones más de 8 sprites en la **misma línea horizontal**, el core marca `OVERFLOW` y los
sobrantes no se dibujan.

> **Regla práctica:** objetos "pocos y móviles libres" → **sprites**; muchos objetos en
> rejilla, o el cuerpo largo de un personaje → **tiles**.

### 4.2 El espejo/giro con FLIP solo es fiable en SPRITES
Los atributos de tile de fondo documentan bits de flip X/Y, pero **no voltean de forma
fiable**. Los **sprites** sí: usa `VC_SPR_FLIP_X` / `VC_SPR_FLIP_Y` en sus `FLAGS`.

> **Regla:** todo lo que tenga que girar/espejarse, que sea **sprite**. Si un objeto grande
> giratorio no cabe como sprites, dibújalo con **4 patrones** (uno por orientación).

### 4.3 No hay rotación de 90° — solo espejos X/Y
Con 4 direcciones necesitas al menos **un patrón por eje** + flips para el contrario: base
"mira a la derecha" → izquierda = `FLIP_X`; base "mira arriba" → abajo = `FLIP_Y`.

**Trampa sutil:** si tu sprite es **simétrico en un eje**, el flip de ese eje se ve
**idéntico** y parece que "no funciona". Para ese eje necesitas un **dibujo distinto**, no
solo el flip.

### 4.4 Escribe la OAM **dentro del VBLANK** (o los sprites fallan)
Si mueves un sprite **después** de escribir muchas celdas de VRAM, puede que el VBLANK ya
haya acabado → el sprite no se aplica o sale partido.

- Toca OAM/VRAM/scroll **dentro** de `vc_wait_vblank()` … `vc_wait_vblank_end()`.
- En pantallas **estáticas** con sprite, **recolócalo cada frame** para que no se pierda.

### 4.5 Verifica que la librería escribe el valor correcto ANTES de culpar al hardware
Ante un "el hardware ignora X", **primero** comprueba que tu código/librería **escribe X
bien**. Un byte mal pasado puede parecer un bug de HW. Mantén la librería actualizada y
prueba casos simples de forma aislada.

> Hubo un bug real en un helper de OAM: escribía el *índice* en lugar del *dato*, y el
> sprite siempre dibujaba el patrón 0. Se corrigió en la librería.

---

## 5. Entrada y HUD / texto

### 5.1 Tu función de texto: ¿envuelve o recorta?
Si al llegar a la columna 40 salta a la fila siguiente, **cualquier texto que no quepa se
descuadra**. Casi siempre quieres que **recorte**:

```c
while (*s && col < VC_SCREEN_COLS) { /* escribir */ col++; s++; }   /* recorta */
```

### 5.2 Centrar bien
Centra por cálculo, no a ojo: `col = (40 - len) / 2`. Si hay un parpadeo que borra el
texto, **borra la misma zona** que dibujaste (misma col y longitud).

### 5.3 Filas seguras: 1..28
La **fila 0** se ve desplazada por el pipeline y la **fila 29** puede recortarse por
overscan. Mantén texto y UI en las filas **1..28**.

### 5.4 Periféricos mapeados en memoria: usa read-modify-write
Si compartes un registro con otros periféricos (p. ej. bits de un joystick + bits de otro
chip), **no pises** los ajenos: haz RMW y **enmascara al leer**. Un valor fijo escrito de
golpe puede romper otro periférico.

---

## 6. ⚠️ Desfase de artefactos (bitstream / librería) — la trampa que más tiempo cuesta

**Síntoma:** el código es correcto según el manual y la librería, pero **el hardware no
responde** (p. ej. un registro no hace nada).

**Causas posibles:**
- El **bitstream grabado en el FPGA** es más antiguo que el fuente VHDL del repo.
- La **`.lib`** es anterior al fuente de la librería (no se regeneró).

**Cómo diagnosticarlo SIN adivinar:**
1. **Compara fechas**: fuente → objeto (`.o`) → `.lib` → bitstream. El artefacto debe ser
   **posterior** a sus fuentes.
2. Escribe un **test mínimo** que aísle UNA cosa y use un efecto **muy visible** con un
   mecanismo que **sabes** que funciona. Si lo visible funciona pero lo sospechoso no →
   **es desfase de versión**, no tu código.
3. Pregunta al repo del FPGA **a qué commit corresponde el bitstream**.

**Solución:** regenerar y **flashear** el bitstream desde el `main` actualizado.

**Moraleja:** ante un "no funciona y mi código es correcto", **verifica versiones** antes
de tocar la lógica.

### 6.1 Pistas de decodificación que verás
- Si **nada** responde (ni escritura ni paletas): puede que el puerto esté en **otra
  dirección** (la lib a veces tiene un `.ifdef` para tests de host vs. hardware).
- **No uses registros de hardware como contadores** (`inc $REG` para llevar una cuenta):
  corrompe el área/registro y puede colgar el bucle. Usa variables en RAM/ZP.

---

## 7. Sonido (SID)

### 7.1 Registros (SID 6581, mapeado como C64: `$D400-$D41F`)
- `$D400 + 7*voz + 0/1` = frecuencia (lo/hi).
- `+4` = CTRL: `bit0 GATE`, `bits 7:4 forma` (`TRI 0x10`, `SAW 0x20`, `PULSE 0x40`,
  `NOISE 0x80`).
- `+5`/`+6` = attack/decay y sustain/release.
- `$D418` = volumen master + filtro.

**Patrón de efectos "percutivos":** dispara la voz con `GATE=1`, y en un `snd_update()` por
frame decrementa un timer; al llegar a 0, baja el `GATE` (deja que el release apague la
voz). No bloquees el juego esperando al sonido.

### 7.2 Un sonido CONTINUO debe pararse explícitamente al cambiar de estado
Un sonido con **gate abierto** (zumbido, motor, alarma) **se queda pegado** si el bucle
que lo apagaba deja de ejecutarse (al pasar a GAME OVER, pausar o cambiar de escena).

> **Bug clásico:** al terminar la partida con un efecto continuo sonando, la voz se
> quedaba con `GATE=1` para siempre, porque el `update()` por frame ya no corría.
> **Solución:** llamar al `stop` del sonido continuo **al salir de cada estado**.

Reparto de voces: **planifícalo desde el principio** (qué voz hace qué).

### 7.3 Aleatoriedad de verdad: el oscilador de la voz 3 (`$D41B`)
El core expone el **oscilador 3** como fuente de azar. Pon la voz 3 en modo RUIDO **sin
gate** (para que no suene) y lee el registro:

```c
/* Voz 3 en modo ruido, sin GATE (silenciosa) */
escribir(SID_V3 + 1, 0xFF); escribir(SID_V3 + 0, 0xFF);   /* frecuencia alta */
escribir(SID_V3 + 4, 0x80);                               /* WF_NOISE */
uint8_t r = leer(0xD41B);                                 /* bits aleatorios */
escribir(SID_V3 + 4, 0x00);                               /* apagar */
```

**Trampa clásica:** sembrar el RNG con una **constante fija** hace que todo salga
**siempre en el mismo sitio**. **Siembra no determinista:** mezcla `$D41B` con
`rom_get_micros()` (temporizador real) y un contador de arranques.

---

## 8. Depuración: orden de sospechas

1. **¿Compila/enlaza?** Mira `output/juego.map`: tamaños de segmentos y que no desborde la
   RAM.
2. **¿-no funciona- nada aunque edito?** Config del linker sin relink (§1.3).
3. **¿No se ve un tile?** índice fuera de rango; dibujo en `$20-$7F` (fuente); índice 0
   (transparente); atributo con paleta cuyo color es el fondo; celda sin escribir.
4. **¿No se ve un sprite?** OAM escrita **fuera** del VBLANK; `Y` ≥ 248 (deshabilitado);
   >8 en la misma línea (`OVERFLOW`); dato mal escrito a la OAM.
5. **¿Nada reacciona a un cambio de registro?** Antes de culpar al hardware, comprueba que
   la **librería escribe** ese valor (§4.5) — y que el **bitstream** es de la versión
   correcta (§6).
6. **¿Texto descolocado?** envoltura (§5.1) o filas 0/29 (§5.3).
7. **¿Queda un elemento tras cambiar de pantalla?** casi siempre un **sprite que no
   apagaste** o VRAM sin limpiar.
8. **¿Un sonido se queda sonando?** efecto continuo sin `stop` al cambiar de estado (§7.2).
9. **¿Aleatorio repetido?** semilla fija (§7.3).

---

## 9. Checklist para un juego nuevo

- [ ] Proyecto compilable desde el shell correcto (`CC65_HOME=D:/cc65`).
- [ ] Decidido: **lib local copiada o referenciada** (y anotado cómo actualizarla).
- [ ] Verificado que la **`.lib`** y el **bitstream del FPGA** son de la misma época.
- [ ] `.cfg` ajustado: RAM, **stack de cc65** y dirección de carga (sin BSS desbordado).
- [ ] Definidos los **buffers** y su ubicación **antes** de programar.
- [ ] `setup_video()`: `wait_ready` → `vblank` → `clear_vram` → fondo → paletas → patrones.
- [ ] Fondo con `vc_set_bgcolor`; contenido en paletas 0/1/2 (dejar la 3 al margen).
- [ ] Tiles propios fuera de `$20-$7F`; letras dentro de `$20-$7F`.
- [ ] Todo el movimiento de OAM/VRAM/scroll dentro del **VBLANK**.
- [ ] Lo que gira/espeja → **sprite** con `VC_SPR_FLIP_*` (o 4 patrones si es tile).
- [ ] Texto: función que **recorta**, centrado por `(40-len)/2`, filas **1..28**.
- [ ] Reparto de **voces del SID** planificado; sonidos continuos con su `stop`.
- [ ] RNG sembrado con `$D41B` + `rom_get_micros()` (nunca semilla fija).
- [ ] Salida limpia: `vc_clear_oam()` + scroll 0 + silenciar sonido.
- [ ] Recursos dentro de límites: ≤32 sprites (≤8/línea), ≤256 patrones de fondo, ≤64 de
      sprite.
- [ ] Persistencia: la RAM es de sesión; usar SD si se necesita.
- [ ] Probar en **hardware real** pronto (no esperar al final).
- [ ] **Limpiar `output/`** antes de entregar (los `.bin` de tests confunden).

---

## 10. Referencia rápida

### Registros del core
| Registro | Uso |
|---|---|
| `$D800/$D801/$D802` | puerto indirecto VRAM/OAM/patrones |
| `$D803` | STATUS: VBLANK, OVERFLOW, SOLID_HIT, VIDEO_READY |
| `$D804-807` | scroll X/Y |
| `$D809/$D80E` | raster (split en bandas) |
| `$D813-815` | paletas programables (`$D813` = índice de entrada) |
| `$D816/$D817` | setup de VRAM por hardware (disparo / BUSY) |
| `$D400-$D41F` | SID (voz 3: `$D41B` = OSC3 random) |

### Límites y rangos
| Cosa | Valor |
|---|---|
| Pantalla visible | 40×30 celdas (320×240 px) |
| Mapa de fondo | 64×32 celdas |
| Fuente de texto (tile = ASCII) | `$20-$7F` |
| Tiles propios (libres) | `$01-$1F` |
| Filas de texto seguras | `1..28` |
| Patrones de fondo | 256 |
| Patrones de sprite | 64 |
| Sprites (OAM) | 32 (máx. **8 por línea**) |

### Herramientas
```bash
# Compilar (desde Git Bash/cmd, NO WSL)
make CC65_HOME=D:/cc65

# Forzar relink tras editar el linker script
rm -f output/juego.bin && make CC65_HOME=D:/cc65

# Mapa de memoria (¡fin del BSS!)
grep -A9 "^Segment list:" output/juego.map

# Listado ASM con tamaños por función (.proc/.endproc)
D:/cc65/bin/cl65.exe -c -t none -O --cpu 6502 -I include -l build/juego.lst -o /tmp/juego.o juego.c
```
