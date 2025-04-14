/*
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely.
*/
#define SDL_MAIN_USE_CALLBACKS 1  /* use the callbacks instead of main() */
#include <stdio.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3_ttf/SDL_ttf.h>

#define GAME_WIDTH  120U
#define GAME_HEIGHT 90U
#define MATRIX_SIZE (GAME_WIDTH * GAME_HEIGHT)
#define BLOCK_SIZE_IN_PIXELS 8
#define BACKGROUND_SCALE 4
#define STEP_RATE_IN_MILLISECONDS 45
#define SDL_WINDOW_WIDTH           (BLOCK_SIZE_IN_PIXELS * GAME_WIDTH)
#define SDL_WINDOW_HEIGHT          (BLOCK_SIZE_IN_PIXELS * GAME_HEIGHT)
#define PLAYER_COUNT 2

// SDL static variables
static SDL_Window *window = NULL;
static SDL_Renderer *renderer = NULL;
static SDL_Texture *texture = NULL;
static TTF_Font *font = NULL;

extern unsigned char tiny_ttf[];
extern unsigned int tiny_ttf_len;

// Cell on the game board - shows if any player occupies that square
typedef enum
{
    CELL_NOTHING = 0U,
    CELL_P1 = 1U,
    CELL_P2 = 2U,
    CELL_P3 = 3U,
    CELL_P4 = 4U,
} Cell;

// possible states the game can be in
typedef enum
{
    START = 0U,
    RUNNING = 1U,
    PAUSED = 2U,
    GAME_OVER = 3U
} State;

// represents where the players car is currently located, and where it's heading
typedef struct
{
    int head_xpos;
    int head_ypos;
    char next_dir;
    int player_id;
} CharacterContext;

SDL_Scancode player_keys[PLAYER_COUNT][4] = {
    {SDL_SCANCODE_RIGHT, SDL_SCANCODE_LEFT, SDL_SCANCODE_UP, SDL_SCANCODE_DOWN},  // Player 1
    {SDL_SCANCODE_D, SDL_SCANCODE_A, SDL_SCANCODE_W, SDL_SCANCODE_S},  // Player 2
};

int player_positions[PLAYER_COUNT][2] = {
    {GAME_WIDTH / 4, GAME_HEIGHT / 2},   // Player 1
    {3 * GAME_WIDTH / 4, GAME_HEIGHT / 2}, // Player 2
   //{GAME_WIDTH / 4, GAME_HEIGHT / 2},
};

// contains game specific data
typedef struct
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    //SDL_Texture *texture;
    CharacterContext character_ctx[PLAYER_COUNT];
    int num_players;
    //int is_paused;
    State state;
    Uint64 pause_time;
    Cell matrix[GAME_WIDTH][GAME_HEIGHT];
    Uint64 last_step;
} AppState;

// possible direction
typedef enum
{
    DIR_RIGHT,
    DIR_UP,
    DIR_LEFT,
    DIR_DOWN
} CharacterDirection;

typedef struct
{
    int r;
    int g;
    int b;
    int a;
} Color;

// Colors
static const Color COLOR_BG         = {8, 12, 20, SDL_ALPHA_OPAQUE};   // Deep faded blue
static const Color COLOR_BG_OUTLINE = {18, 24, 35, SDL_ALPHA_OPAQUE};  // Subtle grid outline
static const Color COLOR_P1 = {0, 255, 255, SDL_ALPHA_OPAQUE};   // Cyan (classic Tron)
static const Color COLOR_P2 = {255, 0, 255, SDL_ALPHA_OPAQUE};   // Magenta (cyberpunky)
static const Color COLOR_P3 = {255, 165, 0, SDL_ALPHA_OPAQUE};   // Orange (warm neon)
static const Color COLOR_P4 = {0, 255, 128, SDL_ALPHA_OPAQUE};   // Mint green (futuristic)

static void set_rect_xy(SDL_FRect *r, short x, short y, short w, short h, short scaler)
{
    r->x = (float)(x * BLOCK_SIZE_IN_PIXELS);
    r->y = (float)(y * BLOCK_SIZE_IN_PIXELS);
    r->w = BLOCK_SIZE_IN_PIXELS * scaler;
    r->h = BLOCK_SIZE_IN_PIXELS * scaler;
}

static const Color get_color_for_cell(Cell cell) {
    switch (cell) {
        case CELL_P1: return COLOR_P1;
        case CELL_P2: return COLOR_P2;
        case CELL_P3: return COLOR_P3;
        case CELL_P4: return COLOR_P4;
        default:      return COLOR_BG;
    }
}

void set_sdl_color(SDL_Renderer *renderer, const Color *color) {
    SDL_SetRenderDrawColor(renderer, color->r, color->g, color->b, color->a);
}

