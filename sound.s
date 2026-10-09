; ============================================================================
; sound.s - Driver de sonido SID 6581 (compatible C64) para PAC-MAN
; ============================================================================
; Escritura DIRECTA a los registros del SID (STA $D40x), sin helpers.
;
; Mapa de registros por voz (base $D400 + 7*voz):
;   +0 FREQ_LO  +1 FREQ_HI  +2 PW_LO  +3 PW_HI
;   +4 CTRL (gate/sync/ring/test/waveform)  +5 AD (attack/decay)
;   +6 SR (sustain/release)
;   $D418 = MODE/VOL (bits 3:0 = volumen master)
;
; Formas de onda (bits 7:4 de CTRL): $10 tri, $20 saw, $40 pulse, $80 noise.
; Gate (bit 0) = 1 para que suene.
;
; MODELO: los efectos usan SUSTAIN ALTO y un barrido de frecuencia por software
; (en snd_update). Asi suenan "con cuerpo" (no clicks), como el arcade.
;
; VOCES (Pac-Man):
;   Voz 1 ($D400): waka-waka continuo (comer / moverse).
;   Voz 2 ($D407): power pellet, comer fantasma, muerte, game over.
;   Voz 3 ($D40E): FONDO continuo con 3 estados:
;                    0 = sirena suave de fondo (juego normal)
;                    1 = sonido de fantasmas azules (asustados)
;                    2 = silencio
;
; CONVENCION cc65: funciones sin parametros -> nada por stack/A.
; ============================================================================

        .export _snd_init
        .export _snd_waka
        .export _snd_waka_stop
        .export _snd_power
        .export _snd_eat_ghost
        .export _snd_eyes_start
        .export _snd_eyes_stop
        .export _snd_die
        .export _snd_game_over
        .export _snd_bg_siren
        .export _snd_bg_fright
        .export _snd_bg_off
        .export _snd_fright_start
        .export _snd_fright_stop
        .export _snd_silence
        .export _snd_update

; --- Registros del SID ---
SID_V1   = $D400
SID_V2   = $D407
SID_V3   = $D40E
SID_VOL  = $D418

; --- Formas de onda / gate ---
WF_TRI   = $10
WF_SAW   = $20
WF_PULSE = $40
WF_NOISE = $80
GATE     = $01

; --- Duraciones (frames) ---
WAKA_FRAMES  = 20      ; duracion del waka tras cada punto (se reinicia al comer)
POWER_FRAMES = 45      ; power pellet (barrido ascendente)
EAT_FRAMES   = 18      ; comer fantasma (glup corto)
DIE_FRAMES   = 90      ; muerte (barrido descendente lento, cuadra con la pausa)
OVER_FRAMES  = 180     ; game over

; --- Modos del FONDO (voz 3) ---
BG_SIREN  = 0          ; sirena suave de fondo (juego normal)
BG_FRIGHT = 1          ; sonido de fantasmas azules
BG_OFF    = 2          ; silencio

; ============================================================================
        .segment "CODE"
; ============================================================================

; ----------------------------------------------------------------------------
; void snd_init(void) - silencia las 3 voces, volumen master al maximo.
; ----------------------------------------------------------------------------
_snd_init:
        lda     #0
        sta     SID_V1+4
        sta     SID_V2+4
        sta     SID_V3+4
        sta     SID_V1+5
        sta     SID_V1+6
        sta     SID_V2+5
        sta     SID_V2+6
        sta     SID_V3+5
        sta     SID_V3+6
        sta     snd_t2
        sta     snd_t3
        sta     snd_t1
        sta     snd_waka_on
        sta     snd_fright
        sta     snd_bg_mode
        sta     snd_bg_dir
        sta     snd_bg_div
        sta     snd_eyes
        sta     snd_eyes_live
        lda     #$0F
        sta     SID_VOL        ; filtro off + volumen maximo
        rts

