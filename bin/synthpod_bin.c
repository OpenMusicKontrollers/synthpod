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

#include <getopt.h>
#include <inttypes.h>
#include <unistd.h> // vfork
#include <sys/wait.h> // waitpid
#include <errno.h> // waitpid
#include <signal.h>

#include <synthpod_bin.h>
#include <sandbox_slave.h>
#include <synthpod_sandbox_x11_driver.h>

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_BLUE    "\x1b[34m"
#define ANSI_COLOR_MAGENTA "\x1b[35m"
#define ANSI_COLOR_CYAN    "\x1b[36m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#define SBOX_BUF_SIZE (size_t)0x1000000 // 16M
#define CHUNK_SIZE 0x100000 // 1M
#define MAX_MSGS 10 //FIXME limit to how many events?

#define CONTROL_PORT_INDEX 14
#define NOTIFY_PORT_INDEX 15

static atomic_bool done = ATOMIC_VAR_INIT(false);

static uint8_t ui_buf [CHUNK_SIZE]; //FIXME

enum {
	COLOR_TRACE = 0,
	COLOR_LOG,
	COLOR_ERROR,
	COLOR_NOTE,
	COLOR_WARNING,

	COLOR_DSP
};

static const char *prefix [2][6] = {
	[0] = {
		[COLOR_TRACE]   = "[Trace]",
		[COLOR_LOG]     = "[Log]  ",
		[COLOR_ERROR]   = "[Error]",
		[COLOR_NOTE]    = "[Note] ",
		[COLOR_WARNING] = "[Warn] ",

		[COLOR_DSP]     = "(DSP)"
	},
	[1] = {
		[COLOR_TRACE]   = "["ANSI_COLOR_BLUE   "Trace"ANSI_COLOR_RESET"]",
		[COLOR_LOG]     = "["ANSI_COLOR_MAGENTA"Log"ANSI_COLOR_RESET"]  ",
		[COLOR_ERROR]   = "["ANSI_COLOR_RED    "Error"ANSI_COLOR_RESET"]",
		[COLOR_NOTE]    = "["ANSI_COLOR_GREEN  "Note"ANSI_COLOR_RESET"] ",
		[COLOR_WARNING] = "["ANSI_COLOR_YELLOW "Warn"ANSI_COLOR_RESET"] ",

		[COLOR_DSP]     = "("ANSI_COLOR_CYAN "DSP"ANSI_COLOR_RESET")"
	}
};

__realtime static void
_close_request(void *data)
{
	bin_t *bin = data;

	bin_quit(bin);
}

__non_realtime static void
_opened(void *data, int status)
{
	bin_t *bin = data;

	nsmc_opened(bin->nsm, status);
}

__non_realtime static void
_saved(void *data, int status)
{
	bin_t *bin = data;

	nsmc_saved(bin->nsm, status);
}

__realtime static void *
_app_to_ui_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	if(minimum <= CHUNK_SIZE)
	{
		*maximum = CHUNK_SIZE;
		return ui_buf;
	}

	bin_log_trace(bin, "%s: buffer overflow\n", __func__);
	return NULL;
}
__realtime static void
_app_to_ui_advance(size_t written, void *data)
{
	bin_t *bin = data;

	if(sandbox_master_send(bin->sb, NOTIFY_PORT_INDEX, written, bin->atom_eventTransfer, ui_buf) == -1)
		bin_log_trace(bin, "%s: buffer overflow\n", __func__);

	sandbox_master_signal_tx(bin->sb);
}

__realtime static void *
_app_to_worker_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	return varchunk_write_request_max(bin->app_to_worker, minimum, maximum);
}
__realtime static void
_app_to_worker_advance(size_t written, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_to_worker, written);
	sem_post(&bin->sem);
}

__non_realtime static void *
_worker_to_app_request(size_t minimum, size_t *maximum, void *data)
{
	bin_t *bin = data;

	void *ptr;
	do
	{
		ptr = varchunk_write_request_max(bin->app_from_worker, minimum, maximum);
	}
	while(!ptr); // wait until there is enough space

	return ptr;
}
__non_realtime static void
_worker_to_app_advance(size_t written, void *data)
{
	bin_t *bin = data;

	varchunk_write_advance(bin->app_from_worker, written);
}

static inline void
_atomic_spin_lock(atomic_flag *flag)
{
	while(atomic_flag_test_and_set_explicit(flag, memory_order_acquire))
	{
		// spin
	}
}

