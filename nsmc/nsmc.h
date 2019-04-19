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

#ifndef _NSMC_H
#define _NSMC_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NSMC_API
#	define NSMC_API static
#endif

#include <stdbool.h>

typedef struct _nsmc_t nsmc_t;
typedef struct _nsmc_driver_t nsmc_driver_t;
	
typedef int (*nsmc_open_t)(const char *path, const char *name,
	const char *id, void *data);
typedef int (*nsmc_save_t)(void *data);
typedef int (*nsmc_show_t)(void *data);
typedef int (*nsmc_hide_t)(void *data);

struct _nsmc_driver_t {
	nsmc_open_t open;
	nsmc_save_t save;
	nsmc_show_t show;
	nsmc_hide_t hide;
};

NSMC_API nsmc_t *
nsmc_new(const char *call, const char *exe, const char *fallback_path,
	const nsmc_driver_t *driver, void *data);

NSMC_API void
nsmc_free(nsmc_t *nsm);

NSMC_API void
nsmc_run(nsmc_t *nsm);

NSMC_API void
nsmc_opened(nsmc_t *nsm, int status);

NSMC_API void
nsmc_shown(nsmc_t *nsm);

NSMC_API void
nsmc_hidden(nsmc_t *nsm);

NSMC_API void
nsmc_saved(nsmc_t *nsm, int status);

NSMC_API bool
nsmc_managed(nsmc_t *nsm);

#ifdef NSMC_IMPLEMENTATION

#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <unistd.h> // getpid

#include <osc.lv2/writer.h>
#include <osc.lv2/reader.h>
#include <osc.lv2/stream.h>

#include <varchunk.h>

typedef void (*osc_cb_t)(LV2_OSC_Reader *reader, nsmc_t *nsm);
typedef struct _osc_msg_t osc_msg_t;

struct _osc_msg_t {
	const char *path;
	osc_cb_t cb;
};

struct _nsmc_t {
	bool managed;
	bool connected;
	bool connectionless;

	char *url;
	char *call;
	char *exe;

	const nsmc_driver_t *driver;
	void *data;

	LV2_OSC_Stream stream;

	varchunk_t *tx;
	varchunk_t *rx;
};

static void
_reply(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	const char *target = NULL;
	lv2_osc_reader_get_string(reader, &target);

	//fprintf(stdout, "nsmc reply: %s\n", target);

	if(target && !strcmp(target, "/nsm/server/announce"))
	{
		const char *message = NULL;
		const char *manager = NULL;
		const char *capabilities = NULL;

		lv2_osc_reader_get_string(reader, &message);
		lv2_osc_reader_get_string(reader, &manager);
		lv2_osc_reader_get_string(reader, &capabilities);

		//TODO, e.g. toggle SM LED, check capabilities
	}
}

static void
_error(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	const char *msg = NULL;
	int32_t code = 0;
	const char *err = NULL;

	lv2_osc_reader_get_string(reader, &msg);
	lv2_osc_reader_get_int32(reader, &code);
	lv2_osc_reader_get_string(reader, &err);

	fprintf(stderr, "nsmc error: #%i in %s: %s\n", code, msg, err);
}

static void
_client_open(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	const char *dir = NULL;
	const char *name = NULL;
	const char *id = NULL;

	lv2_osc_reader_get_string(reader, &dir);
	lv2_osc_reader_get_string(reader, &name);
	lv2_osc_reader_get_string(reader, &id);

	char tmp [PATH_MAX];
	const char *resolvedpath = realpath(dir, tmp);
	if(!resolvedpath)
		resolvedpath = dir;

	if(nsm->driver->open && nsm->driver->open(resolvedpath, name, id, nsm->data))
		fprintf(stderr, "NSM load failed: '%s'\n", dir);
}

static void
_client_save(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	// save app
	if(nsm->driver->save && nsm->driver->save(nsm->data))
		fprintf(stderr, "NSM save failed:\n");
}

static void
_client_show_optional_gui(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	// show gui
	if(nsm->driver->show && nsm->driver->show(nsm->data))
	{
		fprintf(stderr, "NSM show GUI failed\n");
		return;
	}

	nsmc_shown(nsm);
}

static void
_client_hide_optional_gui(LV2_OSC_Reader *reader, nsmc_t *nsm)
{
	// hide gui
	if(nsm->driver->hide && nsm->driver->hide(nsm->data))
	{
		fprintf(stderr, "NSM hide GUI failed\n");
		return;
	}

	nsmc_hidden(nsm);
}

