/*
 * seven_seg.cpp
 *
 * Copyright (C) 2010 Andrew H. Armenia.
 * This program is released under the terms of the 
 * GNU General Public License, version 3. See COPYING
 * file for details.
 */

/* 
 * Convention for 7-segment display layout...
 *
 *    555
 *   6   4
 *   6   4
 *   6   4
 *    000
 *   1   3
 *   1   3
 *   1   3
 *    222
 *
 * This is so the user can click in the middle, then just work around the rest
 * of the segments counter-clockwise, when setting up the system.
 */


#include "SDL.h"
#include "picture.h"

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <stdexcept>

#define N_DIGITS 4

Picture *fixed_png;

struct point {
    uint16_t x, y;
};

struct color {
    uint16_t r, g, b;
};

struct digit {
    struct point segment_pos[7];
};

const struct color seg_colors[] = {
    { 102, 51, 51 }, /* brown (1) */
    { 255, 0, 0 }, /* red (2) */
    { 255, 102, 0 }, /* orange (3) */
    { 255, 255, 0 }, /* yellow (4) */
    { 0, 255, 0 }, /* green (5) */
    { 0, 0, 255 }, /* blue (6) */
    { 0, 255, 255 }, /* violet (7) */
};

const bool seg_truth_table[][7] = {
    { false, true, true, true, true, true, true }, /* "0" */
    { false, false, false, true, true, false, false }, /* "1" */
    { true, true, true, false, true, true, false }, /* "2" */
    { true, false, true, true, true, true, false }, /* "3" */
    { true, false, false, true, true, false, true }, /* "4" */
    { true, false, true, true, false, true, true }, /* "5" */
    { true, true, true, true, false, true, true }, /* "6" */
    { false, false, false, true, true, true, false }, /* "7" */
    { true, true, true, true, true, true, true }, /* "8" */
    { true, false, true, true, true, true, true }, /* "9" */
    /* an all-dead digit #0 means to interpret 1-3 as :ss.t */
    { false, false, false, false, false, false, false }, 
};

Picture *read_image(void) {
    return Picture::copy(fixed_png);
}

static void putpixel(SDL_Surface *output, int16_t x, int16_t y,
    uint8_t r, uint8_t g, uint8_t b) {
    
    if (SDL_MUSTLOCK(output)) {
        if (SDL_LockSurface(output) != 0) {
            throw std::runtime_error("putpixel could not lock surface");
        }
    }

    uint8_t *pixel_ptr = (uint8_t *) output->pixels;
    pixel_ptr += (output->pitch * y);
    pixel_ptr += (output->format->BytesPerPixel * x);

    /* for now just hope it's RGB... */
    pixel_ptr[0] = r;
    pixel_ptr[1] = g;
    pixel_ptr[2] = b;

    if (SDL_MUSTLOCK(output)) {
        SDL_UnlockSurface(output);
    }
}

void blit_picture_to_sdl(Picture *p, SDL_Surface *surf) {
    unsigned int blit_w, blit_h, y;
    uint8_t *pixels;

    assert(p->pix_fmt == RGB8);

    if (p->w > surf->w) {
        blit_w = surf->w;
    } else {
        blit_w = p->w;
    }

    if (p->h > surf->h) {
        blit_h = surf->h;
    } else {
        blit_h = p->h;
    }

    if (SDL_MUSTLOCK(surf)) {
        SDL_LockSurface(surf);
    }
    
    pixels = (uint8_t *)surf->pixels;

    for (y = 0; y < blit_h; y++) {
        memcpy(pixels, p->scanline(y), blit_w * 3);
        pixels += 3 * surf->w;
    }

    if (SDL_MUSTLOCK(surf)) {
        SDL_UnlockSurface(surf);
    }
}

void draw_box(SDL_Surface *surf, int x0, int y0, const struct color *c) {
    int x, y;

    for (x = -2; x <= 2; ++x) {
        for (y = -2; y <= 2; ++y) {
            if (x + x0 > 0 && y + y0 > 0) {
                putpixel(surf, x+x0, y+y0, c->r, c->g, c->b);
            }
        }
    }
}

void overlay_segments(SDL_Surface *surf, const struct digit *d) {
    unsigned int i;
    for (i = 0; i < 7; ++i) {
        draw_box(surf, d->segment_pos[i].x, d->segment_pos[i].y,
                &seg_colors[i]);
    }
}

uint8_t getpixel_y(Picture *p, unsigned int x, unsigned int y) {
    uint8_t *rgb = p->scanline(y) + 3 * x;
    /* crudely estimate y as (r + 2g + b) / 4 */
    uint16_t y1 = rgb[0]  + 2 * rgb[1] + rgb[2];
    return (y1 >> 2);
}

uint8_t boxavg_y(Picture *p, const struct point *pt) {
    int x, y, n = 0;
    uint16_t ysum = 0;

    for (x = pt->x - 2; x <= pt->x + 2; ++x) {
        for (y = pt->y - 2; y <= pt->y + 2; ++y) {
            if (x > 0 && y > 0 && x < p->w && y < p->h) {
                ysum += getpixel_y(p, x, y);
                n++;
            }
        }
    }    
    
    if (n > 0) {
        return (uint8_t) ysum / n;
    } else {
        return 0;
    }
}