static inline bool
_atomic_try_lock(atomic_flag *flag)
{
	return !atomic_flag_test_and_set_explicit(flag, memory_order_acquire);
}

static inline void
_atomic_unlock(atomic_flag *flag)
{
	atomic_flag_clear_explicit(flag, memory_order_release);
}

static inline bool
_is_dsp_thread(bin_t *bin)
{
	const pthread_t this = pthread_self();

	return pthread_equal(this, bin->dsp_thread);
}

__non_realtime static int
_log_vprintf(void *data, LV2_URID type, const char *fmt, va_list args)
{
	bin_t *bin = data;

	const bool is_worker_thread = !_is_dsp_thread(bin);

	// check for trace mode AND DSP thread ID
	if(!is_worker_thread)
	{
		size_t written = -1;
		if(_atomic_try_lock(&bin->trace_lock)) //FIXME use per-dsp-thread ringbuffer
		{
			size_t sz = 0;
			char *trace;
			if((trace = varchunk_write_request_max(bin->app_to_log, 512, &sz)))
			{
				vsnprintf(trace, sz, fmt, args);

				written = strlen(trace) + 1;
				varchunk_write_advance(bin->app_to_log, written);
				sem_post(&bin->sem);
			}
		}
		_atomic_unlock(&bin->trace_lock);
		return written;
	}
	else
	{
		// !log_trace OR not DSP thread ID
		int idx = COLOR_LOG;
		if(type == bin->log_trace)
			idx = COLOR_TRACE;
		else if(type == bin->log_error)
			idx = COLOR_ERROR;
		else if(type == bin->log_note)
			idx = COLOR_NOTE;
		else if(type == bin->log_warning)
			idx = COLOR_WARNING;

		//TODO send to UI?

		const int istty = isatty(STDERR_FILENO);
		fprintf(stderr, "%s %s ", prefix[istty][COLOR_DSP], prefix[istty][idx]);
		return vfprintf(stderr, fmt, args);
	}

	return 0;
}

__non_realtime static int __attribute__((format(printf, 3, 4)))
_log_printf(void *data, LV2_URID type, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(data, type, fmt, args);
  va_end(args);

	return ret;
}

__realtime static bool
_sb_recv_cb(void *data, uint32_t index, uint32_t size, uint32_t format,
	const void *buf)
{
	bin_t *bin = data;

	if(index == CONTROL_PORT_INDEX) // control for synthpod:stereo
	{
		const LV2_Atom *atom = buf;

		bin->advance_ui = sp_app_from_ui(bin->app, atom);
		if(!bin->advance_ui)
		{
			//fprintf(stderr, "ui is blocked\n");
			return false; // pause handling messages from UI (until fully drained)
		}
	}
	else
	{
		bin_log_trace(bin, "%s: unknown port index\n", __func__);
	}

	return true; // continue handling messages from UI
}

__realtime static void
_sb_subscribe_cb(void *data, uint32_t index, uint32_t protocol, bool state)
{
	bin_t *bin = data;

	// nothing
}

static bin_t *bin_ptr = NULL;
__non_realtime static void
_sig(int sig)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
	if(bin_ptr)
		sem_post(&bin_ptr->sem);
}

__realtime static char *
_mapper_alloc_rt(void *data, size_t size)
{
	bin_t *bin = data;
	const bool is_worker_thread = !_is_dsp_thread(bin);

	bool more;
	char *pool = lfrtm_alloc(bin->lfrtm, size, &more);

	if(!pool)
	{
		if(is_worker_thread)
		{
			bin_log_error(bin, "%s: allocation failed\n", __func__);
		}
		else
		{
			bin_log_trace(bin, "%s: allocation failed\n", __func__);
		}
	}
	else if(more)
	{
		if(is_worker_thread)
		{
			if(lfrtm_inject(bin->lfrtm))
			{
				bin_log_error(bin, "%s: injection failed\n", __func__);
			}
		}
		else
		{
			atomic_store(&bin->inject, true);
			sem_post(&bin->sem);
		}
	}

	return pool;
}

__realtime static void
_mapper_free_rt(void *data, char *uri)
{
	(void)data;
	(void)uri;

	// nothing
}

__realtime static uint32_t
_voice_map_new_uuid(void *data, uint32_t flags __attribute__((unused)))
{
	xpress_t *xpress = data;

	return xpress_map(xpress);
}

