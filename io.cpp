#include <unistd.h>
#include <ncurses.h>
#include <ctype.h>
#include <stdlib.h>
#include <limits.h>
#include <vector>
#include <string.h>

#include "io.h"
#include "character.h"
#include "poke327.h"
#include "pokemon.h"

typedef struct io_message
{
  /* Will print " --more-- " at end of line when another message follows. *
   * Leave 10 extra spaces for that.                                      */
  char msg[71];
  struct io_message *next;
} io_message_t;

static io_message_t *io_head, *io_tail;

void io_init_terminal(void)
{
  initscr();
  raw();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  start_color();
  init_pair(COLOR_RED, COLOR_RED, COLOR_BLACK);
  init_pair(COLOR_GREEN, COLOR_GREEN, COLOR_BLACK);
  init_pair(COLOR_YELLOW, COLOR_YELLOW, COLOR_BLACK);
  init_pair(COLOR_BLUE, COLOR_BLUE, COLOR_BLACK);
  init_pair(COLOR_MAGENTA, COLOR_MAGENTA, COLOR_BLACK);
  init_pair(COLOR_CYAN, COLOR_CYAN, COLOR_BLACK);
  init_pair(COLOR_WHITE, COLOR_WHITE, COLOR_BLACK);
}

void io_reset_terminal(void)
{
  endwin();

  while (io_head)
  {
    io_tail = io_head;
    io_head = io_head->next;
    free(io_tail);
  }
  io_tail = NULL;
}

void io_queue_message(const char *format, ...)
{
  io_message_t *tmp;
  va_list ap;

  if (!(tmp = (io_message_t *)malloc(sizeof(*tmp))))
  {
    perror("malloc");
    exit(1);
  }

  tmp->next = NULL;

  va_start(ap, format);

  vsnprintf(tmp->msg, sizeof(tmp->msg), format, ap);

  va_end(ap);

  if (!io_head)
  {
    io_head = io_tail = tmp;
  }
  else
  {
    io_tail->next = tmp;
    io_tail = tmp;
  }
}

static void io_print_message_queue(uint32_t y, uint32_t x)
{
  while (io_head)
  {
    io_tail = io_head;
    attron(COLOR_PAIR(COLOR_CYAN));
    mvprintw(y, x, "%-80s", io_head->msg);
    attroff(COLOR_PAIR(COLOR_CYAN));
    io_head = io_head->next;
    if (io_head)
    {
      attron(COLOR_PAIR(COLOR_CYAN));
      mvprintw(y, x + 70, "%10s", " --more-- ");
      attroff(COLOR_PAIR(COLOR_CYAN));
      refresh();
      getch();
    }
    free(io_tail);
  }
  io_tail = NULL;
}

/**************************************************************************
 * Compares trainer distances from the PC according to the rival distance *
 * map.  This gives the approximate distance that the PC must travel to   *
 * get to the trainer (doesn't account for crossing buildings).  This is  *
 * not the distance from the NPC to the PC unless the NPC is a rival.     *
 *                                                                        *
 * Not a bug.                                                             *
 **************************************************************************/
static int compare_trainer_distance(const void *v1, const void *v2)
{
  const Character *const *c1 = (const Character *const *)v1;
  const Character *const *c2 = (const Character *const *)v2;

  return (world.rival_dist[(*c1)->pos[dim_y]][(*c1)->pos[dim_x]] -
          world.rival_dist[(*c2)->pos[dim_y]][(*c2)->pos[dim_x]]);
}

