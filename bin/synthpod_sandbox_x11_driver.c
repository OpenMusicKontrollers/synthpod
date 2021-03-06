/*
 * Copyright (c) 2015-2016 Hanspeter Portner (dev@open-music-kontrollers.ch)
 *
 * This is free software: you can redistribute it and/or modify
 * it under the terms of the Artistic License 2.0 as published by
 * The Perl Foundation.
 *
 * This source is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Artistic License 2.0 for more details.
 *
 * You should have received a copy of the Artistic License 2.0
 * along the source as a COPYING file. If not, obtain it from
 * http://www.perlfoundation.org/artistic_license_2_0.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define CROSS_CLOCK_IMPLEMENTATION
#include <cross_clock/cross_clock.h>

#include <sandbox_slave.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <xcb/xcb.h>
#include <xcb/xcb_icccm.h>
#include <xcb/xcb_xrm.h>
#include <signal.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;
	void *dsp_instance;
	LV2UI_Handle *handle;
	const LV2UI_Idle_Interface *idle_iface;
	const LV2UI_Resize *resize_iface;

	xcb_connection_t *conn;
	xcb_screen_t *screen;
	xcb_drawable_t win;
	xcb_drawable_t widget;
	xcb_intern_atom_cookie_t cookie;
	xcb_intern_atom_reply_t* reply;
	xcb_intern_atom_cookie_t cookie2;
	xcb_intern_atom_reply_t* reply2;
	int w;
	int h;
	cross_clock_t clk_real;
	atomic_bool *done;
};

static atomic_bool *_done;

static inline void
_sig(int signum)
{
	atomic_store_explicit(_done, true, memory_order_relaxed);
}

static inline int
_resize(void *data, int w, int h)
{
	app_t *app= data;

	app->w = w;
	app->h = h;

	const uint32_t values [2] = {app->w, app->h};
	xcb_configure_window(app->conn, app->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);
	xcb_flush(app->conn);

	return 0;
}

static inline void
_clone_size_hints(app_t *app)
{
	// clone size hints from widget to parent window
	xcb_get_property_cookie_t reply = xcb_icccm_get_wm_size_hints(app->conn,
		app->widget, XCB_ATOM_WM_NORMAL_HINTS);
	xcb_size_hints_t size_hints;
	memset(&size_hints, 0, sizeof(size_hints));
	xcb_icccm_get_wm_size_hints_reply(app->conn, reply, &size_hints, NULL);

#if 0
	fprintf(stdout, "%u, (%i, %i), (%i, %i), (%i, %i), (%i, %i), (%i, %i), (%i, %i), (%i, %i), (%i, %i), %u\n",
			size_hints.flags,
			size_hints.x, size_hints.y,
			size_hints.width, size_hints.height,
			size_hints.min_width, size_hints.min_height,
			size_hints.max_width, size_hints.max_height,
			size_hints.width_inc, size_hints.height_inc,
			size_hints.min_aspect_num, size_hints.min_aspect_den,
			size_hints.max_aspect_num, size_hints.max_aspect_den,
			size_hints.base_width, size_hints.base_height,
			size_hints.win_gravity);
#endif

	// quirk for invalid min/max size hints reported by e.g. zyn
	if(  (size_hints.flags & (XCB_ICCCM_SIZE_HINT_P_MIN_SIZE | XCB_ICCCM_SIZE_HINT_P_MAX_SIZE) )
		&& (size_hints.min_width == 1) && (size_hints.min_height == 1)
		&& (size_hints.max_width == 1) && (size_hints.max_height == 1) )
	{
		return;
	}

	xcb_icccm_set_wm_size_hints(app->conn, app->win, XCB_ATOM_WM_NORMAL_HINTS, &size_hints);
	xcb_flush(app->conn);
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	signal(SIGINT, _sig);

	app->conn = xcb_connect(NULL, NULL);
	app->screen = xcb_setup_roots_iterator(xcb_get_setup(app->conn)).data;
	app->win = xcb_generate_id(app->conn);
	const uint32_t mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
	const uint32_t values [2] = {
		app->screen->white_pixel,
		XCB_EVENT_MASK_STRUCTURE_NOTIFY
	};

	app->w = 640;
	app->h = 360;
	xcb_create_window(app->conn, XCB_COPY_FROM_PARENT, app->win, app->screen->root,
		0, 0, app->w, app->h, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, app->screen->root_visual, mask, values);

	const float dpi0 = 96.f;
	float dpi1 = dpi0;

	// read DPI from users's ~/.Xresources
	xcb_xrm_database_t *database = xcb_xrm_database_from_default(app->conn);
	if(database != NULL)
	{
		char *value = NULL;

		if(xcb_xrm_resource_get_string(database, "Xft.dpi", NULL, &value) >= 0)
		{
			dpi1 = atof(value);
			free(value);
		}

		xcb_xrm_database_free(database);
	}

	const float scale_factor = dpi1 / dpi0;
	sandbox_slave_scale_factor_set(sb, scale_factor);

	const char *title = sandbox_slave_title_get(sb);
	if(title)
	{
		xcb_change_property(app->conn, XCB_PROP_MODE_REPLACE, app->win,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			strlen(title), title);
	}

	app->cookie = xcb_intern_atom(app->conn, 1, 12, "WM_PROTOCOLS");
	app->reply = xcb_intern_atom_reply(app->conn, app->cookie, 0);

	app->cookie2 = xcb_intern_atom(app->conn, 0, 16, "WM_DELETE_WINDOW");
	app->reply2 = xcb_intern_atom_reply(app->conn, app->cookie2, 0);

	xcb_change_property(app->conn,
		XCB_PROP_MODE_REPLACE, app->win, (*app->reply).atom, 4, 32, 1, &(*app->reply2).atom);

	xcb_map_window(app->conn, app->win);
	xcb_flush(app->conn);

	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = (void *)(uintptr_t)app->win
	};

	if(!(app->handle = sandbox_slave_instantiate(sb, &parent_feature,
		app->dsp_instance, (uintptr_t *)&app->widget)))
	{
		return -1;
	}
	if(!app->widget)
	{
		return -1;
	}

	_clone_size_hints(app);

	app->idle_iface = sandbox_slave_extension_data(sb, LV2_UI__idleInterface);
	app->resize_iface = sandbox_slave_extension_data(sb, LV2_UI__resize);

	// work-around for broken lsp-plugins
	if((uintptr_t)app->resize_iface == (uintptr_t)app->idle_iface)
	{
		app->resize_iface = NULL;
	}

	cross_clock_init(&app->clk_real, CROSS_CLOCK_REALTIME);

	return 0;
}

static inline void
_run(sandbox_slave_t *sb, float update_rate, void *data)
{
	app_t *app = data;
	const unsigned ns = 1000000000 / update_rate;
	struct timespec to;
	cross_clock_gettime(&app->clk_real, &to);

	while(!atomic_load_explicit(app->done, memory_order_relaxed))
	{
		xcb_generic_event_t *e;
		while((e = xcb_poll_for_event(app->conn)))
		{
			switch(e->response_type & ~0x80)
			{
				case XCB_CONFIGURE_NOTIFY:
				{
					const xcb_configure_notify_event_t *ev = (const xcb_configure_notify_event_t *)e;
					if( (app->w != ev->width) || (app->h != ev->height) )
					{
						app->w = ev->width;
						app->h = ev->height;

						const uint32_t values [2] = {app->w, app->h};
						xcb_configure_window(app->conn, app->widget,
							XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT,
							&values);
						xcb_flush(app->conn);

						if(app->resize_iface)
						{
							app->resize_iface->ui_resize(app->handle, app->w, app->h);
						}
					}
					break;
				}
				case XCB_REPARENT_NOTIFY:
				{
					_clone_size_hints(app);
				} break;
				case XCB_CLIENT_MESSAGE:
				{
					const xcb_client_message_event_t *ev = (const xcb_client_message_event_t *)e;
					if(ev->data.data32[0] == (*app->reply2).atom)
					{
						atomic_store_explicit(app->done, true, memory_order_relaxed);
					}
					break;
				}
			}
			free(e);
		}

		if(sandbox_slave_timedwait(sb, &to)) // timedout
		{
			struct timespec tf;

			cross_clock_gettime(&app->clk_real, &tf);
			const uint64_t dd = (tf.tv_sec - to.tv_sec) * 1000000000
				 + (tf.tv_nsec - to.tv_nsec);

#if 0
			printf(":: %i, %lums\n", getpid(), dd/1000000);
#endif

			if(dd <= ns)
			{
				if(app->idle_iface)
				{
					if(app->idle_iface->idle(app->handle))
					{
						atomic_store_explicit(app->done, true, memory_order_relaxed);
					}
				}
			}

			to.tv_nsec += ns;
			while(to.tv_nsec >= 1000000000)
			{
				to.tv_nsec -= 1000000000;
				to.tv_sec += 1;
			}
		}
		else
		{
			if(sandbox_slave_recv(sb))
			{
				atomic_store_explicit(app->done, true, memory_order_relaxed);
			}
		}
	}
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	xcb_destroy_subwindows(app->conn, app->win);
	xcb_destroy_window(app->conn, app->win);
	xcb_disconnect(app->conn);

	cross_clock_deinit(&app->clk_real);
}

static inline int
_request(void *data, LV2_URID key, size_t path_len, char *path)
{
	app_t *app = data;
	(void)app;

	FILE *fin = popen("zenity --file-selection", "r");
	const size_t len = fread(path, sizeof(char), path_len, fin);
	pclose(fin);

	if(len)
	{
		path[len] = '\0';
		return 0;
	}

	return 1;
}

static const sandbox_slave_driver_t x11_driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = _resize,
	.request_cb = _request
};

int
x11_app_run(int argc, char **argv, void *dsp_instance, atomic_bool *done)
{
	app_t app;
	int res;

	memset(&app, 0x0, sizeof(app_t));

	app.done = done;
	_done = done;

	app.dsp_instance = dsp_instance;
	app.sb = sandbox_slave_new(argc, argv, &x11_driver, &app, &res);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		return res;
	}

	return res;
}
