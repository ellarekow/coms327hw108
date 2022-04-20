#ifndef IO_H
#define IO_H

#include "pokemon.h"
#include "poke327.h"

class Character;
typedef int16_t pair_t[2];

void io_init_terminal(void);
void io_reset_terminal(void);
void io_display(void);
void io_handle_input(pair_t dest);
void io_queue_message(const char *format, ...);
void io_battle(Character *aggressor, Character *defender);
void io_fightTrainer(Npc *npc);
void io_fightPoke(Pokemon *p);
void io_encounter_pokemon(void);
void io_backpack(int inBattle);
void io_getItem(int inBattle, int itemIdx);
void io_choose(void);

#endif
