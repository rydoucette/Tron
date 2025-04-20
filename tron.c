/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.

  TODO
    * Add more complex AI for computer players
    * Add support for modifying game setting
    * Add visual effects on tails
    * Add items
*/
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <stdbool.h>

#define GAME_WIDTH  120U
#define GAME_HEIGHT 90U
#define BLOCK_SIZE_IN_PIXELS 8
#define SDL_WINDOW_WIDTH           (BLOCK_SIZE_IN_PIXELS * GAME_WIDTH)
#define SDL_WINDOW_HEIGHT          (BLOCK_SIZE_IN_PIXELS * GAME_HEIGHT)
#define BACKGROUND_SCALE 4
#define STEP_RATE_IN_MILLISECONDS 45
#define PLAYER_COUNT 4 // Todo - make this customizable

// SDL static variables
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_AudioDeviceID audio_device = 0;

// Cell on the game board - shows if any player occupies that square
typedef enum
{
    CELL_NOTHING = 0U,
    CELL_P1 =   1U,
    CELL_P2 =   2U,
    CELL_P3 =   3U,
    CELL_P4 =   4U,
    CELL_DEAD = 5U
} Cell;

// possible states the game can be in
typedef enum
{
    START = 0U,
    RUNNING = 1U,
    PAUSED = 2U,
    GAME_OVER = 3U
} State;

// Possible Game Modes
typedef enum
{
    PVP = 0U,
    PVE = 1U,
} GameMode;

// represents where the players car head is currently located, and its next direction
typedef struct
{
    int head_xpos;
    int head_ypos;
    char next_dir;
    int player_id;
    char player_name[20];
    bool is_human;
    bool is_alive;
} CharacterContext;

// possible direction
typedef enum
{
    DIR_RIGHT,
    DIR_UP,
    DIR_LEFT,
    DIR_DOWN
} CharacterDirection;

// Key mapping for which keys belong to which human players (limitation: max 2 humans)
SDL_Scancode player_keys[2][4] = {
    {SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN},  // Player 1
    {SDL_SCANCODE_D, SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S}              // Player 2
};

// Starting off positions for the players (limitation: max 4 players in game)
int starting_positions[4][2] = {
    {GAME_WIDTH / 4, GAME_HEIGHT / 2},     // Player 1
    {3 * GAME_WIDTH / 4, GAME_HEIGHT / 2}, // Player 2
    {GAME_WIDTH / 4, GAME_HEIGHT / 4},     // Player 3
    {3 * GAME_WIDTH / 4, GAME_HEIGHT / 4}, // Player 4
};

// contains game specific data
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    CharacterContext character_ctx[PLAYER_COUNT]; // Todo - make this customizable
    Cell matrix[GAME_WIDTH][GAME_HEIGHT];
    State state;
    GameMode game_mode;
    int total_human_players;
    int total_computer_players;
    int remaining_players;
    char winner[20];
    Uint64 pause_time;
    bool is_muted;
    Uint64 last_step;
} AppState;

typedef struct
{
    const char* title;
    const char* msg;
    const char* msg2;
    int x;
    int y;
    int w;
    int h;
    SDL_Color bg_color;
    SDL_Color outline_color;
    SDL_Color title_font_color;
    SDL_Color msg_font_color;
} Menu;

typedef struct
{
    Menu menu;
} StartMenu;

// things that are playing sound (the audiostream itself, plus the original data, so we can refill to loop
typedef struct Sound {
    Uint8 *wav_data;
    Uint32 wav_data_len;
    SDL_AudioStream *stream;
} Sound;

static Sound sounds[2];