; ----------------------------------------------------------------------------
; void snd_silence(void) - apaga las 3 voces.
; ----------------------------------------------------------------------------
_snd_silence:
        lda     #0
        sta     SID_V1+4
        sta     SID_V2+4
        sta     SID_V3+4
        sta     snd_t2
        sta     snd_t3
        sta     snd_t1
        sta     snd_waka_on
        sta     snd_fright
        sta     snd_eyes
        sta     snd_eyes_live
        lda     #BG_OFF
        sta     snd_bg_mode
        rts

; ----------------------------------------------------------------------------
; waka_tick: avanza el waka-waka (sube/baja). Lo llama snd_update mientras
;   snd_t1 > 0. NO se exporta (uso interno).
; ----------------------------------------------------------------------------
waka_tick:
        ; barrido hacia el objetivo (snd_w_hi), direccion snd_w_dir.
        ; OJO: NO leemos el SID (algunos SID no devuelven los registros de
        ; frecuencia). Llevamos la frecuencia en snd_w_freq y la reescribimos.
        ; Avanzamos 2 unidades de freq_hi por frame.
        lda     snd_w_dir
        bne     @down
        lda     snd_w_freq
        clc
        adc     #$02
        cmp     snd_w_hi
        bcc     @store
        lda     snd_w_hi
        jmp     @store
@down:
        lda     snd_w_freq
        sec
        sbc     #$02
        cmp     snd_w_hi
        bcs     @store
        lda     snd_w_hi
@store:
        sta     snd_w_freq
        sta     SID_V1+1
        lda     snd_w_freq
        cmp     snd_w_hi
        bne     @off
        lda     snd_w_dir
        eor     #$01
        sta     snd_w_dir
        beq     @set_hi
        lda     #$08
        sta     snd_w_hi
        rts
@set_hi:
        lda     #$18
        sta     snd_w_hi
@off:
        rts

; ----------------------------------------------------------------------------
; void snd_waka(void) - come un punto: (re)arranca el waka-waka (voz 1).
;   Reinicia el temporizador; asi comiendo puntos seguidos suena continuo,
;   y al dejar de comer snd_update lo apaga solo.
; ----------------------------------------------------------------------------
_snd_waka:
        ; envolvente: suena con cuerpo, corte suave
        lda     #$00
        sta     SID_V1+5       ; attack 0, decay 0
        lda     #$E8
        sta     SID_V1+6       ; sustain alto, release 8
        lda     #$80
        sta     SID_V1+2       ; PW lo
        lda     #$08
        sta     SID_V1+3       ; PW hi (~50%)
        lda     #$00
        sta     SID_V1+0       ; freq_lo = 0 (solo barremos freq_hi)
        ; si ya sonaba, NO reiniciamos la frecuencia (sigue su barrido)
        lda     snd_waka_on
        bne     @running
        lda     #$08
        sta     snd_w_freq     ; frecuencia actual de arranque
        sta     SID_V1+1
        lda     #$18
        sta     snd_w_hi       ; objetivo alto
        lda     #$00
        sta     snd_w_dir      ; 0 = subir
        lda     #(WF_PULSE|GATE)
        sta     SID_V1+4
@running:
        lda     #$01
        sta     snd_waka_on
        lda     #WAKA_FRAMES
        sta     snd_t1         ; reinicia el temporizador
        rts

; ----------------------------------------------------------------------------
; void snd_waka_stop(void) - para el waka ya (muerte, salida).
; ----------------------------------------------------------------------------
_snd_waka_stop:
        lda     #$00
        sta     snd_waka_on
        sta     snd_t1
        sta     SID_V1+4       ; gate off
        rts

