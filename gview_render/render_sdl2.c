/*******************************************************************************#
#           guvcview              http://guvcview.sourceforge.net               #
#                                                                               #
#           Paulo Assis <pj.assis@gmail.com>                                    #
#           Nobuhiro Iwamatsu <iwamatsu@nigauri.org>                            #
#                             Add UYVY color support(Macbook iSight)            #
#           Flemming Frandsen <dren.dk@gmail.com>                               #
#                             Add VU meter OSD                                  #
#                                                                               #
# This program is free software; you can redistribute it and/or modify          #
# it under the terms of the GNU General Public License as published by          #
# the Free Software Foundation; either version 2 of the License, or             #
# (at your option) any later version.                                           #
#                                                                               #
# This program is distributed in the hope that it will be useful,               #
# but WITHOUT ANY WARRANTY; without even the implied warranty of                #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                 #
# GNU General Public License for more details.                                  #
#                                                                               #
# You should have received a copy of the GNU General Public License             #
# along with this program; if not, write to the Free Software                   #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA     #
#                                                                               #
********************************************************************************/

#include <SDL.h>
#include <assert.h>

#include "gview.h"
#include "gviewrender.h"
#include "render.h"
#include "render_sdl2.h"

extern int verbosity;

SDL_DisplayMode display_mode;

static SDL_Window*  sdl_window = NULL;
static SDL_Texture* rending_texture = NULL;
static SDL_Renderer*  main_renderer = NULL;

/*
 * initialize sdl video
 * args:
 *   width - video width
 *   height - video height
 *
 * asserts:
 *   none
 *
 * returns: error code
 */
static int video_init(int width, int height)
{
	int w = width;
	int h = height;

	if(verbosity > 0)
		printf("RENDER: Initializing SDL2 render\n");

    if (sdl_window == NULL) /*init SDL*/
    {
        if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER) < 0)
        {
            fprintf(stderr, "RENDER: Couldn't initialize SDL2: %s\n", SDL_GetError());
            return -1;
        }

        SDL_SetHint("SDL_HINT_RENDER_SCALE_QUALITY", "1");

		sdl_window = SDL_CreateWindow(
			"Guvcview Video",                  // window title
			SDL_WINDOWPOS_UNDEFINED,           // initial x position
			SDL_WINDOWPOS_UNDEFINED,           // initial y position
			w,                               // width, in pixels
			h,                               // height, in pixels
			SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL
		);

		if(sdl_window == NULL)
		{
			fprintf(stderr, "RENDER: (SDL2) Couldn't open window: %s\n", SDL_GetError());
			render_sdl2_clean();
            return -2;
		}

		int display_index = SDL_GetWindowDisplayIndex(sdl_window);

		int err = SDL_GetDesktopDisplayMode(display_index, &display_mode);
		if(!err)
		{
			if(verbosity > 0)
				printf("RENDER: video display %i ->  %dx%dpx @ %dhz\n",
					display_index,
					display_mode.w,
					display_mode.h,
					display_mode.refresh_rate);
		}
		else
			fprintf(stderr, "RENDER: Couldn't determine display mode for video display %i\n", display_index);

		if(w > display_mode.w)
			w = display_mode.w;
		if(h > display_mode.h)
			h = display_mode.h;

		if(verbosity > 0)
			printf("RENDER: setting window size to %ix%i\n", w, h);

		SDL_SetWindowSize(sdl_window, w, h);
    }

    main_renderer = SDL_CreateRenderer(sdl_window, -1,
		SDL_RENDERER_TARGETTEXTURE |
		SDL_RENDERER_PRESENTVSYNC  |
		SDL_RENDERER_ACCELERATED);

	if(main_renderer == NULL)
	{
		fprintf(stderr, "RENDER: (SDL2) Couldn't get a renderer: %s\n", SDL_GetError());
		render_sdl2_clean();
		return -3;
	}

	SDL_RenderSetLogicalSize(main_renderer, width, height);
	SDL_SetRenderDrawBlendMode(main_renderer, SDL_BLENDMODE_NONE);

    rending_texture = SDL_CreateTexture(main_renderer,
		SDL_PIXELFORMAT_YUY2,
		SDL_TEXTUREACCESS_STREAMING,
		width,
		height);

	if(rending_texture == NULL)
	{
		fprintf(stderr, "RENDER: (SDL2) Couldn't get a texture for rending: %s\n", SDL_GetError());
		render_sdl2_clean();
		return -4;
	}

    return 0;
}

