; ============================================================================
; math.s - Aritmetica sin division/multiplicacion por hardware
; ============================================================================
; Rutinas que reemplazan las operaciones de 16/8 bits que cc65 resuelve con
; div.o/udiv.o/umod.o (runtime grande). Como son divisiones por CONSTANTE
; pequeña, se hacen por RESTAS SUCESIVAS, que en ASM ocupan muy poco.
;
; Al no usarse / y % en el codigo C, esos modulos de none.lib dejan de
; enlazarse (ahorro de RAM).
;
; CONVENCION (cc65): igual que video.s / text.s.
;   ultimo parametro en A (o A/X para 16 bits); anteriores por stack.
; ============================================================================

        .export _u8_mod
        .export _u16_div_u8

        .import popa, popax

; ============================================================================
        .segment "ZEROPAGE"
; ============================================================================
m_num_lo:       .res 1
m_num_hi:       .res 1
m_den:          .res 1
m_quo:          .res 1

; ============================================================================
        .segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; uint8_t u8_mod(uint8_t a, uint8_t b)
;   Convencion: popa = a ; A = b
;   Devuelve a % b (por restas). A = resto.
;   Nota: si b == 0 devuelve a (no infinito).
; ----------------------------------------------------------------------------
_u8_mod:
        sta m_den              ; b
        jsr popa
        ; A = a
        ldx m_den
        beq @done              ; b==0 -> devolver a
@loop:
        cmp m_den
        bcc @done              ; a < b -> resto
        sec
        sbc m_den
        jmp @loop
@done:
        ldx #0
        rts

; ----------------------------------------------------------------------------
; uint8_t u16_div_u8(uint16_t num, uint8_t den)
;   Convencion (como video.s): popax = num ; A = den
;   Devuelve num / den (cociente, 8 bits). Por restas de 16 bits.
; ----------------------------------------------------------------------------
_u16_div_u8:
        sta m_den              ; A = den
        jsr popax              ; num (lo=A, hi=X)
        sta m_num_lo
        stx m_num_hi
        lda #0
        sta m_quo
        lda m_den
        bne @loop
        ; den == 0 -> devolver 0 para evitar bucle infinito
        lda #0
        ldx #0
        rts
@loop:
        ; if (num < den) done
        lda m_num_hi
        bne @sub              ; hi != 0 -> num >= 256 >= den (den<=255)
        lda m_num_lo
        cmp m_den
        bcc @done
@sub:
        lda m_num_lo
        sec
        sbc m_den
        sta m_num_lo
        bcs @nb
        dec m_num_hi
@nb:
        inc m_quo
        jmp @loop
@done:
        lda m_quo
        ldx #0
        rts