; ----------------------------------------------------------------------------
; void snd_power(void) - comer power pellet. Voz 2: barrido ascendente agudo.
; ----------------------------------------------------------------------------
_snd_power:
        lda     #$00
        sta     SID_V2+5       ; attack 0, decay 0
        lda     #$E8
        sta     SID_V2+6       ; sustain alto, release 8
        lda     #$80
        sta     SID_V2+2
        lda     #$08
        sta     SID_V2+3
        lda     #$00
        sta     SID_V2+0
        lda     #$08
        sta     snd_v2_freq     ; frecuencia inicial media
        sta     SID_V2+1
        lda     #(WF_PULSE|GATE)
        sta     SID_V2+4
        lda     #POWER_FRAMES
        sta     snd_t2
        lda     #$02           ; modo: sweep ascendente
        sta     snd_v2_mode
        lda     #$03           ; paso: cambia cada 3 frames (barrido lento)
        sta     snd_v2_step
        lda     #$00
        sta     snd_v2_div
        rts

; ----------------------------------------------------------------------------
; void snd_eat_ghost(void) - comerse un fantasma. Voz 2: "glup" corto
;   (pulso con barrido descendente). Melodico, no ruido blanco.
; ----------------------------------------------------------------------------
_snd_eat_ghost:
        lda     #$00
        sta     SID_V2+5       ; attack 0, decay 0
        lda     #$B8
        sta     SID_V2+6       ; sustain alto (B), release 8
        lda     #$80
        sta     SID_V2+2       ; PW lo
        lda     #$06
        sta     SID_V2+3       ; PW hi (~40%)
        lda     #$00
        sta     SID_V2+0       ; freq_lo = 0
        lda     #$28
        sta     snd_v2_freq     ; empieza medio-agudo
        sta     SID_V2+1
        lda     #(WF_PULSE|GATE) ; pulso: sonido "doblep"
        sta     SID_V2+4
        lda     #EAT_FRAMES
        sta     snd_t2
        lda     #$01           ; sweep descendente
        sta     snd_v2_mode
        lda     #$02           ; paso: cambia cada 2 frames
        sta     snd_v2_step
        lda     #$00
        sta     snd_v2_div
        rts

; ----------------------------------------------------------------------------
; eyes_open: configura la voz 2 para el "whoop" de ojos y abre el gate.
;   Uso interno (snd_eyes_start y snd_update). No toca snd_eyes_live por si
;   misma; el llamante decide.
; ----------------------------------------------------------------------------
eyes_open:
        lda     #$00
        sta     SID_V2+5       ; attack 0, decay 0
        lda     #$C8
        sta     SID_V2+6       ; sustain alto, release 8
        lda     #$80
        sta     SID_V2+2       ; PW lo
        lda     #$08
        sta     SID_V2+3       ; PW hi (~50%)
        lda     #$00
        sta     SID_V2+0       ; freq_lo = 0
        lda     eyes_tab        ; freq inicial
        sta     SID_V2+1
        lda     #(WF_PULSE|GATE)
        sta     SID_V2+4
        lda     #$01
        sta     snd_eyes_live
        rts

; ----------------------------------------------------------------------------
; void snd_eyes_start(void) - arranca el "whoop" de los OJOS volviendo a casa
;   (voz 2). Oscila la frecuencia; dura hasta snd_eyes_stop().
; ----------------------------------------------------------------------------
_snd_eyes_start:
        lda     snd_eyes
        bne     @edone         ; ya sonando: no reiniciar
        lda     #$01
        sta     snd_eyes
        lda     #$00
        sta     snd_eyes_phase
        jmp     eyes_open
@edone:
        rts

; ----------------------------------------------------------------------------
; void snd_eyes_stop(void) - para el "whoop" de los ojos.
; ----------------------------------------------------------------------------
_snd_eyes_stop:
        lda     snd_eyes
        beq     @estop
        lda     #$00
        sta     snd_eyes
        sta     snd_eyes_live
        ; solo baja el gate si NO hay un efecto corto sonando (no lo corte)
        lda     snd_t2
        bne     @estop
        sta     SID_V2+4       ; gate off
@estop:
        rts

