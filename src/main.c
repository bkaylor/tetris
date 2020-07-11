#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_image.h"

#include "vec2.h"
#include "draw.h"

#define TICK_TIME 750

#define DEBUG_PRINT(_a, _b) do {                                               \
        sprintf(buf, _a, _b);                                                  \
        draw_text(renderer, 0, y, buf, font, (SDL_Color){255, 255, 255, 255}); \
        y += 15;                                                               \
    } while (0)


typedef enum {
    I = 1, 
    O, 
    T, 
    J, 
    L, 
    S, 
    Z,
    DOT,
} Tetronimo_Type;

typedef struct {
    int width;
    int height;
    vec2 pivot;
    bool cells[16];
} BoundingBox;

typedef struct {
    int id;
    Tetronimo_Type type;
    bool grounded;
    bool deleted;

    vec2 position;
    float rotation;

    BoundingBox bounding_box;
} Tetronimo;

typedef struct {
    Tetronimo entities[1000];
    int entity_count;

    Tetronimo *active;

    int width;
    int height;

    SDL_Rect rect;
    float cell_size;

    bool check_for_clear;
    int score;
} Board;

typedef struct {
    int x;
    int y;
} Window;

typedef struct {
    Window window;

    struct {
        int x;
        int y;
        bool clicked;
    } mouse;

    Board board;

    bool do_left_move;
    bool do_right_move;
    bool do_down_move;
    bool do_drop;
    bool do_rotate_clockwise;
    bool do_rotate_counter_clockwise;

    int turn_timer;
    int turn_count;

    bool reset;
    bool quit;
} State;

void render(SDL_Renderer *renderer, State state, TTF_Font *font)
{
    SDL_RenderClear(renderer);

    // Set background color.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);

    // Draw the board.
    SDL_SetRenderDrawColor(renderer, 35, 35, 35, 255);
    SDL_RenderFillRect(renderer, &state.board.rect);

    // Draw the entities.
    for (int i = 0; i < state.board.entity_count; i += 1)
    {
        Tetronimo *t = &state.board.entities[i];

        if (t->deleted) continue;

        switch (t->type)
        {
            case I:   SDL_SetRenderDrawColor(renderer, 0,   255, 255, 255); break;
            case O:   SDL_SetRenderDrawColor(renderer, 255, 255, 0,   255); break;
            case T:   SDL_SetRenderDrawColor(renderer, 128, 0,   128, 255); break;
            case J:   SDL_SetRenderDrawColor(renderer, 0,   0,   255, 255); break;
            case L:   SDL_SetRenderDrawColor(renderer, 255, 165, 0,   255); break;
            case S:   SDL_SetRenderDrawColor(renderer, 0,   255, 0,   255); break;
            case Z:   SDL_SetRenderDrawColor(renderer, 255, 0,   0,   255); break;
            case DOT: SDL_SetRenderDrawColor(renderer, 35,  35,  35,  255); break;
        }

        for (int i = 0; i < t->bounding_box.width; i += 1)
        {
            for (int j = 0; j < t->bounding_box.height; j += 1)
            {
                if (t->bounding_box.cells[(j*t->bounding_box.width) + i])
                {
                    SDL_Rect rect = (SDL_Rect){
                        state.board.rect.x + ((t->position.x + i) * state.board.cell_size),
                        state.board.rect.y + ((t->position.y + j) * state.board.cell_size),
                        state.board.cell_size,
                        state.board.cell_size,
                    };

                    SDL_RenderFillRect(renderer, &rect);

                }
            }
        }

        SDL_SetRenderDrawColor(renderer, 255, 100, 255, 255);
        draw_circle(renderer, 
                    state.board.rect.x + ((t->position.x) * state.board.cell_size),
                    state.board.rect.y + ((t->position.y) * state.board.cell_size),
                    3);

        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        draw_circle(renderer, 
                    state.board.rect.x + ((t->position.x + t->bounding_box.pivot.x) * state.board.cell_size),
                    state.board.rect.y + ((t->position.y + t->bounding_box.pivot.y) * state.board.cell_size),
                    3);
    }

    // Draw text.
    char buf[50];
    int y = 0;

    DEBUG_PRINT("%d ms", state.turn_timer);
    DEBUG_PRINT("%d lines", state.board.score);
    DEBUG_PRINT("%d entities", state.board.entity_count);

    SDL_RenderPresent(renderer);
}

