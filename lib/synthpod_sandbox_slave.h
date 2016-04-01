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

#ifndef _SYNTHPOD_SANDBOX_SLAVE_H
#define _SYNTHPOD_SANDBOX_SLAVE_H

#ifdef _WIN32
#	define SYNTHPOD_SYMBOL_EXTERN __declspec(dllexport)
#else
#	define SYNTHPOD_SYMBOL_EXTERN __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _sandbox_slave_t sandbox_slave_t;
typedef struct _sandbox_slave_driver_t sandbox_slave_driver_t;
	
typedef int (*sandbox_slave_driver_init_t)(sandbox_slave_t *sb, void *data);
typedef void (*sandbox_slave_driver_run_t)(sandbox_slave_t *sb, void *data);
typedef void (*sandbox_slave_driver_deinit_t)(void *data);
typedef int (*sandbox_slave_driver_resize_t)(void *data, int width, int height);

struct _sandbox_slave_driver_t {
	sandbox_slave_driver_init_t init_cb;
	sandbox_slave_driver_run_t run_cb;
	sandbox_slave_driver_deinit_t deinit_cb;
	sandbox_slave_driver_resize_t resize_cb;
};

SYNTHPOD_SYMBOL_EXTERN sandbox_slave_t *
sandbox_slave_new(int argc, char **argv, const sandbox_slave_driver_t *driver, void *data);

SYNTHPOD_SYMBOL_EXTERN void
sandbox_slave_free(sandbox_slave_t *sb);

SYNTHPOD_SYMBOL_EXTERN int
sandbox_slave_instantiate(sandbox_slave_t *sb, void *parent, void *widget);

SYNTHPOD_SYMBOL_EXTERN void
sandbox_slave_recv(sandbox_slave_t *sb);

SYNTHPOD_SYMBOL_EXTERN bool
sandbox_slave_flush(sandbox_slave_t *sb);

SYNTHPOD_SYMBOL_EXTERN int
sandbox_slave_idle(sandbox_slave_t *sb);

SYNTHPOD_SYMBOL_EXTERN void
sandbox_slave_run(sandbox_slave_t *sb);

SYNTHPOD_SYMBOL_EXTERN void
sandbox_slave_fd_get(sandbox_slave_t *sb, int *fd);

SYNTHPOD_SYMBOL_EXTERN const char *
sandbox_slave_title_get(sandbox_slave_t *sb);

#ifdef __cplusplus
}
#endif

#endif