; ----------------------------------------------------------------------------
; void snd_die(void) - muerte de Pac-Man. Voz 2: barrido DESCENDENTE largo.
; ----------------------------------------------------------------------------
_snd_die:
        lda     #$00
        sta     SID_V2+5       ; attack 0, decay 0
        lda     #$E8
        sta     SID_V2+6       ; sustain alto (el sweep lo controla snd_update)
        lda     #$00
        sta     SID_V2+0
        lda     #$40
        sta     snd_v2_freq     ; empieza agudo
        sta     SID_V2+1
        lda     #(WF_SAW|GATE)
        sta     SID_V2+4
        lda     #DIE_FRAMES
        sta     snd_t2
        lda     #$01           ; modo: sweep descendente
        sta     snd_v2_mode
        lda     #$02           ; paso: cambia cada 2 frames (barrido lento)
        sta     snd_v2_step
        lda     #$00
        sta     snd_v2_div
        rts

; ----------------------------------------------------------------------------
; void snd_game_over(void) - fin de partida. Voz 2: barrido grave descendente.
; ----------------------------------------------------------------------------
_snd_game_over:
        lda     #$00
        sta     SID_V2+5
        lda     #$E8
        sta     SID_V2+6
        lda     #$00
        sta     SID_V2+0
        lda     #$20
        sta     snd_v2_freq
        sta     SID_V2+1
        lda     #(WF_NOISE|GATE)
        sta     SID_V2+4
        lda     #OVER_FRAMES
        sta     snd_t2
        lda     #$01           ; sweep descendente
        sta     snd_v2_mode
        lda     #$04           ; paso lento
        sta     snd_v2_step
        lda     #$00
        sta     snd_v2_div
        rts

; ----------------------------------------------------------------------------
; void snd_bg_siren(void) - fondo: SIRENA suave (juego normal). Voz 3.
;   El barrido es continuo (gestionado en snd_update).
; ----------------------------------------------------------------------------
_snd_bg_siren:
        lda     #BG_SIREN
        sta     snd_bg_mode
        ; envolvente suave
        lda     #$00
        sta     SID_V3+5
        lda     #$F8
        sta     SID_V3+6       ; sustain muy alto, release 8
        lda     #$80
        sta     SID_V3+2       ; PW lo
        lda     #$08
        sta     SID_V3+3       ; PW hi (~50%)
        lda     #$00
        sta     SID_V3+0       ; freq_lo = 0
        lda     #$10
        sta     snd_bg_freq    ; frecuencia actual
        sta     SID_V3+1
        lda     #$20
        sta     snd_bg_hi      ; objetivo alto
        lda     #$00
        sta     snd_bg_dir     ; 0 = subir
        sta     snd_bg_div
        lda     #(WF_TRI|GATE) ; onda triangular: sirena dulce
        sta     SID_V3+4
        rts

; ----------------------------------------------------------------------------
; void snd_bg_fright(void) - fondo: SONIDO DE FANTASMAS AZULES. Voz 3.
;   Mas rapido y agudo que la sirena.
; ----------------------------------------------------------------------------
_snd_bg_fright:
        lda     #BG_FRIGHT
        sta     snd_bg_mode
        lda     #$00
        sta     SID_V3+5
        lda     #$F8
        sta     SID_V3+6
        lda     #$80
        sta     SID_V3+2
        lda     #$08
        sta     SID_V3+3
        lda     #$00
        sta     SID_V3+0
        lda     #$20
        sta     snd_bg_freq
        sta     SID_V3+1
        lda     #$3C
        sta     snd_bg_hi      ; objetivo alto (agudo)
        lda     #$00
        sta     snd_bg_dir
        sta     snd_bg_div
        lda     #(WF_PULSE|GATE) ; pulso: mas "electrico"
        sta     SID_V3+4
        rts

; ----------------------------------------------------------------------------
; void snd_bg_off(void) - apaga el fondo (voz 3 en silencio).
; ----------------------------------------------------------------------------
_snd_bg_off:
        lda     #BG_OFF
        sta     snd_bg_mode
        lda     #$00
        sta     SID_V3+4       ; gate off
        rts

; ----------------------------------------------------------------------------
; void snd_fright_start(void) - compatibilidad: arranca el sonido de azules.
; ----------------------------------------------------------------------------
_snd_fright_start:
        lda     #$01
        sta     snd_fright
        jmp     _snd_bg_fright