__non_realtime void
bin_init(bin_t *bin, uint32_t sample_rate)
{
	bin_ptr = bin;

	bin->sample_rate = sample_rate;

	// varchunk init
	sem_init(&bin->sem, 0, 0);
	bin->app_to_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_worker = varchunk_new(CHUNK_SIZE, true);
	bin->app_to_log = varchunk_new(CHUNK_SIZE, true);
	bin->app_from_com = varchunk_new(CHUNK_SIZE, false);
	bin->app_from_app = varchunk_new(CHUNK_SIZE, false);

	bin->lfrtm = lfrtm_new(512, 0x100000); // 1M
	bin->mapper = mapper_new(0x20000, 0, NULL, _mapper_alloc_rt, _mapper_free_rt, bin); // 128K

	bin->map = mapper_get_map(bin->mapper);
	bin->unmap = mapper_get_unmap(bin->mapper);

	xpress_init(&bin->xpress, 0, bin->map, NULL,
		XPRESS_EVENT_NONE, NULL, NULL, NULL);
	bin->xmap.new_uuid = _voice_map_new_uuid;
	bin->xmap.handle = &bin->xpress;

	bin->log_error = bin->map->map(bin->map->handle, LV2_LOG__Error);
	bin->log_note = bin->map->map(bin->map->handle, LV2_LOG__Note);
	bin->log_trace = bin->map->map(bin->map->handle, LV2_LOG__Trace);
	bin->log_warning = bin->map->map(bin->map->handle, LV2_LOG__Warning);
	bin->atom_eventTransfer = bin->map->map(bin->map->handle, LV2_ATOM__eventTransfer);

	bin->log.handle = bin;
	bin->log.printf = _log_printf;
	bin->log.vprintf = _log_vprintf;
	bin->trace_lock = (atomic_flag)ATOMIC_FLAG_INIT;
	
	bin->app_driver.map = bin->map;
	bin->app_driver.unmap = bin->unmap;
	bin->app_driver.xmap = &bin->xmap;
	bin->app_driver.log = &bin->log;
	bin->app_driver.to_ui_request = _app_to_ui_request;
	bin->app_driver.to_ui_advance = _app_to_ui_advance;
	bin->app_driver.to_worker_request = _app_to_worker_request;
	bin->app_driver.to_worker_advance = _app_to_worker_advance;
	bin->app_driver.to_app_request = _worker_to_app_request;
	bin->app_driver.to_app_advance = _worker_to_app_advance;
	bin->app_driver.num_slaves = bin->num_slaves;

	bin->app_driver.audio_prio = bin->audio_prio;
	bin->app_driver.bad_plugins = bin->bad_plugins;
	bin->app_driver.cpu_affinity = bin->cpu_affinity;
	bin->app_driver.close_request = _close_request;
	bin->app_driver.opened = _opened;
	bin->app_driver.saved = _saved;

	bin->worker_thread = pthread_self(); // thread ID of UI thread
	bin->first = true;

	atomic_init(&bin->gui_done, true);

	bin->sb_driver.socket_path = bin->socket_path;
	bin->sb_driver.map = bin->map;
	bin->sb_driver.unmap = bin->unmap;
	bin->sb_driver.recv_cb = _sb_recv_cb;
	bin->sb_driver.subscribe_cb = _sb_subscribe_cb;

	bin->sb = sandbox_master_new(&bin->sb_driver, bin, SBOX_BUF_SIZE);

	signal(SIGTERM, _sig);
	signal(SIGQUIT, _sig);
	signal(SIGINT, _sig);

	if(!nsmc_managed())
	{
		if(bin->has_gui)
		{
			bin_show(bin);
		}
	}

	cross_clock_init(&bin->clk_mono, CROSS_CLOCK_MONOTONIC);
	cross_clock_init(&bin->clk_real, CROSS_CLOCK_REALTIME);
}

static bool
_gui_rolling_threaded(bin_t *bin)
{
	const bool done = atomic_load(&bin->gui_done);

	return !done;
}

