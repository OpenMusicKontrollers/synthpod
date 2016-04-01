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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include <synthpod_sandbox_slave.h>

#include <xcb/xcb.h>
#include <signal.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	xcb_connection_t *conn;
	xcb_screen_t *screen;
	xcb_drawable_t win;
	xcb_drawable_t widget;
	xcb_intern_atom_cookie_t cookie;
 	xcb_intern_atom_reply_t* reply;
	xcb_intern_atom_cookie_t cookie2;
 	xcb_intern_atom_reply_t* reply2;
};

static volatile bool done = false;

static inline void
_sig(int signum)
{
	done = true;
}

static inline int
_resize(void *data, int w, int h)
{
	app_t *app= data;

	const uint32_t values[] = {w, h};
	xcb_configure_window(app->conn, app->win,
		XCB_CONFIG_WINDOW_WIDTH | XCB_CONFIG_WINDOW_HEIGHT, values);

	return 0;
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
		XCB_EVENT_MASK_EXPOSURE
	};

  xcb_create_window(app->conn, XCB_COPY_FROM_PARENT, app->win, app->screen->root,
		0, 0, 640, 360, 0,
		XCB_WINDOW_CLASS_INPUT_OUTPUT, app->screen->root_visual, mask, values);

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		xcb_change_property(app->conn, XCB_PROP_MODE_REPLACE, app->win,
			XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
			strlen(title), title);

	app->cookie = xcb_intern_atom(app->conn, 1, 12, "WM_PROTOCOLS");
 	app->reply = xcb_intern_atom_reply(app->conn, app->cookie, 0);

	app->cookie2 = xcb_intern_atom(app->conn, 0, 16, "WM_DELETE_WINDOW");
	app->reply2 = xcb_intern_atom_reply(app->conn, app->cookie2, 0);

	xcb_change_property(app->conn,
		XCB_PROP_MODE_REPLACE, app->win, (*app->reply).atom, 4, 32, 1, &(*app->reply2).atom);
 
  xcb_map_window(app->conn, app->win);
  xcb_flush(app->conn);

	if(sandbox_slave_instantiate(sb, (void *)(uintptr_t)app->win, (void *)(uintptr_t *)&app->widget))
		return -1;

	return 0;
}

static inline void
_run(sandbox_slave_t *sb, void *data)
{
	app_t *app = data;

	while(!done)
	{
		xcb_generic_event_t *e;
		if((e = xcb_poll_for_event(app->conn)))
		{
			switch(e->response_type & ~0x80)
			{
				case XCB_EXPOSE:
					xcb_flush(app->conn);
					break;
				case XCB_CLIENT_MESSAGE:
					if( (*(xcb_client_message_event_t*)e).data.data32[0] == (*app->reply2).atom)
						done = true;
					break;
			}
			free(e);
		}

		usleep(40000); // 25 fps

		sandbox_slave_recv(sb);
		if(sandbox_slave_idle(sb))
			done = true;
		const bool sent = sandbox_slave_flush(sb);
		if(!sent)
			fprintf(stderr, "sandbox_slave_flush failed\n");
	}
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	xcb_destroy_subwindows(app->conn, app->win);
	xcb_destroy_window(app->conn, app->win);
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = _resize
};

int
main(int argc, char **argv)
{
	static app_t app;

	app.sb = sandbox_slave_new(argc, argv, &driver, &app);
	if(app.sb)
	{
		sandbox_slave_run(app.sb);
		sandbox_slave_free(app.sb);
		printf("bye from %s\n", argv[0]);
		return 0;
	}

	printf("fail from %s\n", argv[0]);
	return -1;
}