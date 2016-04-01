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
#include <dlfcn.h>
#include <string.h>
#include <ctype.h>

#include <synthpod_sandbox_slave.h>
#include <synthpod_sandbox_io.h>

#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>
#include <lv2/lv2plug.in/ns/extensions/ui/ui.h>

#include <lilv/lilv.h>
#include <symap.h>

struct _sandbox_slave_t {
	Symap *symap;
	LV2_URID_Map map;
	LV2_URID_Unmap unmap;

	LV2_Log_Log log;

	LV2UI_Port_Map port_map;
	LV2UI_Port_Subscribe port_subscribe;

	LV2UI_Resize host_resize;
	const LV2UI_Resize *client_resize;

	const LV2UI_Idle_Interface *idle_iface;

	LilvWorld *world;
	LilvNode *bundle_node;
	LilvNode *plugin_node;
	LilvNode *ui_node;

	const LilvPlugin *plug;
	const LilvUI *ui;

	void *lib;
	const LV2UI_Descriptor *desc;
	void *handle;

	sandbox_io_t io;

	const sandbox_slave_driver_t *driver;
	void *data;

	const char *plugin_uri;
	const char *bundle_path;
	const char *ui_uri;
	const char *socket_path;
	const char *window_title;
};
	
static inline LV2_URID
_map(void *data, const char *uri)
{
	sandbox_slave_t *sb= data;

	return symap_map(sb->symap, uri);
}

static inline const char *
_unmap(void *data, LV2_URID urid)
{
	sandbox_slave_t *sb= data;

	return symap_unmap(sb->symap, urid);
}

static inline int
_log_vprintf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, va_list args)
{
	sandbox_slave_t *sb = handle;

	vprintf(fmt, args);

	return 0;
}

static inline int
_log_printf(LV2_Log_Handle handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;

  va_start (args, fmt);
	const int ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static inline uint32_t
_port_index(LV2UI_Feature_Handle handle, const char *symbol)
{
	sandbox_slave_t *sb = handle;
	uint32_t index = LV2UI_INVALID_PORT_INDEX;

	LilvNode *symbol_uri = lilv_new_string(sb->world, symbol);
	if(symbol_uri)
	{
		const LilvPort *port = lilv_plugin_get_port_by_symbol(sb->plug, symbol_uri);

		if(port)
			index = lilv_port_get_index(sb->plug, port);

		lilv_node_free(symbol_uri);
	}

	return index;
}

static inline void
_write_function(LV2UI_Controller controller, uint32_t index,
	uint32_t size, uint32_t protocol, const void *buf)
{
	sandbox_slave_t *sb = controller;

	const bool sent = _sandbox_io_send(&sb->io, index, size, protocol, buf);
	if(!sent)
		fprintf(stderr, "_sandbox_io_send failed\n");
}

static inline uint32_t
_port_subscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	sandbox_slave_t *sb = handle;

	const sandbox_io_subscription_t sub = {
		.state = 1,
		.protocol = protocol
	};
	_write_function(handle, index, sizeof(sandbox_io_subscription_t), sb->io.ui_port_subscribe, &sub);

	return 0;
}

static inline uint32_t
_port_unsubscribe(LV2UI_Feature_Handle handle, uint32_t index, uint32_t protocol,
	const LV2_Feature *const *features)
{
	sandbox_slave_t *sb = handle;

	const sandbox_io_subscription_t sub = {
		.state = 0,
		.protocol = protocol
	};
	_write_function(handle, index, sizeof(sandbox_io_subscription_t), sb->io.ui_port_subscribe, &sub);

	return 0;
}

static inline void
_sandbox_recv_cb(LV2UI_Handle handle, uint32_t index, uint32_t size,
	uint32_t protocol, const void *buf)
{
	sandbox_slave_t *sb = handle;

	if(sb->desc && sb->desc->port_event)
		sb->desc->port_event(sb->handle, index, size, protocol, buf);
}

static inline int
_sandbox_resize(sandbox_slave_t *sb, int w, int h)
{
	if(sb->client_resize)
		return sb->client_resize->ui_resize(sb->data, w, h);

	return 0;
}