static void
_announce(nsmc_t *nsm)
{
	// send announce message
	pid_t pid = getpid();

	int has_gui = nsm->driver->show && nsm->driver->hide;
	const char *capabilities = has_gui
		? ":message:optional-gui:"
		: ":message:";

	uint8_t *tx;
	size_t max;
	if( (tx = varchunk_write_request_max(nsm->tx, 1024, &max)) )
	{
		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, tx, max);

		LV2_OSC_Writer_Frame bndl = {0}, itm = {0};
		lv2_osc_writer_push_bundle(&writer, &bndl, LV2_OSC_IMMEDIATE);
		{
			lv2_osc_writer_push_item(&writer, &itm);
			lv2_osc_writer_message_vararg(&writer, "/nsm/server/announce", "sssiii",
				nsm->call, capabilities, nsm->exe, 1, 2, pid);
			lv2_osc_writer_pop_item(&writer, &itm);
		}
		if(has_gui)
		{
			// report initial gui visibility //FIXME should be saved in state somewhere
			if(nsm->driver->show(nsm->data) == 0)
			{
				lv2_osc_writer_push_item(&writer, &itm);
				lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_shown", "");
				lv2_osc_writer_pop_item(&writer, &itm);
			}
			else
			{
				lv2_osc_writer_push_item(&writer, &itm);
				lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_hidden", "");
				lv2_osc_writer_pop_item(&writer, &itm);
			}
		}
		lv2_osc_writer_pop_bundle(&writer, &bndl);

		size_t written;
		if(lv2_osc_writer_finalize(&writer, &written))
		{
			varchunk_write_advance(nsm->tx, written);
		}
		else
		{
			fprintf(stderr, "OSC sending failed\n");
		}
	}
}

static const osc_msg_t messages [] = {
	{"/reply", _reply},
	{"/error", _error},

	{"/nsm/client/open", _client_open},
	{"/nsm/client/save", _client_save},
	{"/nsm/client/show_optional_gui", _client_show_optional_gui},
	{"/nsm/client/hide_optional_gui", _client_hide_optional_gui},

	{NULL, NULL}
};

static void
_unpack_messages(LV2_OSC_Reader *reader, size_t len, nsmc_t *nsm)
{
	if(lv2_osc_reader_is_message(reader))
	{
		const char *path = NULL;
		const char *fmt = NULL;
		lv2_osc_reader_get_string(reader, &path);
		lv2_osc_reader_get_string(reader, &fmt);
		for(const osc_msg_t *msg = messages; msg->cb; msg++)
		{
			if(!strcmp(msg->path, path))
			{
				msg->cb(reader, nsm);
				break;
			}
		}
	}
	else if(lv2_osc_reader_is_bundle(reader))
	{
		OSC_READER_BUNDLE_FOREACH(reader, itm, len)
		{
			LV2_OSC_Reader reader2;
			lv2_osc_reader_initialize(&reader2, itm->body, itm->size);
			_unpack_messages(&reader2, itm->size, nsm);
		}
	}
}

static void *
_recv_req(void *data, size_t size, size_t *max)
{
	nsmc_t *nsm = data;

	return varchunk_write_request_max(nsm->rx, size, max);
}

static void
_recv_adv(void *data, size_t written)
{
	nsmc_t *nsm = data;

	varchunk_write_advance(nsm->rx, written);
}

static const void *
_send_req(void *data, size_t *len)
{
	nsmc_t *nsm = data;

	return varchunk_read_request(nsm->tx, len);
}

static void
_send_adv(void *data)
{
	nsmc_t *nsm = data;

	varchunk_read_advance(nsm->tx);
}

static const LV2_OSC_Driver driver = {
	.write_req = _recv_req,
	.write_adv = _recv_adv,
	.read_req = _send_req,
	.read_adv = _send_adv
};

NSMC_API nsmc_t *
nsmc_new(const char *call, const char *exe, const char *fallback_path,
	const nsmc_driver_t *nsm_driver, void *data)
{
	if(!nsm_driver)
		return NULL;

	nsmc_t *nsm = calloc(1, sizeof(nsmc_t));
	if(!nsm)
		return NULL;

	nsm->driver = nsm_driver;
	nsm->data = data;

	nsm->call = call ? strdup(call) : NULL;
	nsm->exe = exe ? strdup(exe) : NULL;

	nsm->url = getenv("NSM_URL");
	if(nsm->url)
	{
		nsm->managed = true;
		nsm->connectionless = !strncmp(nsm->url, "osc.udp", 7) ? true : false;

		nsm->url = strdup(nsm->url); //FIXME
		if(!nsm->url)
			return NULL;

		// remove trailing slash
		if(!isdigit(nsm->url[strlen(nsm->url)-1]))
			nsm->url[strlen(nsm->url)-1] = '\0';

		nsm->tx = varchunk_new(8192, false);
		if(!nsm->tx)
			return NULL;

		nsm->rx = varchunk_new(8192, false);
		if(!nsm->rx)
			return NULL;

		if(lv2_osc_stream_init(&nsm->stream, nsm->url, &driver, nsm) != 0)
			return NULL;
	}
	else if(fallback_path)
	{
		char tmp [PATH_MAX];
		const char *resolvedfallback_path = realpath(fallback_path, tmp);
		if(!resolvedfallback_path)
			resolvedfallback_path = fallback_path;

		if(nsm->driver->open && nsm->driver->open(resolvedfallback_path, "unmanaged", nsm->call, nsm->data))
			fprintf(stderr, "NSM load failed: '%s'\n", fallback_path);
	}

	return nsm;
}

