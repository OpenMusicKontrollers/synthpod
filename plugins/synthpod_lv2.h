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

#ifndef _SYNTHPOD_LV2_H
#define _SYNTHPOD_LV2_H

#ifdef _WIN32
#	define mlock(...)
#	define munlock(...)
#else
#	include <sys/mman.h> // mlock
#endif

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"

#include <xpress.lv2/xpress.h>

#include <synthpod_common.h>

#define __realtime __attribute__((annotate("realtime")))
#define __non_realtime __attribute__((annotate("non-realtime")))

#define SEQ_SIZE 0x2000
#define _ATOM_ALIGNED __attribute__((aligned(8)))

// bundle uri
#define SYNTHPOD_EVENT_URI				SYNTHPOD_PREFIX"event"

// plugin uris
#define SYNTHPOD_KEYBOARD_URI					SYNTHPOD_PREFIX"keyboard"
#define SYNTHPOD_CV2CONTROL_URI				SYNTHPOD_PREFIX"cv2control"
#define SYNTHPOD_CONTROL2CV_URI				SYNTHPOD_PREFIX"control2cv"
#define SYNTHPOD_MIDISPLITTER_URI			SYNTHPOD_PREFIX"midisplitter"
#define SYNTHPOD_HEAVYLOAD_URI				SYNTHPOD_PREFIX"heavyload"
#define SYNTHPOD_PANIC_URI						SYNTHPOD_PREFIX"panic"
#define SYNTHPOD_PLACEHOLDER_URI			SYNTHPOD_PREFIX"placeholder"

extern const LV2_Descriptor synthpod_stereo;
extern const LV2_Descriptor synthpod_keyboard;
extern const LV2_Descriptor synthpod_cv2control;
extern const LV2_Descriptor synthpod_control2cv;
extern const LV2_Descriptor synthpod_midisplitter;
extern const LV2_Descriptor synthpod_heavyload;
extern const LV2_Descriptor synthpod_panic;
extern const LV2_Descriptor synthpod_placeholder;

extern const LV2UI_Descriptor synthpod_common_4_nk;
extern const LV2UI_Descriptor synthpod_root_4_nk;

extern const LV2UI_Descriptor synthpod_common_5_d2tk;
extern const LV2UI_Descriptor synthpod_root_5_d2tk;

// keyboard UI uris
#define SYNTHPOD_KEYBOARD_NK_URI	SYNTHPOD_PREFIX"keyboard_4_nk"

extern const LV2UI_Descriptor synthpod_keyboard_4_nk;

#endif // _SYNTHPOD_LV2_H
