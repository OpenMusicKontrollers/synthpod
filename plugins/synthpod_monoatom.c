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
#include <stdlib.h>
#include <assert.h>

#include <synthpod_lv2.h>
#include <synthpod_app.h>

#include <lv2/lv2plug.in/ns/ext/atom/atom.h>
#include <lv2/lv2plug.in/ns/ext/atom/forge.h>
#include <lv2/lv2plug.in/ns/ext/midi/midi.h>
#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/worker/worker.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/log/log.h>
#include <lv2/lv2plug.in/ns/ext/buf-size/buf-size.h>
#include <lv2/lv2plug.in/ns/ext/options/options.h>

#include <zero_worker.h>
#include <lv2_osc.h>
#include <varchunk.h>

#include <Eina.h>

#define CHUNK_SIZE 0x10000
#define MAX_MSGS 10 //FIXME limit to how many events?

typedef struct _plughandle_t plughandle_t;

struct _plughandle_t {
	sp_app_t *app;
	sp_app_driver_t driver;

	LV2_Worker_Schedule *schedule;
	Zero_Worker_Schedule *zero_sched;
	LV2_Log_Log *log;
	LV2_Options_Option *opts;

	bool dirty_in;

	struct {
		struct {
			LV2_URID entry;
			LV2_URID error;
			LV2_URID note;
			LV2_URID trace;
			LV2_URID warning;
		} log;
		struct {
			LV2_URID max_block_length;
			LV2_URID min_block_length;
			LV2_URID sequence_size;
		} bufsz;
		struct {
			LV2_URID event;
		} synthpod;
	} uri;

	struct {
		LV2_Atom_Forge event_out;
		LV2_Atom_Forge com_in;
		LV2_Atom_Forge notify;
	} forge;

	struct {
		const LV2_Atom_Sequence *event_in;
		LV2_Atom_Sequence *event_out;

		const LV2_Atom_Sequence *control;
		LV2_Atom_Sequence *notify;
	} port;

	struct {
		LV2_Atom_Sequence *event_in;
		LV2_Atom_Sequence *com_in;
	} source;

	struct {
		const LV2_Atom_Sequence *event_out;
		const float *audio_out[2];
		const float *output[4];
		const LV2_Atom_Sequence *com_out;
	} sink;

	uint8_t buf [CHUNK_SIZE] _ATOM_ALIGNED;

	bool advance_worker;
	bool advance_ui;
	bool trigger_worker;
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;
	varchunk_t *app_from_ui;
	varchunk_t *app_from_app;
};

static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	plughandle_t *handle = data;

	if(handle->log)
	{
		return handle->log->vprintf(handle->log->handle, type, fmt, args);
	}
	else if(type != handle->uri.log.trace)
	{
		const char *type_str = NULL;
		if(type == handle->uri.log.entry)
			type_str = "Entry";
		else if(type == handle->uri.log.error)
			type_str = "Error";
		else if(type == handle->uri.log.note)
			type_str = "Note";
		else if(type == handle->uri.log.trace)
			type_str = "Trace";
		else if(type == handle->uri.log.warning)
			type_str = "Warning";

		fprintf(stderr, "[%s]", type_str);
		vfprintf(stderr, fmt, args);
		fputc('\n', stderr);

		return 0;
	}
	
	return -1;
}

// non-rt || rt with LV2_LOG__Trace
static int
_log_printf(void *data, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(data, type, fmt, args);
  va_end(args);

	return ret;
}

static LV2_State_Status
_state_save(LV2_Handle instance, LV2_State_Store_Function store,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	return sp_app_save(app, store, state, flags, features);
}

static LV2_State_Status
_state_restore(LV2_Handle instance, LV2_State_Retrieve_Function retrieve,
	LV2_State_Handle state, uint32_t flags,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	handle->dirty_in = true;

	return sp_app_restore(app, retrieve, state, flags, features);
}
	
static const LV2_State_Interface state_iface = {
	.save = _state_save,
	.restore = _state_restore
};

