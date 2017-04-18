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

#include <synthpod_lv2.h>
#include <synthpod_patcher.h>

#include "lv2/lv2plug.in/ns/ext/urid/urid.h"
#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/midi/midi.h"
#include "lv2/lv2plug.in/ns/ext/port-groups/port-groups.h"
#include "lv2/lv2plug.in/ns/ext/presets/presets.h"
#include "lv2/lv2plug.in/ns/ext/patch/patch.h"

#include <osc.lv2/osc.h>
#include <xpress.lv2/xpress.h>

#include <math.h>

#define NK_PUGL_API
#include <nk_pugl/nk_pugl.h>

#include <lilv/lilv.h>

#define SEARCH_BUF_MAX 128
#define ATOM_BUF_MAX 1024
#define CONTROL 14 //FIXME

#ifdef Bool
#	undef Bool // interferes with atom forge
#endif

typedef enum _property_type_t property_type_t;
typedef enum _selector_grid_t selector_grid_t;
typedef enum _selector_search_t selector_search_t;

typedef union _param_union_t param_union_t;

typedef struct _hash_t hash_t;
typedef struct _scale_point_t scale_point_t;
typedef struct _control_port_t control_port_t;
typedef struct _audio_port_t audio_port_t;
typedef struct _port_t port_t;
typedef struct _param_t param_t;
typedef struct _prop_t prop_t;
typedef struct _mod_conn_t mod_conn_t;
typedef struct _port_conn_t port_conn_t;
typedef struct _mod_t mod_t;
typedef struct _plughandle_t plughandle_t;

enum _property_type_t {
	PROPERTY_TYPE_NONE				= 0,

	PROPERTY_TYPE_CONTROL			= (1 << 0),
	PROPERTY_TYPE_PARAM				= (1 << 1),

	PROPERTY_TYPE_AUDIO				= (1 << 2),
	PROPERTY_TYPE_CV					= (1 << 3),
	PROPERTY_TYPE_ATOM				= (1 << 4),

	PROPERTY_TYPE_MIDI				= (1 << 8),
	PROPERTY_TYPE_OSC					= (1 << 9),
	PROPERTY_TYPE_TIME				= (1 << 10),
	PROPERTY_TYPE_PATCH				= (1 << 11),
	PROPERTY_TYPE_XPRESS			= (1 << 12),
	PROPERTY_TYPE_AUTOMATION	= (1 << 13),

	PROPERTY_TYPE_MAX
};

enum _selector_grid_t {
	SELECTOR_GRID_PLUGINS = 0,
	SELECTOR_GRID_PRESETS,

	SELECTOR_GRID_MAX
};

enum _selector_search_t {
	SELECTOR_SEARCH_NAME = 0,
	SELECTOR_SEARCH_COMMENT,
	SELECTOR_SEARCH_AUTHOR,
	SELECTOR_SEARCH_CLASS,
	SELECTOR_SEARCH_PROJECT,

	SELECTOR_SEARCH_MAX
};

struct _hash_t {
	void **nodes;
	unsigned size;
};

union _param_union_t {
 int32_t b;
 int32_t i;
 int64_t h;
 float f;
 double d;
};

struct _scale_point_t {
	char *label;
	param_union_t val;
};

struct _control_port_t {
	hash_t points;
	scale_point_t *points_ref;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
	bool is_int;
	bool is_bool;
	bool is_readonly;
};

struct _audio_port_t {
	float peak;
	float gain;
};

struct _port_t {
	property_type_t type;
	const LilvPort *port;
	LilvNodes *groups;

	union {
		control_port_t control;
		audio_port_t audio;
	};
};

struct _param_t {
	const LilvNode *param;
	bool is_readonly;
	LV2_URID range;
	param_union_t min;
	param_union_t max;
	param_union_t span;
	param_union_t val;
};

struct _prop_t {
	union {
		port_t port;
		param_t param;
	};
};

struct _mod_t {
	LV2_URID urn;
	const LilvPlugin *plug;

	hash_t ports;
	hash_t groups;
	hash_t banks;
	hash_t params;

	LilvNodes *readables;
	LilvNodes *writables;
	LilvNodes *presets;

	struct nk_vec2 pos;
	struct nk_vec2 dim;
	bool moving;
	bool hovered;
	bool hilighted;

	hash_t sources;
	hash_t sinks;

	property_type_t source_type;
	property_type_t sink_type;
};

struct _port_conn_t {
	port_t *source_port;
	port_t *sink_port;
};

struct _mod_conn_t {
	mod_t *source_mod;
	mod_t *sink_mod;
	property_type_t type;
	hash_t conns;

	struct nk_vec2 pos;
	bool moving;
};

struct _plughandle_t {
	LilvWorld *world;

	LV2_Atom_Forge forge;

	LV2_URID atom_eventTransfer;

	LV2_URID_Map *map;
	LV2_URID_Unmap *unmap;
	LV2UI_Write_Function writer;
	LV2UI_Controller controller;

	nk_pugl_window_t win;

	selector_grid_t grid_selector;
	const LilvPlugin *plugin_selector;
	mod_t *module_selector;
	const LilvNode *preset_selector;

	hash_t mods;
	hash_t conns;

	struct {
		LilvNode *pg_group;
		LilvNode *lv2_integer;
		LilvNode *lv2_toggled;
		LilvNode *lv2_minimum;
		LilvNode *lv2_maximum;
		LilvNode *lv2_default;
		LilvNode *pset_Preset;
		LilvNode *pset_bank;
		LilvNode *rdfs_comment;
		LilvNode *rdfs_range;
		LilvNode *doap_name;
		LilvNode *lv2_minorVersion;
		LilvNode *lv2_microVersion;
		LilvNode *doap_license;
		LilvNode *rdfs_label;
		LilvNode *lv2_name;
		LilvNode *lv2_OutputPort;
		LilvNode *lv2_AudioPort;
		LilvNode *lv2_CVPort;
		LilvNode *lv2_ControlPort;
		LilvNode *atom_AtomPort;
		LilvNode *patch_readable;
		LilvNode *patch_writable;
		LilvNode *rdf_type;
		LilvNode *lv2_Plugin;
		LilvNode *midi_MidiEvent;
		LilvNode *osc_Message;
		LilvNode *time_Position;
		LilvNode *patch_Message;
		LilvNode *xpress_Message;
	} node;

	float dy;

	enum nk_collapse_states plugin_collapse_states;
	enum nk_collapse_states preset_import_collapse_states;
	enum nk_collapse_states preset_export_collapse_states;
	enum nk_collapse_states plugin_info_collapse_states;
	enum nk_collapse_states preset_info_collapse_states;

	selector_search_t plugin_search_selector;
	selector_search_t preset_search_selector;
	selector_search_t port_search_selector;

	hash_t plugin_matches;
	hash_t preset_matches;
	hash_t port_matches;

	char plugin_search_buf [SEARCH_BUF_MAX];
	char preset_search_buf [SEARCH_BUF_MAX];
	char port_search_buf [SEARCH_BUF_MAX];

	struct nk_text_edit plugin_search_edit;
	struct nk_text_edit preset_search_edit;
	struct nk_text_edit port_search_edit;

	bool first;

	reg_t regs;
	union {
		LV2_Atom atom;
		uint8_t buf [ATOM_BUF_MAX];
	};

	bool has_control_a;

	struct nk_vec2 scrolling;
	struct nk_vec2 nxt;
	float scale;

	bool plugin_find_matches;
	bool preset_find_matches;
	bool port_find_matches;

	struct {
		bool active;
		mod_t *source_mod;
	} linking;

	property_type_t type;
};

static const char *grid_labels [SELECTOR_GRID_MAX] = {
	[SELECTOR_GRID_PLUGINS] = "Plugins",
	[SELECTOR_GRID_PRESETS] = "Presets"
};

static const char *grid_tooltips [SELECTOR_GRID_MAX] = {
	[SELECTOR_GRID_PLUGINS] = "Ctrl-P",
	[SELECTOR_GRID_PRESETS] = "Ctrl-R"
};

static const char *search_labels [SELECTOR_SEARCH_MAX] = {
	[SELECTOR_SEARCH_NAME] = "Name",
	[SELECTOR_SEARCH_COMMENT] = "Comment",
	[SELECTOR_SEARCH_AUTHOR] = "Author",
	[SELECTOR_SEARCH_CLASS] = "Class",
	[SELECTOR_SEARCH_PROJECT] = "Project"
};

static size_t
_textedit_len(struct nk_text_edit *edit)
{
	return nk_str_len(&edit->string);
}

static const char *
_textedit_const(struct nk_text_edit *edit)
{
	return nk_str_get_const(&edit->string);
}

static void
_textedit_zero_terminate(struct nk_text_edit *edit)
{
	char *str = nk_str_get(&edit->string);
	if(str)
		str[nk_str_len(&edit->string)] = '\0';
}

#define HASH_FOREACH(hash, itr) \
	for(void **(itr) = (hash)->nodes; (itr) - (hash)->nodes < (hash)->size; (itr)++)

#define HASH_FREE(hash, ptr) \
	for(void *(ptr) = _hash_pop((hash)); (ptr); (ptr) = _hash_pop((hash)))

static bool
_hash_empty(hash_t *hash)
{
	return hash->size == 0;
}

static size_t
_hash_size(hash_t *hash)
{
	return hash->size;
}

static void
_hash_add(hash_t *hash, void *node)
{
	hash->nodes = realloc(hash->nodes, (hash->size + 1)*sizeof(void *));
	if(hash->nodes)
	{
		hash->nodes[hash->size] = node;
		hash->size++;
	}
}

static void
_hash_remove(hash_t *hash, void *node)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(node_ptr != node)
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_remove_cb(hash_t *hash, bool (*cb)(void *node, void *data), void *data)
{
	void **nodes = NULL;
	size_t size = 0;

	HASH_FOREACH(hash, node_itr)
	{
		void *node_ptr = *node_itr;

		if(cb(node_ptr, data))
		{
			nodes = realloc(nodes, (size + 1)*sizeof(void *));
			if(nodes)
			{
				nodes[size] = node_ptr;
				size++;
			}
		}
	}

	free(hash->nodes);
	hash->nodes = nodes;
	hash->size = size;
}

static void
_hash_free(hash_t *hash)
{
	free(hash->nodes);
	hash->nodes = NULL;
	hash->size = 0;
}

static void *
_hash_pop(hash_t *hash)
{
	if(hash->size)
	{
		void *node = hash->nodes[--hash->size];

		if(!hash->size)
			_hash_free(hash);

		return node;
	}

	return NULL;
}

static void
_hash_sort(hash_t *hash, int (*cmp)(const void *a, const void *b))
{
	if(hash->size)
		qsort(hash->nodes, hash->size, sizeof(void *), cmp);
}

static void
_hash_sort_r(hash_t *hash, int (*cmp)(const void *a, const void *b, void *data),
	void *data)
{
	if(hash->size)
		qsort_r(hash->nodes, hash->size, sizeof(void *), cmp, data);
}

static int
_node_as_int(const LilvNode *node, int dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return floorf(lilv_node_as_float(node));
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static float
_node_as_float(const LilvNode *node, float dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node);
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node);
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1.f : 0.f;
	else
		return dflt;
}

static int32_t
_node_as_bool(const LilvNode *node, int32_t dflt)
{
	if(lilv_node_is_int(node))
		return lilv_node_as_int(node) != 0;
	else if(lilv_node_is_float(node))
		return lilv_node_as_float(node) != 0.f;
	else if(lilv_node_is_bool(node))
		return lilv_node_as_bool(node) ? 1 : 0;
	else
		return dflt;
}

static void
_port_conn_free(plughandle_t *handle, port_conn_t *port_conn)
{
	free(port_conn);
}

static mod_conn_t *
_mod_conn_find(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod)
{
	HASH_FOREACH(&handle->conns, mod_conn_itr)
	{
		mod_conn_t *mod_conn = *mod_conn_itr;

		if( (mod_conn->source_mod == source_mod) && (mod_conn->sink_mod == sink_mod) )
			return mod_conn;
	}

	return NULL;
}

static mod_conn_t *
_mod_conn_add(plughandle_t *handle, mod_t *source_mod, mod_t *sink_mod)
{
	mod_conn_t *mod_conn = calloc(1, sizeof(mod_conn_t));
	if(mod_conn)
	{
		mod_conn->source_mod = source_mod;
		mod_conn->sink_mod = sink_mod;
		mod_conn->pos = nk_vec2(
			(source_mod->pos.x + sink_mod->pos.x)/2,
			(source_mod->pos.y + sink_mod->pos.y)/2);
		mod_conn->type = PROPERTY_TYPE_NONE;
		_hash_add(&handle->conns, mod_conn);
	}

	return mod_conn;
}

