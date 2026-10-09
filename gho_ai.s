; ============================================================================
; gho_ai.s - IA de los fantasmas (Pac-Man) en ASM
; ============================================================================
; Traduccion a ASM de la version C de 16 bits, validada con test_ai.py
; (12M casos) y verificable con sim65 (emulador de cc65).
;
;   gho_target(i, tx, ty)                      -> objetivo del fantasma i
;   gho_choose_dir_i(i,col,row,use,tx,ty)      -> direccion elegida (1=ok)
;
; Reimplementa cell_passable() y cell_ghost_house() leyendo maze_bits.
;
; CONVENCION cc65: ultimo ptr en A/X, anteriores por stack (popax/popa).
; Todas las temporales van en BSS (la ZEROPAGE esta llena). Los shifts de
; 16 bits se hacen en registros (no hay lsr/rol de memoria en BSS).
; ============================================================================

        .export _gho_target
        .export _gho_choose_dir_i
        .import popa, popax
        .import _pac_x, _pac_y, _pac_dx, _pac_dy
        .import _gho_x, _gho_y, _gho_dx, _gho_dy, _gho_state
        .import _maze_bits

MAZE_W   = 28
MAZE_H   = 28
MAZE_X0  = 6
MAZE_Y0  = 2
MC_WALL  = 3
GHO_FREE = 2

; ----------------------------------------------------------------------------
; ZP propia (punteros indirectos): se declara AL PRINCIPIO para que ca65 sepa
; que son zeropage al ensamblar (ai_tx),y.
; ----------------------------------------------------------------------------
        .segment "ZEROPAGE"
        .exportzp ai_tx, ai_ty
ai_tx:    .res 2
ai_ty:    .res 2

        .segment "CODE"

; ----------------------------------------------------------------------------
; shr3: (ai_pc+1:ai_pc) >>= 3   (16 bits, logico). En registros (BSS no admite
; lsr/ror de memoria en 6502).
; ----------------------------------------------------------------------------
shr3:
        ldx     #3
@l:
        lda     ai_pc+1
        lsr     a
        sta     ai_pc+1
        lda     ai_pc
        ror     a
        sta     ai_pc
        dex
        bne     @l
        rts

; ----------------------------------------------------------------------------
; maze_code: A = codigo (0..3) de (col=ai_c, row=ai_r).
; ----------------------------------------------------------------------------
maze_code:
        ; idx = row*7 + (col>>2)
        lda     ai_r
        sta     ai_tmp
        asl     a
        asl     a
        asl     a              ; row*8
        sec
        sbc     ai_tmp         ; row*7
        sta     ai_tmp
        lda     ai_c
        lsr     a
        lsr     a
        clc
        adc     ai_tmp
        tay
        lda     _maze_bits,y   ; byte empaquetado
        ; shift = (3-(col&3))*2
        sta     ai_tmp         ; guarda byte
        lda     ai_c
        and     #3
        eor     #3
        asl     a              ; shift 0,2,4,6
        beq     @m0
        tax
        lda     ai_tmp
@msh:
        lsr     a
        dex
        bne     @msh
        jmp     @md
@m0:
        lda     ai_tmp
@md:
        and     #3
        rts

; ----------------------------------------------------------------------------
; chk_pass: A = 1 si (ai_c,ai_r) transitable (cell_passable).
; ----------------------------------------------------------------------------
chk_pass:
        lda     ai_r
        cmp     #MAZE_H
        bcs     @no
        lda     ai_c
        cmp     #MAZE_W
        bcc     @in
        lda     ai_r
        cmp     #14
        beq     @yes
@no:
        lda     #0
        rts
@in:
        jsr     maze_code
        cmp     #MC_WALL
        beq     @no
@yes:
        lda     #1
        rts

; ----------------------------------------------------------------------------
; chk_house: A = 1 si (ai_c,ai_r) es la casa (cell_ghost_house).
; ----------------------------------------------------------------------------
chk_house:
        lda     ai_r
        cmp     #12
        bne     @nd
        lda     ai_c
        cmp     #13
        bcc     @nd
        cmp     #15
        bcs     @nd
        lda     #1
        rts