// non-rt worker-thread
static LV2_Worker_Status
_work(LV2_Handle instance,
	LV2_Worker_Respond_Function respond,
	LV2_Worker_Respond_Handle target,
	uint32_t _size,
	const void *_body)
{
	plughandle_t *handle = instance;
	
	//printf("_work: %u\n", size);
	
	size_t size;
	const void *body;
	while((body = varchunk_read_request(handle->app_to_worker, &size)))
	{
		sp_worker_from_app(handle->app, size, body);
		varchunk_read_advance(handle->app_to_worker);
	}

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_work_response(LV2_Handle instance, uint32_t size, const void *body)
{
	//plughandle_t *handle = instance;

	return LV2_WORKER_SUCCESS;
}

// rt-thread
static LV2_Worker_Status
_end_run(LV2_Handle instance)
{
	//plughandle_t *handle = instance;

	return LV2_WORKER_SUCCESS;
}

static const LV2_Worker_Interface work_iface = {
	.work = _work,
	.work_response = _work_response,
	.end_run = _end_run
};

// non-rt
static Zero_Worker_Status 
_zero_work(LV2_Handle instance, Zero_Worker_Request_Function request,
	Zero_Worker_Advance_Function advance, Zero_Worker_Handle target,
	uint32_t _size, const void *_body)
{
	plughandle_t *handle = instance;
	
	//printf("_zero_work: %u\n", size);

	size_t size;
	const void *body;
	while((body = varchunk_read_request(handle->app_to_worker, &size)))
	{
		sp_worker_from_app(handle->app, size, body);
		varchunk_read_advance(handle->app_to_worker);
	}
	
	return ZERO_WORKER_SUCCESS;
}

// rt-thread
static Zero_Worker_Status
_zero_response(LV2_Handle instance, uint32_t size,
	const void* body)
{
	//plughandle_t *handle = instance;

	return ZERO_WORKER_SUCCESS;
}

// rt-thread
static Zero_Worker_Status
_zero_end(LV2_Handle instance)
{
	//plughandle_t *handle = instance;

	return ZERO_WORKER_SUCCESS;
}

static const Zero_Worker_Interface zero_iface = {
	.work = _zero_work,
	.response = _zero_response,
	.end = _zero_end
};

// rt-thread
static void *
_to_ui_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	return handle->buf;
}
static void
_to_ui_advance(size_t size, void *data)
{
	plughandle_t *handle = data;
	LV2_Atom_Forge *forge = &handle->forge.notify;

	//printf("_to_ui_advance: %zu\n", size);

	if(forge->offset + size > forge->size)
		return; // buffer overflow

	lv2_atom_forge_frame_time(forge, 0);
	lv2_atom_forge_raw(forge, handle->buf, size);
	lv2_atom_forge_pad(forge, size);
}

// rt
static void *
_to_worker_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	return varchunk_write_request(handle->app_to_worker, size);
}
static void
_to_worker_advance(size_t size, void *data)
{
	plughandle_t *handle = data;

	varchunk_write_advance(handle->app_to_worker, size);
	handle->trigger_worker = true;
}

// non-rt worker-thread
static void *
_to_app_request(size_t size, void *data)
{
	plughandle_t *handle = data;

	void *ptr;
	do
	{
		ptr = varchunk_write_request(handle->app_from_worker, size);
	}
	while(!ptr); // wait until there is enough space

	return ptr;
}
static void
_to_app_advance(size_t size, void *data)
{
	plughandle_t *handle = data;	

	varchunk_write_advance(handle->app_from_worker, size);
}