static void
_mod_conn_free(plughandle_t *handle, mod_conn_t *mod_conn)
{
	HASH_FREE(&mod_conn->conns, port_conn_ptr)
	{
		port_conn_t *port_conn = port_conn_ptr;

		_port_conn_free(handle, port_conn);
	}

	free(mod_conn);
}

static void
_register_parameter(plughandle_t *handle, mod_t *mod, const LilvNode *parameter, bool is_readonly)
{
	param_t *param = calloc(1, sizeof(param_t));
	if(!param)
		return;

	_hash_add(&mod->params, param);

	param->param = parameter;
	param->is_readonly = is_readonly;

	param->range = 0;
	LilvNode *range = lilv_world_get(handle->world, parameter, handle->node.rdfs_range, NULL);
	if(range)
	{
		param->range = handle->map->map(handle->map->handle, lilv_node_as_uri(range));
		lilv_node_free(range);
	}

	if(!param->range)
		return;

	LilvNode *min = lilv_world_get(handle->world, parameter, handle->node.lv2_minimum, NULL);
	if(min)
	{
		if(param->range == handle->forge.Int)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, false);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 0.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 0.f);
		//FIXME
		lilv_node_free(min);
	}

	LilvNode *max = lilv_world_get(handle->world, parameter, handle->node.lv2_maximum, NULL);
	if(max)
	{
		if(param->range == handle->forge.Int)
			param->max.i = _node_as_int(max, 1);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 1);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, true);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 1.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 1.f);
		//FIXME
		lilv_node_free(max);
	}

	LilvNode *val = lilv_world_get(handle->world, parameter, handle->node.lv2_default, NULL);
	if(val)
	{
		if(param->range == handle->forge.Int)
			param->val.i = _node_as_int(val, 0);
		else if(param->range == handle->forge.Long)
			param->min.i = _node_as_int(min, 0);
		else if(param->range == handle->forge.Bool)
			param->min.i = _node_as_bool(min, false);
		else if(param->range == handle->forge.Float)
			param->min.i = _node_as_float(min, 0.f);
		else if(param->range == handle->forge.Double)
			param->min.i = _node_as_float(min, 0.f);
		//FIXME
		lilv_node_free(val);
	}

	if(param->range == handle->forge.Int)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Long)
		param->span.h = param->max.h - param->min.h;
	else if(param->range == handle->forge.Bool)
		param->span.i = param->max.i - param->min.i;
	else if(param->range == handle->forge.Float)
		param->span.f = param->max.f - param->min.f;
	else if(param->range == handle->forge.Double)
		param->span.d = param->max.d - param->min.d;
	//FIXME
}

static inline float
dBFSp6(float peak)
{
	const float d = 6.f + 20.f * log10f(fabsf(peak) / 2.f);
	const float e = (d + 64.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}

#if 0
static inline float
dBFS(float peak)
{
	const float d = 20.f * log10f(fabsf(peak));
	const float e = (d + 70.f) / 70.f;
	return NK_CLAMP(0.f, e, 1.f);
}
#endif

static int
_sort_rdfs_label(const void *a, const void *b, void *data)
{
	plughandle_t *handle = data;

	const LilvNode *group_a = *(const LilvNode **)a;
	const LilvNode *group_b = *(const LilvNode **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_world_get(handle->world, group_a, handle->node.rdfs_label, NULL);
	if(!node_a)
		node_a = lilv_world_get(handle->world, group_a, handle->node.lv2_name, NULL);

	LilvNode *node_b = lilv_world_get(handle->world, group_b, handle->node.rdfs_label, NULL);
	if(!node_b)
		node_b = lilv_world_get(handle->world, group_b, handle->node.lv2_name, NULL);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static int
_sort_port_name(const void *a, const void *b, void *data)
{
	mod_t *mod = data;

	const port_t *port_a = *(const port_t **)a;
	const port_t *port_b = *(const port_t **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_port_get_name(mod->plug, port_a->port);
	LilvNode *node_b = lilv_port_get_name(mod->plug, port_b->port);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static int
_sort_scale_point_name(const void *a, const void *b)
{
	const scale_point_t *scale_point_a = *(const scale_point_t **)a;
	const scale_point_t *scale_point_b = *(const scale_point_t **)b;

	const char *name_a = scale_point_a->label;
	const char *name_b = scale_point_b->label;

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	return ret;
}

static int
_sort_param_name(const void *a, const void *b, void *data)
{
	plughandle_t *handle = data;

	const param_t *param_a = *(const param_t **)a;
	const param_t *param_b = *(const param_t **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_world_get(handle->world, param_a->param, handle->node.rdfs_label, NULL);
	if(!node_a)
		node_a = lilv_world_get(handle->world, param_a->param, handle->node.lv2_name, NULL);

	LilvNode *node_b = lilv_world_get(handle->world, param_b->param, handle->node.rdfs_label, NULL);
	if(!node_b)
		node_b = lilv_world_get(handle->world, param_b->param, handle->node.lv2_name, NULL);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_mod_insert(plughandle_t *handle, const LilvPlugin *plug)
{
	const LilvNode *node= lilv_plugin_get_uri(plug);
	const char *uri = lilv_node_as_string(node);
	const LV2_URID urid = handle->map->map(handle->map->handle, uri);

	lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
	if(synthpod_patcher_insert(&handle->regs, &handle->forge,
		handle->regs.synthpod.module_list.urid, 0,
		sizeof(uint32_t), handle->forge.URID, &urid))
	{
		handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
		handle->regs.port.event_transfer.urid, &handle->atom);
	}
}

static mod_t *
_mod_find_by_subject(plughandle_t *handle, LV2_URID subj)
{
	HASH_FOREACH(&handle->mods, itr)
	{
		mod_t *mod = *itr;

		if(mod->urn == subj)
			return mod;
	}

	return NULL;
}

static void
_mod_add(plughandle_t *handle, LV2_URID urn)
{
	mod_t *mod = calloc(1, sizeof(mod_t));
	if(!mod)
		return;

	mod->urn = urn;
	mod->pos = nk_vec2(handle->nxt.x, handle->nxt.y);
	_hash_add(&handle->mods, mod);

	handle->nxt.x += 50.f * handle->scale;
	handle->nxt.y += 50.f * handle->scale;
}

static void
_mod_init(plughandle_t *handle, mod_t *mod, const LilvPlugin *plug)
{
	mod->plug = plug;
	const unsigned num_ports = lilv_plugin_get_num_ports(plug);

	for(unsigned p=0; p<num_ports; p++)
	{
		port_t *port = calloc(1, sizeof(port_t));
		if(!port)
			continue;

		_hash_add(&mod->ports, port);

		port->port = lilv_plugin_get_port_by_index(plug, p);
		port->groups = lilv_port_get_value(plug, port->port, handle->node.pg_group);

		LILV_FOREACH(nodes, i, port->groups)
		{
			const LilvNode *port_group = lilv_nodes_get(port->groups, i);

			bool match = false;
			HASH_FOREACH(&mod->groups, itr)
			{
				const LilvNode *mod_group = *itr;

				if(lilv_node_equals(mod_group, port_group))
				{
					match = true;
					break;
				}
			}

			if(!match)
				_hash_add(&mod->groups, (void *)port_group);
		}

		const bool is_audio = lilv_port_is_a(plug, port->port, handle->node.lv2_AudioPort);
		const bool is_cv = lilv_port_is_a(plug, port->port, handle->node.lv2_CVPort);
		const bool is_control = lilv_port_is_a(plug, port->port, handle->node.lv2_ControlPort);
		const bool is_atom = lilv_port_is_a(plug, port->port, handle->node.atom_AtomPort);
		const bool is_output = lilv_port_is_a(plug, port->port, handle->node.lv2_OutputPort);

		if(is_audio)
		{
			port->type = PROPERTY_TYPE_AUDIO;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_cv)
		{
			port->type = PROPERTY_TYPE_CV;
			audio_port_t *audio = &port->audio;

			audio->peak = dBFSp6(2.f * rand() / RAND_MAX);
			audio->gain = (float)rand() / RAND_MAX;
			//TODO
		}
		else if(is_control)
		{
			port->type = PROPERTY_TYPE_CONTROL;
			control_port_t *control = &port->control;

			control->is_readonly = is_output;
			control->is_int = lilv_port_has_property(plug, port->port, handle->node.lv2_integer);
			control->is_bool = lilv_port_has_property(plug, port->port, handle->node.lv2_toggled);

			LilvNode *val = NULL;
			LilvNode *min = NULL;
			LilvNode *max = NULL;
			lilv_port_get_range(plug, port->port, &val, &min, &max);

			if(val)
			{
				if(control->is_int)
					control->val.i = _node_as_int(val, 0);
				else if(control->is_bool)
					control->val.i = _node_as_bool(val, false);
				else
					control->val.f = _node_as_float(val, 0.f);

				lilv_node_free(val);
			}
			if(min)
			{
				if(control->is_int)
					control->min.i = _node_as_int(min, 0);
				else if(control->is_bool)
					control->min.i = _node_as_bool(min, false);
				else
					control->min.f = _node_as_float(min, 0.f);

				lilv_node_free(min);
			}
			if(max)
			{
				if(control->is_int)
					control->max.i = _node_as_int(max, 1);
				else if(control->is_bool)
					control->max.i = _node_as_bool(max, true);
				else
					control->max.f = _node_as_float(max, 1.f);

				lilv_node_free(max);
			}

			if(control->is_int)
				control->span.i = control->max.i - control->min.i;
			else if(control->is_bool)
				control->span.i = control->max.i - control->min.i;
			else
				control->span.f = control->max.f - control->min.f;

			if(control->is_int && (control->min.i == 0) && (control->max.i == 1) )
			{
				control->is_int = false;
				control->is_bool = true;
			}

			LilvScalePoints *port_points = lilv_port_get_scale_points(plug, port->port);
			if(port_points)
			{
				LILV_FOREACH(scale_points, i, port_points)
				{
					const LilvScalePoint *port_point = lilv_scale_points_get(port_points, i);
					const LilvNode *label_node = lilv_scale_point_get_label(port_point);
					const LilvNode *value_node = lilv_scale_point_get_value(port_point);

					if(label_node && value_node)
					{
						scale_point_t *point = calloc(1, sizeof(scale_point_t));
						if(!point)
							continue;

						_hash_add(&port->control.points, point);

						point->label = strdup(lilv_node_as_string(label_node));

						if(control->is_int)
						{
							point->val.i = _node_as_int(value_node, 0);
						}
						else if(control->is_bool)
						{
							point->val.i = _node_as_bool(value_node, false);
						}
						else // is_float
						{
							point->val.f = _node_as_float(value_node, 0.f);
						}
					}
				}

				int32_t diff1 = INT32_MAX;
				HASH_FOREACH(&port->control.points, itr)
				{
					scale_point_t *point = *itr;

					const int32_t diff2 = abs(point->val.i - control->val.i); //FIXME

					if(diff2 < diff1)
					{
						control->points_ref = point;
						diff1 = diff2;
					}
				}

				_hash_sort(&port->control.points, _sort_scale_point_name);

				lilv_scale_points_free(port_points);
			}
		}
		else if(is_atom)
		{
			port->type = PROPERTY_TYPE_ATOM;

			if(lilv_port_supports_event(plug, port->port, handle->node.midi_MidiEvent))
				port->type |= PROPERTY_TYPE_MIDI;
			if(lilv_port_supports_event(plug, port->port, handle->node.osc_Message))
				port->type |= PROPERTY_TYPE_OSC;
			if(lilv_port_supports_event(plug, port->port, handle->node.time_Position))
				port->type |= PROPERTY_TYPE_TIME;
			if(lilv_port_supports_event(plug, port->port, handle->node.patch_Message))
				port->type |= PROPERTY_TYPE_PATCH;
			if(lilv_port_supports_event(plug, port->port, handle->node.xpress_Message))
				port->type |= PROPERTY_TYPE_XPRESS;

			//TODO
		}

		if(is_audio || is_cv || is_atom)
		{
			if(is_output)
				_hash_add(&mod->sources, port);
			else
				_hash_add(&mod->sinks, port);
		}

		if(is_output)
			mod->source_type |= port->type;
		else
			mod->sink_type |= port->type;
	}

	_hash_sort_r(&mod->ports, _sort_port_name, mod);
	_hash_sort_r(&mod->groups, _sort_rdfs_label, handle);

	mod->presets = lilv_plugin_get_related(plug, handle->node.pset_Preset);
	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_load_resource(handle->world, preset);
		}

		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);

			LilvNodes *banks = lilv_world_find_nodes(handle->world, preset, handle->node.pset_bank, NULL);
			if(banks)
			{
				const LilvNode *bank = lilv_nodes_size(banks)
					? lilv_nodes_get_first(banks) : NULL;

				if(bank)
				{
					bool match = false;
					HASH_FOREACH(&mod->banks, itr)
					{
						const LilvNode *mod_bank = *itr;

						if(lilv_node_equals(mod_bank, bank))
						{
							match = true;
							break;
						}
					}

					if(!match)
					{
						_hash_add(&mod->banks, lilv_node_duplicate(bank));
					}
				}
				lilv_nodes_free(banks);
			}
		}

		_hash_sort_r(&mod->banks, _sort_rdfs_label, handle);
	}

	mod->readables = lilv_plugin_get_value(plug, handle->node.patch_readable);
	mod->writables = lilv_plugin_get_value(plug, handle->node.patch_writable);

	LILV_FOREACH(nodes, i, mod->readables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, mod, parameter, true);
	}
	LILV_FOREACH(nodes, i, mod->writables)
	{
		const LilvNode *parameter = lilv_nodes_get(mod->readables, i);

		_register_parameter(handle, mod, parameter, false);
	}

	_hash_sort_r(&mod->params, _sort_param_name, handle);

	nk_pugl_post_redisplay(&handle->win); //FIXME
}

static void
_mod_free(plughandle_t *handle, mod_t *mod)
{
	HASH_FREE(&mod->ports, ptr)
	{
		port_t *port = ptr;

		if(port->groups)
			lilv_nodes_free(port->groups);

		if(port->type == PROPERTY_TYPE_CONTROL)
		{
			HASH_FREE(&port->control.points, ptr2)
			{
				scale_point_t *point = ptr2;

				if(point->label)
					free(point->label);

				free(point);
			}
		}

		free(port);
	}
	_hash_free(&mod->sources);
	_hash_free(&mod->sinks);

	HASH_FREE(&mod->banks, ptr)
	{
		LilvNode *node = ptr;
		lilv_node_free(node);
	}

	_hash_free(&mod->groups);

	HASH_FREE(&mod->params, ptr)
	{
		param_t *param = ptr;
		free(param);
	}

	if(mod->presets)
	{
		LILV_FOREACH(nodes, i, mod->presets)
		{
			const LilvNode *preset = lilv_nodes_get(mod->presets, i);
			lilv_world_unload_resource(handle->world, preset);
		}

		lilv_nodes_free(mod->presets);
	}

	if(mod->readables)
		lilv_nodes_free(mod->readables);

	if(mod->writables)
		lilv_nodes_free(mod->writables);
}

static void
_load(plughandle_t *handle)
{
	switch(handle->grid_selector)
	{
		case SELECTOR_GRID_PLUGINS:
		{
			_mod_insert(handle, handle->plugin_selector);
		} break;
		case SELECTOR_GRID_PRESETS:
		{
			//TODO
		} break;

		default: break;
	}
}

static bool
_tooltip_visible(struct nk_context *ctx)
{
	return nk_widget_has_mouse_click_down(ctx, NK_BUTTON_RIGHT, nk_true)
		|| (nk_widget_is_hovered(ctx) && nk_input_is_key_down(&ctx->input, NK_KEY_CTRL));
}

static void
_expose_main_header(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	nk_menubar_begin(ctx);
	{
		nk_layout_row_dynamic(ctx, dy, 5);
		{
			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-N");
			nk_button_label(ctx, "New");

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-O");
			nk_button_label(ctx, "Open");

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-S");
			nk_button_label(ctx, "Save");

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-Shift-S");
			nk_button_label(ctx, "Save As");

			if(_tooltip_visible(ctx))
				nk_tooltip(ctx, "Ctrl-Q");
			nk_button_label(ctx, "Quit");
		}
		nk_menubar_end(ctx);
	}
}

static int
_sort_plugin_name(const void *a, const void *b)
{
	const LilvPlugin *plug_a = *(const LilvPlugin **)a;
	const LilvPlugin *plug_b = *(const LilvPlugin **)b;

	const char *name_a = NULL;
	const char *name_b = NULL;

	LilvNode *node_a = lilv_plugin_get_name(plug_a);
	LilvNode *node_b = lilv_plugin_get_name(plug_b);

	if(node_a)
		name_a = lilv_node_as_string(node_a);
	if(node_b)
		name_b = lilv_node_as_string(node_b);

	const int ret = name_a && name_b
		? strcasecmp(name_a, name_b)
		: 0;

	if(node_a)
		lilv_node_free(node_a);
	if(node_b)
		lilv_node_free(node_b);

	return ret;
}

static void
_refresh_main_plugin_list(plughandle_t *handle)
{
	_hash_free(&handle->plugin_matches);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	LilvNode *p = NULL;
	if(handle->plugin_search_selector == SELECTOR_SEARCH_COMMENT)
		p = handle->node.rdfs_comment;
	else if(handle->plugin_search_selector == SELECTOR_SEARCH_PROJECT)
		p = handle->node.doap_name;

	bool selector_visible = false;
	LILV_FOREACH(plugins, i, plugs)
	{
		const LilvPlugin *plug = lilv_plugins_get(plugs, i);

		LilvNode *name_node = lilv_plugin_get_name(plug);
		if(name_node)
		{
			const char *name_str = lilv_node_as_string(name_node);
			bool visible = _textedit_len(&handle->plugin_search_edit) == 0;

			if(!visible)
			{
				switch(handle->plugin_search_selector)
				{
					case SELECTOR_SEARCH_NAME:
					{
						if(strcasestr(name_str, _textedit_const(&handle->plugin_search_edit)))
							visible = true;
					} break;
					case SELECTOR_SEARCH_COMMENT:
					{
						LilvNodes *label_nodes = p ? lilv_plugin_get_value(plug, p) : NULL;
						if(label_nodes)
						{
							const LilvNode *label_node = lilv_nodes_size(label_nodes)
								? lilv_nodes_get_first(label_nodes) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
							}
							lilv_nodes_free(label_nodes);
						}
					} break;
					case SELECTOR_SEARCH_AUTHOR:
					{
						LilvNode *author_node = lilv_plugin_get_author_name(plug);
						if(author_node)
						{
							if(strcasestr(lilv_node_as_string(author_node), _textedit_const(&handle->plugin_search_edit)))
								visible = true;
							lilv_node_free(author_node);
						}
					} break;
					case SELECTOR_SEARCH_CLASS:
					{
						const LilvPluginClass *class = lilv_plugin_get_class(plug);
						if(class)
						{
							const LilvNode *label_node = lilv_plugin_class_get_label(class);
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
							}
						}
					} break;
					case SELECTOR_SEARCH_PROJECT:
					{
						LilvNode *project = lilv_plugin_get_project(plug);
						if(project)
						{
							LilvNode *label_node = p ? lilv_world_get(handle->world, lilv_plugin_get_uri(plug), p, NULL) : NULL;
							if(label_node)
							{
								if(strcasestr(lilv_node_as_string(label_node), _textedit_const(&handle->plugin_search_edit)))
									visible = true;
								lilv_node_free(label_node);
							}
							lilv_node_free(project);
						}
					} break;

					default: break;
				}
			}

			if(visible)
			{
				_hash_add(&handle->plugin_matches, (void *)plug);
			}

			lilv_node_free(name_node);
		}
	}

	_hash_sort(&handle->plugin_matches, _sort_plugin_name);
}