static void draw_pause_menu(SDL_Renderer *renderer){
    const char* menu_text = "GAME OVER";
    SDL_FRect r = {SDL_WINDOW_WIDTH/3,SDL_WINDOW_HEIGHT/4,SDL_WINDOW_WIDTH / 3, SDL_WINDOW_HEIGHT/2 };
    const Color pause_menu_color         = {15,15,35,SDL_ALPHA_OPAQUE};
    const Color pause_menu_outline_color = {0,255,255,SDL_ALPHA_OPAQUE};
    const Color title_font_color  = {255, 255, 255, SDL_ALPHA_OPAQUE};
    const Color message_font_color = {255, 255, 255, SDL_ALPHA_OPAQUE};

    // Menu Background
    set_sdl_color(renderer, &pause_menu_color); 
    SDL_RenderFillRect(renderer, &r);
    r = (SDL_FRect){SDL_WINDOW_WIDTH / 3, SDL_WINDOW_HEIGHT / 4, SDL_WINDOW_WIDTH / 3, SDL_WINDOW_HEIGHT / 2};
    
    // Menu Outline
    set_sdl_color(renderer, &pause_menu_outline_color);
    SDL_RenderRect(renderer, &r);

    
    /* Create the text */
    SDL_Color color = { 255, 0, 51, SDL_ALPHA_OPAQUE };
    SDL_Surface *text;
    text = TTF_RenderText_Blended(font, "GAME OVER", 0, color);
    if (text) {
        texture = SDL_CreateTextureFromSurface(renderer, text);
        SDL_DestroySurface(text);
    }
    if (!texture) {
        SDL_Log("Couldn't create text: %s\n", SDL_GetError());
        //return SDL_APP_FAILURE;
    }

    int w = 0, h = 0;
    SDL_FRect dst;
    const float scale = 1.0f;

    /* Center the text and scale it up */
    SDL_GetRenderOutputSize(renderer, &w, &h);
    //SDL_SetRenderScale(as->renderer, scale, scale);
    SDL_GetTextureSize(texture, &dst.w, &dst.h);
    dst.x = ((w / scale) - dst.w) / 2;
    dst.y = ((h / scale) - dst.h) / 3;

    /* Draw the text */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderTexture(renderer, texture, NULL, &dst);
}