static LV2_Handle
instantiate(const LV2_Descriptor* descriptor, double rate,
	const char *bundle_path, const LV2_Feature *const *features)
{
	eina_init();

	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	handle->driver.sample_rate = rate;
	handle->driver.seq_size = SEQ_SIZE;
	handle->driver.log_printf = _log_printf;
	handle->driver.log_vprintf = _log_vprintf;
	handle->driver.system_port_add = NULL;
	handle->driver.system_port_del = NULL;
	handle->driver.osc_sched = NULL;
	handle->driver.features = 0;

	const LilvWorld *world = NULL;

	for(int i=0; features[i]; i++)
		if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->driver.map = (LV2_URID_Map *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->driver.unmap = (LV2_URID_Unmap *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_LOG__log))
			handle->log = (LV2_Log_Log *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_WORKER__schedule))
			handle->schedule = (LV2_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_OPTIONS__options))
			handle->opts = (LV2_Options_Option *)features[i]->data;
		else if(!strcmp(features[i]->URI, SYNTHPOD_PREFIX"world"))
			world = (const LilvWorld *)features[i]->data;
		else if(!strcmp(features[i]->URI, ZERO_WORKER__schedule))
			handle->zero_sched = (Zero_Worker_Schedule *)features[i]->data;
		else if(!strcmp(features[i]->URI, OSC__schedule))
			handle->driver.osc_sched = (osc_schedule_t *)features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_BUF_SIZE__fixedBlockLength))
			handle->driver.features |= SP_APP_FEATURE_FIXED_BLOCK_LENGTH;
		else if(!strcmp(features[i]->URI, LV2_BUF_SIZE__powerOf2BlockLength))
			handle->driver.features |= SP_APP_FEATURE_POWER_OF_2_BLOCK_LENGTH;

	if(!handle->driver.map)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	
	if(!handle->schedule && !handle->zero_sched)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support worker:schedule\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	if(handle->zero_sched)
	{
		_log_printf(handle, handle->uri.log.note,
			"%s: Host supports zero-worker:schedule\n",
			descriptor->URI);
	}
	
	if(!handle->opts)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: Host does not support options:option\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	// map URIs
	handle->uri.log.entry = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Entry);
	handle->uri.log.error = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Error);
	handle->uri.log.note = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Note);
	handle->uri.log.trace = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Trace);
	handle->uri.log.warning = handle->driver.map->map(handle->driver.map->handle,
		LV2_LOG__Warning);
			
	handle->uri.bufsz.max_block_length = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__maxBlockLength);
	handle->uri.bufsz.min_block_length = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__minBlockLength);
	handle->uri.bufsz.sequence_size = handle->driver.map->map(handle->driver.map->handle,
		LV2_BUF_SIZE__sequenceSize);

	handle->uri.synthpod.event = handle->driver.map->map(handle->driver.map->handle,
		SYNTHPOD_EVENT_URI);

	for(LV2_Options_Option *opt = handle->opts;
		(opt->key != 0) && (opt->value != NULL);
		opt++)
	{
		if(opt->key == handle->uri.bufsz.max_block_length)
			handle->driver.max_block_size = *(int32_t *)opt->value;
		else if(opt->key == handle->uri.bufsz.sequence_size)
			handle->driver.min_block_size = *(int32_t *)opt->value;
		//TODO handle more options
	}

	handle->advance_worker = true; //TODO reset in activate ?
	handle->advance_ui = true; //TODO reset in activate ?
	handle->app_to_worker = varchunk_new(CHUNK_SIZE);
	handle->app_from_worker = varchunk_new(CHUNK_SIZE);
	handle->app_from_ui = varchunk_new(CHUNK_SIZE);
	handle->app_from_app = varchunk_new(CHUNK_SIZE);

	handle->driver.to_ui_request = _to_ui_request;
	handle->driver.to_ui_advance = _to_ui_advance;
	handle->driver.to_worker_request = _to_worker_request;
	handle->driver.to_worker_advance = _to_worker_advance;
	handle->driver.to_app_request = _to_app_request;
	handle->driver.to_app_advance = _to_app_advance;

	handle->app = sp_app_new(world, &handle->driver, handle);
	if(!handle->app)
	{
		_log_printf(handle, handle->uri.log.error,
			"%s: creation of app failed\n",
			descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge.event_out, handle->driver.map);
	lv2_atom_forge_init(&handle->forge.com_in, handle->driver.map);
	lv2_atom_forge_init(&handle->forge.notify, handle->driver.map);

	return handle;
}

static void
connect_port(LV2_Handle instance, uint32_t port, void *data)
{
	plughandle_t *handle = instance;

	switch(port)
	{
		case 0:
			handle->port.event_in = (const LV2_Atom_Sequence *)data;
			break;
		case 1:
			handle->port.event_out = (LV2_Atom_Sequence *)data;
			break;

		case 2:
			handle->port.control = (const LV2_Atom_Sequence *)data;
			break;
		case 3:
			handle->port.notify = (LV2_Atom_Sequence *)data;
			break;

		default:
			break;
	}
}

static void
activate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	sp_app_activate(handle->app);
}

