/* The pre-game "New Game" dialog for the SDL interface to Xconq.
   Copyright (C) 2026 portnoyslp.

Xconq is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.  See the file COPYING.  */

#include "sdlpreconq.h"
#include "conq.h"
#include "kpublic.h"
#include "sdlconq.h"

/* A minimal pre-game "New Game" dialog: pick a module from possible_games
   (kernel/ui.cc's collect_possible_games(), reading lib/game.dir) and
   toggle its variants, then Start.  This runs before any Screen/Map/game
   state exists (main() calls it ahead of load_all_modules()), so it can't
   reuse the in-game Panel/SDLButton/ask_string widgets -- those all
   assume an active Screen.  Instead it's a small, self-contained event
   loop drawing directly onto mscreen with draw_string()/SDL_FillSurfaceRect
   and doing its own mouse hit-testing. */

#define GAMEDLG_ROW_H 20
#define GAMEDLG_TOP 60
#define GAMEDLG_MARGIN 24
#define GAMEDLG_MAX_BLURB_LINES 4
#define GAMEDLG_LINE_CHARS 80
#define GAMEDLG_SCROLLBAR_W 14
#define GAMEDLG_SCROLLBAR_THUMB_MIN_H 16

typedef struct {
    Module *module;
    int numvariants;
    int *varvalues;	/* current chosen value per variant, index-aligned
			   with module->variants[] */
    int *varkind;	/* 0 = boolean toggle, 1 = world-size style stepper */
} GameDialogState;

/* Break str into up to GAMEDLG_MAX_BLURB_LINES lines of at most
   GAMEDLG_LINE_CHARS chars each, breaking on whitespace.  A fixed
   character-count wrap rather than measuring real glyph widths -- good
   enough for a blurb, matches the "basic" scope of this dialog. */

static int
gamedlg_wrap_blurb(const char *str, char lines[GAMEDLG_MAX_BLURB_LINES][GAMEDLG_LINE_CHARS + 1])
{
    int nlines = 0, i = 0, linelen, lastspace, j;

    while (str[i] != '\0' && nlines < GAMEDLG_MAX_BLURB_LINES) {
	while (str[i] == ' ' || str[i] == '\n')
	  ++i;
	if (str[i] == '\0')
	  break;
	linelen = 0;  lastspace = -1;
	while (str[i + linelen] != '\0' && str[i + linelen] != '\n'
	       && linelen < GAMEDLG_LINE_CHARS) {
	    if (str[i + linelen] == ' ')
	      lastspace = linelen;
	    ++linelen;
	}
	if (str[i + linelen] != '\0' && str[i + linelen] != '\n' && lastspace > 0)
	  linelen = lastspace;
	for (j = 0; j < linelen; ++j)
	  lines[nlines][j] = str[i + j];
	lines[nlines][linelen] = '\0';
	++nlines;
	i += linelen;
    }
    return nlines;
}

/* Number of game rows that fit in the list page's visible area. */

static int
gamedlg_visible_rows(void)
{
    return (mscreen->h - GAMEDLG_TOP - 40) / GAMEDLG_ROW_H;
}

/* Geometry of the scrollbar track, shared between drawing and
   click/drag hit-testing so they can't drift apart. */

static void
gamedlg_scrollbar_track(SDL_Rect *track)
{
    track->x = mscreen->w - GAMEDLG_MARGIN - GAMEDLG_SCROLLBAR_W;
    track->y = GAMEDLG_TOP;
    track->w = GAMEDLG_SCROLLBAR_W;
    track->h = gamedlg_visible_rows() * GAMEDLG_ROW_H;
}

/* Map a mouse y-coordinate to a scroll offset, for click-to-jump and
   drag -- linear across the whole track, not thumb-relative. */

static int
gamedlg_scroll_from_y(int my)
{
    SDL_Rect track;
    int max_scroll = numgames - gamedlg_visible_rows();
    int scroll;

    if (max_scroll <= 0)
      return 0;
    gamedlg_scrollbar_track(&track);
    scroll = (my - track.y) * numgames / track.h;
    if (scroll < 0)
      scroll = 0;
    if (scroll > max_scroll)
      scroll = max_scroll;
    return scroll;
}

/* Is (mx, my) within the scrollbar's clickable column? (Not clamped to
   the track's height -- dragging above/below it should still count.) */

static int
gamedlg_in_scrollbar_column(int mx)
{
    SDL_Rect track;

    gamedlg_scrollbar_track(&track);
    return (numgames > gamedlg_visible_rows()
	    && mx >= track.x && mx < track.x + track.w);
}

/* Draw the scrollable list of possible_games, plus a scrollbar if there
   are more games than fit on screen. */