static void
_expose_main_plugin_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	if(_hash_empty(&handle->plugin_matches) || find_matches)
		_refresh_main_plugin_list(handle);

	const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);

	int count = 0;
	bool selector_visible = false;
	HASH_FOREACH(&handle->plugin_matches, itr)
	{
		const LilvPlugin *plug = *itr;
		if(plug)
		{
			LilvNode *name_node = lilv_plugin_get_name(plug);
			if(name_node)
			{
				const char *name_str = lilv_node_as_string(name_node);

				if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
				{
					handle->plugin_selector = plug;
					_load(handle);
				}

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed,
					nk_style_item_color(nk_rgb(30, 30, 30)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover_active,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed_active,
					nk_style_item_color(nk_rgb(30, 30, 30)));

				const int selected = plug == handle->plugin_selector;
				if(nk_select_label(ctx, name_str, NK_TEXT_LEFT, selected))
				{
					handle->plugin_selector = plug;
				}

				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);

				if(plug == handle->plugin_selector)
					selector_visible = true;

				lilv_node_free(name_node);
			}
		}
	}

	if(!selector_visible)
		handle->plugin_selector = NULL;
}

static void
_expose_main_plugin_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvPlugin *plug = handle->plugin_selector;

	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	const LilvNode *uri_node = lilv_plugin_get_uri(plug);
	const LilvNode *bundle_node = lilv_plugin_get_bundle_uri(plug);
	LilvNode *author_name_node = lilv_plugin_get_author_name(plug);
	LilvNode *author_email_node = lilv_plugin_get_author_email(plug);
	LilvNode *author_homepage_node = lilv_plugin_get_author_homepage(plug);
	LilvNodes *minor_nodes = lilv_plugin_get_value(plug, handle->node.lv2_minorVersion);
	LilvNodes *micro_nodes = lilv_plugin_get_value(plug, handle->node.lv2_microVersion);
	LilvNodes *license_nodes = lilv_plugin_get_value(plug, handle->node.doap_license);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(uri_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(uri_node));
	if(bundle_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bundle:  %s", lilv_node_as_uri(bundle_node));
	if(author_name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Author:  %s", lilv_node_as_string(author_name_node));
	if(author_email_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Email:   %s", lilv_node_as_string(author_email_node));
	if(author_homepage_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Web:     %s", lilv_node_as_string(author_homepage_node));
	if(lilv_nodes_size(minor_nodes) && lilv_nodes_size(micro_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(lilv_nodes_get_first(minor_nodes)),
			lilv_node_as_int(lilv_nodes_get_first(micro_nodes)) );
	if(lilv_nodes_size(license_nodes))
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s",
			lilv_node_as_uri(lilv_nodes_get_first(license_nodes)) );
	//TODO project

	if(name_node)
		lilv_node_free(name_node);
	if(author_name_node)
		lilv_node_free(author_name_node);
	if(author_email_node)
		lilv_node_free(author_email_node);
	if(author_homepage_node)
		lilv_node_free(author_homepage_node);
	if(minor_nodes)
		lilv_nodes_free(minor_nodes);
	if(micro_nodes)
		lilv_nodes_free(micro_nodes);
	if(license_nodes)
		lilv_nodes_free(license_nodes);
}

static void
_refresh_main_preset_list_for_bank(plughandle_t *handle,
	LilvNodes *presets, const LilvNode *preset_bank)
{
	bool search = _textedit_len(&handle->preset_search_edit) != 0;

	LILV_FOREACH(nodes, i, presets)
	{
		const LilvNode *preset = lilv_nodes_get(presets, i);

		bool visible = false;

		LilvNode *bank = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
		if(bank)
		{
			if(lilv_node_equals(preset_bank, bank))
				visible = true;

			lilv_node_free(bank);
		}
		else if(!preset_bank)
			visible = true;

		if(visible)
		{
			//FIXME support other search criteria
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				const char *label_str = lilv_node_as_string(label_node);

				if(!search || strcasestr(label_str, _textedit_const(&handle->preset_search_edit)))
				{
					_hash_add(&handle->preset_matches, (void *)preset);
				}

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_refresh_main_preset_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->preset_matches);

	HASH_FOREACH(&mod->banks, itr)
	{
		const LilvNode *bank = *itr;

		_refresh_main_preset_list_for_bank(handle, mod->presets, bank);
	}

	_refresh_main_preset_list_for_bank(handle, mod->presets, NULL);

	_hash_sort_r(&handle->preset_matches, _sort_rdfs_label, handle);
}

static void
_tab_label(struct nk_context *ctx, const char *label)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	struct nk_rect bounds = nk_widget_bounds(ctx);
	const struct nk_color bg = nk_rgb(24, 24, 24); //FIXME use color from style
	//const struct nk_color bg = ctx->style.window.header.normal.data.color;

	nk_fill_rect(canvas, bounds, 10, bg);

	const float h = bounds.h;
	bounds.h /= 2;
	bounds.y += bounds.h;
	nk_fill_rect(canvas, bounds, 0, bg);

	nk_label(ctx, label, NK_TEXT_CENTERED);
}

static void
_expose_main_preset_list_for_bank(plughandle_t *handle, struct nk_context *ctx,
	const LilvNode *preset_bank)
{
	bool first = true;
	int count = 0;
	HASH_FOREACH(&handle->preset_matches, itr)
	{
		const LilvNode *preset = *itr;

		bool visible = false;

		LilvNode *bank = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
		if(bank)
		{
			if(lilv_node_equals(preset_bank, bank))
				visible = true;

			lilv_node_free(bank);
		}
		else if(!preset_bank)
			visible = true;

		if(visible)
		{
			LilvNode *label_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
			if(!label_node)
				label_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
			if(label_node)
			{
				if(first)
				{
					LilvNode *bank_label_node = NULL;
					if(preset_bank)
					{
						bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.rdfs_label, NULL);
						if(!bank_label_node)
							bank_label_node = lilv_world_get(handle->world, preset_bank, handle->node.lv2_name, NULL);
					}
					const char *bank_label = bank_label_node
						? lilv_node_as_string(bank_label_node)
						: "Unbanked";

					_tab_label(ctx, bank_label);

					if(bank_label_node)
						lilv_node_free(bank_label_node);

					first = false;
				}

				const char *label_str = lilv_node_as_string(label_node);

				if(nk_widget_is_mouse_clicked(ctx, NK_BUTTON_RIGHT))
				{
					handle->preset_selector = preset;
					_load(handle);
				}

				nk_style_push_style_item(ctx, &ctx->style.selectable.normal, (count++ % 2)
					? nk_style_item_color(nk_rgb(40, 40, 40))
					: nk_style_item_color(nk_rgb(45, 45, 45))); // NK_COLOR_WINDOW
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed,
					nk_style_item_color(nk_rgb(30, 30, 30)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.hover_active,
					nk_style_item_color(nk_rgb(35, 35, 35)));
				nk_style_push_style_item(ctx, &ctx->style.selectable.pressed_active,
					nk_style_item_color(nk_rgb(30, 30, 30)));

				int selected = preset == handle->preset_selector;
				if(nk_selectable_label(ctx, label_str, NK_TEXT_LEFT, &selected))
				{
					handle->preset_selector = preset;
				}

				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);
				nk_style_pop_style_item(ctx);

				lilv_node_free(label_node);
			}
		}
	}
}

static void
_expose_main_preset_list(plughandle_t *handle, struct nk_context *ctx,
	bool find_matches)
{
	mod_t *mod = handle->module_selector;

	if(mod && mod->presets)
	{
		if(_hash_empty(&handle->preset_matches) || find_matches)
			_refresh_main_preset_list(handle, mod);

		HASH_FOREACH(&mod->banks, itr)
		{
			const LilvNode *bank = *itr;

			_expose_main_preset_list_for_bank(handle, ctx, bank);
		}

		_expose_main_preset_list_for_bank(handle, ctx, NULL);
	}
}

static void
_expose_main_preset_info(plughandle_t *handle, struct nk_context *ctx)
{
	const LilvNode *preset = handle->preset_selector;

	if(!preset)
		return;

	//FIXME
	LilvNode *name_node = lilv_world_get(handle->world, preset, handle->node.rdfs_label, NULL);
	if(!name_node)
		name_node = lilv_world_get(handle->world, preset, handle->node.lv2_name, NULL);
	LilvNode *comment_node = lilv_world_get(handle->world, preset, handle->node.rdfs_comment, NULL);
	LilvNode *bank_node = lilv_world_get(handle->world, preset, handle->node.pset_bank, NULL);
	LilvNode *minor_node = lilv_world_get(handle->world, preset, handle->node.lv2_minorVersion, NULL);
	LilvNode *micro_node = lilv_world_get(handle->world, preset, handle->node.lv2_microVersion, NULL);
	LilvNode *license_node = lilv_world_get(handle->world, preset, handle->node.doap_license, NULL);

	if(name_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Name:    %s", lilv_node_as_string(name_node));
	if(preset)
		nk_labelf(ctx, NK_TEXT_LEFT, "URI:     %s", lilv_node_as_uri(preset));
	if(comment_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Comment: %s", lilv_node_as_string(comment_node));
	if(bank_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Bank:    %s", lilv_node_as_uri(bank_node));
	if(minor_node && micro_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "Version: %i.%i",
			lilv_node_as_int(minor_node), lilv_node_as_int(micro_node) );
	if(license_node)
		nk_labelf(ctx, NK_TEXT_LEFT, "License: %s", lilv_node_as_uri(license_node));
	//TODO author, project

	if(name_node)
		lilv_node_free(name_node);
	if(comment_node)
		lilv_node_free(comment_node);
	if(bank_node)
		lilv_node_free(bank_node);
	if(minor_node)
		lilv_node_free(minor_node);
	if(micro_node)
		lilv_node_free(micro_node);
	if(license_node)
		lilv_node_free(license_node);
}

static int
_dial_bool(struct nk_context *ctx, int32_t *val, struct nk_color color, bool editable)
{
	const int32_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const bool left_mouse_click_in_cursor = nk_widget_is_mouse_clicked(ctx, NK_BUTTON_LEFT);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			bool mouse_has_scrolled = false;

			if(left_mouse_click_in_cursor)
			{
				states = NK_WIDGET_STATE_ACTIVED;
			}
			else if(nk_input_is_mouse_hovering_rect(in, bounds))
			{
				if(in->mouse.scroll_delta != 0.f) // has scrolling
				{
					mouse_has_scrolled = true;
					in->mouse.scroll_delta = 0.f;
				}

				states = NK_WIDGET_STATE_HOVER;
			}

			if(left_mouse_click_in_cursor || mouse_has_scrolled)
			{
				*val = !*val;
			}
		}

		const struct nk_style_item *fg = NULL;
		const struct nk_style_item *bg = NULL;

		switch(states)
		{
			case NK_WIDGET_STATE_HOVER:
			{
				bg = &ctx->style.progress.hover;
				fg = &ctx->style.progress.cursor_hover;
			}	break;
			case NK_WIDGET_STATE_ACTIVED:
			{
				bg = &ctx->style.progress.active;
				fg = &ctx->style.progress.cursor_active;
			}	break;
			default:
			{
				bg = &ctx->style.progress.normal;
				fg = &ctx->style.progress.cursor_normal;
			}	break;
		}

		const struct nk_color bg_color = bg->data.color;
		struct nk_color fg_color = fg->data.color;

		fg_color.r = (int)fg_color.r * color.r / 0xff;
		fg_color.g = (int)fg_color.g * color.g / 0xff;
		fg_color.b = (int)fg_color.b * color.b / 0xff;
		fg_color.a = (int)fg_color.a * color.a / 0xff;

		struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
		const float w2 = bounds.w/2;
		const float h2 = bounds.h/2;
		const float r1 = NK_MIN(w2, h2);
		const float r2 = r1 / 2;
		const float cx = bounds.x + w2;
		const float cy = bounds.y + h2;

		nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, fg_color);
		nk_fill_arc(canv, cx, cy, r2 - 2, 0.f, 2*M_PI, ctx->style.window.background);
		nk_fill_arc(canv, cx, cy, r2 - 4, 0.f, 2*M_PI,
			*val ? fg_color : bg_color);
	}

	return tmp != *val;
}

static float
_dial_numeric_behavior(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states *states, int *divider, struct nk_input *in)
{
	const struct nk_mouse_button *btn = &in->mouse.buttons[NK_BUTTON_LEFT];;
	const bool left_mouse_down = btn->down;
	const bool left_mouse_click_in_cursor = nk_input_has_mouse_click_down_in_rect(in,
		NK_BUTTON_LEFT, bounds, nk_true);

	float dd = 0.f;
	if(left_mouse_down && left_mouse_click_in_cursor)
	{
		const float dx = in->mouse.delta.x;
		const float dy = in->mouse.delta.y;
		dd = fabs(dx) > fabs(dy) ? dx : -dy;

		*states = NK_WIDGET_STATE_ACTIVED;
	}
	else if(nk_input_is_mouse_hovering_rect(in, bounds))
	{
		if(in->mouse.scroll_delta != 0.f) // has scrolling
		{
			dd = in->mouse.scroll_delta;
			in->mouse.scroll_delta = 0.f;
		}

		*states = NK_WIDGET_STATE_HOVER;
	}

	if(nk_input_is_key_down(in, NK_KEY_CTRL))
		*divider *= 4;
	if(nk_input_is_key_down(in, NK_KEY_SHIFT))
		*divider *= 4;

	return dd;
}

static void
_dial_numeric_draw(struct nk_context *ctx, struct nk_rect bounds,
	enum nk_widget_states states, float perc, struct nk_color color)
{
	struct nk_command_buffer *canv= nk_window_get_canvas(ctx);
	const struct nk_style_item *bg = NULL;
	const struct nk_style_item *fg = NULL;

	switch(states)
	{
		case NK_WIDGET_STATE_HOVER:
		{
			bg = &ctx->style.progress.hover;
			fg = &ctx->style.progress.cursor_hover;
		}	break;
		case NK_WIDGET_STATE_ACTIVED:
		{
			bg = &ctx->style.progress.active;
			fg = &ctx->style.progress.cursor_active;
		}	break;
		default:
		{
			bg = &ctx->style.progress.normal;
			fg = &ctx->style.progress.cursor_normal;
		}	break;
	}

	const struct nk_color bg_color = bg->data.color;
	struct nk_color fg_color = fg->data.color;

	fg_color.r = (int)fg_color.r * color.r / 0xff;
	fg_color.g = (int)fg_color.g * color.g / 0xff;
	fg_color.b = (int)fg_color.b * color.b / 0xff;
	fg_color.a = (int)fg_color.a * color.a / 0xff;

	const float w2 = bounds.w/2;
	const float h2 = bounds.h/2;
	const float r1 = NK_MIN(w2, h2);
	const float r2 = r1 / 2;
	const float cx = bounds.x + w2;
	const float cy = bounds.y + h2;
	const float aa = M_PI/6;
	const float a1 = M_PI/2 + aa;
	const float a2 = 2*M_PI + M_PI/2 - aa;
	const float a3 = a1 + (a2 - a1)*perc;

	nk_fill_arc(canv, cx, cy, r1, a1, a2, bg_color);
	nk_fill_arc(canv, cx, cy, r1, a1, a3, fg_color);
	nk_fill_arc(canv, cx, cy, r2, 0.f, 2*M_PI, ctx->style.window.background);
}

static int
_dial_double(struct nk_context *ctx, double min, double *val, double max, float mul,
	struct nk_color color, bool editable)
{
	const double tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const double range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				*val += dd * per_pixel_inc;
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_long(struct nk_context *ctx, int64_t min, int64_t *val, int64_t max, float mul,
	struct nk_color color, bool editable)
{
	const int64_t tmp = *val;
	struct nk_rect bounds = nk_layout_space_bounds(ctx);
	const enum nk_widget_layout_states layout_states = nk_widget(&bounds, ctx);

	if(layout_states != NK_WIDGET_INVALID)
	{
		enum nk_widget_states states = NK_WIDGET_STATE_INACTIVE;
		const int64_t range = max - min;
		struct nk_input *in = ( (layout_states == NK_WIDGET_ROM)
			|| (ctx->current->layout->flags & NK_WINDOW_ROM) ) ? 0 : &ctx->input;

		if(in && editable && (layout_states == NK_WIDGET_VALID) )
		{
			int divider = 1;
			const float dd = _dial_numeric_behavior(ctx, bounds, &states, &divider, in);

			if(dd != 0.f) // update value
			{
				const double per_pixel_inc = mul * range / bounds.w / divider;

				const double diff = dd * per_pixel_inc;
				*val += diff < 0.0 ? floor(diff) : ceil(diff);
				*val = NK_CLAMP(min, *val, max);
			}
		}

		const float perc = (float)(*val - min) / range;
		_dial_numeric_draw(ctx, bounds, states, perc, color);
	}

	return tmp != *val;
}

static int
_dial_float(struct nk_context *ctx, float min, float *val, float max, float mul,
	struct nk_color color, bool editable)
{
	double tmp = *val;
	const int res = _dial_double(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static int
_dial_int(struct nk_context *ctx, int32_t min, int32_t *val, int32_t max, float mul,
	struct nk_color color, bool editable)
{
	int64_t tmp = *val;
	const int res = _dial_long(ctx, min, &tmp, max, mul, color, editable);
	*val = tmp;

	return res;
}

static void
_expose_atom_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str, bool is_cv)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		//FIXME

		nk_group_end(ctx);
	}

	//FIXME
}

static void
_expose_audio_port(struct nk_context *ctx, mod_t *mod, audio_port_t *audio,
	float dy, const char *name_str, bool is_cv)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		struct nk_rect bounds;
		const enum nk_widget_layout_states states = nk_widget(&bounds, ctx);
		if(states != NK_WIDGET_INVALID)
		{
			struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);

			const struct nk_color bg = ctx->style.property.normal.data.color;
			nk_fill_rect(canvas, bounds, ctx->style.property.rounding, bg);
			nk_stroke_rect(canvas, bounds, ctx->style.property.rounding, ctx->style.property.border, bg);

			const struct nk_rect orig = bounds;
			struct nk_rect outline;
			const float mx1 = 58.f / 70.f;
			const float mx2 = 12.f / 70.f;
			const uint8_t alph = 0x7f;

			// <= -6dBFS
			{
				const float dbfs = NK_MIN(audio->peak, mx1);
				const uint8_t dcol = 0xff * dbfs / mx1;
				const struct nk_color left = is_cv
					? nk_rgba(0xff, 0x00, 0xff, alph)
					: nk_rgba(0x00, 0xff, 0xff, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = is_cv
					? nk_rgba(0xff, dcol, 0xff-dcol, alph)
					: nk_rgba(dcol, 0xff, 0xff-dcol, alph);
				const struct nk_color top = right;

				const float ox = ctx->style.font->height/2 + ctx->style.property.border + ctx->style.property.padding.x;
				const float oy = ctx->style.property.border + ctx->style.property.padding.y;
				bounds.x += ox;
				bounds.y += oy;
				bounds.w -= 2*ox;
				bounds.h -= 2*oy;
				outline = bounds;
				bounds.w *= dbfs;

				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// > 6dBFS
			if(audio->peak > mx1)
			{
				const float dbfs = audio->peak - mx1;
				const uint8_t dcol = 0xff * dbfs / mx2;
				const struct nk_color left = nk_rgba(0xff, 0xff, 0x00, alph);
				const struct nk_color bottom = left;
				const struct nk_color right = nk_rgba(0xff, 0xff - dcol, 0x00, alph);
				const struct nk_color top = right;

				bounds = outline;
				bounds.x += bounds.w * mx1;
				bounds.w *= dbfs;
				nk_fill_rect_multi_color(canvas, bounds, left, top, right, bottom);
			}

			// draw 6dBFS lines from -60 to +6
			for(unsigned i = 4; i <= 70; i += 6)
			{
				const bool is_zero = (i == 64);
				const float dx = outline.w * i / 70.f;

				const float x0 = outline.x + dx;
				const float y0 = is_zero ? orig.y + 2.f : outline.y;

				const float border = (is_zero ? 2.f : 1.f) * ctx->style.window.group_border;

				const float x1 = x0;
				const float y1 = is_zero ? orig.y + orig.h - 2.f : outline.y + outline.h;

				nk_stroke_line(canvas, x0, y0, x1, y1, border, ctx->style.window.group_border_color);
			}

			nk_stroke_rect(canvas, outline, 0.f, ctx->style.window.group_border, ctx->style.window.group_border_color);
		}

		nk_group_end(ctx);
	}

	if(_dial_float(ctx, 0.f, &audio->gain, 1.f, 1.f, nk_rgb(0xff, 0xff, 0xff), true))
	{
		//FIXME
	}
}