static inline void
_process_pre(plughandle_t *handle, uint32_t nsamples, bool bypassed)
{
	sp_app_t *app = handle->app;

	// drain worker buffer
	{
		size_t size;
		const void *body;
		unsigned n = 0;
		while((body = varchunk_read_request(handle->app_from_worker, &size))
			&& (n++ < MAX_MSGS) )
		{
			bool advance = sp_app_from_worker(handle->app, size, body);
			if(!advance)
			{
				//fprintf(stderr, "plugin worker is blocked\n");
				break;
			}
			varchunk_read_advance(handle->app_from_worker);
		}
	}

	// run app pre
	if(!bypassed)
		sp_app_run_pre(app, nsamples);

	// drain events from UI ringbuffer
	{
		const LV2_Atom *atom;
		size_t size;
		unsigned n = 0;
		while((atom = varchunk_read_request(handle->app_from_ui, &size))
			&& (n++ < MAX_MSGS) )
		{
			handle->advance_ui = sp_app_from_ui(app, atom);
			if(!handle->advance_ui)
			{
				//fprintf(stderr, "plugin ui indirect is blocked\n");
				break;
			}
			varchunk_read_advance(handle->app_from_ui);
		}
	}

	// drain events from feedback ringbuffer
	{
		const LV2_Atom *atom;
		size_t size;
		unsigned n = 0;
		while((atom = varchunk_read_request(handle->app_from_app, &size))
			&& (n++ < MAX_MSGS) )
		{
			handle->advance_ui = sp_app_from_ui(app, atom);
			if(!handle->advance_ui)
			{
				//fprintf(stderr, "plugin feedback is blocked\n");
				break;
			}
			varchunk_read_advance(handle->app_from_app);
		}
	}

	//FIXME drain event from separate feedback ringbuffer

	// handle events from UI
	LV2_ATOM_SEQUENCE_FOREACH(handle->port.control, ev)
	{
		const LV2_Atom *atom = &ev->body;
		const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

		if(atom->type == handle->forge.notify.Object)
		{
			// copy com events to com buffer 
			if(sp_app_com_event(handle->app, obj->body.id))
			{
				uint32_t size = obj->atom.size + sizeof(LV2_Atom);
				lv2_atom_forge_frame_time(&handle->forge.com_in, ev->time.frames);
				lv2_atom_forge_raw(&handle->forge.com_in, obj, size);
				lv2_atom_forge_pad(&handle->forge.com_in, size);
			}

			// try do process events directly
			handle->advance_ui = sp_app_from_ui(app, atom);
			if(!handle->advance_ui) // queue event in ringbuffer instead
			{
				//fprintf(stderr, "plugin ui direct is blocked\n");

				void *ptr;
				size_t size = lv2_atom_total_size(atom);
				if((ptr = varchunk_write_request(handle->app_from_ui, size)))
				{
					memcpy(ptr, atom, size);
					varchunk_write_advance(handle->app_from_ui, size);
				}
				else
				{
					//fprintf(stderr, "app_from_ui ringbuffer full\n");
					//FIXME
				}
			}
		}
	}

	// run app post
	if(!bypassed)
		sp_app_run_post(app, nsamples);

	// write com events to feedback buffer
	if(handle->sink.com_out)
	{
		LV2_ATOM_SEQUENCE_FOREACH(handle->sink.com_out, ev)
		{
			const LV2_Atom *atom = &ev->body;

			void *ptr;
			size_t size = lv2_atom_total_size(atom);
			if((ptr = varchunk_write_request(handle->app_from_app, size)))
			{
				memcpy(ptr, atom, size);
				varchunk_write_advance(handle->app_from_app, size);
			}
			else
			{
				//FIXME
			}
		}
	}
}
		
static inline void
_process_post(plughandle_t *handle)
{
	if(handle->trigger_worker)
	{
		//fprintf(stderr, "work triggered\n");

		if(handle->zero_sched)
		{
			int32_t *i;
			if((i = handle->zero_sched->request(handle->zero_sched->handle, sizeof(int32_t))))
			{
				*i = 1;
				handle->zero_sched->advance(handle->zero_sched->handle, sizeof(int32_t));
			}
			else
			{
				//FIXME
			}
		}
		else // !handle->zero_sched
		{
			const int32_t i = 1;
			handle->schedule->schedule_work(handle->schedule->handle, sizeof(int32_t), &i); //FIXME check
		}

		handle->trigger_worker = false;
	}
}

