# Herramientas de desarrollo (NO forman parte del build del juego)

Estos archivos son utilidades de desarrollo y verificación. El juego NO depende
de ellos; el `makefile` de la raíz no los usa.

## `gen_fruits.py`
Convierte las matrices 8x8 de las 8 frutas (colores ARGB tal como vienen de la
herramienta de diseño) a los 2 planos de bits por fila que espera
`vc_load_spr_pattern(spr, plan0, plan1)`.

Mapeo de color -> indice de paleta (1..3): verde, rojo, naranja.
Genera los arrays `fr_*_p0` / `fr_*_p1` que estan en `game.c` (patrones 40..47).

Uso:  `python gen_fruits.py`

## `test_ai.py`
Test diferencial (en Python, sin compilador) de la logica de IA: compara la
version C original (`int32_t`) contra la version de 16 bits. Sirvio para validar
el cambio `int32` -> `int16` antes de migrar a ASM. 12M casos, 0 discrepancias.

Uso:  `python test_ai.py`

## `test_gho.c` / `test_gho_full.c`
Test diferencial del **ASM real** (`gho_ai.s`) contra la misma logica en C,
ejecutado con **`sim65`** (el emulador de 6502 que trae cc65). `test_gho_full.c`
hace el barrido grande (409.600 casos).

Uso (desde la raiz del proyecto):
```
D:/cc65/bin/cl65.exe -t sim6502 -O -o _tghofull tools/test_gho_full.c gho_ai.s
D:/cc65/bin/sim65.exe _tghofull
```
Salida esperada: `total=409600 mism=0`.

Gracias a este test se detecto (y corrigio) un bug de sign-extension en el
calculo de distancia del ASM ANTES de probarlo en hardware.