static Character *io_nearest_visible_trainer()
{
  Character **c, *n;
  uint32_t x, y, count;

  c = (Character **)malloc(world.cur_map->num_trainers * sizeof(*c));

  /* Get a linear list of trainers */
  for (count = 0, y = 1; y < MAP_Y - 1; y++)
  {
    for (x = 1; x < MAP_X - 1; x++)
    {
      if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
                                           &world.pc)
      {
        c[count++] = world.cur_map->cmap[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  qsort(c, count, sizeof(*c), compare_trainer_distance);

  n = c[0];

  free(c);

  return n;
}

void io_display()
{
  uint32_t y, x;
  Character *c;

  clear();
  for (y = 0; y < MAP_Y; y++)
  {
    for (x = 0; x < MAP_X; x++)
    {
      if (world.cur_map->cmap[y][x])
      {
        mvaddch(y + 1, x, world.cur_map->cmap[y][x]->symbol);
      }
      else
      {
        switch (world.cur_map->map[y][x])
        {
        case ter_boulder:
        case ter_mountain:
          attron(COLOR_PAIR(COLOR_MAGENTA));
          mvaddch(y + 1, x, '%');
          attroff(COLOR_PAIR(COLOR_MAGENTA));
          break;
        case ter_tree:
        case ter_forest:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, '^');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        case ter_path:
        case ter_exit:
          attron(COLOR_PAIR(COLOR_YELLOW));
          mvaddch(y + 1, x, '#');
          attroff(COLOR_PAIR(COLOR_YELLOW));
          break;
        case ter_mart:
          attron(COLOR_PAIR(COLOR_BLUE));
          mvaddch(y + 1, x, 'M');
          attroff(COLOR_PAIR(COLOR_BLUE));
          break;
        case ter_center:
          attron(COLOR_PAIR(COLOR_RED));
          mvaddch(y + 1, x, 'C');
          attroff(COLOR_PAIR(COLOR_RED));
          break;
        case ter_grass:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, ':');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        case ter_clearing:
          attron(COLOR_PAIR(COLOR_GREEN));
          mvaddch(y + 1, x, '.');
          attroff(COLOR_PAIR(COLOR_GREEN));
          break;
        default:
          /* Use zero as an error symbol, since it stands out somewhat, and it's *
           * not otherwise used.                                                 */
          attron(COLOR_PAIR(COLOR_CYAN));
          mvaddch(y + 1, x, '0');
          attroff(COLOR_PAIR(COLOR_CYAN));
        }
      }
    }
  }

  mvprintw(23, 1, "PC position is (%2d,%2d) on map %d%cx%d%c.",
           world.pc.pos[dim_x],
           world.pc.pos[dim_y],
           abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_x] - (WORLD_SIZE / 2) >= 0 ? 'E' : 'W',
           abs(world.cur_idx[dim_y] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_y] - (WORLD_SIZE / 2) <= 0 ? 'N' : 'S');
  mvprintw(22, 1, "%d known %s.", world.cur_map->num_trainers,
           world.cur_map->num_trainers > 1 ? "trainers" : "trainer");
  mvprintw(22, 30, "Nearest visible trainer: ");
  if ((c = io_nearest_visible_trainer()))
  {
    attron(COLOR_PAIR(COLOR_RED));
    mvprintw(22, 55, "%c at %d %c by %d %c.",
             c->symbol,
             abs(c->pos[dim_y] - world.pc.pos[dim_y]),
             ((c->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ? 'N' : 'S'),
             abs(c->pos[dim_x] - world.pc.pos[dim_x]),
             ((c->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ? 'W' : 'E'));
    attroff(COLOR_PAIR(COLOR_RED));
  }
  else
  {
    attron(COLOR_PAIR(COLOR_BLUE));
    mvprintw(22, 55, "NONE.");
    attroff(COLOR_PAIR(COLOR_BLUE));
  }

  io_print_message_queue(0, 0);

  refresh();
}

uint32_t io_teleport_pc(pair_t dest)
{
  /* Just for fun. And debugging.  Mostly debugging. */

  do
  {
    dest[dim_x] = rand_range(1, MAP_X - 2);
    dest[dim_y] = rand_range(1, MAP_Y - 2);
  } while (world.cur_map->cmap[dest[dim_y]][dest[dim_x]] ||
           move_cost[char_pc][world.cur_map->map[dest[dim_y]]
                                                [dest[dim_x]]] == INT_MAX ||
           world.rival_dist[dest[dim_y]][dest[dim_x]] < 0);

  return 0;
}

static void io_scroll_trainer_list(char (*s)[40], uint32_t count)
{
  uint32_t offset;
  uint32_t i;

  offset = 0;

  while (1)
  {
    for (i = 0; i < 13; i++)
    {
      mvprintw(i + 6, 19, " %-40s ", s[i + offset]);
    }
    switch (getch())
    {
    case KEY_UP:
      if (offset)
      {
        offset--;
      }
      break;
    case KEY_DOWN:
      if (offset < (count - 13))
      {
        offset++;
      }
      break;
    case 27:
      return;
    }
  }
}

static void io_list_trainers_display(Npc **c,
                                     uint32_t count)
{
  uint32_t i;
  char(*s)[40]; /* pointer to array of 40 char */

  s = (char(*)[40])malloc(count * sizeof(*s));

  mvprintw(3, 19, " %-40s ", "");
  /* Borrow the first element of our array for this string: */
  snprintf(s[0], 40, "You know of %d trainers:", count);
  mvprintw(4, 19, " %-40s ", s[0]);
  mvprintw(5, 19, " %-40s ", "");

  for (i = 0; i < count; i++)
  {
    snprintf(s[i], 40, "%16s %c: %2d %s by %2d %s",
             char_type_name[c[i]->ctype],
             c[i]->symbol,
             abs(c[i]->pos[dim_y] - world.pc.pos[dim_y]),
             ((c[i]->pos[dim_y] - world.pc.pos[dim_y]) <= 0 ? "North" : "South"),
             abs(c[i]->pos[dim_x] - world.pc.pos[dim_x]),
             ((c[i]->pos[dim_x] - world.pc.pos[dim_x]) <= 0 ? "West" : "East"));
    if (count <= 13)
    {
      /* Handle the non-scrolling case right here. *
       * Scrolling in another function.            */
      mvprintw(i + 6, 19, " %-40s ", s[i]);
    }
  }

  if (count <= 13)
  {
    mvprintw(count + 6, 19, " %-40s ", "");
    mvprintw(count + 7, 19, " %-40s ", "Hit escape to continue.");
    while (getch() != 27 /* escape */)
      ;
  }
  else
  {
    mvprintw(19, 19, " %-40s ", "");
    mvprintw(20, 19, " %-40s ",
             "Arrows to scroll, escape to continue.");
    io_scroll_trainer_list(s, count);
  }

  free(s);
}

static void io_list_trainers()
{
  Character **c;
  uint32_t x, y, count;

  c = (Character **)malloc(world.cur_map->num_trainers * sizeof(*c));

  /* Get a linear list of trainers */
  for (count = 0, y = 1; y < MAP_Y - 1; y++)
  {
    for (x = 1; x < MAP_X - 1; x++)
    {
      if (world.cur_map->cmap[y][x] && world.cur_map->cmap[y][x] !=
                                           &world.pc)
      {
        c[count++] = world.cur_map->cmap[y][x];
      }
    }
  }

  /* Sort it by distance from PC */
  qsort(c, count, sizeof(*c), compare_trainer_distance);

  /* Display it */
  io_list_trainers_display((Npc **)(c), count);
  free(c);

  /* And redraw the map */
  io_display();
}

void io_pokemart()
{
  mvprintw(0, 0, "Welcome to the Pokemart.  Could I interest you in some Pokeballs?");
  refresh();
  getch();
}

void io_pokemon_center()
{
  mvprintw(0, 0, "Welcome to the Pokemon Center.  How can Nurse Joy assist you?");
  refresh();
  getch();
}

void io_battle(Character *aggressor, Character *defender)
{
  Npc *npc;

  io_display();
  mvprintw(0, 0, "Aww, how'd you get so strong?  You and your pokemon must share a special bond!");
  refresh();
  getch();
  if (!(npc = dynamic_cast<Npc *>(aggressor)))
  {
    npc = dynamic_cast<Npc *>(defender);
    io_fightTrainer(npc);
  }

  npc->defeated = 1;
  if (npc->ctype == char_hiker || npc->ctype == char_rival)
  {
    npc->mtype = move_wander;
  }
}

uint32_t move_pc_dir(uint32_t input, pair_t dest)
{
  dest[dim_y] = world.pc.pos[dim_y];
  dest[dim_x] = world.pc.pos[dim_x];

  switch (input)
  {
  case 1:
  case 2:
  case 3:
    dest[dim_y]++;
    break;
  case 4:
  case 5:
  case 6:
    break;
  case 7:
  case 8:
  case 9:
    dest[dim_y]--;
    break;
  }
  switch (input)
  {
  case 1:
  case 4:
  case 7:
    dest[dim_x]--;
    break;
  case 2:
  case 5:
  case 8:
    break;
  case 3:
  case 6:
  case 9:
    dest[dim_x]++;
    break;
  case '>':
    if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
        ter_mart)
    {
      io_pokemart();
    }
    if (world.cur_map->map[world.pc.pos[dim_y]][world.pc.pos[dim_x]] ==
        ter_center)
    {
      io_pokemon_center();
    }
    break;
  }

  if ((world.cur_map->map[dest[dim_y]][dest[dim_x]] == ter_exit) &&
      (input == 1 || input == 3 || input == 7 || input == 9))
  {
    // Exiting diagonally leads to complicated entry into the new map
    // in order to avoid INT_MAX move costs in the destination.
    // Most easily solved by disallowing such entries here.
    return 1;
  }

  if (world.cur_map->cmap[dest[dim_y]][dest[dim_x]])
  {
    if (dynamic_cast<Npc *>(world.cur_map->cmap[dest[dim_y]][dest[dim_x]]) &&
        ((Npc *)world.cur_map->cmap[dest[dim_y]][dest[dim_x]])->defeated)
    {
      // Some kind of greeting here would be nice
      return 1;
    }
    else if (dynamic_cast<Npc *>(world.cur_map->cmap[dest[dim_y]][dest[dim_x]]))
    {
      io_battle(&world.pc, world.cur_map->cmap[dest[dim_y]][dest[dim_x]]);
      // Not actually moving, so set dest back to PC position
      dest[dim_x] = world.pc.pos[dim_x];
      dest[dim_y] = world.pc.pos[dim_y];
    }
  }

  if (move_cost[char_pc][world.cur_map->map[dest[dim_y]][dest[dim_x]]] ==
      INT_MAX)
  {
    return 1;
  }

  return 0;
}

void io_teleport_world(pair_t dest)
{
  int x, y;

  world.cur_map->cmap[world.pc.pos[dim_y]][world.pc.pos[dim_x]] = NULL;

  mvprintw(0, 0, "Enter x [-200, 200]: ");
  refresh();
  echo();
  curs_set(1);
  mvscanw(0, 21, (char *)"%d", &x);
  mvprintw(0, 0, "Enter y [-200, 200]:          ");
  refresh();
  mvscanw(0, 21, (char *)"%d", &y);
  refresh();
  noecho();
  curs_set(0);

  if (x < -200)
  {
    x = -200;
  }
  if (x > 200)
  {
    x = 200;
  }
  if (y < -200)
  {
    y = -200;
  }
  if (y > 200)
  {
    y = 200;
  }

  x += 200;
  y += 200;

  world.cur_idx[dim_x] = x;
  world.cur_idx[dim_y] = y;

  new_map(1);
  io_teleport_pc(dest);
}

void io_encounter_pokemon()
{
  Pokemon *p;

  int md = (abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)) +
            abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)));
  int minl, maxl;

  if (md <= 200)
  {
    minl = 1;
    maxl = md / 2;
  }
  else
  {
    minl = (md - 200) / 2;
    maxl = 100;
  }
  if (minl < 1)
  {
    minl = 1;
  }
  if (minl > 100)
  {
    minl = 100;
  }
  if (maxl < 1)
  {
    maxl = 1;
  }
  if (maxl > 100)
  {
    maxl = 100;
  }

  p = new Pokemon(rand() % (maxl - minl + 1) + minl);

  //  std::cerr << *p << std::endl << std::endl;

  // io_queue_message("%s%s%s: HP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
  //                  p->is_shiny() ? "*" : "", p->get_species(),
  //                  p->is_shiny() ? "*" : "", p->get_hp(), p->get_atk(),
  //                  p->get_def(), p->get_spatk(), p->get_spdef(),
  //                  p->get_speed(), p->get_gender_string());
  // io_queue_message("%s's moves: %s %s", p->get_species(),
  //                  p->get_move(0), p->get_move(1));

  clear();
  mvprintw(0, 0, "You have enountered a %s%s%s!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
           p->is_shiny() ? "*" : "", p->get_species(),
           p->is_shiny() ? "*" : "", p->get_hp(), p->get_atk(),
           p->get_def(), p->get_spatk(), p->get_spdef(),
           p->get_speed(), p->get_gender_string());
  refresh();
  io_fightPoke(p);
}