static void
run(LV2_Handle instance, uint32_t nsamples)
{
	plughandle_t *handle = instance;
	sp_app_t *app = handle->app;

	const size_t sample_buf_size = sizeof(float) * nsamples;

	// get input buffers
	handle->source.event_in = NULL;
	
	const sp_app_system_source_t *sources = sp_app_get_system_sources(app);
	int audio_ptr = 0;
	int control_ptr = 0;
	for(const sp_app_system_source_t *source=sources;
		source->type != SYSTEM_PORT_NONE;
		source++)
	{
		switch(source->type)
		{
			case SYSTEM_PORT_MIDI:
				handle->source.event_in = source->buf;
				break;
			case SYSTEM_PORT_COM:
				handle->source.com_in = source->buf;
				break;

			case SYSTEM_PORT_AUDIO:
			case SYSTEM_PORT_CONTROL:
			case SYSTEM_PORT_CV:
			case SYSTEM_PORT_OSC:
			case SYSTEM_PORT_NONE:
				break;
		}
	}

	//TODO use __builtin_assume_aligned

	// fill input buffers
	if(handle->source.event_in)
		memcpy(handle->source.event_in, handle->port.event_in, SEQ_SIZE);

	// get output buffers
	const sp_app_system_sink_t *sinks = sp_app_get_system_sinks(app);

	// fill output buffers
	handle->sink.event_out = NULL;
	
	audio_ptr = 0;
	control_ptr = 0;
	for(const sp_app_system_sink_t *sink=sinks;
		sink->type != SYSTEM_PORT_NONE;
		sink++)
	{
		switch(sink->type)
		{
			case SYSTEM_PORT_MIDI:
				handle->sink.event_out = sink->buf;
				break;
			case SYSTEM_PORT_COM:
				handle->sink.com_out = sink->buf;
				break;

			case SYSTEM_PORT_AUDIO:
			case SYSTEM_PORT_CONTROL:
			case SYSTEM_PORT_CV:
			case SYSTEM_PORT_OSC:
			case SYSTEM_PORT_NONE:
				break;
		}
	}

	if(handle->dirty_in)
	{
		//printf("dirty\n");
		//TODO refresh UI
		handle->dirty_in = false;
	}

	struct {
		LV2_Atom_Forge_Frame event_out;
		LV2_Atom_Forge_Frame com_in;
		LV2_Atom_Forge_Frame notify;
	} frame;

	// prepare forge(s) & sequence(s)
	lv2_atom_forge_set_buffer(&handle->forge.event_out,
		(uint8_t *)handle->port.event_out, handle->port.event_out->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.event_out, &frame.event_out, 0);
	
	lv2_atom_forge_set_buffer(&handle->forge.com_in,
		(uint8_t *)handle->source.com_in, SEQ_SIZE);
	lv2_atom_forge_sequence_head(&handle->forge.com_in, &frame.com_in, 0);
	
	lv2_atom_forge_set_buffer(&handle->forge.notify,
		(uint8_t *)handle->port.notify, handle->port.notify->atom.size);
	lv2_atom_forge_sequence_head(&handle->forge.notify, &frame.notify, 0);

	if(sp_app_bypassed(app))
	{
		//fprintf(stderr, "plugin app is bypassed\n");

		_process_pre(handle, nsamples, true);
		_process_post(handle);

		// end sequence(s)
		lv2_atom_forge_pop(&handle->forge.event_out, &frame.event_out);
		lv2_atom_forge_pop(&handle->forge.com_in, &frame.com_in);
		lv2_atom_forge_pop(&handle->forge.notify, &frame.notify);

		return;
	}

	_process_pre(handle, nsamples, false);
	_process_post(handle);

	// end sequence(s)
	lv2_atom_forge_pop(&handle->forge.event_out, &frame.event_out);
	lv2_atom_forge_pop(&handle->forge.com_in, &frame.com_in);
	lv2_atom_forge_pop(&handle->forge.notify, &frame.notify);

	if(handle->sink.event_out)
		memcpy(handle->port.event_out, handle->sink.event_out, SEQ_SIZE);
	else
		memset(handle->port.event_out, 0x0, SEQ_SIZE);
}

static void
deactivate(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	sp_app_deactivate(handle->app);
}

static void
cleanup(LV2_Handle instance)
{
	plughandle_t *handle = instance;

	sp_app_free(handle->app);
	free(handle);

	varchunk_free(handle->app_to_worker);
	varchunk_free(handle->app_from_worker);
	varchunk_free(handle->app_from_ui);
	varchunk_free(handle->app_from_app);

	eina_shutdown();
}

static uint32_t
_opts_get(LV2_Handle instance, LV2_Options_Option *options)
{
	// we have no options

	return LV2_OPTIONS_ERR_BAD_KEY;
}

static uint32_t
_opts_set(LV2_Handle instance, const LV2_Options_Option *options)
{
	plughandle_t *handle = instance;

	// route options to all plugins
	return sp_app_options_set(handle->app, options);
}

static const LV2_Options_Interface opts_iface = {
	.get = _opts_get,
	.set = _opts_set
};

static const void*
extension_data(const char* uri)
{
	if(!strcmp(uri, LV2_WORKER__interface))
		return &work_iface;
	else if(!strcmp(uri, LV2_STATE__interface))
		return &state_iface;
	else if(!strcmp(uri, ZERO_WORKER__interface))
		return &zero_iface;
	else if(!strcmp(uri, LV2_OPTIONS__interface))
		return &opts_iface;
	else
		return NULL;
}

const LV2_Descriptor synthpod_monoatom = {
	.URI						= SYNTHPOD_MONOATOM_URI,
	.instantiate		= instantiate,
	.connect_port		= connect_port,
	.activate				= activate,
	.run						= run,
	.deactivate			= deactivate,
	.cleanup				= cleanup,
	.extension_data	= extension_data
};