; ----------------------------------------------------------------------------
; void snd_fright_stop(void) - compatibilidad: vuelve a la sirena de fondo.
; ----------------------------------------------------------------------------
_snd_fright_stop:
        lda     snd_fright
        beq     @done
        lda     #$00
        sta     snd_fright
        jmp     _snd_bg_siren
@done:
        rts

; ----------------------------------------------------------------------------
; void snd_update(void) - un tick por frame: gestiona sweeps y envolventes.
; ----------------------------------------------------------------------------
_snd_update:
        ; --- Voz 1: waka (temporizador). Al agotarse, apaga el waka. ---
        lda     snd_t1
        beq     @v2
        jsr     waka_tick
        dec     snd_t1
        bne     @v2
        lda     #$00
        sta     snd_waka_on
        sta     SID_V1+4       ; gate off
@v2:
        ; --- Voz 2: efecto corto (power/eat/die/over) TIENE PRIORIDAD. ---
        lda     snd_t2
        beq     @v2_eyes
        jsr     v2_sweep
        dec     snd_t2
        bne     @v3
        ; el efecto corto acaba: apaga la voz 2...
        lda     #$00
        sta     SID_V2+4       ; gate off del efecto
        sta     snd_eyes_live  ; la voz 2 ya no tiene el whoop configurado
        jmp     @v3

@v2_eyes:
        ; --- Si hay OJOS volviendo y no hay efecto corto: "whoop" (voz 2). ---
        lda     snd_eyes
        beq     @v3
        ; si la voz 2 aun no esta configurada para el whoop, reconfigurala
        ; (puede haberse apagado al terminar el efecto corto).
        lda     snd_eyes_live
        bne     @eyes_tone
        jsr     eyes_open
@eyes_tone:
        lda     snd_eyes_phase
        and     #$03
        tay
        lda     eyes_tab,y
        sta     SID_V2+1
        inc     snd_eyes_phase
@v3:
        ; --- Voz 3: FONDO continuo (sirena o fantasmas azules). ---
        jsr     bg_sweep
@done:
        rts

; ----------------------------------------------------------------------------
; bg_sweep: barrido continuo de la voz 3 (fondo). Segun snd_bg_mode:
;   BG_SIREN  -> lento y suave (triangular), ritmo parecido al waka.
;   BG_FRIGHT -> rapido y agudo (pulso).
;   BG_OFF    -> no hace nada.
; ----------------------------------------------------------------------------
bg_sweep:
        lda     snd_bg_mode
        cmp     #BG_OFF
        bne     @notoff
        jmp     @done
@notoff:
        cmp     #BG_FRIGHT
        bne     @siren
        jmp     @fright_step

        ; --- SIRENA: mismo ritmo que el waka (2 por frame), suave ---
@siren:
        lda     snd_bg_dir
        bne     @siren_down
        lda     snd_bg_freq
        clc
        adc     #$02
        cmp     snd_bg_hi
        bcc     @store
        lda     snd_bg_hi
        jmp     @store
@siren_down:
        lda     snd_bg_freq
        sec
        sbc     #$02
        cmp     snd_bg_hi
        bcs     @store
        lda     snd_bg_hi
@store:
        sta     snd_bg_freq
        sta     SID_V3+1
        lda     snd_bg_freq
        cmp     snd_bg_hi
        bne     @done
        ; invierte direccion y cambia limite (16-24)
        lda     snd_bg_dir
        eor     #$01
        sta     snd_bg_dir
        beq     @siren_tophi
        lda     #$10
        sta     snd_bg_hi
        jmp     @done
@siren_tophi:
        lda     #$20
        sta     snd_bg_hi
        jmp     @done

        ; --- FRIGHT: mas rapido y agudo (pulso electrico) ---