bool solid_below(Tetronimo *tetronimo, Board *board)
{
    for (int i = 0; i < tetronimo->bounding_box.width; i += 1)
    {
        for (int j = 0; j < tetronimo->bounding_box.height; j += 1)
        {
            int index = j*tetronimo->bounding_box.width + i;
            if (!tetronimo->bounding_box.cells[index]) continue;

            int width = i + tetronimo->position.x;
            int height = j + tetronimo->position.y;

            if (height == board->height - 1) return true;

            if (board->entity_count <= 1) continue;

            for (int k = 0; k < board->entity_count - 1; k += 1)
            {
                Tetronimo *t = &board->entities[k];
                if (t->deleted) continue;
                if (!t->grounded) continue;
                if (t->id == tetronimo->id) continue;

                for (int m = 0; m < t->bounding_box.width; m += 1)
                {
                    for (int n = 0; n < t->bounding_box.width; n += 1)
                    {
                        int inner_index = n*t->bounding_box.width + m;
                        if (!t->bounding_box.cells[inner_index]) continue;

                        int inner_width = m + t->position.x;
                        int inner_height = n + t->position.y;

                        if (inner_width == width && inner_height == (height + 1)) 
                        {
                            return true;
                        }
                    }
                }
            }
        }
    }

    return false;
}

