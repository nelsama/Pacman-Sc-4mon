/**
 * ============================================================================
 * sound.h - Driver de sonido para el SID 6581 (compatible C64) - PAC-MAN
 * ============================================================================
 * El SID esta mapeado en $D400-$D41F, igual que en el Commodore 64.
 *
 * Mapa de registros por voz (offset 0, 7, 14):
 *   +0 FREQ_LO  +1 FREQ_HI  +2 PW_LO  +3 PW_HI
 *   +4 CTRL (gate/sync/ring/test/waveform)  +5 AD  +6 SR
 * $D418 = MODE/VOL (bits 3:0 = volumen master)
 *
 * Voces asignadas (Pac-Man):
 *   Voz 1: waka-waka (suena al comer puntos).
 *   Voz 2: power pellet, comer fantasma, muerte de Pac-Man, game over.
 *   Voz 3: fondo continuo (sirena normal / sonido de fantasmas azules).
 * ============================================================================
 */

#ifndef SOUND_H
#define SOUND_H

#include <stdint.h>

/* Inicializa el SID: silencia las 3 voces, volumen master al maximo. */
void snd_init(void);

/* Efectos (no bloqueantes: disparan la voz y suenan solos). */
void snd_waka(void);          /* come un punto: (re)arranca el waka-waka */
void snd_waka_stop(void);     /* para el waka-waka (muerte / salida) */
void snd_power(void);         /* comer power pellet (barrido ascendente) */
void snd_eat_ghost(void);     /* comerse un fantasma (glup corto) */
void snd_eyes_start(void);    /* arranca el whoop de los ojos volviendo a casa */
void snd_eyes_stop(void);     /* para el whoop de los ojos */
void snd_die(void);           /* muerte de Pac-Man (barrido descendente) */
void snd_game_over(void);     /* fin de partida */

/* Zumbido continuo mientras los fantasmas estan asustados (voz 3).
 * snd_fright_start() lo arranca; snd_fright_stop() lo para. */
void snd_fright_start(void);
void snd_fright_stop(void);

/* Fondo continuo (voz 3): la sirena suena durante el juego normal; al comer
 * un power pellet cambia al sonido de fantasmas azules. */
void snd_bg_siren(void);      /* sirena suave de fondo (juego normal) */
void snd_bg_fright(void);     /* sonido de fantasmas azules */
void snd_bg_off(void);        /* apaga el fondo (voz 3 en silencio) */

/* Apaga todas las voces (al salir o cambiar de estado). */
void snd_silence(void);

/* Tick por frame: gestiona las envolventes de los efectos. */
void snd_update(void);

#endif /* SOUND_H */
