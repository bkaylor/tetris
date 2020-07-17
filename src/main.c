#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>

#include "SDL.h"
#include "SDL_ttf.h"
#include "SDL_image.h"

#include "vec2.h"
#include "draw.h"

#define TICK_TIME 650

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
    SDL_Color color;

    BoundingBox bounding_box;
} Tetronimo;

typedef struct {
    int id;
    SDL_Color color;
    bool marked_for_delete;

    bool exists;

    vec2 position;
} Tetron;

typedef struct {
    Tetronimo entities[1000];
    int entity_count;

    Tetron cells[20*10];
    int cell_count;

    Tetronimo *active;
    Tetronimo *ghost;
    Tetronimo_Type next;

    int width;
    int height;

    SDL_Rect rect;
    float cell_size;

    bool check_for_clear;
    bool there_are_rows_to_be_cleared;
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

SDL_Color get_color(Tetronimo_Type type)
{
    SDL_Color c;

    switch (type)
    {
        case I:   c = (SDL_Color){0,   255, 255, 255}; break;
        case O:   c = (SDL_Color){255, 255, 0,   255}; break;
        case T:   c = (SDL_Color){128, 0,   128, 255}; break;
        case J:   c = (SDL_Color){0,   0,   255, 255}; break;
        case L:   c = (SDL_Color){255, 165, 0,   255}; break;
        case S:   c = (SDL_Color){0,   255, 0,   255}; break;
        case Z:   c = (SDL_Color){255, 0,   0,   255}; break;
        case DOT: c = (SDL_Color){35,  35,  35,  255}; break;
    }

    return c;
}

Tetronimo make_tetronimo(Tetronimo_Type type, vec2 position)
{
    Tetronimo t;

    t.type = type;

    t.color = get_color(t.type);
    t.rotation = 0.0f;
    t.position = position;
    t.grounded = false;
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

    return t;
}

bool solid_below(Tetronimo *tetronimo, Board *board)
{
    for (int i = 0; i < tetronimo->bounding_box.width; i += 1)
    {
        for (int j = 0; j < tetronimo->bounding_box.height; j += 1)
        {
            int index = get_2d_index(i, j, tetronimo->bounding_box.width);
            if (!tetronimo->bounding_box.cells[index]) continue;

            int width = i + tetronimo->position.x;
            int height = j + tetronimo->position.y;

            // Check if we hit the floor.
            if (height == board->height-1) return true;

            for (int k = 0; k < board->cell_count; k += 1)
            {
                Tetron *t = &board->cells[k];
                if (!t->exists) continue;

                if (t->position.x == width && t->position.y == (height+1))
                {
                    return true;
                }
            }
        }
    }

    return false;
}

void render(SDL_Renderer *renderer, State state, TTF_Font *font)
{
    SDL_RenderClear(renderer);

    // Set background color.
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderFillRect(renderer, NULL);

    // Draw the board.
    SDL_SetRenderDrawColor(renderer, 35, 35, 35, 255);
    SDL_RenderFillRect(renderer, &state.board.rect);

    float cell_padding = 0.02f;
    float cell_padding_abs = cell_padding * state.board.cell_size;

    // Draw the tetrons.
    for (int i = 0; i < state.board.cell_count; i += 1)
    {
        Tetron *t = &state.board.cells[i];
        if (!t->exists) continue;

        SDL_Rect rect = (SDL_Rect){
            state.board.rect.x + ((t->position.x) * state.board.cell_size),
            state.board.rect.y + ((t->position.y) * state.board.cell_size),
            state.board.cell_size,
            state.board.cell_size,
        };

        rect.x += cell_padding_abs;
        rect.y += cell_padding_abs;
        rect.w -= 2*cell_padding_abs;
        rect.h -= 2*cell_padding_abs;

        SDL_SetRenderDrawColor(renderer, t->color.r, t->color.g, t->color.b, 255);
        SDL_RenderFillRect(renderer, &rect);
    }

    if (state.board.active)
    {
        Tetronimo *t = state.board.active;

        // Draw the tetronimo's drop ghost
        SDL_SetRenderDrawColor(renderer, t->color.r/5, t->color.g/5, t->color.b/5, 255);

        // TODO(bkaylor): We could build the ghost in the not-render-function.
        Tetronimo ghost = *t;
        while (!solid_below(&ghost, &state.board))
        {
            ghost.position.y += 1;
        }

        for (int i = 0; i < ghost.bounding_box.width; i += 1)
        {
            for (int j = 0; j < ghost.bounding_box.height; j += 1)
            {
                if (ghost.bounding_box.cells[get_2d_index(i, j, ghost.bounding_box.width)])
                {
                    SDL_Rect rect = (SDL_Rect){
                        state.board.rect.x + ((ghost.position.x + i) * state.board.cell_size),
                        state.board.rect.y + ((ghost.position.y + j) * state.board.cell_size),
                        state.board.cell_size,
                        state.board.cell_size,
                    };

                    /*
                    rect.x += cell_padding_abs;
                    rect.y += cell_padding_abs;
                    rect.w -= 2*cell_padding_abs;
                    rect.h -= 2*cell_padding_abs;
                    */

                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }

        // Draw the tetronimo.
        SDL_SetRenderDrawColor(renderer, t->color.r, t->color.g, t->color.b, 255);

        for (int i = 0; i < t->bounding_box.width; i += 1)
        {
            for (int j = 0; j < t->bounding_box.height; j += 1)
            {
                if (t->bounding_box.cells[get_2d_index(i, j, t->bounding_box.width)])
                {
                    SDL_Rect rect = (SDL_Rect){
                        state.board.rect.x + ((t->position.x + i) * state.board.cell_size),
                        state.board.rect.y + ((t->position.y + j) * state.board.cell_size),
                        state.board.cell_size,
                        state.board.cell_size,
                    };

                    rect.x += cell_padding_abs;
                    rect.y += cell_padding_abs;
                    rect.w -= 2*cell_padding_abs;
                    rect.h -= 2*cell_padding_abs;

                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }

        /*
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
        */
    }

    // Draw the next tetron
    SDL_Rect next_rect = (SDL_Rect){
        state.board.rect.x + (state.board.rect.w * 1.2),
        state.board.rect.y + (state.board.rect.h / 4) - (state.board.cell_size * 2),
        state.board.cell_size * 4,
        state.board.cell_size * 4,
    };

    Tetronimo next = make_tetronimo(state.board.next, vec2_make(0.0f, 0.0f));
    SDL_SetRenderDrawColor(renderer, next.color.r, next.color.g, next.color.b, 255);

    for (int i = 0; i < next.bounding_box.width; i += 1)
    {
        for (int j = 0; j < next.bounding_box.height; j += 1)
        {
            if (next.bounding_box.cells[get_2d_index(i, j, next.bounding_box.width)])
            {
                SDL_Rect rect = (SDL_Rect){
                    next_rect.x + ((next.position.x + i) * state.board.cell_size),
                    next_rect.y + ((next.position.y + j) * state.board.cell_size),
                    state.board.cell_size,
                    state.board.cell_size,
                };

                rect.x += cell_padding_abs;
                rect.y += cell_padding_abs;
                rect.w -= 2*cell_padding_abs;
                rect.h -= 2*cell_padding_abs;

                SDL_RenderFillRect(renderer, &rect);
            }
        }
    }

    // Draw text.
    char buf[50];
    int y = 0;

    // DEBUG_PRINT("%d ms", state.turn_timer);
    DEBUG_PRINT("%d lines", state.board.score);
    // DEBUG_PRINT("%d entities", state.board.entity_count);

    SDL_RenderPresent(renderer);
}

int get_2d_index(int w, int h, int width)
{
    return(h*width + w);
}

void copy_tetron_to(Tetron *source, Tetron *destination)
{
    destination->id = source->id;
    destination->color = source->color;
    destination->marked_for_delete = source->marked_for_delete;
    destination->exists = source->exists;
    // destination->position = source->position;
}

void transform_to_tetrons(Tetronimo *tetronimo, Board *board)
{
    for (int i = 0; i < tetronimo->bounding_box.width; i += 1)
    {
        for (int j = 0; j < tetronimo->bounding_box.height; j += 1)
        {
            int index = get_2d_index(i, j, tetronimo->bounding_box.width);
            if (!tetronimo->bounding_box.cells[index]) continue;

            int x = tetronimo->position.x + i;
            int y = tetronimo->position.y + j;

            Tetron t;
            t.id = 0;
            t.color = tetronimo->color;
            t.marked_for_delete = false;
            t.exists = true;
            t.position = vec2_make(x, y);

            board->cells[get_2d_index(x, y, 10)] = t;
            // board->cell_count += 1;
        }
    }
}

bool collides_with_wall(Tetronimo *a, Board *b)
{
    for (int j = 0; j < a->bounding_box.width; j += 1)
    {
        for (int k = 0; k < a->bounding_box.height; k += 1)
        {
            int index = get_2d_index(j, k, a->bounding_box.width);

            if (!a->bounding_box.cells[index]) continue;

            int width = j + a->position.x;
            int height = k + a->position.y;

            if (width < 0 || width >= b->width)
            {
                return true;
            }
        }
    }

    return false;
}

bool collides_with_cells(Tetronimo *a, Board *b)
{
    for (int j = 0; j < a->bounding_box.width; j += 1)
    {
        for (int k = 0; k < a->bounding_box.height; k += 1)
        {
            int index = get_2d_index(j, k, a->bounding_box.width);

            if (!a->bounding_box.cells[index]) continue;

            int width = j + a->position.x;
            int height = k + a->position.y;

            int world_index = get_2d_index(width, height, 10);
            for (int i = 0; i < b->cell_count; i += 1)
            {
                if (b->cells[i].exists && i == world_index) return true;
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
        b->ghost = NULL;
        b->check_for_clear = false;
        b->there_are_rows_to_be_cleared = false;
        b->score = 0;

        b->next = (rand() % 7) + 1;

        for (int i = 0; i < 20*10; i += 1)
        {
            b->cells[i].exists = false;
            b->cells[i].marked_for_delete = false;
        }

        b->cell_count = 20*10;

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
        Tetronimo t = make_tetronimo(b->next, vec2_make(b->width/2, 0));
        b->next = (rand() % 7) + 1;

        b->entities[b->entity_count] = t;
        b->active = &(b->entities[b->entity_count]);
        b->entity_count += 1;

        if (collides_with_cells(&t, b)) state->reset = true;
    }

    // Handle inputs on the active tetronimo.
    if (b->active)
    {
        Tetronimo *a = b->active;

        if (state->do_left_move)
        {
            a->position.x -= 1;
            if (collides_with_wall(a, b) || collides_with_cells(a, b)) 
            {
                a->position.x += 1;
            }

            state->do_left_move = false;
        }

        if (state->do_right_move)
        {
            a->position.x += 1;
            if (collides_with_wall(a, b) || collides_with_cells(a, b)) 
            {
                a->position.x -= 1;
            }

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

            transform_to_tetrons(a, b);
            b->active = NULL;
            b->check_for_clear = true;

            state->turn_timer = 0;

            state->do_drop = false;
        }

        if (state->do_rotate_clockwise || state->do_rotate_counter_clockwise)
        {
            bool original_cells[16];
            bool cells_transposed[16];
            bool cells_reversed[16];

            // Save initial state
            // original_cells = a->bounding_box.cells;
            for (int i = 0; i < a->bounding_box.width; i += 1)
            {
                for (int j = 0; j < a->bounding_box.height; j += 1)
                {
                    int index = j * a->bounding_box.width + i;
                    original_cells[index] = a->bounding_box.cells[index];
                }
            }

            // Transpose
            for (int i = 0; i < a->bounding_box.width; i += 1)
            {
                for (int j = 0; j < a->bounding_box.height; j += 1)
                {
                    int index = j * a->bounding_box.width + i;
                    cells_transposed[i * a->bounding_box.height + j] = a->bounding_box.cells[index];
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
                        cells_reversed[new_index] = cells_transposed[old_index];
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
                        cells_reversed[new_index] = cells_transposed[old_index];
                    }
                }

                state->do_rotate_counter_clockwise = false;
            }

            // cells_reversed now contains the fully transformed grid of cells.
            // Check that, given this new grid, we aren't colliding with anything.
            // If we are, undo the operation.

            for (int i = 0; i < a->bounding_box.width; i += 1)
            {
                for (int j = 0; j < a->bounding_box.height; j += 1)
                {
                    int index = j * a->bounding_box.width + i;
                    a->bounding_box.cells[index] = cells_reversed[index];
                }
            }

            // Wallbang!
            if (collides_with_wall(a, b) || collides_with_cells(a, b))
            {
                a->position.x -= 1;
                if (collides_with_wall(a, b) || collides_with_cells(a, b))
                {
                    a->position.x += 2;
                    if (collides_with_wall(a, b) || collides_with_cells(a, b))
                    {
                        a->position.x -= 1;

                        for (int i = 0; i < a->bounding_box.width; i += 1)
                        {
                            for (int j = 0; j < a->bounding_box.height; j += 1)
                            {
                                int index = j * a->bounding_box.width + i;
                                a->bounding_box.cells[index] = original_cells[index];
                            }
                        }
                    }
                }
            }
        }
    }

    state->turn_timer -= dt;

    if (state->turn_timer <= 0)
    {
        state->turn_timer = TICK_TIME;
        state->turn_count += 1;

        // Timer ran out, move the active tetronimo.
        if (b->active)
        {
            if (solid_below(b->active, b)) {
                transform_to_tetrons(b->active, b);
                b->check_for_clear = true;
                b->active = NULL;
            } else {
                b->active->position.y += 1;
            }
        }

        // Find white rows, delete them and shift other rows down.
        if (b->there_are_rows_to_be_cleared)
        {
            Tetron *grid = b->cells;

            for (int row = 0; row < 20; row += 1)
            {
                for (int column = 0; column < 10; column += 1)
                {
                    Tetron *t = &grid[get_2d_index(column, row, 10)];
                    if (!t->exists) continue;

                    if (t->marked_for_delete) {
                        // t->exists = false;

                        // Bump down any rows above.
                        for (int row_above = row; row_above > 0; row_above -= 1)
                        {
                            // TODO(bkaylor): There is some weirdness around positions. We never really reset them 
                            // after a tetronimo gets converted to tetrons. So they're static ... ish.
                            // This raw copy will work later when the position handling is fixed.
                            // In general I'm not sure how to handle positions in a grid-based game. Keeping the
                            // indices into the grid and the position on the entity itself in line with each other
                            // can cause weird hard-to-find bugs like this.
                            //
                            // grid[get_2d_index(column, row_above, 10)] = grid[get_2d_index(column, row_above-1, 10)];
                            //

                            Tetron *destination = &grid[get_2d_index(column, row_above, 10)]; 
                            Tetron *source      = &grid[get_2d_index(column, row_above-1, 10)];
                            copy_tetron_to(source, destination);
                        }

                        t->marked_for_delete = false;
                    }
                }
            }

            b->there_are_rows_to_be_cleared = false;
        }
    }

    if (b->check_for_clear)
    {
        Tetron *grid = b->cells;

        // Figure out which rows are filled.
        for (int row = 0; row < 20; row += 1)
        {
            bool row_is_filled = true;
            for (int column = 0; column < 10; column += 1)
            {
                Tetron *t = &grid[get_2d_index(column, row, 10)];
                if (!t->exists) {
                    row_is_filled = false; 
                    break;
                }
            }

            if (row_is_filled) 
            {
                // Set cells to white and mark for delete.
                for (int column = 0; column < 10; column += 1)
                {
                    Tetron *t = &grid[get_2d_index(column, row, 10)];
                    t->marked_for_delete = true;
                    t->color = (SDL_Color){255, 255, 255, 255};
                }

                b->there_are_rows_to_be_cleared = true;
            }
        }

        b->check_for_clear = false;
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