const char *lab = "#"; //FIXME

static void
_expose_control_port(struct nk_context *ctx, mod_t *mod, control_port_t *control,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(control->is_int)
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, control->val.i);
			}
			else // !readonly
			{
				const float inc = control->span.i / nk_widget_width(ctx);
				int val = control->val.i;
				nk_property_int(ctx, lab, control->min.i, &val, control->max.i, 1.f, inc);
				control->val.i = val;
			}
		}
		else if(control->is_bool)
		{
			nk_spacing(ctx, 1);
		}
		else if(!_hash_empty(&control->points))
		{
			scale_point_t *ref = control->points_ref;
			if(nk_combo_begin_label(ctx, ref->label, nk_vec2(nk_widget_width(ctx), 7*dy)))
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				HASH_FOREACH(&control->points, itr)
				{
					scale_point_t *point = *itr;

					if(nk_combo_item_label(ctx, point->label, NK_TEXT_LEFT) && !control->is_readonly)
					{
						control->points_ref = point;
						control->val = point->val;
						//FIXME
					}
				}

				nk_combo_end(ctx);
			}
		}
		else // is_float
		{
			if(control->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", control->val.f);
			}
			else // !readonly
			{
				const float step = control->span.f / 100.f;
				const float inc = control->span.f / nk_widget_width(ctx);
				nk_property_float(ctx, lab, control->min.f, &control->val.f, control->max.f, step, inc);
			}
		}

		nk_group_end(ctx);
	}

	if(control->is_int)
	{
		if(_dial_int(ctx, control->min.i, &control->val.i, control->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
	else if(control->is_bool)
	{
		if(_dial_bool(ctx, &control->val.i, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
	else if(!_hash_empty(&control->points))
	{
		nk_spacing(ctx, 1);
	}
	else // is_float
	{
		if(_dial_float(ctx, control->min.f, &control->val.f, control->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !control->is_readonly))
		{
			//FIXME
		}
	}
}

static void
_expose_port(struct nk_context *ctx, mod_t *mod, port_t *port, float dy)
{
	LilvNode *name_node = lilv_port_get_name(mod->plug, port->port);
	const char *name_str = name_node ? lilv_node_as_string(name_node) : "Unknown";

	if(nk_group_begin(ctx, name_str, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		switch(port->type)
		{
			case PROPERTY_TYPE_AUDIO:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, name_str, false);
			} break;
			case PROPERTY_TYPE_CV:
			{
				_expose_audio_port(ctx, mod, &port->audio, dy, name_str, true);
			} break;
			case PROPERTY_TYPE_CONTROL:
			{
				_expose_control_port(ctx, mod, &port->control, dy, name_str);
			} break;
			case PROPERTY_TYPE_ATOM:
			{
				_expose_atom_port(ctx, mod, &port->audio, dy, name_str, false);
			} break;
			case PROPERTY_TYPE_PARAM:
			{
				//FIXME
			} break;

			default:
				break;
		}

		nk_group_end(ctx);
	}

	if(name_node)
		lilv_node_free(name_node);
}

static void
_expose_param_inner(struct nk_context *ctx, param_t *param, plughandle_t *handle,
	float dy, const char *name_str)
{
	const float DY = nk_window_get_content_region(ctx).h
		- 2*ctx->style.window.group_padding.y;
	const float ratio [] = {0.7, 0.3};

	nk_layout_row(ctx, NK_DYNAMIC, DY, 2, ratio);
	if(nk_group_begin(ctx, name_str, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_layout_row_dynamic(ctx, dy, 1);
		nk_label(ctx, name_str, NK_TEXT_LEFT);

		if(param->range == handle->forge.Int)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi32, param->val.i);
			}
			else // !readonly
			{
				const float inc = param->span.i / nk_widget_width(ctx);
				int val = param->val.i;
				nk_property_int(ctx, lab, param->min.i, &val, param->max.i, 1.f, inc);
				param->val.i = val;
			}
		}
		else if(param->range == handle->forge.Long)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%"PRIi64, param->val.h);
			}
			else // !readonly
			{
				const float inc = param->span.h / nk_widget_width(ctx);
				int val = param->val.h;
				nk_property_int(ctx, lab, param->min.h, &val, param->max.h, 1.f, inc);
				param->val.h = val;
			}
		}
		else if(param->range == handle->forge.Bool)
		{
			nk_spacing(ctx, 1);
		}
		else if(param->range == handle->forge.Float)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%f", param->val.f);
			}
			else // !readonly
			{
				const float step = param->span.f / 100.f;
				const float inc = param->span.f / nk_widget_width(ctx);
				nk_property_float(ctx, lab, param->min.f, &param->val.f, param->max.f, step, inc);
			}
		}
		else if(param->range == handle->forge.Double)
		{
			if(param->is_readonly)
			{
				nk_labelf(ctx, NK_TEXT_RIGHT, "%lf", param->val.d);
			}
			else // !readonly
			{
				const double step = param->span.d / 100.0;
				const float inc = param->span.d / nk_widget_width(ctx);
				nk_property_double(ctx, lab, param->min.d, &param->val.d, param->max.d, step, inc);
			}
		}
		else
		{
			nk_spacing(ctx, 1);
		}
		//FIXME

		nk_group_end(ctx);
	}

	if(param->range == handle->forge.Int)
	{
		if(_dial_int(ctx, param->min.i, &param->val.i, param->max.i, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Long)
	{
		if(_dial_long(ctx, param->min.h, &param->val.h, param->max.h, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Bool)
	{
		if(_dial_bool(ctx, &param->val.i, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Float)
	{
		if(_dial_float(ctx, param->min.f, &param->val.f, param->max.f, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else if(param->range == handle->forge.Double)
	{
		if(_dial_double(ctx, param->min.d, &param->val.d, param->max.d, 1.f, nk_rgb(0xff, 0xff, 0xff), !param->is_readonly))
		{
			//FIXME
		}
	}
	else
	{
		nk_spacing(ctx, 1);
	}
	//FIXME
}

static void
_expose_param(plughandle_t *handle, struct nk_context *ctx, param_t *param, float dy)
{
	LilvNode *name_node = lilv_world_get(handle->world, param->param, handle->node.rdfs_label, NULL);
	if(!name_node)
		name_node = lilv_world_get(handle->world, param->param, handle->node.lv2_name, NULL);
	const char *name_str = name_node ? lilv_node_as_string(name_node) : "Unknown";

	if(nk_group_begin(ctx, name_str, NK_WINDOW_BORDER | NK_WINDOW_NO_SCROLLBAR))
	{
		_expose_param_inner(ctx, param, handle, dy, name_str);

		nk_group_end(ctx);
	}

	if(name_node)
		lilv_node_free(name_node);
}

static void
_refresh_main_port_list(plughandle_t *handle, mod_t *mod)
{
	_hash_free(&handle->port_matches);

	bool search = _textedit_len(&handle->port_search_edit) != 0;

	HASH_FOREACH(&mod->ports, itr)
	{
		port_t *port = *itr;

		bool visible = true;
		if(search)
		{
			LilvNode *name_node = lilv_port_get_name(mod->plug, port->port);
			if(name_node)
			{
				if(!strcasestr(lilv_node_as_string(name_node), _textedit_const(&handle->port_search_edit)))
					visible = false;

				lilv_node_free(name_node);
			}
		}

		if(visible)
			_hash_add(&handle->port_matches, port);
	}
}

static void
_expose_control_list(plughandle_t *handle, mod_t *mod, struct nk_context *ctx,
	float DY, float dy, bool find_matches)
{
	if(_hash_empty(&handle->port_matches) || find_matches)
		_refresh_main_port_list(handle, mod);

	HASH_FOREACH(&mod->groups, itr)
	{
		const LilvNode *mod_group = *itr;

		LilvNode *group_label_node = lilv_world_get(handle->world, mod_group, handle->node.rdfs_label, NULL);
		if(!group_label_node)
			group_label_node = lilv_world_get(handle->world, mod_group, handle->node.lv2_name, NULL);
		if(group_label_node)
		{
			bool first = true;
			HASH_FOREACH(&handle->port_matches, port_itr)
			{
				port_t *port = *port_itr;
				if(!lilv_nodes_contains(port->groups, mod_group))
					continue;

				if(first)
				{
					nk_layout_row_dynamic(ctx, dy, 1);
					_tab_label(ctx, lilv_node_as_string(group_label_node));

					nk_layout_row_dynamic(ctx, DY, 4);
					first = false;
				}

				_expose_port(ctx, mod, port, dy);
			}

			lilv_node_free(group_label_node);
		}
	}

	{
		bool first = true;
		HASH_FOREACH(&handle->port_matches, itr)
		{
			port_t *port = *itr;
			if(lilv_nodes_size(port->groups))
				continue;

			if(first)
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				_tab_label(ctx, "Ungrouped");

				nk_layout_row_dynamic(ctx, DY, 4);
				first = false;
			}

			_expose_port(ctx, mod, port, dy);
		}
	}

	{
		bool first = true;
		HASH_FOREACH(&mod->params, itr)
		{
			param_t *param = *itr;

			if(first)
			{
				nk_layout_row_dynamic(ctx, dy, 1);
				_tab_label(ctx, "Parameters");

				nk_layout_row_dynamic(ctx, DY, 4);
				first = false;
			}

			_expose_param(handle, ctx, param, dy);
		}
	}
}

//FIXME move up
const struct nk_color grid_line_color = {40, 40, 40, 255};
const struct nk_color grid_background_color = {30, 30, 30, 255};
const struct nk_color hilight_color = {200, 100, 0, 255};
const struct nk_color button_border_color = {100, 100, 100, 255};
const struct nk_color grab_handle_color = {100, 100, 100, 255};

static bool
_mod_moveable(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_rect *bounds)
{
	const struct nk_input *in = &ctx->input;

	const bool is_hovering = nk_input_is_mouse_hovering_rect(in, *bounds);

	if(mod->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod->moving = false;
		}
		else
		{
			mod->pos.x += in->mouse.delta.x;
			mod->pos.y += in->mouse.delta.y;
			bounds->x += in->mouse.delta.x;
			bounds->y += in->mouse.delta.y;

			// move connections together with mod
			HASH_FOREACH(&handle->conns, mod_conn_itr)
			{
				mod_conn_t *mod_conn = *mod_conn_itr;

				if(mod_conn->source_mod == mod)
				{
					mod_conn->pos.x += in->mouse.delta.x/2;
					mod_conn->pos.y += in->mouse.delta.y/2;
				}

				if(mod_conn->sink_mod == mod)
				{
					mod_conn->pos.x += in->mouse.delta.x/2;
					mod_conn->pos.y += in->mouse.delta.y/2;
				}
			}
		}
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)
		&& nk_input_is_key_down(in, NK_KEY_CTRL) )
	{
		mod->moving = true;
	}

	return is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_RIGHT);
}

static void
_mod_connectors(plughandle_t *handle, struct nk_context *ctx, mod_t *mod,
	struct nk_vec2 dim, bool is_hilighted)
{
	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	const float cw = 4.f * handle->scale;

	// output connector
	if(mod->source_type & handle->type)
	{
		const float cx = mod->pos.x - scrolling.x + dim.x/2 + 2*cw;
		const float cy = mod->pos.y - scrolling.y;
		const struct nk_rect outer = nk_rect(
			cx - cw, cy - cw,
			4*cw, 4*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI,
			is_hilighted ? hilight_color : grab_handle_color);
		if(  (nk_input_is_mouse_hovering_rect(in, outer) && !handle->linking.active)
			|| (handle->linking.active && (handle->linking.source_mod == mod)) )
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, hilight_color);
		}

		// start linking process
		if(nk_input_has_mouse_click_down_in_rect(in, NK_BUTTON_LEFT, outer, nk_true)) {
			handle->linking.active = nk_true;
			handle->linking.source_mod = mod;
		}

		// draw ilne from linked node slot to mouse position
		if(  handle->linking.active
			&& (handle->linking.source_mod == mod) )
		{
			struct nk_vec2 m = in->mouse.pos;

			nk_stroke_line(canvas, cx, cy, m.x, m.y, 1.f, hilight_color);
		}
	}

	// input connector
	if(mod->sink_type & handle->type)
	{
		const float cx = mod->pos.x - scrolling.x - dim.x/2 - 2*cw;
		const float cy = mod->pos.y - scrolling.y;
		const struct nk_rect outer = nk_rect(
			cx - cw, cy - cw,
			4*cw, 4*cw
		);

		nk_fill_arc(canvas, cx, cy, cw, 0.f, 2*NK_PI,
			is_hilighted ? hilight_color : grab_handle_color);
		if(  nk_input_is_mouse_hovering_rect(in, outer)
			&& handle->linking.active)
		{
			nk_stroke_arc(canvas, cx, cy, 2*cw, 0.f, 2*NK_PI, 1.f, hilight_color);
		}

		if(  nk_input_is_mouse_released(in, NK_BUTTON_LEFT)
			&& nk_input_is_mouse_hovering_rect(in, outer)
			&& handle->linking.active)
		{
			handle->linking.active = nk_false;

			mod_t *src = handle->linking.source_mod;
			if(src)
			{
				mod_conn_t *mod_conn = _mod_conn_find(handle, src, mod);
				if(!mod_conn) // does not yet exist
					mod_conn = _mod_conn_add(handle, src, mod);
				if(mod_conn)
				{
					mod_conn->type |= handle->type;

					if(nk_input_is_key_down(in, NK_KEY_CTRL)) // automatic connection
					{
						unsigned i = 0;
						HASH_FOREACH(&src->sources, source_port_itr)
						{
							port_t *source_port = *source_port_itr;
							if(source_port->type != handle->type)
								continue;

							unsigned j = 0;
							HASH_FOREACH(&mod->sinks, sink_port_itr)
							{
								port_t *sink_port = *sink_port_itr;
								if(sink_port->type != handle->type)
									continue;

#if 0
								if(i == j)
									jack_connect(handle->mod, source_port->name, sink_port->name);
#endif

								j++;
							}

							i++;
						}
					}
				}
			}
		}
	}
}

static void
_expose_mod(plughandle_t *handle, struct nk_context *ctx, mod_t *mod, float dy)
{
	if(!(mod->source_type & handle->type) && !(mod->sink_type & handle->type) )
		return;

	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_input *in = &ctx->input;

	const LilvPlugin *plug = mod->plug;
	if(!plug)
		return;

	LilvNode *name_node = lilv_plugin_get_name(plug);
	if(!name_node)
		return;

	mod->dim.x = 200.f * handle->scale;
	mod->dim.y = handle->dy;

	const struct nk_vec2 scrolling = handle->scrolling;

	struct nk_rect bounds = nk_rect(
		mod->pos.x - mod->dim.x/2 - scrolling.x,
		mod->pos.y - mod->dim.y/2 - scrolling.y,
		mod->dim.x, mod->dim.y);

	if(_mod_moveable(handle, ctx, mod, &bounds))
	{
		//FIXME right click
	}

	const bool is_hovering = nk_input_is_mouse_hovering_rect(in, bounds);
	if(  is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
	{
		handle->module_selector = mod;
		handle->preset_selector = NULL;
		handle->preset_find_matches = true;
		handle->port_find_matches = true;
	}

	mod->hovered = is_hovering;
	const bool is_hilighted = mod->hilighted || is_hovering || mod->moving
		|| (handle->module_selector == mod);

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;
		const struct nk_user_font *font = ctx->style.font;

		nk_fill_rect(canvas, body, style->rounding, style->hover.data.color);
		nk_stroke_rect(canvas, body, style->rounding, style->border,
			is_hilighted ? hilight_color : style->border_color);

		const char *mod_name = lilv_node_as_string(name_node);
		const size_t mod_name_len = strlen(mod_name);
		const float fw = font->width(font->userdata, font->height, mod_name, mod_name_len);
		const float fh = font->height;
		body.x += (body.w - fw)/2;
		body.y += (body.h - fh)/2;
		nk_draw_text(canvas, body, mod_name, mod_name_len, font,
			style->normal.data.color, style->text_normal);
	}

	_mod_connectors(handle, ctx, mod, nk_vec2(bounds.w, bounds.h), is_hilighted);
}

static void
_expose_mod_conn(plughandle_t *handle, struct nk_context *ctx, mod_conn_t *mod_conn, float dy)
{
	if(!(mod_conn->type & handle->type))
		return;

	const struct nk_input *in = &ctx->input;
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_vec2 scrolling = handle->scrolling;

	mod_t *src = mod_conn->source_mod;
	mod_t *snk = mod_conn->sink_mod;

	if(!src || !snk)
		return;

	int nx = 0;
	HASH_FOREACH(&mod_conn->source_mod->sources, source_port_itr)
	{
		port_t *source_port = *source_port_itr;

		if(!(source_port->type & handle->type))
			continue;

		nx += 1;
	}

	int ny = 0;
	HASH_FOREACH(&mod_conn->sink_mod->sinks, sink_port_itr)
	{
		port_t *sink_port = *sink_port_itr;

		if(!(sink_port->type & handle->type))
			continue;

		ny += 1;
	}

	const float ps = 16.f * handle->scale;
	const float pw = nx * ps;
	const float ph = ny * ps;
	struct nk_rect bounds = nk_rect(
		mod_conn->pos.x - scrolling.x - pw/2,
		mod_conn->pos.y - scrolling.y - ph/2,
		pw, ph
	);

	const int is_hovering = nk_input_is_mouse_hovering_rect(in, bounds);

	if(mod_conn->moving)
	{
		if(nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			mod_conn->moving = false;
		}
		else
		{
			mod_conn->pos.x += in->mouse.delta.x;
			mod_conn->pos.y += in->mouse.delta.y;
			bounds.x += in->mouse.delta.x;
			bounds.y += in->mouse.delta.y;
		}
	}
	else if(is_hovering
		&& nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT)
		&& nk_input_is_key_down(in, NK_KEY_CTRL) )
	{
		mod_conn->moving = true;
	}

	const bool is_hilighted = mod_conn->source_mod->hovered
		|| mod_conn->sink_mod->hovered
		|| is_hovering || mod_conn->moving;

	if(is_hilighted)
	{
		mod_conn->source_mod->hilighted = true;
		mod_conn->sink_mod->hilighted = true;
	}

	const float cs = 4.f * handle->scale;

	{
		const float cx = mod_conn->pos.x - scrolling.x;
		const float cxr = cx + pw/2;
		const float cy = mod_conn->pos.y - scrolling.y;
		const float cyl = cy - ph/2;
		const struct nk_color col = is_hilighted ? hilight_color : grab_handle_color;

		const float l0x = src->pos.x - scrolling.x + src->dim.x/2 + cs*2;
		const float l0y = src->pos.y - scrolling.y;
		const float l1x = snk->pos.x - scrolling.x - snk->dim.x/2 - cs*2;
		const float l1y = snk->pos.y - scrolling.y;

		const float bend = 50.f * handle->scale;
		nk_stroke_curve(canvas,
			l0x, l0y,
			l0x + bend, l0y,
			cx, cyl - bend,
			cx, cyl,
			1.f, col);
		nk_stroke_curve(canvas,
			cxr, cy,
			cxr + bend, cy,
			l1x - bend, l1y,
			l1x, l1y,
			1.f, col);

		nk_fill_arc(canvas, cx, cyl, cs, 2*M_PI/2, 4*M_PI/2, col);
		nk_fill_arc(canvas, cxr, cy, cs, 3*M_PI/2, 5*M_PI/2, col);
	}

	nk_layout_space_push(ctx, nk_layout_space_rect_to_local(ctx, bounds));

	struct nk_rect body;
	const enum nk_widget_layout_states states = nk_widget(&body, ctx);
	if(states != NK_WIDGET_INVALID)
	{
		struct nk_style_button *style = &ctx->style.button;

		nk_fill_rect(canvas, body, style->rounding, style->normal.data.color);

		for(float x = ps; x < body.w; x += ps)
		{
			nk_stroke_line(canvas,
				body.x + x, body.y,
				body.x + x, body.y + body.h,
				style->border, style->border_color);
		}

		for(float y = ps; y < body.h; y += ps)
		{
			nk_stroke_line(canvas,
				body.x, body.y + y,
				body.x + body.w, body.y + y,
				style->border, style->border_color);
		}

		nk_stroke_rect(canvas, body, style->rounding, style->border,
			is_hilighted ? hilight_color : style->border_color);

		float x = body.x + ps/2;
		HASH_FOREACH(&mod_conn->source_mod->sources, source_port_itr)
		{
			port_t *source_port = *source_port_itr;

			if(!(source_port->type & handle->type))
				continue;

			float y = body.y + ps/2;
			HASH_FOREACH(&mod_conn->sink_mod->sinks, sink_port_itr)
			{
				port_t *sink_port = *sink_port_itr;

				if(!(sink_port->type & handle->type))
					continue;

#if 0
				port_conn_t *port_conn = _port_conn_find(mod_conn, source_port, sink_port);

				if(port_conn)
					nk_fill_arc(canvas, x, y, cs, 0.f, 2*NK_PI, toggle_color);
#endif

				const struct nk_rect tile = nk_rect(x - ps/2, y - ps/2, ps, ps);

				if(  nk_input_is_mouse_hovering_rect(in, tile)
					&& !mod_conn->moving)
				{
					LilvNode *source_node = lilv_port_get_name(mod_conn->source_mod->plug, source_port->port);
					LilvNode *sink_node = lilv_port_get_name(mod_conn->sink_mod->plug, sink_port->port);

					const char *source_name = source_node ? lilv_node_as_string(source_node) : NULL;
					const char *sink_name = sink_node ? lilv_node_as_string(sink_node) : NULL;

					if(source_name && sink_name)
					{
						char tmp [128];
						snprintf(tmp, 128, "%s || %s", source_name, sink_name);
						nk_tooltip(ctx, tmp);

						if(nk_input_is_mouse_pressed(in, NK_BUTTON_LEFT))
						{
#if 0
							if(port_conn)
								jack_disconnect(handle->mod, source_port->name, sink_port->name);
							else
								jack_connect(handle->mod, source_port->name, sink_port->name);
#endif
						}
					}

					if(source_node)
						lilv_node_free(source_node);
					if(sink_node)
						lilv_node_free(sink_node);
				}

				y += ps;
			}

			x += ps;
		}
	}
}

static void
_expose_main_body(plughandle_t *handle, struct nk_context *ctx, float dh, float dy)
{
	struct nk_command_buffer *canvas = nk_window_get_canvas(ctx);
	const struct nk_input *in = &ctx->input;

	handle->plugin_find_matches = false;
	handle->preset_find_matches = false;
	handle->port_find_matches = false;

	const struct nk_rect total_space = nk_window_get_content_region(ctx);
	const float vertical = total_space.h
		- handle->dy
		- 3*ctx->style.window.group_padding.y;
	const float upper_h = vertical * 0.6f;
	const float lower_h = vertical * 0.4f
		- handle->dy
		- 2*ctx->style.window.group_padding.y;

	nk_layout_space_begin(ctx, NK_STATIC, upper_h,
		_hash_size(&handle->mods) + _hash_size(&handle->conns));
	{
    const struct nk_rect old_clip = canvas->clip;
		const struct nk_rect space_bounds= nk_layout_space_bounds(ctx);
		nk_push_scissor(canvas, space_bounds);

		// graph content scrolling
		if(  nk_input_is_mouse_hovering_rect(in, space_bounds)
			&& nk_input_is_mouse_down(in, NK_BUTTON_MIDDLE))
		{
			handle->scrolling.x -= in->mouse.delta.x;
			handle->scrolling.y -= in->mouse.delta.y;
		}

		const struct nk_vec2 scrolling = handle->scrolling;

		// display grid
		{
			struct nk_rect ssize = nk_layout_space_bounds(ctx);
			ssize.h -= ctx->style.window.group_padding.y;
			const float grid_size = 28.0f * handle->scale;

			nk_fill_rect(canvas, ssize, 0.f, grid_background_color);

			for(float x = fmod(ssize.x - scrolling.x, grid_size);
				x < ssize.w;
				x += grid_size)
			{
				nk_stroke_line(canvas, x + ssize.x, ssize.y, x + ssize.x, ssize.y + ssize.h,
					1.0f, grid_line_color);
			}

			for(float y = fmod(ssize.y - scrolling.y, grid_size);
				y < ssize.h;
				y += grid_size)
			{
				nk_stroke_line(canvas, ssize.x, y + ssize.y, ssize.x + ssize.w, y + ssize.y,
					1.0f, grid_line_color);
			}
		}

		HASH_FOREACH(&handle->mods, mod_itr)
		{
			mod_t *mod = *mod_itr;

			_expose_mod(handle, ctx, mod, dy);

			mod->hilighted = false;
		}

		HASH_FOREACH(&handle->conns, mod_conn_itr)
		{
			mod_conn_t *mod_conn = *mod_conn_itr;

			_expose_mod_conn(handle, ctx, mod_conn, dy);
		}

		// reset linking connection
		if(  handle->linking.active
			&& nk_input_is_mouse_released(in, NK_BUTTON_LEFT))
		{
			handle->linking.active = false;
		}

    nk_push_scissor(canvas, old_clip);
	}
	nk_layout_space_end(ctx);

	{
		nk_layout_row_dynamic(ctx, dy, 11);

		const bool is_audio = handle->type == PROPERTY_TYPE_AUDIO;
		const bool is_cv = handle->type == PROPERTY_TYPE_CV;
		const bool is_atom = handle->type == PROPERTY_TYPE_ATOM;

		const bool is_midi = handle->type == PROPERTY_TYPE_MIDI;
		const bool is_osc = handle->type == PROPERTY_TYPE_OSC;
		const bool is_time = handle->type == PROPERTY_TYPE_TIME;
		const bool is_patch = handle->type == PROPERTY_TYPE_PATCH;
		const bool is_xpress = handle->type == PROPERTY_TYPE_XPRESS;

		const bool is_automation = handle->type == PROPERTY_TYPE_AUTOMATION;

		if(is_audio)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "Audio"))
			handle->type = PROPERTY_TYPE_AUDIO;
		if(is_audio)
			nk_style_pop_color(ctx);

		if(is_cv)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "CV"))
			handle->type = PROPERTY_TYPE_CV;
		if(is_cv)
			nk_style_pop_color(ctx);

		if(is_atom)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "Atom"))
			handle->type = PROPERTY_TYPE_ATOM;
		if(is_atom)
			nk_style_pop_color(ctx);

		nk_spacing(ctx, 1);

		if(is_midi)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "MIDI"))
			handle->type = PROPERTY_TYPE_MIDI;
		if(is_midi)
			nk_style_pop_color(ctx);

		if(is_osc)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "OSC"))
			handle->type = PROPERTY_TYPE_OSC;
		if(is_osc)
			nk_style_pop_color(ctx);

		if(is_time)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "Time"))
			handle->type = PROPERTY_TYPE_TIME;
		if(is_time)
			nk_style_pop_color(ctx);

		if(is_patch)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "Patch"))
			handle->type = PROPERTY_TYPE_PATCH;
		if(is_patch)
			nk_style_pop_color(ctx);

		if(is_xpress)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "XPression"))
			handle->type = PROPERTY_TYPE_XPRESS;
		if(is_xpress)
			nk_style_pop_color(ctx);

		nk_spacing(ctx, 1);

		if(is_automation)
			nk_style_push_color(ctx, &ctx->style.button.border_color, hilight_color);
		if(nk_button_label(ctx, "Automation"))
			handle->type = PROPERTY_TYPE_AUTOMATION;
		if(is_automation)
			nk_style_pop_color(ctx);
	}

	{
		const float lower_ratio [3] = {0.2, 0.2, 0.6};
		nk_layout_row(ctx, NK_DYNAMIC, lower_h, 3, lower_ratio);

		if(nk_group_begin(ctx, "Plugins", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [2] = {0.4, 0.6};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
				const selector_search_t old_sel = handle->plugin_search_selector;
				handle->plugin_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->plugin_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->plugin_search_selector)
					handle->plugin_find_matches = true;
				const size_t old_len = _textedit_len(&handle->plugin_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->plugin_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->plugin_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->plugin_search_edit)) )
					handle->plugin_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->plugin_search_edit);
			}
			nk_menubar_end(ctx);

			nk_layout_row_dynamic(ctx, dy, 1);
			_expose_main_plugin_list(handle, ctx, handle->plugin_find_matches);