static bool
_gui_rolling_ipc(bin_t *bin)
{
	bool rolling = true;

	if(bin->child > 0)
	{
		int status;
		const int res = waitpid(bin->child, &status, WUNTRACED | WNOHANG);
		if(res < 0)
		{
			if(errno == ECHILD) // child not existing
			{
				rolling = false;
			}
		}
		else if(res == bin->child)
		{
			if(!WIFSTOPPED(status) && !WIFCONTINUED(status)) // child exited/crashed
			{
				rolling = false;
			}
		}
	}

	if(!rolling)
	{
		bin->child = 0; // invalidate
	}

	return rolling;
}

static bool
_gui_rolling(bin_t *bin)
{
	if(bin->threaded_gui)
	{
		return _gui_rolling_threaded(bin);
	}

	return _gui_rolling_ipc(bin);
}

__realtime void
bin_run(bin_t *bin, const char *name, char **argv, nsmc_callback_t callback)
{
	char *fallback_path = NULL;

	bin->argv = argv;
	bin->optind = optind;

	if(!argv[optind])
	{
		const char *home_dir = getenv("HOME");

		if(asprintf(&fallback_path, "%s/.lv2/Synthpod_default.preset.lv2/", home_dir) == -1)
		{
			fallback_path = NULL;
		}
	}

	// NSM init
	const char *exe = strrchr(argv[0], '/');
	exe = exe ? exe + 1 : argv[0]; // we only want the program name without path
	bin->nsm = nsmc_new(name, exe, fallback_path ? fallback_path : argv[optind],
		callback, bin); //TODO check

	if(fallback_path)
	{
		free(fallback_path);
	}

	pthread_t self = pthread_self();

	if(bin->worker_prio)
	{
		struct sched_param schedp;
		memset(&schedp, 0, sizeof(struct sched_param));
		schedp.sched_priority = bin->worker_prio;
		
		if(schedp.sched_priority)
		{
			if(pthread_setschedparam(self, SCHED_RR, &schedp))
				fprintf(stderr, "pthread_setschedparam error\n");
		}
	}

	//FIXME no timeout needed, but with yet-to-come NSM support
	const unsigned nsecs = 1000000000;
	const unsigned nfreq = 120; // Hz
	const unsigned nstep = nsecs / nfreq;
	struct timespec to;
	cross_clock_gettime(&bin->clk_real, &to);

	while(!atomic_load_explicit(&done, memory_order_relaxed))
	{
		bool timedout = false;

		if(sem_timedwait(&bin->sem, &to) == -1)
		{
			timedout = (errno == ETIMEDOUT);
		}

		if(timedout)
		{
			// check if GUI still running
			const bool rolling = _gui_rolling(bin);

			if(bin->last_rolling && !rolling)
			{
				if(nsmc_managed())
				{
					sp_app_visibility_set(bin->app, false);
					nsmc_hidden(bin->nsm);
				}

				if(bin->kill_gui || bin->threaded_gui)
				{
					atomic_store_explicit(&done, true, memory_order_relaxed);
				}
			}

			bin->last_rolling = rolling;

			// schedule next timeout
			uint64_t nanos = to.tv_nsec + nstep;
			while(nanos >= nsecs)
			{
				nanos -= nsecs;
				to.tv_sec += 1;
			}
			to.tv_nsec = nanos;
		}

		// handle lfrtm memory injection
		if(atomic_exchange(&bin->inject, false))
		{
			if(lfrtm_inject(bin->lfrtm))
			{
				bin_log_error(bin, "%s: injection failed\n", __func__);
			}
		}

		// read events from worker
		{
			size_t size;
			const void *body;
			while((body = varchunk_read_request(bin->app_to_worker, &size)))
			{
				sp_worker_from_app(bin->app, size, body);
				varchunk_read_advance(bin->app_to_worker);
			}
		}

		// read events from logger
		{
			size_t size;
			const char *trace;
			while((trace = varchunk_read_request(bin->app_to_log, &size)))
			{
				const int istty = isatty(STDERR_FILENO);
				fprintf(stderr, "%s %s %s", prefix[istty][COLOR_DSP], prefix[istty][COLOR_TRACE], trace);

				varchunk_read_advance(bin->app_to_log);
			}
		}

		// run NSM
		if(nsmc_managed())
			nsmc_run(bin->nsm);

		//sched_yield();
	}
}

