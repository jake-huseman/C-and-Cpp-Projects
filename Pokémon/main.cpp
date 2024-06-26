#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <climits>
#include <ncurses.h>
#include <sys/time.h>
#include <unistd.h>
#include "config.h"
#include "heap.h"
#include "pokedex.h"
#include "character.h"
#include "region.h"
#include "pathfinding.h"
#include "global_events.h"
#include "trainer_events.h"

// Global variables
// 2D array of pointers, each pointer points to one of the regions the world
Region *region_ptr[WORLD_SIZE][WORLD_SIZE] = {NULL};
Pc *pc;
int32_t dist_map_hiker[MAX_ROW][MAX_COL];
int32_t dist_map_rival[MAX_ROW][MAX_COL];
heap_t move_queue;

void usage(const char *argv0) {
  std::cout << "Usage: " << argv0 << " [--numtrainers|--seed] <int>" 
            << std::endl;
  exit(-1);
}

int main (int argc, char *argv[])
{
  int32_t seed;
  int32_t numtrainers_opt = NUM_TRAINERS;
  int32_t loaded_region_x = WORLD_SIZE/2;
  int32_t loaded_region_y = WORLD_SIZE/2;
  int32_t prev_pc_pos_i = -1;
  int32_t prev_pc_pos_j = -1;

/*//////////////////////////////////////////////////////////////////////////////
  if (argc == 2) {
    if (!strcmp(argv[1], "pokemon")) {
      init_pd_pokemon();
    } else if (!strcmp(argv[1], "moves")) {
      init_pd_moves();
    } else if (!strcmp(argv[1], "pokemon_moves")) {
      init_pd_pokemon_moves();
    } else if (!strcmp(argv[1], "pokemon_species")) {
      init_pd_pokemon_species();
    } else if (!strcmp(argv[1], "pokemon_stats")) {
      init_pd_pokemon_stats();
    } else if (!strcmp(argv[1], "experience")) {
      init_pd_experience();
    } else if (!strcmp(argv[1], "type_names")) {
      init_pd_type_name();
    } else if (!strcmp(argv[1], "pokemon_types")) {
      init_pd_pokemon_types();
    } else {
      std::cout << "Usage: " << argv[0] 
                << " [pokemon|moves|pokemon_moves|pokemon_species|pokemon_stats|experience|type_names|pokemon_types]" 
                << std::endl;
      return -1;
    }
  } else {
    std::cout << "Usage: " << argv[0] 
            << " [pokemon|moves|pokemon_moves|pokemon_species|pokemon_stats|experience|type_names|pokemon_types]" 
            << std::endl;
    return -1;
  }
  
  exit(0);
  ////////////////////////////////////////////////////////////////////////////*/
  
   // generate random seed
  struct timeval t, time_last_frame, time_now;
  gettimeofday(&t, NULL);
  seed = (t.tv_usec ^ (t.tv_sec << 20)) & 0xffffffff;

  // handle command line inputs
  if (argc != 1 && argc != 3) {
    usage(argv[0]);
  }
  if (argc == 3) {
    if (!strcmp(argv[1], "--numtrainers")) {
      numtrainers_opt = atoi(argv[2]);
    } else if (!strcmp(argv[1], "--seed")) {
      seed = atoi(argv[2]);
    } else {
      usage(argv[0]);
      return -1;
    }
  }
  srand(seed);
  std::cout << "Using seed: " << seed << std::endl;

  std::cout << "Parsing Pokedex database..."  << std::endl;
  init_pd();

  std::cout << "Initializing terminal..." << std::endl;
  init_terminal();

  // Allocate memory for and generate the starting region
  Region *new_region = new Region(-1, -1, -1, -1, 1, 1);
  region_ptr[WORLD_SIZE/2][WORLD_SIZE/2] = new_region;
  // Pc initialization depends on first region existing
  pc = new Pc(WORLD_SIZE/2, WORLD_SIZE/2);
  // Region population depends on player existing 
  // (Trainer difficulty is calculated by which region the pc is in)
  new_region->populate(numtrainers_opt);

  pc->pick_starter_driver();
  
  Character *c;
  init_trainer_pq(&move_queue, region_ptr[pc->get_x()][pc->get_y()]);
  recalculate_dist_maps(region_ptr[pc->get_x()][pc->get_y()], 
                        pc->get_i(), pc->get_j());

  render_region(new_region);
  usleep(FRAMETIME);
  gettimeofday(&time_last_frame, NULL);

  // Run game
  while(true) { 
    if (pc->get_x() != loaded_region_x || pc->get_y() != loaded_region_y) {
      load_region(pc->get_x(), pc->get_y(), numtrainers_opt);
      init_trainer_pq(&move_queue, region_ptr[pc->get_x()][pc->get_y()]);
      pc_next_region(pc->get_x()    , pc->get_y()    , 
                     loaded_region_x, loaded_region_y);
      loaded_region_x = pc->get_x();
      loaded_region_y = pc->get_y();
    }

    if (pc->get_i() != prev_pc_pos_i || pc->get_j() != prev_pc_pos_j) {
      recalculate_dist_maps(region_ptr[pc->get_x()][pc->get_y()], 
                            pc->get_i(), pc->get_j());
      prev_pc_pos_i = pc->get_i();
      prev_pc_pos_j = pc->get_j();
    }

    int32_t ticks_since_last_frame = 0;
    while (ticks_since_last_frame <= TICKS_PER_FRAME) {
      int32_t step = ((Character*)heap_peek_min(&move_queue))->get_movetime();
      if (step <= TICKS_PER_FRAME) {
        step_all_movetimes(region_ptr[loaded_region_x][loaded_region_y], step);
        while( ((Character*) heap_peek_min(&move_queue))->get_movetime() == 0 ) 
        {
          c = (Character*) heap_remove_min(&move_queue);
          c->process_movement_turn();
          heap_insert(&move_queue, c);
          if (pc->get_x() != loaded_region_x || pc->get_y() != loaded_region_y)
            break;
        }
        if (pc->get_x() != loaded_region_x || pc->get_y() != loaded_region_y)
          break;
      } else {
        step = TICKS_PER_FRAME;
      }
      step_all_movetimes(region_ptr[loaded_region_x][loaded_region_y], step);
      ticks_since_last_frame += step;
    }
    
    // Render game and modulate frame rate
    ////////////////////////////////////////////////////////////////////////////
    render_region(region_ptr[loaded_region_x][loaded_region_y]);
    gettimeofday(&time_now, NULL);
    int32_t timediff = (time_now.tv_sec - time_last_frame.tv_sec) * 1000000 
                      + time_now.tv_usec - time_last_frame.tv_usec;
    int32_t sleep_time = FRAMETIME - timediff;
    if (sleep_time < 0)
      sleep_time = 0;
    // mvprintw(0,0,"FPS: %f", (1000000.0/((float)sleep_time)));
    // refresh();
    usleep(sleep_time);
    gettimeofday(&time_last_frame, NULL);
    ////////////////////////////////////////////////////////////////////////////
  }

  quit_game();

  return 0;
}