// Colors
static const SDL_Color COLOR_BG                   = {8, 12, 20, SDL_ALPHA_OPAQUE};     // Deep faded blue
static const SDL_Color COLOR_BG_OUTLINE           = {18, 24, 35, SDL_ALPHA_OPAQUE};    // Subtle grid outline
static const SDL_Color MENU_COLOR                 = {15,15,35,SDL_ALPHA_OPAQUE};
static const SDL_Color MENU_OUTLINE_COLOR         = {0,255,255,SDL_ALPHA_OPAQUE};
static const SDL_Color MENU_TITLE_COLOR           = {255, 255, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color MENU_MESSAGE_COLOR         = {255, 255, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color HIGHLIGHTED_MENU_OPT_COLOR = {125, 249, 255, SDL_ALPHA_OPAQUE};
static const SDL_Color DISABLED_MENU_OPT_COLOR    = {105, 105, 105, SDL_ALPHA_OPAQUE};
static const SDL_Color COLOR_P1                   = {0, 255, 255, SDL_ALPHA_OPAQUE};    // Cyan (classic Tron)
static const SDL_Color COLOR_P2                   = {255, 0, 255, SDL_ALPHA_OPAQUE};    // Magenta (cyberpunky)
static const SDL_Color COLOR_P3                   = {255, 165, 0, SDL_ALPHA_OPAQUE};    // Orange (warm neon)
static const SDL_Color COLOR_P4                   = {0, 255, 128, SDL_ALPHA_OPAQUE};    // Mint green (futuristic)
static const SDL_Color COLOR_CRASHED              = {90, 100, 110, SDL_ALPHA_OPAQUE};

// helper function to create rectangle objects
static void set_rect_xy(SDL_FRect *r, short x, short y, short w, short h, short scaler)
{
    r->x = (float)(x * BLOCK_SIZE_IN_PIXELS);
    r->y = (float)(y * BLOCK_SIZE_IN_PIXELS);
    r->w = BLOCK_SIZE_IN_PIXELS * scaler;
    r->h = BLOCK_SIZE_IN_PIXELS * scaler;
}

// cell in a matrix will be mapped to a player. This maps each player to a sepcific color.
static const SDL_Color get_color_for_cell(Cell cell) {
    switch (cell) {
        case CELL_P1:   return COLOR_P1;
        case CELL_P2:   return COLOR_P2;
        case CELL_P3:   return COLOR_P3;
        case CELL_P4:   return COLOR_P4;
        case CELL_DEAD: return COLOR_CRASHED;
        default:        return COLOR_BG;
    }
}

// Helper function to set the rendering color instead of manually type RGB values everytime
void set_sdl_color(SDL_Renderer *renderer, const SDL_Color *color) {
    SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
}

// Draw a menu with generic attributes such as a title, a message, background and outline
static void draw_menu(SDL_Renderer *renderer, Menu menu) {
    SDL_Texture *texture = NULL;
    SDL_Surface *surface = NULL;
    TTF_Font    *font    = NULL;
    SDL_FRect menu_rect = {menu.x, menu.y, menu.w, menu.h };
    SDL_FRect title_text_rect;
    SDL_FRect msg_text_rect;
    // Menu Background
    set_sdl_color(renderer, &menu.bg_color); 
    SDL_RenderFillRect(renderer, &menu_rect);

    // Menu Outline
    set_sdl_color(renderer, &menu.outline_color);
    SDL_RenderRect(renderer, &menu_rect);

    // Title Text
    font = TTF_OpenFont("ressources/fonts/Audiowide-Regular.ttf", 36.0f);
    surface = TTF_RenderText_Blended(font, menu.title, 0, menu.title_font_color);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_GetTextureSize(texture, &title_text_rect.w, &title_text_rect.h);
    //SDL_SetRenderScale(as->renderer, scale, scale); // FYI - may want to conside looking into scale
    title_text_rect.x = (SDL_WINDOW_WIDTH - title_text_rect.w) / 2;
    title_text_rect.y = (SDL_WINDOW_HEIGHT - title_text_rect.h) / 3;
    set_sdl_color(renderer, &menu.title_font_color);
    SDL_RenderTexture(renderer, texture, NULL, &title_text_rect);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);

    // Message Text
    font = TTF_OpenFont("ressources/fonts/Audiowide-Regular.ttf", 18.0f);
    surface = TTF_RenderText_Blended(font, menu.msg, 0, menu.msg_font_color);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_GetTextureSize(texture, &msg_text_rect.w, &msg_text_rect.h);
    //SDL_SetRenderScale(as->renderer, scale, scale); // FYI - may want to conside looking into scale
    msg_text_rect.x = (SDL_WINDOW_WIDTH - msg_text_rect.w) / 2;
    msg_text_rect.y = (SDL_WINDOW_HEIGHT - msg_text_rect.h) / 2;
    set_sdl_color(renderer, &menu.msg_font_color);
    SDL_RenderTexture(renderer, texture, NULL, &msg_text_rect);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);

    // Message Text
    font = TTF_OpenFont("ressources/fonts/Audiowide-Regular.ttf", 18.0f);
    surface = TTF_RenderText_Blended(font, menu.msg2, 0, menu.msg_font_color);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_GetTextureSize(texture, &msg_text_rect.w, &msg_text_rect.h);
    //SDL_SetRenderScale(as->renderer, scale, scale); // FYI - may want to conside looking into scale
    msg_text_rect.x = (SDL_WINDOW_WIDTH - msg_text_rect.w) / 2;
    msg_text_rect.y = 2*(SDL_WINDOW_HEIGHT - msg_text_rect.h) / 3;
    set_sdl_color(renderer, &menu.msg_font_color);
    SDL_RenderTexture(renderer, texture, NULL, &msg_text_rect);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);
}