int truth_table_compare(const bool *state, const bool * const *truth_table, int bits, int n) {
    int i, j;
    bool flag;

    for (i = 0; i < n; ++i) {
        flag = true;

        /* compare each bit */
        for (j = 0; j < bits; ++j) {
            if (state[j] != truth_table[i][j]) {
                flag = false;
            }
        }

        if (flag) {
            return i;
        }
    }

    return -1;
}

void compute_and_send_time(Picture *p, const struct digit *digits) {
    int i, j;
    uint8_t ythresh = 64;
    bool states[7];
    int digit_values[N_DIGITS];
    /* seconds/seconds/tenths instead of minutes/minutes/seconds/seconds */
    bool as_sst = false; 
    uint32_t clock;

    for (i = 0; i < N_DIGITS; ++i) {
        for (j = 0; j < 7; ++j) {
            if (boxavg_y(p, &digits[i].segment_pos[j]) > ythresh) {
                states[j] = true;
            } else {
                states[j] = false;
            }
        }

        digit_values[i] = truth_table_compare(states, 
                (const bool * const *) seg_truth_table, 7, 11);
        if (digit_values[i] == -1) {
            fprintf(stderr, "warning: could not decode digit %d", i);
            return;
        }

        if (digit_values[i] == 10) {
            /* handle a blank digit */
            if (i == 0) {
                as_sst = true;
            } else {
                /* probably a leading blank */
                digit_values[i] = 0;
            }
        }
    }

    if (as_sst) {
        clock = digit_values[1] + digit_values[2] * 10 + digit_values[3] * 100;
    } else {
        if (digit_values[3] >= 6) {
            fprintf(stderr, "warning: non-sensical time being decoded\n");
        }
        clock = 
            digit_values[3] * 6000 
            + digit_values[2] * 600 
            + digit_values[3] * 100 
            + digit_values[4] * 10;
    }

    /* send via socket (eventually) */
    fprintf(stderr, "clock value = %d ", clock);
    if (clock >= 600) {
        fprintf(stderr, "(%d:%02d)\n", clock / 600, (clock / 10) % 60);
    } else {
        fprintf(stderr, "(:%02d.%d)\n", clock / 10, clock % 10);
    }
}

int main(void) {
    SDL_Surface *screen;
    SDL_Event evt;
    Picture *in_frame;
    Picture *png = Picture::from_png("hockey_clock.png");
    fixed_png = png->convert_to_format(RGB8);
    Picture::free(fixed_png);

    unsigned int digit_being_initialized = 0;
    unsigned int segment_being_initialized = 0;
    enum { RUNNING, SETUP_DIGITS } mode;

    struct digit digits[N_DIGITS];

    memset(digits, 0, sizeof(digits));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_NOPARACHUTE) != 0) {
        fprintf(stderr, "Failed to initialize SDL!\n");
        return 1;
    }

    screen = SDL_SetVideoMode(320, 240, 24, SDL_HWSURFACE | SDL_DOUBLEBUF);
    if (!screen) {
        fprintf(stderr, "Failed to create frame buffer!\n");
        SDL_Quit( );
        return 1;
    }

    for (;;) {
        /* read frame */
        in_frame = read_image( );
    
        /* draw frame on screen */
        blit_picture_to_sdl(in_frame, screen);

        if (mode == RUNNING) {
            /* do processing */
            compute_and_send_time(in_frame, digits);
        } else if (mode == SETUP_DIGITS) {
            /* overlay the segment positions selected */
            overlay_segments(screen, &digits[digit_being_initialized]);
            draw_box(screen, 2, 317, &seg_colors[segment_being_initialized]);
            draw_box(screen, 7, 317, &seg_colors[segment_being_initialized]);
        }

        if (SDL_PollEvent(&evt)) {
            if (evt.type == SDL_KEYDOWN) {
                switch (evt.key.keysym.sym) {
                    /* keyboard handling */
                    case SDLK_s:
                        digit_being_initialized = 0;
                        segment_being_initialized = 0;
                        mode = SETUP_DIGITS;
                        break;

                    case SDLK_r:
                        mode = RUNNING;
                        break;
                        
                    case SDLK_n:
                        digit_being_initialized++;
                        if (digit_being_initialized == N_DIGITS) {
                            digit_being_initialized = 0;
                        }
                        
                    default:
                        break;
                }
            } else if (evt.type == SDL_MOUSEBUTTONDOWN) {
                if (mode == SETUP_DIGITS) {
                    digits[digit_being_initialized]
                        .segment_pos[segment_being_initialized].x 
                        = evt.button.x;
                    digits[digit_being_initialized]
                        .segment_pos[segment_being_initialized].y 
                        = evt.button.y;

                    segment_being_initialized++;

                    if (segment_being_initialized == 7) {
                        segment_being_initialized = 0;
                    }
                }
            }

        }
                
    }

    SDL_FreeSurface(screen);
    SDL_Quit( );
}
