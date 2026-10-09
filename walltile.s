; ============================================================================
; walltile.s - build_wall_tile() en ENSAMBLADOR (Pac-Man)
; ============================================================================
; Genera el PATRON (2 planos de 8 bytes) de una celda de muro segun su MASCARA
; de borde. Migrado de C para ahorrar bytes de CODE (~400 B en cc65).
;
; void build_wall_tile(uint8_t mascara, uint8_t *p0, uint8_t *p1)
;   CONVENCION cc65: p1 (ultimo, ptr) -> A/X ; p0 -> stack (popax) ;
;                    mascara (primero) -> stack (popa).
;
; MASCARA (bitmask): bit0=arriba, bit1=abajo, bit2=izq, bit3=der (lado que da
; a pasillo -> se dibuja una linea fina pegada a ese borde).
;
; El GROSOR y las columnas cizq/cder se fijan aqui, deben coincidir con
; GROSOR_PX de game.c (actualmente 2):
;     4 -> F0 / 0F     3 -> E0 / 07     2 -> C0 / 03     1 -> 80 / 01
; ============================================================================

        .export _build_wall_tile
        .import popa, popax

        .segment "CODE"

GROSOR   = 2
CIZQ     = $C0
CDER     = $03

; --- ZP propia (declarada ARRIBA para que ca65 sepa que es zeropage) ---
        .segment "ZEROPAGE"
        .exportzp wt_p0, wt_p1, wt_mask
wt_p0:   .res 2
wt_p1:   .res 2
wt_mask: .res 1

        .segment "CODE"

_build_wall_tile:
        ; p1 ya esta en A/X -> guardar
        sta     wt_p1
        stx     wt_p1+1
        ; p0 (popax)
        jsr     popax
        sta     wt_p0
        stx     wt_p0+1
        ; mascara (popa)
        jsr     popa
        sta     wt_mask

        ; ---- 1) limpiar los 8 bytes de p0 y p1 ----
        ldy     #7
@clr:
        lda     #0
        sta     (wt_p0),y
        sta     (wt_p1),y
        dey
        bpl     @clr

        ; ---- 2) mascara & 1 -> linea ARRIBA: filas 0..GROSOR-1 = 0xFF ----
        lda     wt_mask
        and     #$01
        beq     @no_top
        ldy     #(GROSOR-1)
@top:
        lda     #$FF
        sta     (wt_p0),y
        sta     (wt_p1),y
        dey
        bpl     @top
@no_top:

        ; ---- 3) mascara & 2 -> linea ABAJO: filas 8-GROSOR..7 = 0xFF ----
        lda     wt_mask
        and     #$02
        beq     @no_bot
        ldy     #7
@bot:
        lda     #$FF
        sta     (wt_p0),y
        sta     (wt_p1),y
        cpy     #(8-GROSOR)
        beq     @no_bot        ; ya pintamos la primera (fila 8-GROSOR)
        dey
        jmp     @bot
@no_bot:

        ; ---- 4) mascara & 4 -> linea IZQUIERDA: OR de CIZQ en las 8 filas ----
        lda     wt_mask
        and     #$04
        beq     @no_left
        ldy     #7
@left:
        lda     (wt_p0),y
        ora     #CIZQ
        sta     (wt_p0),y
        lda     (wt_p1),y
        ora     #CIZQ
        sta     (wt_p1),y
        dey
        bpl     @left
@no_left:

        ; ---- 5) mascara & 8 -> linea DERECHA: OR de CDER en las 8 filas ----
        lda     wt_mask
        and     #$08
        beq     @no_right
        ldy     #7
@right:
        lda     (wt_p0),y
        ora     #CDER
        sta     (wt_p0),y
        lda     (wt_p1),y
        ora     #CDER
        sta     (wt_p1),y
        dey
        bpl     @right
@no_right:
        rts

;