// Draw the start menu before the core came cycle kicks off
static void draw_start_menu(SDL_Renderer *renderer, GameMode game_mode, StartMenu start_menu) {
    SDL_Texture *texture = NULL;
    SDL_Surface *surface = NULL;
    TTF_Font    *font    = NULL;
    SDL_FRect msg_text_rect;
    SDL_Color option_color_1;
    SDL_Color option_color_2;

    draw_menu(renderer,start_menu.menu);

    if(game_mode == PVP) {
        option_color_1 = HIGHLIGHTED_MENU_OPT_COLOR;
        option_color_2 = DISABLED_MENU_OPT_COLOR;
    } else {
        option_color_1 = DISABLED_MENU_OPT_COLOR;
        option_color_2 = HIGHLIGHTED_MENU_OPT_COLOR;
    }

    // Message Text
    font = TTF_OpenFont("ressources/fonts/Audiowide-Regular.ttf", 18.0f);
    surface = TTF_RenderText_Blended(font, "Player vs Player", 0, option_color_1);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_GetTextureSize(texture, &msg_text_rect.w, &msg_text_rect.h);
    //SDL_SetRenderScale(as->renderer, scale, scale); // FYI - may want to conside looking into scale
    msg_text_rect.x = (SDL_WINDOW_WIDTH - msg_text_rect.w) / 2;
    msg_text_rect.y = 48 * (SDL_WINDOW_HEIGHT - msg_text_rect.h) / 100;
    set_sdl_color(renderer, &option_color_1);
    SDL_RenderTexture(renderer, texture, NULL, &msg_text_rect);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);

    // Message Text
    font = TTF_OpenFont("ressources/fonts/Audiowide-Regular.ttf", 18.0f);
    surface = TTF_RenderText_Blended(font, "Player vs AI", 0, option_color_2);
    texture = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);
    SDL_GetTextureSize(texture, &msg_text_rect.w, &msg_text_rect.h);
    //SDL_SetRenderScale(as->renderer, scale, scale); // FYI - may want to conside looking into scale
    msg_text_rect.x = (SDL_WINDOW_WIDTH - msg_text_rect.w) / 2;
    msg_text_rect.y = 53 * (SDL_WINDOW_HEIGHT - msg_text_rect.h) / 100;
    set_sdl_color(renderer, &option_color_2);
    SDL_RenderTexture(renderer, texture, NULL, &msg_text_rect);
    SDL_DestroyTexture(texture);
    TTF_CloseFont(font);
}