sandbox_slave_t *
sandbox_slave_new(int argc, char **argv, const sandbox_slave_driver_t *driver, void *data)
{
	sandbox_slave_t *sb = calloc(1, sizeof(sandbox_slave_t));
	if(!sb)
	{
		fprintf(stderr, "allocation failed\n");
		goto fail;
	}

	int c;
	while((c = getopt(argc, argv, "p:b:u:s:w:")) != -1)
	{
		switch(c)
		{
			case 'p':
				sb->plugin_uri = optarg;
				break;
			case 'b':
				sb->bundle_path = optarg;
				break;
			case 'u':
				sb->ui_uri = optarg;
				break;
			case 's':
				sb->socket_path = optarg;
				break;
			case 'w':
				sb->window_title = optarg;
				break;
			case '?':
				if( (optopt == 'p') || (optopt == 'b') || (optopt == 'u') || (optopt == 's') || (optopt == 'w') )
					fprintf(stderr, "Option `-%c' requires an argument.\n", optopt);
				else if(isprint(optopt))
					fprintf(stderr, "Unknown option `-%c'.\n", optopt);
				else
					fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
				goto fail;
			default:
				goto fail;
		}
	}

	if(  !sb->plugin_uri
		|| !sb->bundle_path
		|| !sb->ui_uri
		|| !sb->socket_path
		|| !sb->window_title)
	{
		fprintf(stderr, "invalid arguments\n");
		goto fail;
	}

	sb->driver = driver;
	sb->data = data;

	sb->map.handle = sb;
	sb->map.map = _map;

	sb->unmap.handle = sb;
	sb->unmap.unmap = _unmap;

	sb->log.handle = sb;
	sb->log.printf = _log_printf;
	sb->log.vprintf = _log_vprintf;

	sb->port_map.handle = sb;
	sb->port_map.port_index = _port_index;

	sb->port_subscribe.handle = sb;
	sb->port_subscribe.subscribe = _port_subscribe;
	sb->port_subscribe.unsubscribe = _port_unsubscribe;

	sb->host_resize.handle = data;
	sb->host_resize.ui_resize = driver->resize_cb;

	if(!(sb->symap = symap_new()))
	{
		fprintf(stderr, "symap_new failed\n");
		goto fail;
	}

	if(!(sb->world = lilv_world_new()))
	{
		fprintf(stderr, "lilv_world_new failed\n");
		goto fail;
	}

	sb->bundle_node = lilv_new_file_uri(sb->world, NULL, sb->bundle_path);
	sb->plugin_node = lilv_new_uri(sb->world, sb->plugin_uri);
	sb->ui_node = lilv_new_uri(sb->world, sb->ui_uri);

	if(!sb->bundle_node || !sb->plugin_node || !sb->ui_node)
	{
		fprintf(stderr, "lilv_new_uri failes\n");
		goto fail;
	}

	lilv_world_load_bundle(sb->world, sb->bundle_node);
	lilv_world_load_resource(sb->world, sb->ui_node);

	const LilvPlugins *plugins = lilv_world_get_all_plugins(sb->world);
	if(!plugins)
	{
		fprintf(stderr, "lilv_world_get_all_plugins failed\n");
		goto fail;
	}

	if(!(sb->plug = lilv_plugins_get_by_uri(plugins, sb->plugin_node)))
	{
		fprintf(stderr, "lilv_plugins_get_by_uri failed\n");
		goto fail;
	}

	LilvUIs *uis = lilv_plugin_get_uis(sb->plug);
	if(!uis)
	{
		fprintf(stderr, "lilv_plugin_get_uis failed\n");
		goto fail;
	}

	if(!(sb->ui = lilv_uis_get_by_uri(uis, sb->ui_node)))
	{
		fprintf(stderr, "lilv_uis_get_by_uri failed\n");
		goto fail;
	}

	const LilvNode *ui_path = lilv_ui_get_binary_uri(sb->ui);
	if(!ui_path)
	{
		fprintf(stderr, "lilv_ui_get_binary_uri failed\n");
		goto fail;
	}

	if(!(sb->lib = dlopen(lilv_node_get_path(ui_path, NULL), RTLD_LAZY)))
	{
		fprintf(stderr, "dlopen failed\n");
		goto fail;
	}

	LV2UI_DescriptorFunction desc_func = dlsym(sb->lib, "lv2ui_descriptor");
	if(!desc_func)
	{
		fprintf(stderr, "dlsym failed\n");
		goto fail;
	}

	for(int i=0; true; i++)
	{
		const LV2UI_Descriptor *desc = desc_func(i);
		if(!desc) // sentinel
			break;

		if(!strcmp(desc->URI, sb->ui_uri))
		{
			sb->desc = desc;
			break;
		}
	}

	if(!sb->desc)
	{
		fprintf(stderr, "LV2UI_Descriptor lookup failed\n");
		goto fail;
	}

	if(sb->desc->extension_data)
	{
		sb->idle_iface = sb->desc->extension_data(LV2_UI__idleInterface);
		sb->client_resize= sb->desc->extension_data(LV2_UI__resize);
	}

	if(_sandbox_io_init(&sb->io, &sb->map, &sb->unmap, sb->socket_path, false))
	{
		fprintf(stderr, "_sandbox_io_init failed\n");
		goto fail;
	}

	if(driver->init_cb && (driver->init_cb(sb, data) != 0) )
	{
		fprintf(stderr, "driver->init_cb failed\n");
		goto fail;
	}

	return sb; // success

fail:
	sandbox_slave_free(sb);
	return NULL;
}