void io_backpack(int inBattle)
{
  while (1)
  {
    clear();
    mvprintw(0, 0, "Backpack contents:\n");
    unsigned i;
    for (i = 0; i != world.pc.item.size(); i++)
    {
      mvprintw(i + 1, 5, "%d.\t%d %s\n", i + 1, world.pc.num.at(i), world.pc.item.at(i).c_str());
    }
    mvprintw(i + 1, 5, "press (q) to return");
    refresh();
    char input = getch();
    if ((int)input <= (int)i)
      io_getItem(inBattle, (int)input - 1);
    else if (input == 'q')
      break;
    mvprintw(i + 2, 5, "you entered: %c please enter a valid input", input);
    refresh();
  }
}

void io_getItem(int inBattle, int itemIdx)
{
  clear();
  mvprintw(0, 0, "you have choosen %s", world.pc.item[itemIdx]);
  refresh();
  getch();
}

void io_choose()
{
  Pokemon *p1 = new Pokemon(1);
  Pokemon *p2 = new Pokemon(1);
  Pokemon *p3 = new Pokemon(1);
  mvprintw(0, 0, "Choose a pokemon: ");
  mvprintw(1, 5, "1. %s", p1->get_species());
  mvprintw(2, 5, "2. %s", p2->get_species());
  mvprintw(3, 5, "3. %s", p3->get_species());

  refresh();

  char input = getch();
  while (input != '1' && input != '2' && input != '3')
  {
    mvprintw(4, 5, "Please choose a pokemon, input was %c choices were %c %c or %c", input, '1', '2', '3');
    refresh();
    input = getch();
  }

  int i;
  for (i = 0; i < 6; i++)
  {
    world.pc.pokemons[i] = NULL;
  }

  switch (input)
  {
  case '1':
    world.pc.pokemons[0] = p1;
    delete p2;
    delete p3;
    break;

  case '2':
    world.pc.pokemons[0] = p2;
    delete p1;
    delete p3;
    break;

  case '3':
    world.pc.pokemons[0] = p3;
    delete p1;
    delete p2;
    break;
  }

  clear();

  mvprintw(0, 0, "You have choosen %s", world.pc.pokemons[0]->get_species());
  refresh();
  getch();
}