@nd:
        lda     ai_r
        cmp     #13
        bcc     @no
        cmp     #16
        bcs     @no
        lda     ai_c
        cmp     #11
        bcc     @no
        cmp     #17
        bcs     @no
        lda     #1
        rts
@no:
        lda     #0
        rts

; ----------------------------------------------------------------------------
; mul16: ai_prod(16) = ai_mul_a(16) * ai_mul_b(16), low 16 bits (shift-add).
;   Trabaja con los bits de b; no usa shifts de memoria.
; ----------------------------------------------------------------------------
mul16:
        lda     #0
        sta     ai_prod
        sta     ai_prod+1
        ldx     #16
@ml:
        lda     ai_mul_b
        and     #1
        beq     @na
        clc
        lda     ai_prod
        adc     ai_mul_a
        sta     ai_prod
        lda     ai_prod+1
        adc     ai_mul_a+1
        sta     ai_prod+1
@na:
        ; a <<= 1 (16 bits, en registros)
        lda     ai_mul_a
        asl     a
        sta     ai_mul_a
        lda     ai_mul_a+1
        rol     a
        sta     ai_mul_a+1
        ; b >>= 1 (16 bits, en registros)
        lda     ai_mul_b+1
        lsr     a
        sta     ai_mul_b+1
        lda     ai_mul_b
        ror     a
        sta     ai_mul_b
        dex
        bne     @ml
        rts

; ============================================================================
; gho_target(uint8_t i, int16_t *tx, int16_t *ty)
; ============================================================================
_gho_target:
        sta     ai_ty
        stx     ai_ty+1
        jsr     popax
        sta     ai_tx
        stx     ai_tx+1
        jsr     popa
        sta     ai_i

        ; pc = (pac_x >> 3) - 6
        lda     _pac_x
        sta     ai_pc
        lda     _pac_x+1
        sta     ai_pc+1
        jsr     shr3
        sec
        lda     ai_pc
        sbc     #MAZE_X0
        sta     ai_pc
        lda     ai_pc+1
        sbc     #0
        sta     ai_pc+1
        ; pr = (pac_y >> 3) - 2
        lda     _pac_y
        lsr     a
        lsr     a
        lsr     a
        sec
        sbc     #MAZE_Y0
        sta     ai_pr

        lda     ai_i
        beq     @blinky
        cmp     #1
        bne     @q2
        jmp     @pinky
@q2:
        cmp     #2
        bne     @q3
        jmp     @inky
@q3:
        jmp     @clyde

@blinky:
        lda     ai_pc
        sta     ai_ox
        lda     ai_pc+1
        sta     ai_ox+1
        lda     ai_pr
        sta     ai_oy
        lda     #0
        sta     ai_oy+1
        jmp     @store

@pinky:
        lda     _pac_dx
        jsr     mul4sext
        clc
        adc     ai_pc
        sta     ai_ox
        txa
        adc     ai_pc+1
        sta     ai_ox+1
        lda     _pac_dy
        jsr     mul4sext
        clc
        adc     ai_pr
        sta     ai_oy
        txa
        adc     #0
        sta     ai_oy+1
        jmp     @store

