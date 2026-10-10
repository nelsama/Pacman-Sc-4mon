; ============================================================================
; text.s - Texto y HUD rapidos para el Core de Video
; ============================================================================
; Reemplaza las rutinas de texto de game.c por versiones en ASM con escritura
; VRAM DIRECTA (sin jsr a vc_put_cell/vc_set_cell_attr) y numeros por RESTAS
; (sin division de 16 bits -> permite eliminar div.o/udiv.o/umod.o).
;
; CONVENCION DE LLAMADA (cc65, sin __fastcall__): igual que video.s.
;   El ULTIMO parametro llega en A (8 bits) o A/X (16 bits).
;   Los anteriores llegan por el stack software, de izquierda a derecha:
;     f(uint8_t a, uint8_t b, uint8_t c)  -> popa=a ; popa=b ; A=c
;     f(uint8_t col, uint8_t row, const char *s) -> popa=col ; popa=row ; A/X=s
;
; COLOR: la fuente del sistema pinta SIEMPRE el color 3; el color lo elige la
; PALETA de la celda. Aqui se usa la paleta 2 (TEXTO_PAL): atributo = 2.
;
; VRAM: el puerto indirecto NO auto-incrementa. Para escribir una celda:
;   $D800 = addr & $FF ; $D801 = area | ((addr>>8)&7) ; $D802 = dato
;   addr = row*64 + col   (tilemap: area=$00, atributos: area=$40)
; ============================================================================

        .export _txt_put_at
        .export _txt_put_u8_1
        .export _txt_put_u16_5
        .export _txt_clear_at
        .export _txt_top_digit    ; decena de millar del ultimo numero dibujado

        .import popa, popax
        .importzp ptr

; --- Registros del core (mismos valores que video.s) ---
VID_ADDR_LO     = $D800
VID_ADDR_HI     = $D801
VID_DATA        = $D802
AREA_TILEMAP    = $00
AREA_ATTR       = $40

TEXTO_PAL       = $02          ; paleta 2 (color 3 = tinta de la fuente)
COL_MAX         = 40           ; columnas visibles (salto de linea)
CH_SPACE        = $20          ; espacio ASCII
CH_ZERO         = '0'          ; 0x30

; ============================================================================
        .segment "ZEROPAGE"
; ============================================================================
txt_col:        .res 1         ; columna actual
txt_row:        .res 1         ; fila actual
txt_sptr:       .res 2         ; puntero a la cadena (para txt_put_at)
txt_tile:       .res 1         ; temporal: tile a escribir
txt_len:        .res 1         ; contador para txt_clear_at
txt_dig:        .res 1         ; digito en curso
txt_v_lo:       .res 1         ; valor 16 bits (lo)
txt_v_hi:       .res 1         ; valor 16 bits (hi)

; ============================================================================
        .segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; calc_txt_cell: fija $D800/$D801 para la celda (txt_col,txt_row) en TILEMAP.
;   addr = row*64 + col ; $D801 = AREA_TILEMAP | ((addr>>8)&7).
;   Destruye A; usa ptr.
; ----------------------------------------------------------------------------
calc_txt_cell:
        lda txt_row
        lsr a
        lsr a
        sta ptr+1              ; ptr_hi = row >> 2
        lda txt_row
        and #$03
        asl a
        asl a
        asl a
        asl a
        asl a
        asl a
        sta ptr                ; (row & 3) << 6
        lda ptr
        clc
        adc txt_col
        sta ptr
        bcc @done
        inc ptr+1
@done:
        lda ptr
        sta VID_ADDR_LO
        lda ptr+1
        ora #AREA_TILEMAP
        sta VID_ADDR_HI
        rts

; ----------------------------------------------------------------------------
; put_tile: escribe el tile (txt_tile) en (txt_col,txt_row) + su ATRIBUTO.
;   Calcula la direccion una vez y escribe tile y atributo.
;   Destruye A; usa ptr.
; ----------------------------------------------------------------------------
put_tile:
        jsr calc_txt_cell
        lda txt_tile
        sta VID_DATA           ; tile
        lda ptr
        sta VID_ADDR_LO        ; misma celda, area ATTR
        lda ptr+1
        ora #AREA_ATTR
        sta VID_ADDR_HI
        lda #TEXTO_PAL
        sta VID_DATA
        rts

; ----------------------------------------------------------------------------
; col_advance: txt_col++ con salto a col 0 / row+1 al pasar de COL_MAX.
;   Destruye A.
; ----------------------------------------------------------------------------
col_advance:
        inc txt_col
        lda txt_col
        cmp #COL_MAX
        bcc @nocarry
        lda #0
        sta txt_col
        inc txt_row
@nocarry:
        rts

; ----------------------------------------------------------------------------
; void txt_put_at(uint8_t col, uint8_t row, const char *s)
;   Convencion: popa = col ; popa = row ; A/X = s (lo/hi)
; ----------------------------------------------------------------------------
_txt_put_at:
        sta txt_sptr
        stx txt_sptr+1
        jsr popa
        sta txt_row
        jsr popa
        sta txt_col
@loop:
        ldy #0
        lda (txt_sptr),y
        beq @end
        sta txt_tile
        jsr put_tile
        jsr col_advance
        ; avanzar puntero de cadena (16 bits)
        inc txt_sptr
        bne @loop
        inc txt_sptr+1
        jmp @loop