void io_fightTrainer(Npc *npc)
{
  int i = 0;
  for (i = 0; i < 6; i++)
  {
    npc->pokemons[i] = NULL;
  }

  i = 0;

  int md = (abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)) +
            abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)));
  int minl, maxl;

  if (md <= 200)
  {
    minl = 1;
    maxl = md / 2;
  }
  else
  {
    minl = (md - 200) / 2;
    maxl = 100;
  }
  if (minl < 1)
  {
    minl = 1;
  }
  if (minl > 100)
  {
    minl = 100;
  }
  if (maxl < 1)
  {
    maxl = 1;
  }
  if (maxl > 100)
  {
    maxl = 100;
  }

  int numOfPoke = 0;

  for (i = 0; i < rand() % 6 + 1; i++)
  {
    npc->pokemons[i] = new Pokemon(rand() % (maxl - minl + 1) + minl);
    numOfPoke++;
  }

  int battle = 1;
  int npcCurPIdx = 0;

  Pokemon *npcPoke = npc->pokemons[npcCurPIdx];
  Pokemon *cur = world.pc.pokemons[0];

  do
  {
    clear();
    if (npcPoke->get_hp() == 0)
    {
      npcCurPIdx++;
      if (npcCurPIdx > numOfPoke)
        battle = 0;

      else if (npc->pokemons[npcCurPIdx] == NULL)
        battle = 0;

      else
        npcPoke = npc->pokemons[npcCurPIdx];
    }
    else if (cur->get_hp() == 0)
      battle = 0;

    mvprintw(0, 0, "You have enountered a %s%s%s!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
             npcPoke->is_shiny() ? "*" : "", npcPoke->get_species(),
             npcPoke->is_shiny() ? "*" : "", npcPoke->get_hp(), npcPoke->get_atk(),
             npcPoke->get_def(), npcPoke->get_spatk(), npcPoke->get_spdef(),
             npcPoke->get_speed(), npcPoke->get_gender_string());
    mvprintw(5, 0, "%s, I choose you!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d  \nMoves: 1. %s\n2. %s", cur->get_species(), cur->get_hp(), cur->get_atk(),
             cur->get_def(), cur->get_spatk(), cur->get_spdef(), cur->get_speed(), cur->get_move(0), cur->get_move(1));
    mvprintw(10, 0, "select an action: \n(1) move 1\n(2) move 2\n(b) backpack");
    refresh();
    char input = getch();
    int dam;
    if (input == '1' || input == '2')
    {
      int move = 0;
      if (input == '2')
        move = 2;
      dam = cur->get_dam(move, rand() % 16 + 85);
      npcPoke->set_hp(-1 * dam);
      clear();
      mvprintw(0, 0, "You have enountered a %s%s%s!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
               npcPoke->is_shiny() ? "*" : "", npcPoke->get_species(),
               npcPoke->is_shiny() ? "*" : "", npcPoke->get_hp(), npcPoke->get_atk(),
               npcPoke->get_def(), npcPoke->get_spatk(), npcPoke->get_spdef(),
               npcPoke->get_speed(), npcPoke->get_gender_string());
      mvprintw(5, 0, "%s, I choose you!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d  \nMoves: 1. %s\n2. %s", cur->get_species(), cur->get_hp(), cur->get_atk(),
               cur->get_def(), cur->get_spatk(), cur->get_spdef(), cur->get_speed(), cur->get_move(0), cur->get_move(1));
      mvprintw(10, 0, "%s did %d damage!", cur->get_species(), dam);
      refresh();
      getch();
    }
    else if (input == 'b')
      io_backpack(1);

    dam = npcPoke->get_dam(rand() % 1 + 1, rand() % 16 + 85);
    cur->set_hp(-1 * dam);
    mvprintw(10, 0, "%s did %d damage!", npcPoke->get_species(), dam);
    refresh();
    getch();
    if (cur->get_hp() == 0)
      battle = 0;
  } while (battle);
}