// sets winner as the player name of the last one standing
void set_winner(void *appstate) {
    AppState *as = (AppState *)appstate;
    CharacterContext ctx;
    int total_players = as->total_human_players + as->total_computer_players;
    for(int i = 0; i < total_players; i++) {
        ctx = as->character_ctx[i];
        if(ctx.is_alive) {
            strcpy(as->winner,as->character_ctx[i].player_name);
        }
    }
}
// Draw background, first thing that will get drawn on every game cycle
static void draw_background(SDL_Renderer *renderer, const SDL_Color *bg_color, const SDL_Color *bg_outline_color) {
    SDL_FRect r;
    set_sdl_color(renderer, bg_color);
    SDL_RenderClear(renderer);
    set_sdl_color(renderer, bg_outline_color);
    for(int i = 0; i < GAME_WIDTH; i += BACKGROUND_SCALE) {
        for(int j = 0; j < GAME_HEIGHT; j += BACKGROUND_SCALE) {
            set_rect_xy(&r, i, j, BLOCK_SIZE_IN_PIXELS, BLOCK_SIZE_IN_PIXELS, BACKGROUND_SCALE);
            SDL_RenderRect(renderer, &r);
        }
    } 
}

// Draw each character and their tails on the game board
static void draw_game_board(SDL_Renderer *renderer, Cell matrix[GAME_WIDTH][GAME_HEIGHT]) {
    //int cell;
    int cell;
    SDL_FRect r;
    for (int i = 0; i < GAME_WIDTH; i++) {
        for (int j = 0; j < GAME_HEIGHT; j++) {
            cell = matrix[i][j];
            if(cell != CELL_NOTHING) {
                SDL_Color cell_color = get_color_for_cell(cell);
                set_sdl_color(renderer, &cell_color);
                set_rect_xy(&r, i, j, BLOCK_SIZE_IN_PIXELS, BLOCK_SIZE_IN_PIXELS, 1);
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
}

// checks if player collided with another player
bool collides_with_player(Cell matrix[GAME_WIDTH][GAME_HEIGHT], int x, int y) {
    if(matrix[x][y] != CELL_NOTHING)
        return true;
    return false;
}

// Check if player collided with a wall
bool collides_with_wall(Cell matrix[GAME_WIDTH][GAME_HEIGHT], int x, int y) {
    if(x < 0 || x >= GAME_WIDTH || y < 0 || y >= GAME_HEIGHT)
        return true;
    return false;
}

// Checks if player collided with a wall or another player's tail
bool is_collision(Cell matrix[GAME_WIDTH][GAME_HEIGHT], int x, int y) {
    if(collides_with_wall(matrix,x,y))
        return true;
    if(collides_with_player(matrix,x,y))
        return true;
    return false;
}

// Returns the number of empty cells in a single direction from a particular location
int get_path_length(Cell matrix[GAME_WIDTH][GAME_HEIGHT], int x, int y, CharacterDirection direction) {
   int path_length = 0;
   int x_increment = 0;
   int y_increment = 0;
   switch (direction) {
    case DIR_RIGHT:
        x_increment = 1;
        break;
    case DIR_UP:
        y_increment = -1;
        break;
    case DIR_LEFT:
        x_increment =  -1;
        break;
    case DIR_DOWN:
        y_increment = 1;
        break;
    default:
        SDL_Log("ERROR - Invalid direction");
        break;
    }

    x += x_increment;
    y += y_increment;
    bool collision = is_collision(matrix,x,y);
    while(!collision) {
        path_length++;
        x += x_increment;
        y += y_increment;
        collision = is_collision(matrix,x,y);
    }
    return path_length;
}

// Picks a direction for the computer - currently checks with the direction with the most uninterupted cells
CharacterDirection pick_next_dir(Cell matrix[GAME_WIDTH][GAME_HEIGHT], int head_xpos, int head_ypos, CharacterDirection curr_dir) {
    char next_dir = curr_dir;
    int longest_path = 0;

    for(int i = 0; i < 4; i++) {
        //Can't go backwards
        if((curr_dir == DIR_LEFT  && i == DIR_RIGHT) ||
           (curr_dir == DIR_RIGHT && i == DIR_LEFT)  ||
           (curr_dir == DIR_UP    && i == DIR_DOWN)  ||
           (curr_dir == DIR_DOWN  && i == DIR_UP)) {
            continue;
        }

        int current_run = get_path_length(matrix, head_xpos, head_ypos, (CharacterDirection)i);
        if(current_run > longest_path) {
            longest_path = current_run;
            next_dir = (CharacterDirection)i;
        }
    }
    return next_dir;
}

// Moves player forward one unit in a particular direction
void move_head(CharacterContext *ctx) {
    // Move head forward
    switch (ctx->next_dir) {
        case DIR_RIGHT:
            ctx->head_xpos++;
            break;
        case DIR_UP:
            ctx->head_ypos--;
            break;
        case DIR_LEFT:
            ctx->head_xpos--;
            break;
        case DIR_DOWN:
            ctx->head_ypos++;
            break;
        default:
            break;
    }
}

void handle_collision(void *appstate, int player_id) {
    AppState *as = (AppState *)appstate;

    int index = -1;
    for(int i = 0; i < PLAYER_COUNT; i++) {
        if(as->character_ctx[i].player_id == player_id) {
            index = i;
            break;
        }
    }

    as->character_ctx[index].is_alive = false;
    as->remaining_players--; 

    for(int i = 0; i < GAME_WIDTH; i++) {
        for(int j = 0; j < GAME_HEIGHT; j++) {
            if(as->matrix[i][j] == (Cell) player_id)
                as->matrix[i][j] = CELL_DEAD;
        }
    }

    if (SDL_GetAudioStreamQueued(sounds[1].stream) < ((int) sounds[1].wav_data_len)) {
        SDL_PutAudioStreamData(sounds[1].stream, sounds[1].wav_data, (int) sounds[1].wav_data_len);
    }

}

// Checks what direction player has inputed, updates position and checks for collision
void move_player(CharacterContext *ctx, void *appstate) {
    AppState *as = (AppState *)appstate;
    if (collides_with_wall(as->matrix,ctx->head_xpos, ctx->head_ypos)) {
        SDL_Log("ERROR - The player: %d moved out of bounds at an unexpected time \n", ctx->player_id);
    }
    // if player is a computer, determine which direction go
    if(!ctx->is_human)
        ctx->next_dir = pick_next_dir(as->matrix, ctx->head_xpos, ctx->head_ypos, ctx->next_dir);
    move_head(ctx);
    if(is_collision(as->matrix,ctx->head_xpos, ctx->head_ypos)) {
        handle_collision(as, ctx->player_id);
    } else {
        as->matrix[ctx->head_xpos][ctx->head_ypos] = ctx->player_id;
    }
}

// Update the positions of all players in the game if they are still alive
void move_characters(void *appstate) {
    AppState *as = (AppState *)appstate;
    CharacterContext *ctx;
    int total_players = as->total_human_players + as->total_computer_players;

    for(int i = 0; i < total_players; i++) {
        ctx = &as->character_ctx[i];
        if(ctx->is_alive)
            move_player(ctx, as);
    }
}

void toggle_mute(void *appstate) {
    AppState *as = (AppState *)appstate;
    float volume = 0.0;
    if(as->is_muted)
        volume = 1.0;
    as->is_muted = !as->is_muted;
    SDL_SetAudioDeviceGain(audio_device, volume);
}

// Pause and unpause the game depending on current state
void toggle_pause(void *appstate) {
    AppState *as = (AppState *)appstate;

    if(as->state == RUNNING) {
        // Pausing
        as->pause_time = SDL_GetTicks();
        as->state = PAUSED;
    } else if(as->state == PAUSED) {
        // Unpausing - shift last_step forward to ignore time spent paused
        Uint64 now = SDL_GetTicks();
        Uint64 paused_duration = now - as->pause_time;
        as->last_step += paused_duration;
        as->state = RUNNING;
    } else {
        // do nothing
    }
}

// Reset the game board so that each cell is empty
void initialize_game_board(Cell matrix[GAME_WIDTH][GAME_HEIGHT]) {
    for(int i = 0; i < GAME_WIDTH; i++) {
        for(int j = 0; j < GAME_HEIGHT; j++) {
            matrix[i][j] = CELL_NOTHING;
        }
    }
}

// Create required amount of characters, determine their starting positions, directions and attributes
void initialize_characters(void *appstate) {
    AppState *as = (AppState *)appstate;
    int humans_added = 0;
    int computers_added = 0;
    int total_players = as->total_human_players + as->total_computer_players;
    for(int i = 0; i < total_players; i++) {

        // Add the character, set is_human and the player name
        if(humans_added < as->total_human_players) {
            as->character_ctx[i].is_human = true;
            humans_added++;
            sprintf(as->character_ctx[i].player_name, "P%d", humans_added);
        } else {
            as->character_ctx[i].is_human = false;
            computers_added++;
            sprintf(as->character_ctx[i].player_name, "CPU%d", computers_added);
        }

        // alive enabled
        as->character_ctx[i].is_alive = true;

        // Set starting positions
        as->character_ctx[i].head_xpos = starting_positions[i][0];
        as->character_ctx[i].head_ypos = starting_positions[i][1];
        
        // Set player IDs
        as->character_ctx[i].player_id = computers_added + humans_added;

        // Set starting direction
        switch (i) {
        case 0:
            as->character_ctx[i].next_dir = DIR_RIGHT;
            break;
        case 1:
            as->character_ctx[i].next_dir = DIR_LEFT;
            break;
        case 2:
            as->character_ctx[i].next_dir = DIR_UP;
            break;
        case 3:
            as->character_ctx[i].next_dir = DIR_DOWN;
            break;
        default:
            SDL_Log("ERROR - Invalid player id: %d\n", i);
            break;
        }
    }
}

// Kick off the core game cycle
void start_game(void *appstate) {
    AppState *as = (AppState *)appstate;

    initialize_game_board(as->matrix);
    as->state = RUNNING;
    as->last_step  = SDL_GetTicks();

    // Set the number of players and computers
    if(as->game_mode == PVP) {
        as->total_human_players = 2;                 // Todo - make this customizable
        as->total_computer_players = PLAYER_COUNT-2; // Todo - make this customizable
    } else {
        as->total_human_players = 1;                  // Todo - make this customizable
        as->total_computer_players = PLAYER_COUNT -1; // Todo - make this customizable
    }
 
    as->remaining_players = as->total_human_players + as->total_computer_players;

    initialize_characters(as);
}

static bool init_sound(const char *fname, Sound *sound)
{
    bool retval = false;
    SDL_AudioSpec spec;
    char *wav_path = NULL;

    /* Load the .wav files from wherever the app is being run from. */
    SDL_asprintf(&wav_path, "%s", fname);  /* allocate a string of the full file path */
    if (!SDL_LoadWAV(wav_path, &spec, &sound->wav_data, &sound->wav_data_len)) {
        SDL_Log("Couldn't load .wav file: %s", SDL_GetError());
        return false;
    }

    /* Create an audio stream. Set the source format to the wav's format (what
       we'll input), leave the dest format NULL here (it'll change to what the
       device wants once we bind it). */
    sound->stream = SDL_CreateAudioStream(&spec, NULL);
    if (!sound->stream) {
        SDL_Log("Couldn't create audio stream: %s", SDL_GetError());
    } else if (!SDL_BindAudioStream(audio_device, sound->stream)) {  /* once bound, it'll start playing when there is data available! */
        SDL_Log("Failed to bind '%s' stream to device: %s", fname, SDL_GetError());
    } else {
        retval = true;  /* success! */
    }

    SDL_free(wav_path);  /* done with this string. */
    return retval;
}


// map key presses to specific characters and game controls such as pause, reset, enter etc.
static SDL_AppResult handle_key_event(void *appstate, SDL_Scancode key_code) {
    AppState *as = (AppState *)appstate;
    CharacterContext *ctx = NULL;

    // find out which player's key was pressed if any
    for(int i = 0; i < as->total_human_players; i++) {
        for(int j = 0; j < 4; j++) {
            if(player_keys[i][j] == key_code) {
                ctx = &as->character_ctx[i];
                break;
            }
        }
    }
    switch (key_code) {
    /* Start the game. */
    case SDL_SCANCODE_KP_ENTER:
    case SDL_SCANCODE_RETURN:
        if(as->state == START) {
            //go to start2
            //if start2
            start_game(as);
        }
        break;
    /* Quit. */
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_Q:
        return SDL_APP_SUCCESS;
    /* Restart the game as if the program was launched. */
    case SDL_SCANCODE_R:
    case SDL_SCANCODE_SPACE:
        start_game(as);
        break;
    /* Decide new direction of the character. */
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        if(as->state == RUNNING && ctx && ctx->next_dir != DIR_LEFT) {
            ctx->next_dir = DIR_RIGHT;
        }
        break;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
        if(as->state == START) {
            as->game_mode ^= 1U;
        }
        if(as->state == RUNNING && ctx && ctx->next_dir != DIR_DOWN) {
            ctx->next_dir = DIR_UP;
        }
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        if(as->state == RUNNING && ctx && ctx->next_dir != DIR_RIGHT) {
            ctx->next_dir = DIR_LEFT;
        }
        break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        if(as->state == START) {
            as->game_mode ^= 1U;
        }
        if(as->state == RUNNING && ctx && ctx->next_dir != DIR_UP) {
            ctx->next_dir = DIR_DOWN;
        }
        break;
    /* Pause the game. */
    case SDL_SCANCODE_P:
        toggle_pause(as);
        break;
    case SDL_SCANCODE_M:
        toggle_mute(as);
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// This function runs once at startup
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {    
    
    // AppState stores various game specific information
    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as) {
        return SDL_APP_FAILURE;
    }
    *appstate = as;

    /* SDL */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Create the window and renderer */
    if (!SDL_CreateWindowAndRenderer("TRON", SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, 0, &as->window, &as->renderer)) {
        return SDL_APP_FAILURE;
    }

    /* Metadata */
    if (!SDL_SetAppMetadata("TRON", "1.0", "TRONv1.0")) {
        return SDL_APP_FAILURE;
    }

    /* SDL_ttf */
    if (!TTF_Init()) {
        SDL_Log("Couldn't initialise SDL_ttf: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

     /* open the default audio device in whatever format it prefers; our audio streams will adjust to it. */
     audio_device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, NULL);
     if (audio_device == 0) {
         SDL_Log("Couldn't open audio device: %s", SDL_GetError());
         return SDL_APP_FAILURE;
     }
 
     if (!init_sound("ressources/sounds/neon-dreams.wav", &sounds[0])) {
         return SDL_APP_FAILURE;
     } else if (!init_sound("ressources/sounds/crash.wav", &sounds[1])) {
         return SDL_APP_FAILURE;
     }

    /* Initialize some required AppState variables. The rest gets covered on game start */
    as->state      = START;
    as->pause_time = SDL_GetTicks();
    as->last_step  = SDL_GetTicks();
    as->game_mode  = PVP;
    as->is_muted   = false;

    return SDL_APP_CONTINUE;
}

// This function runs when a new event (mouse input, keypresses, etc) occurs
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    AppState *as = (AppState *)appstate;
    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    case SDL_EVENT_KEY_DOWN:
        return handle_key_event(as, event->key.scancode);
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

// This function runs once per frame, and is the heart of the program
SDL_AppResult SDL_AppIterate(void *appstate) {
    AppState *as = (AppState *)appstate;
    StartMenu start_menu;
    Menu start_sub_menu;
    Menu pause_menu;
    Menu game_over_menu;
    char winner_text_buffer[50];
    const Uint64 now = SDL_GetTicks();
    SDL_FRect r;
    int cell;

    for (int i = 0; i < 1; i++) {
        if (SDL_GetAudioStreamQueued(sounds[i].stream) < ((int) sounds[i].wav_data_len)) {
            SDL_PutAudioStreamData(sounds[i].stream, sounds[i].wav_data, (int) sounds[i].wav_data_len);
        }
    } 

    draw_background(as->renderer, &COLOR_BG, &COLOR_BG_OUTLINE);
    
    switch (as->state) {
    case RUNNING:
        // Update characters positions internally if game is not paused
        while (now - as->last_step >= STEP_RATE_IN_MILLISECONDS) {
            move_characters(as); 
            as->last_step += STEP_RATE_IN_MILLISECONDS;
        }
        if(as->remaining_players <= 1) {
            as->state = GAME_OVER;
            set_winner(as);
        }
        draw_game_board(as->renderer, as->matrix);
        break;
    case PAUSED: 
        draw_game_board(as->renderer, as->matrix);
        pause_menu.title = "PAUSED";
        pause_menu.msg = "";
        pause_menu.msg2 = "Press P to continue";
        pause_menu.x = SDL_WINDOW_WIDTH / 3;
        pause_menu.y = SDL_WINDOW_HEIGHT / 4;
        pause_menu.w = SDL_WINDOW_WIDTH / 3;
        pause_menu.h = SDL_WINDOW_HEIGHT / 2;
        pause_menu.bg_color = MENU_COLOR;
        pause_menu.outline_color = MENU_OUTLINE_COLOR;
        pause_menu.title_font_color = MENU_TITLE_COLOR;
        pause_menu.msg_font_color = MENU_MESSAGE_COLOR;
        draw_menu(as->renderer, pause_menu);
        break;
    case GAME_OVER:
        draw_game_board(as->renderer, as->matrix);
        sprintf(winner_text_buffer, "%s WINS", as->winner);
        game_over_menu.title = "GAME OVER";
        game_over_menu.msg  = winner_text_buffer;
        game_over_menu.msg2 = "Press SPACE to restart";
        game_over_menu.x = SDL_WINDOW_WIDTH / 3;
        game_over_menu.y = SDL_WINDOW_HEIGHT / 4;
        game_over_menu.w = SDL_WINDOW_WIDTH / 3;
        game_over_menu.h = SDL_WINDOW_HEIGHT / 2;
        game_over_menu.bg_color = MENU_COLOR;
        game_over_menu.outline_color = MENU_OUTLINE_COLOR;
        game_over_menu.title_font_color = MENU_TITLE_COLOR;
        game_over_menu.msg_font_color = MENU_MESSAGE_COLOR;    
        draw_menu(as->renderer, game_over_menu);
        break;
    case START:
        start_sub_menu.title = "Tron";
        start_sub_menu.msg = "";
        start_sub_menu.msg2 = "Press ENTER to begin";
        start_sub_menu.x = SDL_WINDOW_WIDTH / 3;
        start_sub_menu.y = SDL_WINDOW_HEIGHT / 4;
        start_sub_menu.w = SDL_WINDOW_WIDTH / 3;
        start_sub_menu.h = SDL_WINDOW_HEIGHT / 2;
        start_sub_menu.bg_color = MENU_COLOR;
        start_sub_menu.outline_color = MENU_OUTLINE_COLOR;
        start_sub_menu.title_font_color = MENU_TITLE_COLOR;
        start_sub_menu.msg_font_color = MENU_MESSAGE_COLOR;    
        start_menu.menu = start_sub_menu;
        draw_start_menu(as->renderer, as->game_mode, start_menu);
        break;
    default:
        break;
    }

    SDL_RenderPresent(as->renderer);
    return SDL_APP_CONTINUE;
}

// This function runs once at shutdown
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (appstate != NULL) {
        AppState *as = (AppState *)appstate;
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        SDL_free(as);
    }

    SDL_CloseAudioDevice(audio_device);
 
    for (int i = 0; i < SDL_arraysize(sounds); i++) {
        if (sounds[i].stream) {
            SDL_DestroyAudioStream(sounds[i].stream);
        }
        SDL_free(sounds[i].wav_data);
    }
}