#define ARGC 19
static void *
_gui_thread(void *data)
{
	bin_t *bin = data;

	char srate [32];
	char urate [32];
	char wname [384];
	char minimum [32];
	snprintf(srate, sizeof(srate), "%"PRIu32, bin->sample_rate);
	snprintf(urate, sizeof(urate), "%"PRIu32, bin->update_rate);
	snprintf(wname, sizeof(wname), "Synthpod - %s", bin->socket_path);
	snprintf(minimum, sizeof(minimum), "%zu", SBOX_BUF_SIZE);
	char *ui_uri = bin->d2tk_gui
		? SYNTHPOD_ROOT_D2TK_URI
		: SYNTHPOD_ROOT_NK_URI;

	char *argv [ARGC + 1] = {
		"synthpod_sandbox_x11",
		"-p", SYNTHPOD_STEREO_URI,
		"-P", SYNTHPOD_PLUGIN_DIR,
		"-u", ui_uri,
		"-U", SYNTHPOD_PLUGIN_DIR,
		"-s", (char *)bin->socket_path,
		"-w", wname,
		"-m", minimum,
		"-r", srate,
		"-f", urate,
		NULL
	};

	const intptr_t dummy = 1;
	atomic_store(&bin->gui_done, false);
	x11_app_run(ARGC, argv, (void *)dummy, &bin->gui_done);

	return NULL;
}
#undef ARGC

static int
_bin_show_threaded(bin_t *bin)
{
	if(pthread_create(&bin->gui_thread, NULL, _gui_thread, bin) != 0)
	{
		return -1;
	};

	sp_app_visibility_set(bin->app, true);

	return 0;
}

static int
_bin_show_ipc(bin_t *bin)
{
	char srate [32];
	char urate [32];
	char wname [384];
	char minimum [32];
	snprintf(srate, sizeof(srate), "%"PRIu32, bin->sample_rate);
	snprintf(urate, sizeof(urate), "%"PRIu32, bin->update_rate);
	snprintf(wname, sizeof(wname), "Synthpod - %s", bin->socket_path);
	snprintf(minimum, sizeof(minimum), "%zu", SBOX_BUF_SIZE);
	char *ui_uri = bin->d2tk_gui
		? SYNTHPOD_ROOT_D2TK_URI
		: SYNTHPOD_ROOT_NK_URI;

	bin->child = vfork();
	if(bin->child == 0) // child
	{
		char *const args [] = {
			"synthpod_sandbox_x11",
			"-p", SYNTHPOD_STEREO_URI,
			"-P", SYNTHPOD_PLUGIN_DIR,
			"-u", ui_uri,
			"-U", SYNTHPOD_PLUGIN_DIR,
			"-s", (char *)bin->socket_path,
			"-w", wname,
			"-m", minimum,
			"-r", srate,
			"-f", urate,
			"-q",
			NULL
		};

		execvp(args[0], args);
	}
	else if(bin->child == -1)
	{
		return -1;
	}

	sp_app_visibility_set(bin->app, true);

	return 0;
}

int
bin_show(bin_t *bin)
{
	if(sp_app_visibility_get(bin->app))
	{
		return 0;
	}

	if(bin->threaded_gui)
	{
		return _bin_show_threaded(bin);
	}

	return _bin_show_ipc(bin);
}

static int
_bin_hide_threaded(bin_t *bin)
{
	atomic_store(&bin->gui_done, true);
	pthread_join(bin->gui_thread, NULL);

	return 0;
}

static int
_bin_hide_ipc(bin_t *bin)
{
	if(bin->child > 0)
	{
		int status;

		kill(bin->child, SIGINT);
		waitpid(bin->child, &status, WUNTRACED); // blocking waitpid
		bin->child = 0;
	}

	return 0;
}

static int
_bin_hide(bin_t *bin)
{
	if(bin->threaded_gui)
	{
		return _bin_hide_threaded(bin);
	}

	return _bin_hide_ipc(bin);
}

int
bin_hide(bin_t *bin)
{
	sp_app_visibility_set(bin->app, false);

	return _bin_hide(bin);
}

bool
bin_visibility(bin_t *bin)
{
	return sp_app_visibility_get(bin->app);
}

__non_realtime void
bin_stop(bin_t *bin)
{
	// NSM deinit
	nsmc_free(bin->nsm);

	_bin_hide(bin);

	if(bin->path)
		free(bin->path);
}