void io_fightPoke(Pokemon *p)
{
  int battle = 1;
  int runs = 1;
  do
  {
    Pokemon *cur = world.pc.pokemons[0];
    mvprintw(0, 0, "You have enountered a %s%s%s!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
             p->is_shiny() ? "*" : "", p->get_species(),
             p->is_shiny() ? "*" : "", p->get_hp(), p->get_atk(),
             p->get_def(), p->get_spatk(), p->get_spdef(),
             p->get_speed(), p->get_gender_string());
    mvprintw(5, 0, "%s, I choose you!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d  \nMoves: 1. %s\n2. %s", cur->get_species(), cur->get_hp(), cur->get_atk(),
             cur->get_def(), cur->get_spatk(), cur->get_spdef(), cur->get_speed(), cur->get_move(0), cur->get_move(1));
    mvprintw(10, 0, "select an action: \n(1) move 1\n(2) move 2\n(b) backpack\n(r) run");
    refresh();
    char input = getch();
    int dam;
    if (input == '1' || input == '2')
    {
      int move = 0;
      if (input == '2')
        move = 2;
      dam = cur->get_dam(move, rand() % 16 + 85);
      p->set_hp(-1 * dam);
      clear();
      mvprintw(0, 0, "You have enountered a %s%s%s!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d %s",
               p->is_shiny() ? "*" : "", p->get_species(),
               p->is_shiny() ? "*" : "", p->get_hp(), p->get_atk(),
               p->get_def(), p->get_spatk(), p->get_spdef(),
               p->get_speed(), p->get_gender_string());
      mvprintw(5, 0, "%s, I choose you!\n\tHP:%d ATK:%d DEF:%d SPATK:%d SPDEF:%d SPEED:%d  \nMoves: 1. %s\n2. %s", cur->get_species(), cur->get_hp(), cur->get_atk(),
               cur->get_def(), cur->get_spatk(), cur->get_spdef(), cur->get_speed(), cur->get_move(0), cur->get_move(1));
      mvprintw(10, 0, "%s did %d damage!", cur->get_species(), dam);
      refresh();
      getch();
    }
    else if (input == 'b')
      io_backpack(1);

    else if (input == 'r')
    {
      int odds = ((cur->get_speed() * 32) / ((p->get_speed() / 4) % 256)) + 30 * runs;
      runs++;
      if (odds > rand() % 256)
        battle = 0;
    }
    if (p->get_hp() == 0)
      battle = 0;

    dam = p->get_dam(rand() % 1 + 1, rand() % 16 + 85);
    cur->set_hp(-1 * dam);
    mvprintw(10, 0, "%s did %d damage!", p->get_species(), dam);
    refresh();
    getch();
    if (cur->get_hp() == 0)
      battle = 0;
  } while (battle);
  
}

