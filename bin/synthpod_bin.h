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

#ifndef _SYNTHPOD_BIN_H
#define _SYNTHPOD_BIN_H

#include <semaphore.h>
#include <pthread.h>
#include <stdatomic.h>
#include <limits.h>

#include <synthpod_app.h>

#define LFRTM_IMPLEMENTATION
#include <lfrtm/lfrtm.h>

#define MAPPER_IMPLEMENTATION
#include <mapper.lv2/mapper.h>

#define CROSS_CLOCK_IMPLEMENTATION
#include <cross_clock/cross_clock.h>

#include <varchunk.h>
#include <sandbox_master.h>

#include <synthpod_common.h>

#define NSMC_IMPLEMENTATION
#include <nsmc.h>

#include <lv2/lv2plug.in/ns/ext/urid/urid.h>
#include <lv2/lv2plug.in/ns/ext/state/state.h>
#include <lv2/lv2plug.in/ns/ext/time/time.h>

#ifndef MAX
#	define MAX(A, B) ((A) > (B) ? (A) : (B))
#endif

#define SEQ_SIZE 0x2000
#define JAN_1970 (uint64_t)0x83aa7e80

typedef struct _bin_t bin_t;

struct _bin_t {
	atomic_bool inject;
	lfrtm_t *lfrtm;
	mapper_t *mapper;	
	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	xpress_t xpress;
	xpress_map_t xmap;

	sp_app_t *app;
	sp_app_driver_t app_driver;

	sem_t sem;
	varchunk_t *app_to_worker;
	varchunk_t *app_from_worker;
	varchunk_t *app_to_log;

	varchunk_t *app_from_com;

	bool advance_ui;
	varchunk_t *app_from_app;

	char *path;
	nsmc_t *nsm;

	LV2_URID log_error;
	LV2_URID log_note;
	LV2_URID log_trace;
	LV2_URID log_warning;
	LV2_URID atom_eventTransfer;

	LV2_Log_Log log;

	bool last_rolling;
	atomic_bool gui_done;

	pthread_t gui_thread;
	pthread_t worker_thread;
	pthread_t dsp_thread;
	atomic_flag trace_lock;

	bool d2tk_gui;
	bool has_gui;
	bool kill_gui;
	bool threaded_gui;
	int audio_prio;
	int worker_prio;
	int num_slaves;
	bool bad_plugins;
	char socket_path [NAME_MAX];
	int update_rate;
	bool cpu_affinity;

	sandbox_master_driver_t sb_driver;
	sandbox_master_t *sb;

	pid_t child;

	bool first;

	cross_clock_t clk_mono;
	cross_clock_t clk_real;

	char **argv;
	int optind;

	uint32_t sample_rate;
};

void
bin_init(bin_t *bin, uint32_t sample_rate);

void
bin_run(bin_t *bin, const char *name, char **argv, nsmc_callback_t callback);

void
bin_stop(bin_t *bin);

void
bin_deinit(bin_t *bin);

void
bin_process_pre(bin_t *bin, uint32_t nsamples, bool bypassed);

void
bin_process_post(bin_t *bin);

int
bin_bundle_new(bin_t *bin);

void
bin_bundle_reset(bin_t *bin);

int
bin_bundle_load(bin_t *bin, const char *bundle_path);

int
bin_bundle_save(bin_t *bin, const char *bundle_path);

void
bin_quit(bin_t *bin);

int __attribute__((format(printf, 2, 3)))
bin_log_error(bin_t *bin, const char *fmt, ...);

int __attribute__((format(printf, 2, 3)))
bin_log_note(bin_t *bin, const char *fmt, ...);

int __attribute__((format(printf, 2, 3)))
bin_log_warning(bin_t *bin, const char *fmt, ...);

int __attribute__((format(printf, 2, 3)))
bin_log_trace(bin_t *bin, const char *fmt, ...);

int
bin_show(bin_t *bin);

int
bin_hide(bin_t *bin);

bool
bin_visibility(bin_t *bin);

#endif // _SYNTHPOD_BIN_H