#if 0
			_expose_main_plugin_info(handle, ctx);
#endif

			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Presets", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [2] = {0.4, 0.6};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 2, dim);
				const selector_search_t old_sel = handle->preset_search_selector;
				handle->preset_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->preset_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->preset_search_selector)
					handle->preset_find_matches = true;
				const size_t old_len = _textedit_len(&handle->preset_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->preset_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->preset_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->preset_search_edit)) )
					handle->preset_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->preset_search_edit);
			}
			nk_menubar_end(ctx);

			nk_layout_row_dynamic(ctx, dy, 1);
			_expose_main_preset_list(handle, ctx, handle->preset_find_matches);

#if 0
			_expose_main_preset_info(handle, ctx);
#endif
			nk_group_end(ctx);
		}

		if(nk_group_begin(ctx, "Controls", NK_WINDOW_BORDER | NK_WINDOW_TITLE))
		{
			nk_menubar_begin(ctx);
			{
				const float dim [7] = {0.2, 0.3, 0.1, 0.1, 0.1, 0.1, 0.1};
				nk_layout_row(ctx, NK_DYNAMIC, dy, 7, dim);
				const selector_search_t old_sel = handle->port_search_selector;
				handle->port_search_selector = nk_combo(ctx, search_labels, SELECTOR_SEARCH_MAX,
					handle->port_search_selector, dy, nk_vec2(nk_widget_width(ctx), 7*dy));
				if(old_sel != handle->port_search_selector)
					handle->port_find_matches = true;
				const size_t old_len = _textedit_len(&handle->port_search_edit);
				const nk_flags args = NK_EDIT_FIELD | NK_EDIT_SIG_ENTER | NK_EDIT_AUTO_SELECT;
				const nk_flags flags = nk_edit_buffer(ctx, args, &handle->port_search_edit, nk_filter_default);
				_textedit_zero_terminate(&handle->port_search_edit);
				if( (flags & NK_EDIT_COMMITED) || (old_len != _textedit_len(&handle->port_search_edit)) )
					handle->port_find_matches = true;
				if( (flags & NK_EDIT_ACTIVE) && handle->has_control_a)
					nk_textedit_select_all(&handle->port_search_edit);

				nk_check_label(ctx, "In", nk_true); //FIXME
				nk_check_label(ctx, "Out", nk_true); //FIXME
				nk_check_label(ctx, "Audio", nk_true); //FIXME
				nk_check_label(ctx, "Ctrl.", nk_true); //FIXME
				nk_check_label(ctx, "Event", nk_true); //FIXME
			}
			nk_menubar_end(ctx);

			mod_t *mod = handle->module_selector;
			if(mod)
			{
				const float DY = dy*2 + 6*ctx->style.window.group_padding.y + 2*ctx->style.window.group_border;

				_expose_control_list(handle, mod, ctx, DY, dy, handle->port_find_matches);
			}

			nk_group_end(ctx);
		}
	}
}