void io_handle_input(pair_t dest)
{
  uint32_t turn_not_consumed;
  int key;

  do
  {
    switch (key = getch())
    {
    case '7':
    case 'y':
    case KEY_HOME:
      turn_not_consumed = move_pc_dir(7, dest);
      break;
    case '8':
    case 'k':
    case KEY_UP:
      turn_not_consumed = move_pc_dir(8, dest);
      break;
    case '9':
    case 'u':
    case KEY_PPAGE:
      turn_not_consumed = move_pc_dir(9, dest);
      break;
    case '6':
    case 'l':
    case KEY_RIGHT:
      turn_not_consumed = move_pc_dir(6, dest);
      break;
    case '3':
    case 'n':
    case KEY_NPAGE:
      turn_not_consumed = move_pc_dir(3, dest);
      break;
    case '2':
    case 'j':
    case KEY_DOWN:
      turn_not_consumed = move_pc_dir(2, dest);
      break;
    case '1':
    case 'b':
    case KEY_END:
      turn_not_consumed = move_pc_dir(1, dest);
      break;
    case '4':
    case 'h':
    case KEY_LEFT:
      turn_not_consumed = move_pc_dir(4, dest);
      break;
    case '5':
    case ' ':
    case '.':
    case KEY_B2:
      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      turn_not_consumed = 0;
      break;
    case '>':
      turn_not_consumed = move_pc_dir('>', dest);
      break;
    case 'Q':
      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      world.quit = 1;
      turn_not_consumed = 0;
      break;
      break;
    case 't':
      /* Teleport the PC to a random place in the map.              */
      io_teleport_pc(dest);
      turn_not_consumed = 0;
      break;
    case 'T':
      /* Teleport the PC to any map in the world.                   */
      io_teleport_world(dest);
      turn_not_consumed = 0;
      break;
    case 'm':
      io_list_trainers();
      turn_not_consumed = 1;
      break;

    case 'B':
      io_backpack(0);
      break;

    case 'q':
      /* Demonstrate use of the message queue.  You can use this for *
       * printf()-style debugging (though gdb is probably a better   *
       * option.  Not that it matters, but using this command will   *
       * waste a turn.  Set turn_not_consumed to 1 and you should be *
       * able to figure out why I did it that way.                   */
      io_queue_message("This is the first message.");
      io_queue_message("Since there are multiple messages, "
                       "you will see \"more\" prompts.");
      io_queue_message("You can use any key to advance through messages.");
      io_queue_message("Normal gameplay will not resume until the queue "
                       "is empty.");
      io_queue_message("Long lines will be truncated, not wrapped.");
      io_queue_message("io_queue_message() is variadic and handles "
                       "all printf() conversion specifiers.");
      io_queue_message("Did you see %s?", "what I did there");
      io_queue_message("When the last message is displayed, there will "
                       "be no \"more\" prompt.");
      io_queue_message("Have fun!  And happy printing!");
      io_queue_message("Oh!  And use 'Q' to quit!");

      dest[dim_y] = world.pc.pos[dim_y];
      dest[dim_x] = world.pc.pos[dim_x];
      turn_not_consumed = 0;
      break;
    default:
      /* Also not in the spec.  It's not always easy to figure out what *
       * key code corresponds with a given keystroke.  Print out any    *
       * unhandled key here.  Not only does it give a visual error      *
       * indicator, but it also gives an integer value that can be used *
       * for that key in this (or other) switch statements.  Printed in *
       * octal, with the leading zero, because ncurses.h lists codes in *
       * octal, thus allowing us to do reverse lookups.  If a key has a *
       * name defined in the header, you can use the name here, else    *
       * you can directly use the octal value.                          */
      mvprintw(0, 0, "Unbound key: %#o ", key);
      turn_not_consumed = 1;
    }
    refresh();
  } while (turn_not_consumed);
}