static void
gamedlg_draw_list(int scroll)
{
    int i, y, visible_rows;
    const char *title;
    SDL_Rect full, track, thumb;

    full.x = 0;  full.y = 0;  full.w = mscreen->w;  full.h = mscreen->h;
    SDL_FillSurfaceRect(mscreen, &full, SDL_MapSurfaceRGB(mscreen, 20, 20, 30));
    draw_string(mscreen, GAMEDLG_MARGIN, 20, "Xconq -- Choose a Game");
    draw_string(mscreen, GAMEDLG_MARGIN, mscreen->h - 24,
		"Click a game to configure it.  Scroll for more.");

    visible_rows = gamedlg_visible_rows();
    for (i = 0; i < visible_rows && scroll + i < numgames; ++i) {
	title = possible_games[scroll + i]->title;
	if (empty_string(title))
	  title = possible_games[scroll + i]->name;
	y = GAMEDLG_TOP + i * GAMEDLG_ROW_H;
	draw_string(mscreen, GAMEDLG_MARGIN, y, title);
    }

    if (numgames > visible_rows) {
	int max_scroll = numgames - visible_rows;

	gamedlg_scrollbar_track(&track);
	SDL_FillSurfaceRect(mscreen, &track, SDL_MapSurfaceRGB(mscreen, 40, 40, 55));

	thumb.x = track.x + 2;  thumb.w = track.w - 4;
	thumb.h = track.h * visible_rows / numgames;
	if (thumb.h < GAMEDLG_SCROLLBAR_THUMB_MIN_H)
	  thumb.h = GAMEDLG_SCROLLBAR_THUMB_MIN_H;
	if (thumb.h > track.h)
	  thumb.h = track.h;
	thumb.y = track.y + (track.h - thumb.h) * scroll / max_scroll;
	SDL_FillSurfaceRect(mscreen, &thumb, SDL_MapSurfaceRGB(mscreen, 110, 110, 140));
    }
}

/* Draw the detail page: blurb + variant toggles + Start, for the
   selected module. */

static void
gamedlg_draw_detail(GameDialogState *state)
{
    char blurbbuf[BLURBSIZE];
    char lines[GAMEDLG_MAX_BLURB_LINES][GAMEDLG_LINE_CHARS + 1];
    int nlines, i, y;
    char rowbuf[BUFSIZE];
    SDL_Rect full, startrect;
    const char *title;

    full.x = 0;  full.y = 0;  full.w = mscreen->w;  full.h = mscreen->h;
    SDL_FillSurfaceRect(mscreen, &full, SDL_MapSurfaceRGB(mscreen, 20, 20, 30));
    draw_string(mscreen, GAMEDLG_MARGIN, 20, "< Back");

    title = state->module->title;
    if (empty_string(title))
      title = state->module->name;
    draw_string(mscreen, GAMEDLG_MARGIN, 20 + GAMEDLG_ROW_H, title);

    y = GAMEDLG_TOP;
    if (state->module->blurb != lispnil) {
	blurbbuf[0] = '\0';
	append_blurb_strings(blurbbuf, state->module->blurb);
	nlines = gamedlg_wrap_blurb(blurbbuf, lines);
	for (i = 0; i < nlines; ++i) {
	    draw_string(mscreen, GAMEDLG_MARGIN, y, lines[i]);
	    y += GAMEDLG_ROW_H;
	}
    }
    y += GAMEDLG_ROW_H / 2;

    for (i = 0; i < state->numvariants; ++i) {
	if (state->varkind[i] == 1) {
	    snprintf(rowbuf, BUFSIZE, "%s: %d",
		     state->module->variants[i].name, state->varvalues[i]);
	} else {
	    snprintf(rowbuf, BUFSIZE, "[%c] %s",
		     (state->varvalues[i] ? 'x' : ' '),
		     state->module->variants[i].name);
	}
	draw_string(mscreen, GAMEDLG_MARGIN, y + i * GAMEDLG_ROW_H, rowbuf);
    }

    startrect.x = GAMEDLG_MARGIN;  startrect.y = mscreen->h - 36;
    startrect.w = 120;  startrect.h = 26;
    SDL_FillSurfaceRect(mscreen, &startrect, SDL_MapSurfaceRGB(mscreen, 60, 100, 60));
    draw_string(mscreen, GAMEDLG_MARGIN + 10, mscreen->h - 30, "Start Game");
}

/* Allocate and initialize per-variant state for a freshly selected
   module, defaulting each from its declared default value. */

static void
gamedlg_init_variants(GameDialogState *state, Module *module)
{
    int i, n;

    for (n = 0; module->variants != NULL && module->variants[n].id != lispnil; ++n)
      ;
    if (state->varvalues != NULL) {
	free(state->varvalues);
	free(state->varkind);
    }
    state->module = module;
    state->numvariants = n;
    state->varvalues = (int *) xmalloc(n * sizeof(int));
    state->varkind = (int *) xmalloc(n * sizeof(int));
    for (i = 0; i < n; ++i) {
	Variant *var = &(module->variants[i]);

	state->varvalues[i] = (numberp(var->dflt) ? c_number(var->dflt) : 0);
	state->varkind[i] =
	  (keyword_code(c_string(var->id)) == K_WORLD_SIZE ? 1 : 0);
	if (state->varkind[i] == 1 && state->varvalues[i] <= 0)
	  state->varvalues[i] = 40;
    }
}