@inky:
        ; bc = (gho_x[0]>>3)-6
        lda     _gho_x
        sta     ai_gc
        lda     _gho_x+1
        sta     ai_gc+1
        jsr     shr3gc
        sec
        lda     ai_gc
        sbc     #MAZE_X0
        sta     ai_gc
        lda     ai_gc+1
        sbc     #0
        sta     ai_gc+1
        ; br = (gho_y[0]>>3)-2
        lda     _gho_y
        lsr     a
        lsr     a
        lsr     a
        sec
        sbc     #MAZE_Y0
        sta     ai_br

        ; mx = pc + pac_dx*2 ; my = pr + pac_dy*2
        lda     _pac_dx
        jsr     mul2sext
        clc
        adc     ai_pc
        sta     ai_mx
        txa
        adc     ai_pc+1
        sta     ai_mx+1
        lda     _pac_dy
        jsr     mul2sext
        clc
        adc     ai_pr
        sta     ai_my
        txa
        adc     #0
        sta     ai_my+1

        ; tx = mx + (mx - bc)
        sec
        lda     ai_mx
        sbc     ai_gc
        sta     ai_ox
        lda     ai_mx+1
        sbc     ai_gc+1
        sta     ai_ox+1
        clc
        lda     ai_ox
        adc     ai_mx
        sta     ai_ox
        lda     ai_ox+1
        adc     ai_mx+1
        sta     ai_ox+1
        ; ty = my + (my - br)
        sec
        lda     ai_my
        sbc     ai_br
        sta     ai_oy
        lda     ai_my+1
        sbc     #0
        sta     ai_oy+1
        clc
        lda     ai_oy
        adc     ai_my
        sta     ai_oy
        lda     ai_oy+1
        adc     ai_my+1
        sta     ai_oy+1
        jmp     @store

@clyde:
        ; gc = (gho_x[i]>>3)-6
        lda     ai_i
        asl     a
        tay
        lda     _gho_x,y
        sta     ai_gc
        iny
        lda     _gho_x,y
        sta     ai_gc+1
        jsr     shr3gc
        sec
        lda     ai_gc
        sbc     #MAZE_X0
        sta     ai_gc
        lda     ai_gc+1
        sbc     #0
        sta     ai_gc+1
        ; gr = (gho_y[i]>>3)-2
        ldy     ai_i
        lda     _gho_y,y
        lsr     a
        lsr     a
        lsr     a
        sec
        sbc     #MAZE_Y0
        sta     ai_gr

        ; dc = pc - gc ; dr = pr - gr
        sec
        lda     ai_pc
        sbc     ai_gc
        sta     ai_dc
        lda     ai_pc+1
        sbc     ai_gc+1
        sta     ai_dc+1
        lda     ai_pr
        sec
        sbc     ai_gr
        sta     ai_dr
        lda     #0
        sbc     #0
        sta     ai_dr+1

        ; suma = dc*dc + dr*dr
        lda     ai_dc
        sta     ai_mul_a
        lda     ai_dc+1
        sta     ai_mul_a+1
        sta     ai_mul_b+1
        lda     ai_dc
        sta     ai_mul_b
        jsr     mul16
        lda     ai_prod
        sta     ai_sum
        lda     ai_prod+1
        sta     ai_sum+1
        lda     ai_dr
        sta     ai_mul_a
        lda     ai_dr+1
        sta     ai_mul_a+1
        sta     ai_mul_b+1
        lda     ai_dr
        sta     ai_mul_b
        jsr     mul16
        clc
        lda     ai_sum
        adc     ai_prod
        sta     ai_sum
        lda     ai_sum+1
        adc     ai_prod+1
        sta     ai_sum+1

        ; if suma > 64 -> (pc,pr) else (pc-dx*4, pr-dy*4)
        lda     ai_sum+1
        bne     @cfar
        lda     ai_sum
        cmp     #65
        bcs     @cfar
        lda     _pac_dx
        jsr     mul4sext
        sta     ai_tmp2
        stx     ai_tmp2+1
        sec
        lda     ai_pc
        sbc     ai_tmp2
        sta     ai_ox
        lda     ai_pc+1
        sbc     ai_tmp2+1
        sta     ai_ox+1
        lda     _pac_dy
        jsr     mul4sext
        sta     ai_tmp2
        stx     ai_tmp2+1
        sec
        lda     ai_pr
        sbc     ai_tmp2
        sta     ai_oy
        lda     #0
        sbc     ai_tmp2+1
        sta     ai_oy+1
        jmp     @store
@cfar:
        lda     ai_pc
        sta     ai_ox
        lda     ai_pc+1
        sta     ai_ox+1
        lda     ai_pr
        sta     ai_oy
        lda     #0
        sta     ai_oy+1