@end:
        rts

; ----------------------------------------------------------------------------
; void txt_clear_at(uint8_t col, uint8_t row, uint8_t len)
;   Convencion: popa = col ; popa = row ; A = len
;   Escribe 'len' espacios desde (col,row).
; ----------------------------------------------------------------------------
_txt_clear_at:
        sta txt_len
        jsr popa
        sta txt_row
        jsr popa
        sta txt_col
@loop:
        lda txt_len
        beq @end
        dec txt_len
        lda #CH_SPACE
        sta txt_tile
        jsr put_tile
        inc txt_col
        jmp @loop
@end:
        rts

; ----------------------------------------------------------------------------
; void txt_put_u8_1(uint8_t col, uint8_t row, uint8_t valor)
;   Convencion: popa = col ; popa = row ; A = valor
;   Escribe UN digito = valor % 10, sin division (restas de 10).
; ----------------------------------------------------------------------------
_txt_put_u8_1:
        sta txt_v_lo
        jsr popa
        sta txt_row
        jsr popa
        sta txt_col
        lda txt_v_lo
@mod:
        cmp #10
        bcc @dig
        sec
        sbc #10
        jmp @mod
@dig:
        clc
        adc #CH_ZERO
        sta txt_tile
        jsr put_tile
        rts

; ----------------------------------------------------------------------------
; void txt_put_u16_5(uint8_t col, uint8_t row, uint16_t valor)
;   Convencion: popa = col ; popa = row ; A/X = valor (lo/hi)
;   Escribe 5 digitos (00000-65535) SIN division: primero la decena de millar
;   (restas de 10000, max 6 veces) y luego miles/centenas/decenas/unidades.
;   Deja en _txt_top_digit la decena de millar (0..6), que el juego usa para
;   la vida extra de los 10000 puntos (asi el C no toca aritmetica de 16 bits).
; ----------------------------------------------------------------------------
_txt_put_u16_5:
        sta txt_v_lo
        stx txt_v_hi
        jsr popa
        sta txt_row
        jsr popa
        sta txt_col

        ; --- decenas de millar (restar 10000) ---
        lda #0
        sta txt_dig
@m10000:
        lda txt_v_hi
        cmp #>10000
        bcc @m10000_done
        bne @m10000_sub
        lda txt_v_lo
        cmp #<10000
        bcc @m10000_done
@m10000_sub:
        lda txt_v_lo
        sec
        sbc #<10000
        sta txt_v_lo
        lda txt_v_hi
        sbc #>10000
        sta txt_v_hi
        inc txt_dig
        jmp @m10000
@m10000_done:
        lda txt_dig
        sta _txt_top_digit     ; decena de millar (0..6) para el juego (vida extra)
        jsr put_digit
        jmp txt_u16_body

; ----------------------------------------------------------------------------
; Cuerpo comun: miles/centenas/decenas/unidades.
; ----------------------------------------------------------------------------
txt_u16_body:
        ; --- miles (restar 1000) ---
        lda #0
        sta txt_dig
@m1000:
        lda txt_v_hi
        cmp #>1000
        bcc @m1000_done
        bne @m1000_sub
        lda txt_v_lo
        cmp #<1000
        bcc @m1000_done
@m1000_sub:
        lda txt_v_lo
        sec
        sbc #<1000
        sta txt_v_lo
        lda txt_v_hi
        sbc #>1000
        sta txt_v_hi
        inc txt_dig
        jmp @m1000
@m1000_done:
        jsr put_digit

        ; --- centenas (restar 100, 16 bits: resta de 16 con borrow) ---
        lda #0
        sta txt_dig
@m100:
        lda txt_v_hi
        bne @m100_sub
        lda txt_v_lo
        cmp #100
        bcc @m100_done
@m100_sub:
        lda txt_v_lo
        sec
        sbc #100
        sta txt_v_lo
        bcs @m100_nb
        dec txt_v_hi
@m100_nb:
        inc txt_dig
        jmp @m100
@m100_done:
        jsr put_digit

        ; --- decenas (restar 10) ---
        lda #0
        sta txt_dig
@m10:
        lda txt_v_hi
        bne @m10_sub
        lda txt_v_lo
        cmp #10
        bcc @m10_done
@m10_sub:
        lda txt_v_lo
        sec
        sbc #10
        sta txt_v_lo
        bcs @m10_nb
        dec txt_v_hi
@m10_nb:
        inc txt_dig
        jmp @m10
@m10_done:
        jsr put_digit

        ; --- unidades (v_lo ya < 10) ---
        lda txt_v_lo
        clc
        adc #CH_ZERO
        sta txt_tile
        jsr put_tile
        rts

; ----------------------------------------------------------------------------
; put_digit: escribe txt_dig (0-9) como caracter y avanza columna.
; ----------------------------------------------------------------------------
put_digit:
        lda txt_dig
        clc
        adc #CH_ZERO
        sta txt_tile
        jsr put_tile
        inc txt_col
        rts

; ============================================================================
        .segment "BSS"
; ============================================================================
_txt_top_digit: .res 1         ; decena de millar del ultimo valor dibujado