static void
_expose_main_footer(plughandle_t *handle, struct nk_context *ctx, float dy)
{
	const unsigned n_row = 1;

	nk_layout_row_dynamic(ctx, dy, n_row);
	{
		nk_label(ctx, "Synthpod: "SYNTHPOD_VERSION, NK_TEXT_RIGHT);
	}
}

static void
_init(plughandle_t *handle)
{
	handle->world = lilv_world_new();
	if(!handle->world)
		return;

	LilvNode *node_false = lilv_new_bool(handle->world, false);
	if(node_false)
	{
		lilv_world_set_option(handle->world, LILV_OPTION_DYN_MANIFEST, node_false);
		lilv_node_free(node_false);
	}
	lilv_world_load_all(handle->world);
	LilvNode *synthpod_bundle = lilv_new_file_uri(handle->world, NULL, SYNTHPOD_BUNDLE_DIR"/");
	if(synthpod_bundle)
	{
		lilv_world_load_bundle(handle->world, synthpod_bundle);
		lilv_node_free(synthpod_bundle);
	}

	handle->node.pg_group = lilv_new_uri(handle->world, LV2_PORT_GROUPS__group);
	handle->node.lv2_integer = lilv_new_uri(handle->world, LV2_CORE__integer);
	handle->node.lv2_toggled = lilv_new_uri(handle->world, LV2_CORE__toggled);
	handle->node.lv2_minimum = lilv_new_uri(handle->world, LV2_CORE__minimum);
	handle->node.lv2_maximum = lilv_new_uri(handle->world, LV2_CORE__maximum);
	handle->node.lv2_default = lilv_new_uri(handle->world, LV2_CORE__default);
	handle->node.pset_Preset = lilv_new_uri(handle->world, LV2_PRESETS__Preset);
	handle->node.pset_bank = lilv_new_uri(handle->world, LV2_PRESETS__bank);
	handle->node.rdfs_comment = lilv_new_uri(handle->world, LILV_NS_RDFS"comment");
	handle->node.rdfs_range = lilv_new_uri(handle->world, LILV_NS_RDFS"range");
	handle->node.doap_name = lilv_new_uri(handle->world, LILV_NS_DOAP"name");
	handle->node.lv2_minorVersion = lilv_new_uri(handle->world, LV2_CORE__minorVersion);
	handle->node.lv2_microVersion = lilv_new_uri(handle->world, LV2_CORE__microVersion);
	handle->node.doap_license = lilv_new_uri(handle->world, LILV_NS_DOAP"license");
	handle->node.rdfs_label = lilv_new_uri(handle->world, LILV_NS_RDFS"label");
	handle->node.lv2_name = lilv_new_uri(handle->world, LV2_CORE__name);
	handle->node.lv2_OutputPort = lilv_new_uri(handle->world, LV2_CORE__OutputPort);
	handle->node.lv2_AudioPort = lilv_new_uri(handle->world, LV2_CORE__AudioPort);
	handle->node.lv2_CVPort = lilv_new_uri(handle->world, LV2_CORE__CVPort);
	handle->node.lv2_ControlPort = lilv_new_uri(handle->world, LV2_CORE__ControlPort);
	handle->node.atom_AtomPort = lilv_new_uri(handle->world, LV2_ATOM__AtomPort);
	handle->node.patch_readable = lilv_new_uri(handle->world, LV2_PATCH__readable);
	handle->node.patch_writable = lilv_new_uri(handle->world, LV2_PATCH__writable);
	handle->node.rdf_type = lilv_new_uri(handle->world, LILV_NS_RDF"type");
	handle->node.lv2_Plugin = lilv_new_uri(handle->world, LV2_CORE__Plugin);

	handle->node.midi_MidiEvent = lilv_new_uri(handle->world, LV2_MIDI__MidiEvent);
	handle->node.osc_Message = lilv_new_uri(handle->world, LV2_OSC__Message);
	handle->node.time_Position = lilv_new_uri(handle->world, LV2_TIME__Position);
	handle->node.patch_Message = lilv_new_uri(handle->world, LV2_PATCH__Message);
	handle->node.xpress_Message = lilv_new_uri(handle->world, XPRESS_PREFIX"Message");

	sp_regs_init(&handle->regs, handle->world, handle->map);

	{
		lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
		if(synthpod_patcher_get(&handle->regs, &handle->forge,
			0, 0, handle->regs.synthpod.module_list.urid))
		{
			handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
			handle->regs.port.event_transfer.urid, &handle->atom);
		}
	}
}

