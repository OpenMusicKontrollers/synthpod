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

#include <synthpod_sandbox_slave.h>

#include <gtk/gtk.h>
#include <glib-unix.h>

typedef struct _app_t app_t;

struct _app_t {
	sandbox_slave_t *sb;

	GtkWidget *win;
	GtkWidget *widget;
	GSource *source;
	guint signal;
};

static gboolean
_recv(void *data)
{
	sandbox_slave_t *sb = data;
	sandbox_slave_recv(sb);
	return true;
}

static gboolean
_source_prepare(GSource *base, int *timeout)
{
	*timeout = -1;
	return false; // wait for poll
}

static gboolean
_source_dispatch(GSource *base, GSourceFunc callback, void *data)
{
	return callback(data);
}

static GSourceFuncs source_funcs = {
	.prepare = _source_prepare,
	.check = NULL,
	.dispatch = _source_dispatch,
	.finalize = NULL
};

static void
_destroy(GtkWidget *widget, void *data)
{
	app_t *app = data;

  gtk_main_quit();
	app->win = NULL;
}

static gboolean
_sig(void *data)
{
	app_t *app = data;

	gtk_main_quit();
	app->win = NULL;

	return true;
}

static inline int
_init(sandbox_slave_t *sb, void *data)
{
	app_t *app= data;

	app->win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	if(!app->win)
	{
		fprintf(stderr, "gtk_window_new failed\n");
		goto fail;
	}
	g_signal_connect(G_OBJECT(app->win), "destroy",
		G_CALLBACK(_destroy), app);

	const char *title = sandbox_slave_title_get(sb);
	if(title)
		gtk_window_set_title(GTK_WINDOW(app->win), title);

	if(sandbox_slave_instantiate(sb, (void *)app->win, (void *)&app->widget))
	{
		fprintf(stderr, "sandbox_slave_instantiate failed\n");
		goto fail;
	}

	gtk_container_add(GTK_CONTAINER(app->win), app->widget);
	gtk_widget_show(app->widget);
  gtk_widget_show(app->win);

	int fd;
	sandbox_slave_fd_get(sb, &fd);
	if(fd == -1)
	{
		fprintf(stderr, "sandbox_slave_fd_get failed\n");
		goto fail;
	}

	app->source = g_source_new(&source_funcs, sizeof(GSource));
	if(!app->source)
	{
		fprintf(stderr, "g_source_new failed\n");
		goto fail;
	}

	g_source_set_callback(app->source, _recv, sb, NULL);
	g_source_add_unix_fd(app->source, fd, G_IO_IN);
	g_source_attach(app->source, NULL);

	app->signal = g_unix_signal_add(SIGINT, _sig, app);
	if(!app->signal)
	{
		fprintf(stderr, "g_unix_signal_add failed\n");
		goto fail;
	}

	return 0; //success

fail:
	return -1;
}

static inline void
_run(sandbox_slave_t *sb, void *data)
{
	app_t *app = data;

	gtk_main();
}

static inline void
_deinit(void *data)
{
	app_t *app = data;

	if(app->source)
		g_source_destroy(app->source);

	if(app->signal)
		g_source_remove(app->signal);

	if(app->win)
	{
		gtk_widget_hide(app->win);
		gtk_widget_destroy(app->win);
	}
}

static const sandbox_slave_driver_t driver = {
	.init_cb = _init,
	.run_cb = _run,
	.deinit_cb = _deinit,
	.resize_cb = NULL
};

int
main(int argc, char **argv)
{
	static app_t app;
	
	gtk_init(&argc, &argv);

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