/*
 * init sdl2 render
 * args:
 *    width - overlay width
 *    height - overlay height
 *
 * asserts:
 *
 * returns: error code (0 ok)
 */
 int init_render_sdl2(int width, int height)
 {
	int err = video_init(width, height);

	if(err)
	{
		fprintf(stderr, "RENDER: Couldn't init the SDL2 rendering engine\n");
		return -1;
	}

	assert(rending_texture != NULL);

	return 0;
 }

/*
 * render a frame
 * args:
 *   frame - pointer to frame data (yuyv format)
 *   width - frame width
 *   height - frame height
 *
 * asserts:
 *   poverlay is not nul
 *   frame is not null
 *
 * returns: error code
 */
int render_sdl2_frame(uint8_t *frame, int width, int height)
{
	/*asserts*/
	assert(rending_texture != NULL);
	assert(frame != NULL);

	float vu_level[2];
	render_get_vu_level(vu_level);

	int size = width * height * 2; /* 2 bytes per pixel for yuyv*/

	void* texture_pixels;
	int pitch;

	SDL_SetRenderDrawColor(main_renderer, 0, 0, 0, 255); /*black*/
	SDL_RenderClear(main_renderer);


	if (SDL_LockTexture(rending_texture, NULL, &texture_pixels, &pitch))
	{
		fprintf(stderr, "RENDER: couldn't lock texture to write\n");
		return -1;
	}
	
	memcpy(texture_pixels, frame, size);
	
	/*osd vu meter*/
    if(((render_get_osd_mask() &
		(REND_OSD_VUMETER_MONO | REND_OSD_VUMETER_STEREO))) != 0)
		render_osd_vu_meter(texture_pixels, width, height, vu_level);

	SDL_UnlockTexture(rending_texture);

	SDL_RenderCopy(main_renderer, rending_texture, NULL, NULL);

	SDL_RenderPresent(main_renderer);
}

/*
 * set sdl2 render caption
 * args:
 *   caption - string with render window caption
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void set_render_sdl2_caption(const char* caption)
{
	SDL_SetWindowTitle(sdl_window, caption);
}

/*
 * dispatch sdl2 render events
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl2_dispatch_events()
{
	
	SDL_Event event;

	while( SDL_PollEvent(&event) )
	{
		if(event.type==SDL_KEYDOWN)
		{
			switch( event.key.keysym.sym )
            {
				case SDLK_UP:
					render_call_event_callback(EV_KEY_UP);
					break;

				case SDLK_DOWN:
					render_call_event_callback(EV_KEY_DOWN);
					break;

				case SDLK_RIGHT:
					render_call_event_callback(EV_KEY_RIGHT);
					break;

				case SDLK_LEFT:
					render_call_event_callback(EV_KEY_LEFT);
					break;

				case SDLK_SPACE:
					render_call_event_callback(EV_KEY_SPACE);
					break;

				case SDLK_i:
					render_call_event_callback(EV_KEY_I);
					break;

				case SDLK_v:
					render_call_event_callback(EV_KEY_V);
					break;

				default:
					break;

			}

			switch( event.key.keysym.scancode )
			{
				case 220:
					break;
			}
		}

		if(event.type==SDL_QUIT)
		{
			if(verbosity > 0)
				printf("RENDER: (event) quit\n");
			render_call_event_callback(EV_QUIT);
		}
	}
}
/*
 * clean sdl2 render data
 * args:
 *   none
 *
 * asserts:
 *   none
 *
 * returns: none
 */
void render_sdl2_clean()
{
	if(rending_texture)
		SDL_DestroyTexture(rending_texture);

	rending_texture = NULL;
	
	if(main_renderer)
		SDL_DestroyRenderer(main_renderer);

	main_renderer = NULL;
	
	if(sdl_window)
		SDL_DestroyWindow(sdl_window);

	sdl_window = NULL;
	
	SDL_Quit();
}