static void
_deinit(plughandle_t *handle)
{
	if(handle->world)
	{
		sp_regs_deinit(&handle->regs);

		lilv_node_free(handle->node.pg_group);
		lilv_node_free(handle->node.lv2_integer);
		lilv_node_free(handle->node.lv2_toggled);
		lilv_node_free(handle->node.lv2_minimum);
		lilv_node_free(handle->node.lv2_maximum);
		lilv_node_free(handle->node.lv2_default);
		lilv_node_free(handle->node.pset_Preset);
		lilv_node_free(handle->node.pset_bank);
		lilv_node_free(handle->node.rdfs_comment);
		lilv_node_free(handle->node.rdfs_range);
		lilv_node_free(handle->node.doap_name);
		lilv_node_free(handle->node.lv2_minorVersion);
		lilv_node_free(handle->node.lv2_microVersion);
		lilv_node_free(handle->node.doap_license);
		lilv_node_free(handle->node.rdfs_label);
		lilv_node_free(handle->node.lv2_name);
		lilv_node_free(handle->node.lv2_OutputPort);
		lilv_node_free(handle->node.lv2_AudioPort);
		lilv_node_free(handle->node.lv2_CVPort);
		lilv_node_free(handle->node.lv2_ControlPort);
		lilv_node_free(handle->node.atom_AtomPort);
		lilv_node_free(handle->node.patch_readable);
		lilv_node_free(handle->node.patch_writable);
		lilv_node_free(handle->node.rdf_type);
		lilv_node_free(handle->node.lv2_Plugin);

		lilv_world_free(handle->world);
	}
}