static void draw_background(SDL_Renderer *renderer, const Color *bg_color, const Color *bg_outline_color) {
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

static void draw_game_board(SDL_Renderer *renderer, Cell matrix[GAME_WIDTH][GAME_HEIGHT]) {
    //int cell;
    int cell;
    SDL_FRect r;
    for (int i = 0; i < GAME_WIDTH; i++) {
        for (int j = 0; j < GAME_HEIGHT; j++) {
            cell = matrix[i][j];
            if(cell != CELL_NOTHING) {
                Color cell_color = get_color_for_cell(cell);
                set_sdl_color(renderer, &cell_color);
                set_rect_xy(&r, i, j, BLOCK_SIZE_IN_PIXELS, BLOCK_SIZE_IN_PIXELS, 1);
                SDL_RenderFillRect(renderer, &r);
            }
        }
    }
}

void move_player(CharacterContext *ctx, AppState *as) {
    // Move head forward
    switch (ctx->next_dir) {
        case DIR_RIGHT:
            ++ctx->head_xpos;
            break;
        case DIR_UP:
            --ctx->head_ypos;
            break;
        case DIR_LEFT:
            --ctx->head_xpos;
            break;
        case DIR_DOWN:
            ++ctx->head_ypos;
            break;
        default:
            break;
    }
        
    int x = ctx->head_xpos;
    int y = ctx->head_ypos;
    
    // crash with wall
    if(x >= GAME_WIDTH || x < 0 || y >= GAME_HEIGHT || y < 0) {
        as->state = GAME_OVER;
        as->pause_time = SDL_GetTicks();
    }
    
    // crash with trail
    if(as->matrix[x][y] != CELL_NOTHING) {
        as->state = GAME_OVER;
        as->pause_time = SDL_GetTicks();
    }

    if(as->state == RUNNING)
        as->matrix[ctx->head_xpos][ctx->head_ypos] = ctx->player_id;

}

void move_characters(void *appstate)
{
    AppState *as = (AppState *)appstate;
    CharacterContext *ctx;
    
    for(int i = 0; i < PLAYER_COUNT; i++) {
        ctx = &as->character_ctx[i];
        move_player(ctx,as);
    }
}

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

void tron_initialize(void *appstate)
{
    AppState *as = (AppState *)appstate;

    // initalize the game board
    for(int i = 0; i < GAME_WIDTH; i++) {
        for(int j = 0; j < GAME_HEIGHT; j++) {
            as->matrix[i][j] = CELL_NOTHING;
        }
    }

    as->num_players = PLAYER_COUNT;
    as->state = RUNNING;
    as->last_step = SDL_GetTicks();

    for(int i = 0; i < as->num_players; i++) {
        as->character_ctx[i].player_id = i + 1;
        as->character_ctx[i].head_xpos = player_positions[i][0];
        as->character_ctx[i].head_ypos = player_positions[i][1];
        if(i == 0) {
            as->character_ctx[i].next_dir = DIR_RIGHT;
        } else {
            as->character_ctx[i].next_dir = DIR_LEFT;
        }
        
    }
}

static SDL_AppResult handle_key_event(void *appstate, SDL_Scancode key_code)
{
    AppState *as = (AppState *)appstate;
    CharacterContext *ctx;

    // find out which player's key was pressed if any
    for(int i = 0; i < PLAYER_COUNT; i++) {
        for(int j = 0; j < 4; j++) {
            if(player_keys[i][j] == key_code) {
                ctx = &as->character_ctx[i];
                break;
            }
        }
    }

    switch (key_code) {
    /* Quit. */
    case SDL_SCANCODE_ESCAPE:
    case SDL_SCANCODE_Q:
        return SDL_APP_SUCCESS;
    /* Restart the game as if the program was launched. */
    case SDL_SCANCODE_R:
    case SDL_SCANCODE_SPACE:
        tron_initialize(as);
        break;
    /* Decide new direction of the character. */
    case SDL_SCANCODE_RIGHT:
    case SDL_SCANCODE_D:
        if(ctx->next_dir != DIR_LEFT) {
            ctx->next_dir = DIR_RIGHT;
        }
        break;
    case SDL_SCANCODE_UP:
    case SDL_SCANCODE_W:
        if(ctx->next_dir != DIR_DOWN) {
            ctx->next_dir = DIR_UP;
        }
        break;
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_A:
        if(ctx->next_dir != DIR_RIGHT) {
            ctx->next_dir = DIR_LEFT;
        }
        break;
    case SDL_SCANCODE_DOWN:
    case SDL_SCANCODE_S:
        if(ctx->next_dir != DIR_UP) {
            ctx->next_dir = DIR_DOWN;
        }
        break;
    /* Pause the game. */
    case SDL_SCANCODE_P:
        toggle_pause(as);
        break;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

/* This function runs once at startup. */
SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[])
{    AppState *as = (AppState *)SDL_calloc(1, sizeof(AppState));
    if (!as) {
        return SDL_APP_FAILURE;
    }
    *appstate = as;

    /* Create the window */
    if (!SDL_CreateWindowAndRenderer("TRON", SDL_WINDOW_WIDTH, SDL_WINDOW_HEIGHT, 0, &as->window, &as->renderer)) {
        return SDL_APP_FAILURE;
    }

    if (!SDL_SetAppMetadata("TRON", "1.0", "TRONv1.0")) {
        return SDL_APP_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("Couldn't initialize SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!TTF_Init()) {
        SDL_Log("Couldn't initialise SDL_ttf: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    /* Open the font */
    font = TTF_OpenFont("fonts/Audiowide-Regular.ttf", 36.0f);
    if (!font) {
        SDL_Log("Couldn't open font: %s\n", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    tron_initialize(as); 
    
    as->last_step = SDL_GetTicks();

    return SDL_APP_CONTINUE;
}

/* This function runs when a new event (mouse input, keypresses, etc) occurs. */
SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event)
{
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

/* This function runs once per frame, and is the heart of the program. */
SDL_AppResult SDL_AppIterate(void *appstate)
{
    AppState *as = (AppState *)appstate;

    const Uint64 now = SDL_GetTicks();
    SDL_FRect r;
    int cell;

    // Update characters positions internally if game is not paused
    while (as->state == RUNNING && ((now - as->last_step) >= STEP_RATE_IN_MILLISECONDS)) {
        move_characters(as);
        as->last_step += STEP_RATE_IN_MILLISECONDS;
    }

    draw_background(as->renderer, &COLOR_BG, &COLOR_BG_OUTLINE);
    draw_game_board(as->renderer, as->matrix);

    switch (as->state)
    {
    case RUNNING:
        break;
    case PAUSED:
        draw_pause_menu(as->renderer);
        break;
    case GAME_OVER:
        //draw_game_over_menu(as->renderer)
        break;
    case START:
        break;
    default:
        break;
    }

    SDL_RenderPresent(as->renderer);
    return SDL_APP_CONTINUE;
}

/* This function runs once at shutdown. */
void SDL_AppQuit(void *appstate, SDL_AppResult result)
{
    if (appstate != NULL) {
        AppState *as = (AppState *)appstate;
        SDL_DestroyRenderer(as->renderer);
        SDL_DestroyWindow(as->window);
        SDL_free(as);
    }
}