@store:
        ldy     #0
        lda     ai_ox
        sta     (ai_tx),y
        iny
        lda     ai_ox+1
        sta     (ai_tx),y
        ldy     #0
        lda     ai_oy
        sta     (ai_ty),y
        iny
        lda     ai_oy+1
        sta     (ai_ty),y
        rts

; shr3gc: (ai_gc+1:ai_gc) >>= 3  (en registros)
shr3gc:
        ldx     #3
@l:
        lda     ai_gc+1
        lsr     a
        sta     ai_gc+1
        lda     ai_gc
        ror     a
        sta     ai_gc
        dex
        bne     @l
        rts

; ----------------------------------------------------------------------------
; sext: A (int8) -> A/X (int16 con signo).
; ----------------------------------------------------------------------------
sext:
        ldx     #0
        cmp     #$80
        bcc     @s
        ldx     #$FF
@s:     rts

; mul4sext: A = (int8)A * 4 -> A/X (16 bits).
mul4sext:
        jsr     sext
        asl     a
        tay
        txa
        rol     a
        tax
        tya
        asl     a
        tay
        txa
        rol     a
        tax
        tya
        rts

; mul2sext: A = (int8)A * 2 -> A/X (16 bits).
mul2sext:
        jsr     sext
        asl     a
        tay
        txa
        rol     a
        tax
        tya
        rts

; ============================================================================
; gho_choose_dir_i(uint8_t i, uint8_t col, uint8_t row, uint8_t use_target,
;                  int16_t tx, int16_t ty)
; ============================================================================
_gho_choose_dir_i:
        sta     ai_ty
        stx     ai_ty+1
        jsr     popax
        sta     ai_tx
        stx     ai_tx+1
        jsr     popa
        sta     ai_use
        jsr     popa
        sta     ai_row
        jsr     popa
        sta     ai_col
        jsr     popa
        sta     ai_i

        lda     #0
        sta     ai_have
        lda     #$FF
        sta     ai_best
        sta     ai_best+1
        sta     ai_bdx
        sta     ai_bdy
        lda     #0
        sta     ai_k

@kloop:
        ldx     ai_k
        lda     cand_dx,x
        sta     ai_ndx
        lda     cand_dy,x
        sta     ai_ndy

        ; reversa?
        ldy     ai_i
        lda     _gho_dx,y
        eor     #$FF
        clc
        adc     #1
        cmp     ai_ndx
        bne     @nrev
        lda     _gho_dy,y
        eor     #$FF
        clc
        adc     #1
        cmp     ai_ndy
        bne     @nrev
        jmp     @next
@nrev:
        ; destino = col+ndx, row+ndy
        lda     ai_col
        clc
        adc     ai_ndx
        sta     ai_c
        lda     ai_row
        clc
        adc     ai_ndy
        sta     ai_r
        jsr     chk_pass
        bne     @p1
        jmp     @next
@p1:
        ldy     ai_i
        lda     _gho_state,y
        cmp     #GHO_FREE
        bne     @okh
        jsr     chk_house
        beq     @okh
        jmp     @next
@okh:
        lda     ai_use
        bne     @dist
        lda     ai_have
        beq     @h1
        jmp     @next
@h1:
        lda     ai_ndx
        sta     ai_bdx
        lda     ai_ndy
        sta     ai_bdy
        lda     #1
        sta     ai_have
        jmp     @next

@dist:
        ; dc = (int16)(col+ndx) - tx  (sign-extend del byte)
        ldx     ai_c
        ldy     #0
        cpx     #$80
        bcc     @spos
        ldy     #$FF
@spos:
        lda     ai_c
        sec
        sbc     ai_tx
        sta     ai_dc
        tya
        sbc     ai_tx+1
        sta     ai_dc+1
        ; dr = (int16)(row+ndy) - ty
        ldx     ai_r
        ldy     #0
        cpx     #$80
        bcc     @rpos
        ldy     #$FF
