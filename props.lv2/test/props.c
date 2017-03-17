/*
 * Copyright (c) 2015 Hanspeter Portner (dev@open-music-kontrollers.ch)
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

#include <stdio.h>

#include <props.h>

#include <lv2/lv2plug.in/ns/ext/log/log.h>

#define PROPS_PREFIX		"http://open-music-kontrollers.ch/lv2/props#"
#define PROPS_TEST_URI	PROPS_PREFIX"test"

#define MAX_NPROPS 7
#define MAX_STRLEN 256

typedef struct _plugstate_t plugstate_t;
typedef struct _plughandle_t plughandle_t;

struct _plugstate_t {
	int32_t val1;
	int64_t val2;
	float val3;
	double val4;
	char val5 [MAX_STRLEN];
	char val6 [MAX_STRLEN];
	uint8_t val7 [MAX_STRLEN];
};

struct _plughandle_t {
	LV2_URID_Map *map;
	LV2_Log_Log *log;
	LV2_Atom_Forge forge;
	LV2_Atom_Forge_Ref ref;

	LV2_URID log_trace;
	LV2_URID log_note;

	PROPS_T(props, MAX_NPROPS);
	plugstate_t state;
	plugstate_t stash;

	struct {
		LV2_URID val2;
		LV2_URID val4;
	} urid;

	const LV2_Atom_Sequence *event_in;
	LV2_Atom_Sequence *event_out;
};

static int
_log_vprintf(plughandle_t *handle, LV2_URID type, const char *fmt, va_list args)
{
	return handle->log->vprintf(handle->log->handle, type, fmt, args);
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(plughandle_t *handle, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(handle, type, fmt, args);
  va_end(args);

	return ret;
}

static void
_intercept(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	switch(event)
	{
		case PROP_EVENT_GET:
		{
			_log_printf(handle, handle->log_trace, "GET     : %s", impl->def->property);
			break;
		}
		case PROP_EVENT_SET:
		{
			_log_printf(handle, handle->log_trace, "SET     : %s", impl->def->property);
			break;
		}
		case PROP_EVENT_REGISTER:
		{
			_log_printf(handle, handle->log_trace, "REGISTER: %s", impl->def->property);
			break;
		}
		case PROP_EVENT_SAVE:
		{
			_log_printf(handle, handle->log_note, "SAVE    : %s", impl->def->property);
			break;
		}
		case PROP_EVENT_RESTORE:
		{
			_log_printf(handle, handle->log_note, "RESTORE : %s", impl->def->property);
			break;
		}
	}
}

static void
_intercept_stat1(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->state.val2 = handle->state.val1 * 2;

		if(forge)
			props_set(&handle->props, forge, frames, handle->urid.val2, &handle->ref);
	}
}

static void
_intercept_stat3(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		handle->state.val4 = handle->state.val3 * 2;

		if(forge)
			props_set(&handle->props, forge, frames, handle->urid.val4, &handle->ref);
	}
}

static void
_intercept_stat6(void *data, LV2_Atom_Forge *forge, int64_t frames,
	props_event_t event, props_impl_t *impl)
{
	plughandle_t *handle = data;

	_intercept(data, forge, frames, event, impl);

	if(event & PROP_EVENT_WRITE)
	{
		const char *path = strstr(handle->state.val6, "file://")
			? handle->state.val6 + 7 // skip "file://"
			: handle->state.val6;
		FILE *f = fopen(path, "wb"); // create empty file
		if(f)
			fclose(f);
	}
}

static const props_def_t defs [MAX_NPROPS] = {
	{
		.property = PROPS_PREFIX"statInt",
		.offset = offsetof(plugstate_t, val1),
		.type = LV2_ATOM__Int,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept_stat1,
	},
	{
		.property = PROPS_PREFIX"statLong",
		.access = LV2_PATCH__readable,
		.offset = offsetof(plugstate_t, val2),
		.type = LV2_ATOM__Long,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept,
	},
	{
		.property = PROPS_PREFIX"statFloat",
		.offset = offsetof(plugstate_t, val3),
		.type = LV2_ATOM__Float,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept_stat3,
	},
	{	
		.property = PROPS_PREFIX"statDouble",
		.access = LV2_PATCH__readable,
		.offset = offsetof(plugstate_t, val4),
		.type = LV2_ATOM__Double,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept,
	},
	{
		.property = PROPS_PREFIX"statString",
		.offset = offsetof(plugstate_t, val5),
		.type = LV2_ATOM__String,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept,
		.max_size = MAX_STRLEN // strlen
	},
	{
		.property = PROPS_PREFIX"statPath",
		.offset = offsetof(plugstate_t, val6),
		.type = LV2_ATOM__Path,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept_stat6,
		.max_size = MAX_STRLEN // strlen
	},
	{
		.property = PROPS_PREFIX"statChunk",
		.offset = offsetof(plugstate_t, val7),
		.type = LV2_ATOM__Chunk,
		.event_mask = PROP_EVENT_ALL,
		.event_cb = _intercept,
		.max_size = MAX_STRLEN // strlen
	}
};

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	for(unsigned i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = features[i]->data;
	}

	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->log)
	{
		fprintf(stderr,
			"%s: Host does not support log:log\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	handle->log_trace = handle->map->map(handle->map->handle, LV2_LOG__Trace);
	handle->log_note = handle->map->map(handle->map->handle, LV2_LOG__Note);

	lv2_atom_forge_init(&handle->forge, handle->map);
	if(!props_init(&handle->props, MAX_NPROPS, descriptor->URI, handle->map, handle))
	{
		fprintf(stderr, "failed to initialize property structure\n");
		free(handle);
		return NULL;
	}

	if(!props_register(&handle->props, defs, MAX_NPROPS, &handle->state, &handle->stash))
	{
		_log_printf(handle, handle->log_trace, "ERR     : registering");
		free(handle);
		return NULL;
	}

	handle->urid.val2 = props_map(&handle->props, PROPS_PREFIX"statLong");
	handle->urid.val4 = props_map(&handle->props, PROPS_PREFIX"statDouble");

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = (plughandle_t *)instance;

	switch(port)
	{
		case 0:
			handle->event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->event_out = (LV2_Atom_Sequence *)data;
			break;
		default:
			break;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;

	uint32_t capacity = handle->event_out->atom.size;
	LV2_Atom_Forge_Frame frame;
	lv2_atom_forge_set_buffer(&handle->forge, (uint8_t *)handle->event_out, capacity);
	handle->ref = lv2_atom_forge_sequence_head(&handle->forge, &frame, 0);

	LV2_ATOM_SEQUENCE_FOREACH(handle->event_in, ev)
	{
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)&ev->body;

		if(handle->ref)
			props_advance(&handle->props, &handle->forge, ev->time.frames, obj, &handle->ref); //TODO handle return
	}
	if(handle->ref)
		lv2_atom_forge_pop(&handle->forge, &frame);
	else
		lv2_atom_sequence_clear(handle->event_out);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	free(handle);
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return props_save(&handle->props, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = (plughandle_t *)instance;

	return props_restore(&handle->props, retrieve, state, flags, features);
}

LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

static inline LV2_Worker_Status
_work(LV2_Handle instance, LV2_Worker_Respond_Function respond,
LV2_Worker_Respond_Handle worker, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work(&handle->props, respond, worker, size, body);
}

static inline LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	plughandle_t *handle = instance;

	return props_work_response(&handle->props, size, body);
}

LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = NULL
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;
	return NULL;
}

const LV2_Descriptor props_test = {
	.URI						= PROPS_TEST_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= NULL,
	.run						= run,
	.deactivate			= NULL,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};

#ifdef _WIN32
__declspec(dllexport)
#else
__attribute__((visibility("default")))
#endif
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	switch(index)
	{
		case 0:
			return &props_test;
		default:
			return NULL;
	}
}