@fright_step:
        lda     snd_bg_div
        eor     #$01
        sta     snd_bg_div
        beq     @done         ; 1 de cada 2 frames (chispa mas lenta)
        lda     snd_bg_dir
        bne     @fright_down
        lda     snd_bg_freq
        clc
        adc     #$04
        cmp     snd_bg_hi
        bcc     @fstore
        lda     snd_bg_hi
        jmp     @fstore
@fright_down:
        lda     snd_bg_freq
        sec
        sbc     #$04
        cmp     snd_bg_hi
        bcs     @fstore
        lda     snd_bg_hi
@fstore:
        sta     snd_bg_freq
        sta     SID_V3+1
        lda     snd_bg_freq
        cmp     snd_bg_hi
        bne     @done
        lda     snd_bg_dir
        eor     #$01
        sta     snd_bg_dir
        beq     @fr_tophi
        lda     #$20
        sta     snd_bg_hi
        jmp     @done
@fr_tophi:
        lda     #$3C
        sta     snd_bg_hi
@done:
        rts

; ----------------------------------------------------------------------------
; v2_sweep: barre la frecuencia de la voz 2.
;   snd_v2_mode: 1 = descendente (muerte/over), 2 = ascendente (power/eat).
;   El barrido es mas LENTO que 1 por frame: usa un divisor para que se oiga
;   el deslizamiento (no un simple tono).
; ----------------------------------------------------------------------------
v2_sweep:
        ; divisor: solo cambia la frecuencia 1 de cada snd_v2_step frames
        inc     snd_v2_div
        lda     snd_v2_div
        cmp     snd_v2_step
        bcc     @done
        lda     #$00
        sta     snd_v2_div

        lda     snd_v2_mode
        cmp     #$02
        beq     @up
        ; descendente: si freq > $04, freq--
        lda     snd_v2_freq
        cmp     #$04
        bcc     @done
        dec     snd_v2_freq
        jmp     @write
@up:
        ; ascendente: freq++ hasta $A0
        lda     snd_v2_freq
        cmp     #$A0
        bcs     @done
        inc     snd_v2_freq
@write:
        lda     snd_v2_freq
        sta     SID_V2+1       ; escribimos frecuencia en el SID
@done:
        rts

; ============================================================================
        .segment "RODATA"
; ============================================================================
; Tabla del "whoop" de los ojos (oscila la freq_hi de la voz 2).
eyes_tab: .byte $40,$50,$40,$30

; ============================================================================
        .segment "BSS"
; ============================================================================
snd_t2:        .res 1   ; contador voz 2 (efectos)
snd_t3:        .res 1   ; contador voz 3
snd_t1:        .res 1   ; temporizador del waka (voz 1)
snd_waka_on:   .res 1   ; 1 = waka continuo sonando
snd_w_freq:    .res 1   ; frecuencia ACTUAL del waka (no leer del SID)
snd_w_hi:      .res 1   ; frecuencia objetivo del waka (hi)
snd_w_dir:     .res 1   ; direccion del waka (0 subir, 1 bajar)
snd_v2_mode:   .res 1   ; modo de sweep de la voz 2 (1 bajar, 2 subir)
snd_v2_step:   .res 1   ; frames entre cada paso del sweep de la voz 2
snd_v2_div:    .res 1   ; divisor del sweep de la voz 2
snd_v2_freq:   .res 1   ; frecuencia ACTUAL de la voz 2 (no leer del SID)
snd_bg_mode:   .res 1   ; modo del fondo (0 sirena, 1 fright, 2 off)
snd_bg_freq:   .res 1   ; frecuencia ACTUAL del fondo
snd_bg_hi:     .res 1   ; frecuencia objetivo del fondo
snd_bg_dir:    .res 1   ; direccion del barrido de fondo
snd_bg_div:    .res 1   ; divisor del barrido de fondo (sirena)
snd_fright:    .res 1   ; 1 = modo asustado activo (compatibilidad)
snd_eyes:      .res 1   ; 1 = whoop de ojos sonando
snd_eyes_phase:.res 1   ; fase del whoop de ojos
snd_eyes_live: .res 1   ; 1 = la voz 2 esta configurada para el whoop