/* Apply the chosen module and variant values exactly the way the -g/-v
   CLI flags already do (mainmodule global, net_set_variant_value()), so
   the existing load_all_modules()/set_variants_from_options() path in
   main() picks them up unchanged. */

static void
gamedlg_apply_and_start(GameDialogState *state)
{
    int i;

    mainmodule = state->module;
    for (i = 0; i < state->numvariants; ++i) {
	if (state->varkind[i] == 1) {
	    net_set_variant_value(i, state->varvalues[i], state->varvalues[i],
				   state->varvalues[i]);
	} else {
	    net_set_variant_value(i, state->varvalues[i], 0, 0);
	}
    }
}

void
popup_game_dialog(void)
{
    SDL_Event evt;
    GameDialogState state;
    int scroll = 0, page_detail = FALSE, done = FALSE, row, visible_rows;

    collect_possible_games();
    if (numgames <= 0)
      return;  /* Nothing to pick; falls through to load_default_game(). */

    memset(&state, 0, sizeof(state));

    /* initial_ui_init() already hid the OS cursor in favor of the
       in-game custom cursor, but that's only ever drawn from the
       Screen-based render path (sdlscreen.cc) -- nonexistent this early.
       Show the OS cursor for the dialog's lifetime so clicks are visible,
       then hand back to the normal custom-cursor behavior below. */
    SDL_ShowCursor();

    while (!done) {
	if (page_detail) {
	    gamedlg_draw_detail(&state);
	} else {
	    gamedlg_draw_list(scroll);
	}
	SDL_UpdateWindowSurface(window);

	if (!SDL_WaitEvent(&evt))
	  continue;

	if (evt.type == SDL_EVENT_QUIT) {
	    exit_xconq();
	} else if (evt.type == SDL_EVENT_MOUSE_WHEEL && !page_detail) {
	    visible_rows = gamedlg_visible_rows();
	    scroll -= (int) evt.wheel.y;
	    if (scroll > numgames - visible_rows)
	      scroll = numgames - visible_rows;
	    if (scroll < 0)
	      scroll = 0;
	} else if (evt.type == SDL_EVENT_MOUSE_MOTION && !page_detail
		   && (evt.motion.state & SDL_BUTTON_LMASK)
		   && gamedlg_in_scrollbar_column((int) evt.motion.x)) {
	    /* Dragging the scrollbar thumb (or anywhere in its column). */
	    scroll = gamedlg_scroll_from_y((int) evt.motion.y);
	} else if (evt.type == SDL_EVENT_KEY_DOWN
		   && evt.key.key == SDLK_ESCAPE && page_detail) {
	    page_detail = FALSE;
	} else if (evt.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
	    int mx = (int) evt.button.x, my = (int) evt.button.y;

	    if (!page_detail && gamedlg_in_scrollbar_column(mx)) {
		scroll = gamedlg_scroll_from_y(my);
	    } else if (!page_detail) {
		row = (my - GAMEDLG_TOP) / GAMEDLG_ROW_H;
		if (my >= GAMEDLG_TOP && row >= 0 && scroll + row < numgames) {
		    gamedlg_init_variants(&state, possible_games[scroll + row]);
		    page_detail = TRUE;
		}
	    } else if (my < GAMEDLG_TOP && mx < GAMEDLG_MARGIN + 60) {
		page_detail = FALSE;
	    } else if (my >= mscreen->h - 36 && my < mscreen->h - 10
		       && mx >= GAMEDLG_MARGIN && mx < GAMEDLG_MARGIN + 120) {
		gamedlg_apply_and_start(&state);
		done = TRUE;
	    } else {
		int blurblines = 0, vy;

		if (state.module->blurb != lispnil) {
		    char blurbbuf[BLURBSIZE];
		    char lines[GAMEDLG_MAX_BLURB_LINES][GAMEDLG_LINE_CHARS + 1];

		    blurbbuf[0] = '\0';
		    append_blurb_strings(blurbbuf, state.module->blurb);
		    blurblines = gamedlg_wrap_blurb(blurbbuf, lines);
		}
		vy = GAMEDLG_TOP + blurblines * GAMEDLG_ROW_H + GAMEDLG_ROW_H / 2;
		row = (my - vy) / GAMEDLG_ROW_H;
		if (row >= 0 && row < state.numvariants) {
		    if (state.varkind[row] == 1) {
			state.varvalues[row] += 10;
			if (state.varvalues[row] > 200)
			  state.varvalues[row] = 10;
		    } else {
			state.varvalues[row] = !state.varvalues[row];
		    }
		}
	    }
	}
    }
    if (state.varvalues != NULL) {
	free(state.varvalues);
	free(state.varkind);
    }
    /* Hand back to the in-game custom cursor (use_cursors is still TRUE
       from initial_ui_init()); the real Screen exists by the time this
       matters again. */
    SDL_HideCursor();
}