@rpos:
        lda     ai_r
        sec
        sbc     ai_ty
        sta     ai_dr
        tya
        sbc     ai_ty+1
        sta     ai_dr+1

        ; dist = dc*dc + dr*dr
        lda     ai_dc
        sta     ai_mul_a
        lda     ai_dc+1
        sta     ai_mul_a+1
        sta     ai_mul_b+1
        lda     ai_dc
        sta     ai_mul_b
        jsr     mul16
        lda     ai_prod
        sta     ai_sum
        lda     ai_prod+1
        sta     ai_sum+1
        lda     ai_dr
        sta     ai_mul_a
        lda     ai_dr+1
        sta     ai_mul_a+1
        sta     ai_mul_b+1
        lda     ai_dr
        sta     ai_mul_b
        jsr     mul16
        clc
        lda     ai_sum
        adc     ai_prod
        sta     ai_sum
        lda     ai_sum+1
        adc     ai_prod+1
        sta     ai_sum+1

        ; dist < best ?  (comparacion 16 bits con signo; dist>=0 siempre)
        lda     ai_sum+1
        cmp     ai_best+1
        bcc     @better
        bne     @next
        lda     ai_sum
        cmp     ai_best
        bcs     @next
@better:
        lda     ai_sum
        sta     ai_best
        lda     ai_sum+1
        sta     ai_best+1
        lda     ai_ndx
        sta     ai_bdx
        lda     ai_ndy
        sta     ai_bdy
        lda     #1
        sta     ai_have
@next:
        inc     ai_k
        lda     ai_k
        cmp     #4
        bcs     @end
        jmp     @kloop
@end:
        lda     ai_have
        beq     @rev
        ldy     ai_i
        lda     ai_bdx
        sta     _gho_dx,y
        lda     ai_bdy
        sta     _gho_dy,y
        lda     #1
        ldx     #0
        rts
@rev:
        ldy     ai_i
        lda     _gho_dx,y
        eor     #$FF
        clc
        adc     #1
        sta     _gho_dx,y
        lda     _gho_dy,y
        eor     #$FF
        clc
        adc     #1
        sta     _gho_dy,y
        lda     #0
        ldx     #0
        rts

        .segment "RODATA"
cand_dx: .byte 0, $FF, 0, 1
cand_dy: .byte $FF, 0, 1, 0

        .segment "BSS"
; NOTA: ai_tx/ai_ty NO van aqui (son ZP, arriba).
; gho_target y gho_choose_dir_i NO se llaman entre si: sus temporales se
; SOLAPAN en el mismo bloque (ai_work, 16 bytes). Se declaran como aliases.
;
; Comunes a ambas (no se solapan):
ai_i:     .res 1
ai_tmp:   .res 1
ai_dc:    .res 2
ai_dr:    .res 2
ai_sum:   .res 2
ai_mul_a: .res 2
ai_mul_b: .res 2
ai_prod:  .res 2

ai_work:  .res 16

; --- Aliases de gho_target (offsets del bloque) ---
ai_pc  = ai_work + 0    ; 2 bytes
ai_pr  = ai_work + 2    ; 1
ai_gc  = ai_work + 3    ; 2
ai_gr  = ai_work + 5    ; 1
ai_mx  = ai_work + 6    ; 2
ai_my  = ai_work + 8    ; 2
ai_ox  = ai_work + 10   ; 2
ai_oy  = ai_work + 12   ; 2
ai_tmp2= ai_work + 14   ; 2

; --- Aliases de gho_choose_dir_i (mismo bloque) ---
ai_c   = ai_work + 0
ai_r   = ai_work + 1
ai_col = ai_work + 2
ai_row = ai_work + 3
ai_use = ai_work + 4
ai_k   = ai_work + 5
ai_have= ai_work + 6
ai_ndx = ai_work + 7
ai_ndy = ai_work + 8
ai_bdx = ai_work + 9
ai_bdy = ai_work + 10
ai_best= ai_work + 11   ; 2 bytes (11,12)

ai_br  = ai_gr