__non_realtime void
bin_deinit(bin_t *bin)
{
	if(bin->sb)
		sandbox_master_free(bin->sb);

	// synthpod deinit
	sp_app_free(bin->app);

	// mapper deinit
	mapper_free(bin->mapper);

	// lfrtm deinit
	lfrtm_free(bin->lfrtm);

	// varchunk deinit
	sem_destroy(&bin->sem);
	varchunk_free(bin->app_to_log);
	varchunk_free(bin->app_to_worker);
	varchunk_free(bin->app_from_worker);
	varchunk_free(bin->app_from_com);
	varchunk_free(bin->app_from_app);

	bin_log_note(bin, "bye\n");

	cross_clock_deinit(&bin->clk_mono);
	cross_clock_deinit(&bin->clk_real);

	xpress_deinit(&bin->xpress);
}

__realtime void
bin_process_pre(bin_t *bin, uint32_t nsamples, bool bypassed)
{
	// read events from worker
	{
		size_t size;
		const void *body;
		unsigned n = 0;
		while((body = varchunk_read_request(bin->app_from_worker, &size))
			&& (n++ < MAX_MSGS) )
		{
			bool advance = sp_app_from_worker(bin->app, size, body);
			if(!advance)
			{
				//fprintf(stderr, "worker is blocked\n");
				break;
			}
			varchunk_read_advance(bin->app_from_worker);
		}
	}

	// run synthpod app pre
	if(!bypassed)
		sp_app_run_pre(bin->app, nsamples);

	// read events from UI ringbuffer
	if(sandbox_master_recv(bin->sb))
	{
		bin_quit(bin);
	}

	// read events from feedback ringbuffer
	{
		size_t size;
		const LV2_Atom *atom;
		unsigned n = 0;
		while((atom = varchunk_read_request(bin->app_from_app, &size))
			&& (n++ < MAX_MSGS) )
		{
			bin->advance_ui = sp_app_from_ui(bin->app, atom);
			if(!bin->advance_ui)
			{
				//fprintf(stderr, "ui is blocked\n");
				break;
			}
			varchunk_read_advance(bin->app_from_app);
		}
	}
	
	// run synthpod app post
	if(!bypassed)
		sp_app_run_post(bin->app, nsamples);
}

__realtime void
bin_process_post(bin_t *bin)
{
	// nothing
}

__non_realtime int
bin_bundle_new(bin_t *bin)
{
	// simply load the default state
	return bin_bundle_load(bin, SYNTHPOD_PREFIX"stereo");
}

__non_realtime int
bin_bundle_load(bin_t *bin, const char *bundle_path)
{
	const LV2_URID urn = bin->map->map(bin->map->handle, bundle_path);
	if(!urn)
	{
		bin_log_error(bin, "%s: invalid path: %s\n", __func__, bundle_path);
		return 1;
	}

	const bool visibility_old = sp_app_visibility_get(bin->app);

	sp_app_bundle_load(bin->app, urn, false);

	const bool visibility_new = sp_app_visibility_get(bin->app);

	if(visibility_old != visibility_new)
	{
		sp_app_visibility_set(bin->app, visibility_old);

		if(visibility_new)
		{
			bin_show(bin);
		}
		else
		{
			bin_hide(bin);
		}
	}

	return 0;
}

__non_realtime void
bin_bundle_reset(bin_t *bin)
{
	sp_app_bundle_reset(bin->app);
}

__non_realtime int
bin_bundle_save(bin_t *bin, const char *bundle_path)
{
	const LV2_URID urn = bin->map->map(bin->map->handle, bundle_path);
	if(!urn)
	{
		bin_log_error(bin, "%s: invalid path: %s\n", __func__, bundle_path);
		return 1;
	}

	sp_app_bundle_save(bin->app, urn, false); //FIXME check
	return 0;
}

__realtime void
bin_quit(bin_t *bin)
{
	atomic_store_explicit(&done, true, memory_order_relaxed);
}

__non_realtime int
bin_log_error(bin_t *bin, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(bin, bin->log_error, fmt, args);
  va_end(args);

	return ret;
}

__non_realtime int
bin_log_note(bin_t *bin, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(bin, bin->log_note, fmt, args);
  va_end(args);

	return ret;
}

__non_realtime int
bin_log_warning(bin_t *bin, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(bin, bin->log_warning, fmt, args);
  va_end(args);

	return ret;
}

__realtime int
bin_log_trace(bin_t *bin, const char *fmt, ...)
{
  va_list args;
	int ret;

  va_start (args, fmt);
	ret = _log_vprintf(bin, bin->log_trace, fmt, args);
  va_end(args);

	return ret;
}