void update(State *state, Uint64 dt) 
{
    Board *b = &state->board;

    if (state->reset)
    {
        b->width = 10;
        b->height = 20;
        b->entity_count = 0;

        float cell_width  = state->window.x / b->width;
        float cell_height = state->window.y / b->height;

        float cell_size = cell_width < cell_height ? cell_width : cell_height;
        b->cell_size = cell_size;

        b->rect.w = cell_size * b->width;
        b->rect.h = cell_size * b->height;

        b->rect.y = state->window.y - b->rect.h;
        b->rect.x = (state->window.x/2) - (b->rect.w/2);

        b->active = NULL;
        b->check_for_clear = false;
        b->score = 0;

        state->do_drop             = false;
        state->do_left_move        = false;
        state->do_right_move       = false;
        state->do_down_move        = false;
        state->do_rotate_clockwise = false;

        state->turn_timer = TICK_TIME;
        state->turn_count = 0;

        state->reset = false;
    }

    if (!b->active)
    {
        // Spawn a tetronimo.
        Tetronimo t;
        t.type = (rand() % 7) + 1;
        t.rotation = 0.0f;
        t.position = vec2_make(b->width/2, 0 /*b->height*/);
        t.grounded = false;
        t.id = b->entity_count;
        t.deleted = false;

        BoundingBox box;
        switch (t.type)
        {
            case I: {
                box.width = 4;
                box.height = 4;
                box.pivot = vec2_make(2.0f, 2.0f);
                bool cells[16] = {
                    0, 0, 0, 0, 
                    1, 1, 1, 1, 
                    0, 0, 0, 0, 
                    0, 0, 0, 0
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case O: {
                box.width = 4;
                box.height = 3;
                box.pivot = vec2_make(2.0f, 1.0f);
                bool cells[16] = {
                    0, 1, 1, 0, 
                    0, 1, 1, 0, 
                    0, 0, 0, 0, 
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case T: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    0, 1, 0,
                    1, 1, 1,
                    0, 0, 0,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case J: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    0, 1, 0,
                    0, 1, 0,
                    1, 1, 0,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case L: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    0, 1, 0,
                    0, 1, 0,
                    0, 1, 1,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case S: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    0, 1, 1,
                    1, 1, 0,
                    0, 0, 0,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            case Z: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    1, 1, 0,
                    0, 1, 1,
                    0, 0, 0,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;

            default: {
                box.width = 3;
                box.height = 3;
                box.pivot = vec2_make(1.5f, 1.5f);
                bool cells[16] = {
                    0, 0, 0,
                    0, 1, 0,
                    0, 0, 0,
                };

                memcpy(box.cells, cells, sizeof(bool) * 16);
            } break;
        }


        t.bounding_box = box;

        b->entities[b->entity_count] = t;
        b->active = &(b->entities[b->entity_count]);
        b->entity_count += 1;
    }

    // Handle inputs on the active tetronimo.
    if (b->active)
    {
        Tetronimo *a = b->active;

        if (state->do_left_move)
        {
            bool can_go = true;
            for (int j = 0; j < a->bounding_box.width; j += 1)
            {
                for (int k = 0; k < a->bounding_box.height; k += 1)
                {
                    int index = k*a->bounding_box.width + j;
                    if (!a->bounding_box.cells[index]) continue;

                    int width = j + a->position.x;
                    int height = k + a->position.y;

                    if (width <= 0)
                    {
                        can_go = false;
                        break;
                    }
                }
            }

            if (can_go) a->position.x -= 1;
            state->do_left_move = false;
        }

        if (state->do_right_move)
        {
            bool can_go = true;
            for (int j = 0; j < a->bounding_box.width; j += 1)
            {
                for (int k = 0; k < a->bounding_box.height; k += 1)
                {
                    int index = k*a->bounding_box.width + j;
                    if (!a->bounding_box.cells[index]) continue;

                    int width = j + a->position.x;
                    int height = k + a->position.y;

                    if (width == b->width-1)
                    {
                        can_go = false;
                        break;
                    }
                }
            }

            if (can_go) a->position.x += 1;
            state->do_right_move = false;
        }

        if (state->do_down_move)
        {
            state->turn_timer = 0;
            state->do_down_move = false;
        }

        if (state->do_drop)
        {
            while (!solid_below(a, b))
            {
                a->position.y += 1;
            }

            a->grounded = true;
            b->active = NULL;
            b->check_for_clear = true;

            state->turn_timer = 0;

            state->do_drop = false;
        }

        if ((state->do_rotate_clockwise || state->do_rotate_counter_clockwise) && (a->type != O))
        {
            bool cells_transformed[16];

            // Transpose
            for (int i = 0; i < a->bounding_box.width; i += 1)
            {
                for (int j = 0; j < a->bounding_box.height; j += 1)
                {
                    int index = j * a->bounding_box.width + i;
                    cells_transformed[i * a->bounding_box.height + j] = a->bounding_box.cells[index];
                }
            }

            if (state->do_rotate_clockwise)
            {
                // Reverse rows
                for (int i = 0; i < a->bounding_box.width; i += 1)
                {
                    for (int j = 0; j < a->bounding_box.height; j += 1)
                    {
                        int old_index = j * a->bounding_box.height + i;
                        int new_index = j * a->bounding_box.width + ((a->bounding_box.width-1) - i);
                        a->bounding_box.cells[new_index] = cells_transformed[old_index];
                    }
                }

                state->do_rotate_clockwise = false;
            }
            
            if (state->do_rotate_counter_clockwise)
            {
                // Reverse columns 
                for (int i = 0; i < a->bounding_box.width; i += 1)
                {
                    for (int j = 0; j < a->bounding_box.height; j += 1)
                    {
                        int old_index = j * a->bounding_box.height + i;
                        int new_index = ((a->bounding_box.height -1) - j) * a->bounding_box.width +  i;
                        a->bounding_box.cells[new_index] = cells_transformed[old_index];
                    }
                }

                state->do_rotate_counter_clockwise = false;
            }
        }
    }

    state->turn_timer -= dt;

    if (state->turn_timer <= 0)
    {
        state->turn_timer = TICK_TIME;
        state->turn_count += 1;

        // Timer ran out, move the all ungrounded tetronimoes.
        for (int i = 0; i < b->entity_count; i += 1)
        {
            Tetronimo *t = &b->entities[i];
            if (solid_below(t, b)) {
                t->grounded = true;
                b->check_for_clear = true;
            } else {
                t->position.y += 1;
            }

            // TODO(bkaylor): Jank ... 
            if (b && b->active && b->active->grounded) b->active = NULL;
        }

    }

    if (b->check_for_clear)
    {
        // Zero out a grid.
        int grid[20][10];
        for (int i = 0; i < 20; i += 1)
        {
            for (int j = 0; j < 10; j += 1)
            {
                grid[i][j] = -1;
            }
        }

        // Set the cells to the id of the tetronimo filling it.
        for (int i = 0; i < b->entity_count; i += 1)
        {
            Tetronimo *t = &b->entities[i];

            if (t->deleted) continue;
            if (!t->grounded) continue;

            for (int j = 0; j < t->bounding_box.width; j += 1)
            {
                for (int k = 0; k < t->bounding_box.height; k += 1)
                {
                    int index = k*t->bounding_box.width + j;
                    if (!t->bounding_box.cells[index]) continue;

                    int width = j + t->position.x;
                    int height = k + t->position.y;

                    grid[height][width] = t->id;
                }
            }
        }

        // Figure out which rows are filled.
        bool any_rows_deleted = false;
        for (int i = 0; i < 20; i += 1)
        {
            bool row_is_filled = true;
            for (int j = 0; j < 10; j += 1)
            {
                if (grid[i][j] == -1) {
                    row_is_filled = false; 
                    break;
                }
            }

            if (row_is_filled) 
            {
                for (int j = 0; j < 10; j += 1)
                {
                    // Zap cell at i,j.
                    int id_of_tetronimo_filling_this_cell = grid[i][j];
                    Tetronimo *t = &b->entities[id_of_tetronimo_filling_this_cell];

                    for (int m = 0; m < t->bounding_box.width; m += 1)
                    {
                        for (int n = 0; n < t->bounding_box.height; n += 1)
                        {
                            int cell_x = t->position.x + m;
                            int cell_y = t->position.y + n;

                            if (cell_x == j && cell_y == i) {
                                t->bounding_box.cells[n*t->bounding_box.width + m] = false;
                            }
                        }
                    }

                    // Teleport down any lines above this one.

                    // TODO(bkaylor): At some point, should clean up wholly deleted tetronimoes.
                    // t->deleted = true;
                }

                b->score += 1;
                any_rows_deleted = true;
            }
        }

        if (any_rows_deleted)
        {
            // TODO(bkaylor): Is this the right thing to do?
            //                Maybe tetronimoes that have been chopped don't fall, but whole lines do.
            //                This causes some badness. When you drop a tetronimo, it snaps straight down to
            //                the first grounded cell below it. Since we're ungrounding everything for a couple
            //                seconds here and there, if you drop at the right time, you can drop to the bottom
            //                row.

            // Unground everything and let it fall again.
            for (int i = 0; i < b->entity_count; i += 1)
            {
                Tetronimo *t = &b->entities[i];
                t->grounded = false;
            }
        }
    }

    return;
}

void get_input(State *state)
{
    SDL_GetMouseState(&state->mouse.x, &state->mouse.y);
    state->mouse.clicked = false;

    SDL_Event event;

    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_KEYDOWN:
                switch (event.key.keysym.sym)
                {
                    case SDLK_ESCAPE:
                        state->quit = true;
                        break;

                    case SDLK_r:
                        state->reset = true;
                        break;

                    case SDLK_UP:
                        state->do_drop = true;
                        break;

                    case SDLK_RIGHT:
                        state->do_right_move = true;
                        break;

                    case SDLK_DOWN:
                        state->do_down_move = true;
                        break;

                    case SDLK_LEFT:
                        state->do_left_move = true;
                        break;

                    case SDLK_x:
                        state->do_rotate_clockwise = true;
                        break;

                    case SDLK_z:
                        state->do_rotate_counter_clockwise = true;
                        break;
                }
                break;

            case SDL_MOUSEBUTTONDOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    state->mouse.clicked = true;
                }
                break;

            case SDL_QUIT:
                state->quit = true;
                break;

            default:
                break;
        }
    }
}

int main(int argc, char *argv[])
{
	SDL_Init(SDL_INIT_EVERYTHING);
    IMG_Init(IMG_INIT_PNG);

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Init(SDL_INIT_AUDIO);

	SDL_Window *win = SDL_CreateWindow("Tetris",
			SDL_WINDOWPOS_CENTERED,
			SDL_WINDOWPOS_CENTERED,
			1440, 980,
			SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);

	SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

	TTF_Init();
	TTF_Font *font = TTF_OpenFont("liberation.ttf", 12);
	if (!font)
	{
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error: Font", TTF_GetError(), win);
		return -666;
	}

    srand(time(NULL));

    State state;
    state.quit = false;
    state.reset = true;

    Uint64 frame_time_start, frame_time_finish, delta_t = 0;

    while (!state.quit)
    {
        frame_time_start = SDL_GetTicks();

        SDL_PumpEvents();
        get_input(&state);

        if (!state.quit)
        {
            int x, y;
            SDL_GetWindowSize(win, &state.window.x, &state.window.y);

            update(&state, delta_t);
            render(ren, state, font);

            frame_time_finish = SDL_GetTicks();
            delta_t = frame_time_finish - frame_time_start;
        }
    }

	SDL_DestroyRenderer(ren);
	SDL_DestroyWindow(win);
	SDL_Quit();
    return 0;
}
