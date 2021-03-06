/*
 * Copyright (c) 2018 Hanspeter Portner (dev@open-music-kontrollers.ch)
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
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <semaphore.h>

#define LFRTM_IMPLEMENTATION
#include <lfrtm/lfrtm.h>

#define ITEM_SIZE 32
#define NUM_POOLS 512
#define POOL_SIZE 0x100000 // 1M
	
sem_t sem;
atomic_uint done = ATOMIC_VAR_INIT(0);
atomic_bool flag = ATOMIC_VAR_INIT(false);

static const uint8_t empty [ITEM_SIZE] = { 0x0 };

static void *
_thread(void *data)
{
	lfrtm_t *lfrtm = data;

	for(unsigned i = 0; i < 0x10000; i++)
	{
		const size_t size = i % ITEM_SIZE + 1;
		bool more = false;
		assert(lfrtm_alloc(lfrtm, 0, NULL) == NULL);
		assert(lfrtm_alloc(NULL, 0, &more) == NULL);
		void *mem = lfrtm_alloc(lfrtm, size, &more);
		assert(mem);

		assert(memcmp(mem, empty, size) == 0);
		memset(mem, 0xff, size);

		if(more)
		{
			atomic_store(&flag, true);
			sem_post(&sem);
		}
	}

	atomic_fetch_add(&done, 1);
	sem_post(&sem);

	return NULL;
}

int
main(int argc, char **argv)
{
	pthread_t threads [32];
	assert(argc >= 2);

	const unsigned num_threads = atoi(argv[1]);
	const bool is_preloaded = (argc == 3) ? atoi(argv[2]) : false;
	assert( (num_threads > 0) && (num_threads <= 32) );

	if(is_preloaded)
	{
		// will fail on ldpreloaded calloc
		assert(lfrtm_new(NUM_POOLS, 0) == NULL);
	}

	lfrtm_t *lfrtm = lfrtm_new(NUM_POOLS, POOL_SIZE);
	assert(lfrtm);

	assert(sem_init(&sem, 0, 1) == 0);

	for(unsigned i = 0; i < num_threads; i++)
	{
		assert(pthread_create(&threads[i], NULL, _thread, lfrtm) == 0);
	}

	while(atomic_load(&done) != num_threads)
	{
		sem_wait(&sem);

		if(atomic_exchange(&flag, false))
		{
			assert(lfrtm_inject(lfrtm) == 0);
		}
	}

	// will trigger double injection
	assert(lfrtm_inject(NULL) != 0);
	assert(lfrtm_inject(lfrtm) != 0);

	for(unsigned i = 0; i < num_threads; i++)
	{
		assert(pthread_join(threads[i], NULL) == 0);
	}

	assert(sem_destroy(&sem) == 0);

	assert(lfrtm_free(lfrtm) == 0);
	assert(lfrtm_free(NULL) != 0);

	return 0;
}