NSMC_API void
nsmc_free(nsmc_t *nsm)
{
	if(nsm)
	{
		if(nsm->call)
			free(nsm->call);
		if(nsm->exe)
			free(nsm->exe);

		if(nsm->url)
		{
			if(nsm->rx)
				varchunk_free(nsm->rx);

			if(nsm->tx)
				varchunk_free(nsm->tx);

			lv2_osc_stream_deinit(&nsm->stream);

			free(nsm->url);
		}

		free(nsm);
	}
}

NSMC_API void
nsmc_run(nsmc_t *nsm)
{
	if(!nsm)
		return;

	const LV2_OSC_Enum ev = lv2_osc_stream_run(&nsm->stream);

	if(ev & LV2_OSC_ERR)
	{
		fprintf(stderr, "%s: %s\n", __func__, strerror(ev & LV2_OSC_ERR));
		nsm->connected = false;
	}

	if(nsm->connectionless || (ev & LV2_OSC_CONN) )
	{
		if(!nsm->connected)
		{
			_announce(nsm); // initial announcement
			nsm->connected = true;
		}
	}

	if(ev & LV2_OSC_RECV)
	{
		const uint8_t *rx;
		size_t size;
		while( (rx = varchunk_read_request(nsm->rx, &size)) )
		{
			LV2_OSC_Reader reader;

			lv2_osc_reader_initialize(&reader, rx, size);
			_unpack_messages(&reader, size, nsm);

			varchunk_read_advance(nsm->rx);
		}
	}
}

NSMC_API void
nsmc_opened(nsmc_t *nsm, int status)
{
	if(!nsm)
		return;

	uint8_t *tx;
	size_t max;
	if( (tx = varchunk_write_request_max(nsm->tx, 1024, &max)) )
	{
		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, tx, max);

		if(status == 0)
		{
			lv2_osc_writer_message_vararg(&writer, "/reply", "ss",
				"/nsm/client/open", "opened");
		}
		else
		{
			lv2_osc_writer_message_vararg(&writer, "/error", "sis",
				"/nsm/client/open", 2, "opening failed");
		}

		size_t written;
		if(lv2_osc_writer_finalize(&writer, &written))
		{
			varchunk_write_advance(nsm->tx, written);
		}
		else
		{
			fprintf(stderr, "OSC sending failed\n");
		}
	}
}

NSMC_API void
nsmc_shown(nsmc_t *nsm)
{
	if(!nsm)
		return;

	uint8_t *tx;
	size_t max;
	if( (tx = varchunk_write_request_max(nsm->tx, 1024, &max)) )
	{
		// reply
		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, tx, max);
		lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_shown", "");

		size_t written;
		if(lv2_osc_writer_finalize(&writer, &written))
		{
			varchunk_write_advance(nsm->tx, written);
		}
		else
		{
			fprintf(stderr, "OSC sending failed\n");
		}
	}
}

NSMC_API void
nsmc_hidden(nsmc_t *nsm)
{
	if(!nsm)
		return;

	uint8_t *tx;
	size_t max;
	if( (tx = varchunk_write_request_max(nsm->tx, 1024, &max)) )
	{
		// reply
		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, tx, max);
		lv2_osc_writer_message_vararg(&writer, "/nsm/client/gui_is_hidden", "");

		size_t written;
		if(lv2_osc_writer_finalize(&writer, &written))
		{
			varchunk_write_advance(nsm->tx, written);
		}
		else
		{
			fprintf(stderr, "OSC sending failed\n");
		}
	}
}

NSMC_API void
nsmc_saved(nsmc_t *nsm, int status)
{
	if(!nsm)
		return;

	uint8_t *tx;
	size_t max;
	if( (tx = varchunk_write_request_max(nsm->tx, 1024, &max)) )
	{
		LV2_OSC_Writer writer;
		lv2_osc_writer_initialize(&writer, tx, max);

		if(status == 0)
		{
			lv2_osc_writer_message_vararg(&writer, "/reply", "ss",
				"/nsm/client/save", "saved");
		}
		else
		{
			lv2_osc_writer_message_vararg(&writer, "/error", "sis",
				"/nsm/client/save", 1, "save failed");
		}

		size_t written;
		if(lv2_osc_writer_finalize(&writer, &written))
		{
			varchunk_write_advance(nsm->tx, written);
		}
		else
		{
			fprintf(stderr, "OSC sending failed\n");
		}
	}
}

NSMC_API bool
nsmc_managed(nsmc_t *nsm)
{
	return nsm->managed;
}

#endif /* NSMC_IMPLEMENTATION */

#ifdef __cplusplus
}
#endif

#endif /*_NSMC_H */