static void
_expose(struct nk_context *ctx, struct nk_rect wbounds, void *data)
{
	plughandle_t *handle = data;

	handle->scale = nk_pugl_get_scale(&handle->win);
	handle->dy = 20.f * handle->scale;

	handle->has_control_a = nk_pugl_is_shortcut_pressed(&ctx->input, 'a', true);

	if(nk_begin(ctx, "synthpod", wbounds, NK_WINDOW_NO_SCROLLBAR))
	{
		nk_window_set_bounds(ctx, wbounds);

		// reduce group padding
		nk_style_push_vec2(ctx, &ctx->style.window.group_padding, nk_vec2(2.f, 2.f));

		if(handle->first)
		{
			nk_layout_row_dynamic(ctx, wbounds.h, 1);
			nk_label(ctx, "loading ...", NK_TEXT_CENTERED);

			_init(handle);

			nk_pugl_post_redisplay(&handle->win);
			handle->first = false;
		}
		else
		{
			const float w_padding = ctx->style.window.padding.y;
			const float dy = handle->dy;
			const unsigned n_paddings = 4;
			const float dh = nk_window_get_height(ctx)
				- n_paddings*w_padding - (n_paddings - 2)*dy;

			_expose_main_header(handle, ctx, dy);
			_expose_main_body(handle, ctx, dh, dy);
			_expose_main_footer(handle, ctx, dy);
		}

		nk_style_pop_vec2(ctx);
	}
	nk_end(ctx);
}

static LV2UI_Handle
instantiate(const LV2UI_Descriptor *descriptor, const char *plugin_uri,
	const char *bundle_path, LV2UI_Write_Function write_function,
	LV2UI_Controller controller, LV2UI_Widget *widget,
	const LV2_Feature *const *features)
{
	plughandle_t *handle = calloc(1, sizeof(plughandle_t));
	if(!handle)
		return NULL;

	void *parent = NULL;
	LV2UI_Resize *host_resize = NULL;
	for(int i=0; features[i]; i++)
	{
		if(!strcmp(features[i]->URI, LV2_UI__parent))
			parent = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_UI__resize))
			host_resize = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__map))
			handle->map = features[i]->data;
		else if(!strcmp(features[i]->URI, LV2_URID__unmap))
			handle->unmap = features[i]->data;
	}

	if(!parent)
	{
		fprintf(stderr,
			"%s: Host does not support ui:parent\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->map)
	{
		fprintf(stderr,
			"%s: Host does not support urid:map\n", descriptor->URI);
		free(handle);
		return NULL;
	}
	if(!handle->unmap)
	{
		fprintf(stderr,
			"%s: Host does not support urid:unmap\n", descriptor->URI);
		free(handle);
		return NULL;
	}

	lv2_atom_forge_init(&handle->forge, handle->map);

	handle->atom_eventTransfer = handle->map->map(handle->map->handle, LV2_ATOM__eventTransfer);

	handle->controller = controller;
	handle->writer = write_function;

	nk_pugl_config_t *cfg = &handle->win.cfg;
	cfg->width = 1280;
	cfg->height = 720;
	cfg->resizable = true;
	cfg->ignore = false;
	cfg->class = "synthpod";
	cfg->title = "Synthpod";
	cfg->parent = (intptr_t)parent;
	cfg->host_resize = host_resize;
	cfg->data = handle;
	cfg->expose = _expose;

	if(asprintf(&cfg->font.face, "%sCousine-Regular.ttf", bundle_path) == -1)
		cfg->font.face = NULL;
	cfg->font.size = 13;

	*(intptr_t *)widget = nk_pugl_init(&handle->win);
	nk_pugl_show(&handle->win);

	// adjust styling
	struct nk_style *style = &handle->win.ctx.style;
	style->button.border_color = button_border_color;
	//TODO more styling changes to come here

	handle->scale = nk_pugl_get_scale(&handle->win);

	handle->scrolling = nk_vec2(0.f, 0.f);

	handle->plugin_collapse_states = NK_MAXIMIZED;
	handle->preset_import_collapse_states = NK_MAXIMIZED;
	handle->preset_export_collapse_states = NK_MINIMIZED;
	handle->plugin_info_collapse_states = NK_MINIMIZED;
	handle->preset_info_collapse_states = NK_MINIMIZED;

	nk_textedit_init_fixed(&handle->plugin_search_edit, handle->plugin_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->preset_search_edit, handle->preset_search_buf, SEARCH_BUF_MAX);
	nk_textedit_init_fixed(&handle->port_search_edit, handle->port_search_buf, SEARCH_BUF_MAX);

	handle->first = true;

	handle->type = PROPERTY_TYPE_AUDIO; //FIXME make configurable

	return handle;
}

static void
cleanup(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	if(handle->win.cfg.font.face)
		free(handle->win.cfg.font.face);
	nk_pugl_hide(&handle->win);
	nk_pugl_shutdown(&handle->win);

	HASH_FREE(&handle->mods, ptr)
	{
		mod_t *mod = ptr;
		_mod_free(handle, mod);
	}

	HASH_FREE(&handle->conns, ptr)
	{
		mod_conn_t *mod_conn = ptr;
		_mod_conn_free(handle, mod_conn);
	}

	_hash_free(&handle->plugin_matches);
	_hash_free(&handle->preset_matches);
	_hash_free(&handle->port_matches);

	_deinit(handle);

	free(handle);
}

static void
port_event(LV2UI_Handle instance, uint32_t port_index, uint32_t size,
	uint32_t format, const void *buffer)
{
	plughandle_t *handle = instance;

	if(port_index == 15) // notify
	{
		if(format == handle->regs.port.event_transfer.urid)
		{
			const LV2_Atom_Object *obj = buffer;

			if(lv2_atom_forge_is_object_type(&handle->forge, obj->atom.type))
			{
				if(obj->body.otype == handle->regs.patch.set.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_URID *property = NULL;
					const LV2_Atom *value = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.property.urid, &property,
						handle->regs.patch.value.urid, &value,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0;
					const LV2_URID prop = property && (property->atom.type == handle->forge.URID)
						? property->body
						: 0;

					if(prop && value)
					{
						printf("got patch:Set: %s\n", handle->unmap->unmap(handle->unmap->handle, prop));

						if(prop == handle->regs.synthpod.module_list.urid)
						{
							const LV2_Atom_Tuple *tup = (const LV2_Atom_Tuple *)value;

							handle->nxt = nk_vec2(115.f * handle->scale, 50.f * handle->scale);

							HASH_FREE(&handle->mods, ptr)
							{
								mod_t *mod = ptr;
								_mod_free(handle, mod);
							}

							LV2_ATOM_TUPLE_FOREACH(tup, itm)
							{
								const LV2_Atom_URID *urid = (const LV2_Atom_URID *)itm;
								const char *urn = handle->unmap->unmap(handle->unmap->handle, urid->body);
								printf("<- %s\n", urn);

								// get information for each of those, FIXME only if not already available
								{
									lv2_atom_forge_set_buffer(&handle->forge, handle->buf, ATOM_BUF_MAX);
									if(synthpod_patcher_get(&handle->regs, &handle->forge,
										urid->body, 0, 0))
									{
										handle->writer(handle->controller, CONTROL, lv2_atom_total_size(&handle->atom),
										handle->regs.port.event_transfer.urid, &handle->atom);
									}
								}

								_mod_add(handle, urid->body);
							}
							//TODO
						}
					}
				}
				else if(obj->body.otype == handle->regs.patch.put.urid)
				{
					const LV2_Atom_URID *subject = NULL;
					const LV2_Atom_Object *body = NULL;

					lv2_atom_object_get(obj,
						handle->regs.patch.subject.urid, &subject,
						handle->regs.patch.body.urid, &body,
						0);

					const LV2_URID subj = subject && (subject->atom.type == handle->forge.URID)
						? subject->body
						: 0;

					if(subj && body)
					{
						printf("got patch:Put for %u\n", subj);

						const LV2_Atom_URID *plugin = NULL;

						lv2_atom_object_get(body,
							handle->regs.core.plugin.urid, &plugin,
							0); //FIXME query more

						const LV2_URID urid = plugin
							? plugin->body
							: 0;

						const char *uri = urid
							? handle->unmap->unmap(handle->unmap->handle, urid)
							: NULL;

						mod_t *mod = _mod_find_by_subject(handle, subj);
						if(mod && uri)
						{
							LilvNode *uri_node = lilv_new_uri(handle->world, uri);
							const LilvPlugin *plug = NULL;

							if(uri_node)
							{
								const LilvPlugins *plugs = lilv_world_get_all_plugins(handle->world);
								plug = lilv_plugins_get_by_uri(plugs, uri_node);
								lilv_node_free(uri_node);
							}

							if(plug)
								_mod_init(handle, mod, plug);
						}
					}
					//TODO
				}
			}
		}
	}
}

static int
_idle(LV2UI_Handle instance)
{
	plughandle_t *handle = instance;

	return nk_pugl_process_events(&handle->win);
}

static const LV2UI_Idle_Interface idle_ext = {
	.idle = _idle
};

static const void *
extension_data(const char *uri)
{
	if(!strcmp(uri, LV2_UI__idleInterface))
		return &idle_ext;

	return NULL;
}

const LV2UI_Descriptor synthpod_common_4_nk = {
	.URI						= SYNTHPOD_COMMON_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};

const LV2UI_Descriptor synthpod_root_4_nk = {
	.URI						= SYNTHPOD_ROOT_NK_URI,
	.instantiate		= instantiate,
	.cleanup				= cleanup,
	.port_event			= port_event,
	.extension_data	= extension_data
};