void
sandbox_slave_free(sandbox_slave_t *sb)
{
	if(!sb)
		return;

	if(sb->desc && sb->desc->cleanup)
		sb->desc->cleanup(sb->handle);

	if(sb->driver && sb->driver->deinit_cb)
		sb->driver->deinit_cb(sb->data);

	_sandbox_io_deinit(&sb->io);

	if(sb->lib)
		dlclose(sb->lib);

	if(sb->world)
	{
		if(sb->ui_node)
		{
			lilv_world_unload_resource(sb->world, sb->ui_node);
			lilv_node_free(sb->ui_node);
		}

		if(sb->bundle_node)
		{
			lilv_world_unload_bundle(sb->world, sb->bundle_node);
			lilv_node_free(sb->bundle_node);
		}

		if(sb->plugin_node)
			lilv_node_free(sb->plugin_node);

		lilv_world_free(sb->world);
	}

	if(sb->symap)
		symap_free(sb->symap);

	free(sb);
}

int
sandbox_slave_instantiate(sandbox_slave_t *sb, void *parent, void *widget)
{
	LV2_Options_Option options [] = {
		[0] = {
			.context = LV2_OPTIONS_INSTANCE,
			.subject = 0,
			.key = sb->io.ui_window_title,
			.size = strlen(sb->plugin_uri) + 1,
			.type = sb->io.forge.String,
			.value = sb->plugin_uri
		},
		[1] = {
			.key = 0,
			.value = NULL
		}
	};

	const LV2_Feature map_feature = {
		.URI = LV2_URID__map,
		.data = &sb->map
	};
	const LV2_Feature unmap_feature = {
		.URI = LV2_URID__unmap,
		.data = &sb->unmap
	};
	const LV2_Feature parent_feature = {
		.URI = LV2_UI__parent,
		.data = parent
	};
	const LV2_Feature log_feature = {
		.URI = LV2_LOG__log,
		.data = &sb->log
	};
	const LV2_Feature port_map_feature = {
		.URI = LV2_UI__portMap,
		.data = &sb->port_map
	};
	const LV2_Feature port_subscribe_feature = {
		.URI = LV2_UI__portSubscribe,
		.data = &sb->port_subscribe
	};
	const LV2_Feature idle_feature = {
		.URI = LV2_UI__idleInterface,
		.data = NULL
	};
	const LV2_Feature resize_feature = {
		.URI = LV2_UI__resize,
		.data = &sb->host_resize
	};
	const LV2_Feature options_feature = {
		.URI = LV2_OPTIONS__options,
		.data = options
	};

	const LV2_Feature *const features [] = {
		&map_feature,
		&unmap_feature,
		&parent_feature,
		&log_feature,
		&port_map_feature,
		&port_subscribe_feature,
		&idle_feature,
		&options_feature,
		sb->host_resize.ui_resize ? &resize_feature : NULL,
		NULL
	};

	if(sb->desc && sb->desc->instantiate)
	{
		sb->handle = sb->desc->instantiate(sb->desc, sb->plugin_uri,
			sb->bundle_path, _write_function, sb, widget, features);
	}

	if(sb->handle)
		return 0; // success

	return -1;
}

void
sandbox_slave_recv(sandbox_slave_t *sb)
{
	if(sb)
		_sandbox_io_recv(&sb->io, _sandbox_recv_cb, NULL, sb);
}

bool
sandbox_slave_flush(sandbox_slave_t *sb)
{
	if(sb)
		return _sandbox_io_flush(&sb->io);

	return false;
}

int
sandbox_slave_idle(sandbox_slave_t *sb)
{
	if(sb && sb->idle_iface)
		return sb->idle_iface->idle(sb->handle);

	return 0;
}

void
sandbox_slave_run(sandbox_slave_t *sb)
{
	if(sb && sb->driver && sb->driver->run_cb)
		sb->driver->run_cb(sb, sb->data);
}

void
sandbox_slave_fd_get(sandbox_slave_t *sb, int *fd)
{
	if(sb && fd)
		*fd = _sandbox_io_fd_get(&sb->io);
	else
		*fd = -1;
}

const char *
sandbox_slave_title_get(sandbox_slave_t *sb)
{
	if(sb)
		return sb->window_title;

	return NULL;
}