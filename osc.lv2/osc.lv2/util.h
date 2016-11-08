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

#ifndef LV2_OSC_UTIL_H
#define LV2_OSC_UTIL_H

#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>

#include <osc.lv2/osc.h>

#include <lv2/lv2plug.in/ns/ext/atom/util.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef LV2_ATOM_TUPLE_FOREACH // there is a bug in LV2 1.10.0
#define LV2_ATOM_TUPLE_FOREACH(tuple, iter) \
	for (LV2_Atom* (iter) = lv2_atom_tuple_begin(tuple); \
	     !lv2_atom_tuple_is_end(LV2_ATOM_BODY(tuple), (tuple)->atom.size, (iter)); \
	     (iter) = lv2_atom_tuple_next(iter))

typedef void (*LV2_OSC_Method)(const char *path,
	const LV2_Atom_Tuple *arguments, void *data);

// characters not allowed in OSC path string
static const char invalid_path_chars [] = {
	' ', '#',
	'\0'
};

// allowed characters in OSC format string
static const char valid_format_chars [] = {
	LV2_OSC_INT32, LV2_OSC_FLOAT, LV2_OSC_STRING, LV2_OSC_BLOB,
	LV2_OSC_TRUE, LV2_OSC_FALSE, LV2_OSC_NIL, LV2_OSC_IMPULSE,
	LV2_OSC_INT64, LV2_OSC_DOUBLE, LV2_OSC_TIMETAG,
	LV2_OSC_SYMBOL, LV2_OSC_MIDI,
	'\0'
};

/**
   TODO
*/
static inline bool
lv2_osc_check_path(const char *path)
{
	assert(path);

	if(path[0] != '/')
		return false;

	for(const char *ptr=path+1; *ptr!='\0'; ptr++)
		if( (isprint(*ptr) == 0) || (strchr(invalid_path_chars, *ptr) != NULL) )
			return false;

	return true;
}

/**
   TODO
*/
static inline bool
lv2_osc_check_fmt(const char *format, int offset)
{
	assert(format);

	if(offset && (format[0] != ',') )
		return false;

	for(const char *ptr=format+offset; *ptr!='\0'; ptr++)
		if(strchr(valid_format_chars, *ptr) == NULL)
			return false;

	return true;
}

/**
   TODO
*/
static inline uint64_t 
lv2_osc_timetag_parse(const LV2_OSC_Timetag *timetag)
{
	return ((uint64_t)timetag->integral << 32) | timetag->fraction;
}

/**
   TODO
*/
static inline LV2_OSC_Timetag *
lv2_osc_timetag_create(LV2_OSC_Timetag *timetag, uint64_t tt)
{
	timetag->integral = tt >> 32;
	timetag->fraction = tt & 0xffffffff;

	return timetag;
}

#define LV2_OSC_TIMETAG_CREATE(tt) \
	lv2_osc_timetag_create(&(LV2_OSC_Timetag){.integral = 0, .fraction = 0}, (tt))

/**
   TODO
*/
static inline bool
lv2_osc_is_packet_type(LV2_OSC_URID *osc_urid, LV2_URID type)
{
	return type == osc_urid->OSC_Packet;
}

/**
   TODO
*/
static inline bool
lv2_osc_is_bundle_type(LV2_OSC_URID *osc_urid, LV2_URID type)
{
	return type == osc_urid->OSC_Bundle;
}

/**
   TODO
*/
static inline bool
lv2_osc_is_message_type(LV2_OSC_URID *osc_urid, LV2_URID type)
{
	return type == osc_urid->OSC_Message;
}

/**
   TODO
*/
static inline bool
lv2_osc_is_message_or_bundle_type(LV2_OSC_URID *osc_urid, LV2_URID type)
{
	return lv2_osc_is_message_type(osc_urid, type)
		|| lv2_osc_is_bundle_type(osc_urid, type);
}

static inline LV2_OSC_Type
lv2_osc_argument_type(LV2_OSC_URID *osc_urid, const LV2_Atom *atom)
{
	const LV2_Atom_Object *obj = (const LV2_Atom_Object *)atom;

	if(atom->type == osc_urid->ATOM_Int)
		return LV2_OSC_INT32;
	else if(atom->type == osc_urid->ATOM_Float)
		return LV2_OSC_FLOAT;
	else if(atom->type == osc_urid->ATOM_String)
		return LV2_OSC_STRING;
	else if(atom->type == osc_urid->ATOM_Chunk)
		return LV2_OSC_BLOB;

	else if(atom->type == osc_urid->ATOM_Long)
		return LV2_OSC_INT64;
	else if(atom->type == osc_urid->ATOM_Double)
		return LV2_OSC_DOUBLE;
	else if( (atom->type == osc_urid->ATOM_Object) && (obj->body.otype == osc_urid->OSC_Timetag) )
		return LV2_OSC_TIMETAG;

	else if(atom->type == osc_urid->ATOM_Bool)
	{
		if(((const LV2_Atom_Bool *)atom)->body)
			return LV2_OSC_TRUE;
		else
			return LV2_OSC_FALSE;
	}
	else if(atom->type == osc_urid->ATOM_Literal)
	{
		const LV2_Atom_Literal *lit = (const LV2_Atom_Literal *)atom;
		if(lit->body.datatype == osc_urid->OSC_Nil)
			return LV2_OSC_NIL;
		else if(lit->body.datatype == osc_urid->OSC_Impulse)
			return LV2_OSC_IMPULSE;
		else if(lit->body.datatype == osc_urid->OSC_Char)
			return LV2_OSC_CHAR;
		else if(lit->body.datatype == osc_urid->OSC_RGBA)
			return LV2_OSC_RGBA;
	}

	else if(atom->type == osc_urid->ATOM_URID)
		return LV2_OSC_SYMBOL;
	else if(atom->type == osc_urid->MIDI_MidiEvent)
		return LV2_OSC_MIDI;

	return '\0';
}

static inline const LV2_Atom *
lv2_osc_int32_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, int32_t *i)
{
	assert(i);
	*i = ((const LV2_Atom_Int *)atom)->body;

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_float_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, float *f)
{
	assert(f);
	*f = ((const LV2_Atom_Float *)atom)->body;

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_string_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, const char **s)
{
	assert(s);
	*s = LV2_ATOM_BODY_CONST(atom);

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_blob_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, uint32_t *size,
	const uint8_t **b)
{
	assert(size && b);
	*size = atom->size;
	*b = LV2_ATOM_BODY_CONST(atom);

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_int64_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, int64_t *h)
{
	assert(h);
	*h = ((const LV2_Atom_Long *)atom)->body;

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_double_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, double *d)
{
	assert(d);
	*d = ((const LV2_Atom_Double *)atom)->body;

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom * 
lv2_osc_timetag_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom,
	LV2_OSC_Timetag *timetag)
{
	assert(timetag);

	const LV2_Atom_Long *integral = NULL;
	const LV2_Atom_Long *fraction = NULL;

	lv2_atom_object_get((const LV2_Atom_Object *)atom,
		osc_urid->OSC_timetagIntegral, &integral,
		osc_urid->OSC_timetagFraction, &fraction, 
		0);

	if(  integral && (integral->atom.type == osc_urid->ATOM_Long)
		&& fraction && (fraction->atom.type == osc_urid->ATOM_Long) )
	{
		timetag->integral = integral->body;
		timetag->fraction = fraction->body;
	}
	else
	{
		// set to immediate
		timetag->integral = 0;
		timetag->fraction = 1;
	}

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_true_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom)
{
	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_false_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom)
{
	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_nil_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom)
{
	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_impulse_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom)
{
	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_symbol_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, const char **s)
{
	assert(s);
	*s = LV2_ATOM_BODY_CONST(atom);

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_midi_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, uint32_t *size,
	const uint8_t **m)
{
	assert(size && m);
	*size = atom->size;
	*m = LV2_ATOM_BODY_CONST(atom);

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_char_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom, char *c)
{
	assert(c);
	const char *str = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Literal, atom);
	*c = str[0];

	return lv2_atom_tuple_next(atom);
}

static inline const LV2_Atom *
lv2_osc_rgba_get(LV2_OSC_URID *osc_urid, const LV2_Atom *atom,
	uint8_t *r, uint8_t *g, uint8_t *b, uint8_t *a)
{
	assert(r && g && b && a);
	const char *str = LV2_ATOM_CONTENTS_CONST(LV2_Atom_Literal, atom);
	sscanf(str, "%02"SCNx8"%02"SCNx8"%02"SCNx8"%02"SCNx8, r, g, b, a);

	return lv2_atom_tuple_next(atom);
}

/**
   TODO
*/
static inline bool
lv2_osc_bundle_body_get(LV2_OSC_URID *osc_urid, uint32_t size, const LV2_Atom_Object_Body *body,
	const LV2_Atom_Object **timetag, const LV2_Atom_Tuple **items)
{
	assert(timetag && items);

	*timetag = NULL;
	*items = NULL;

	lv2_atom_object_body_get(size, body,
		osc_urid->OSC_bundleTimetag, timetag,
		osc_urid->OSC_bundleItems, items, 
		0);

	if(!*timetag || ((*timetag)->atom.type != osc_urid->ATOM_Object) || ((*timetag)->body.otype != osc_urid->OSC_Timetag))
		return false;
	if(!*items || ((*items)->atom.type != osc_urid->ATOM_Tuple))
		return false;

	return true;
}

/**
   TODO
*/
static inline bool
lv2_osc_bundle_get(LV2_OSC_URID *osc_urid, const LV2_Atom_Object *obj,
	const LV2_Atom_Object **timetag, const LV2_Atom_Tuple **items)
{
	return lv2_osc_bundle_body_get(osc_urid, obj->atom.size, &obj->body,
		timetag, items);
}

/**
   TODO
*/
static inline bool
lv2_osc_message_body_get(LV2_OSC_URID *osc_urid, uint32_t size, const LV2_Atom_Object_Body *body,
	const LV2_Atom_String **path, const LV2_Atom_Tuple **arguments)
{
	assert(path && arguments);

	*path = NULL;
	*arguments = NULL;

	lv2_atom_object_body_get(size, body,
		osc_urid->OSC_messagePath, path,
		osc_urid->OSC_messageArguments, arguments,
		0);

	if(!*path || ((*path)->atom.type != osc_urid->ATOM_String))
		return false;
	// message without arguments is valid
	if( *arguments && ((*arguments)->atom.type != osc_urid->ATOM_Tuple))
		return false;

	return true;
}

/**
   TODO
*/
static inline bool
lv2_osc_message_get(LV2_OSC_URID *osc_urid, const LV2_Atom_Object *obj,
	const LV2_Atom_String **path, const LV2_Atom_Tuple **arguments)
{
	return lv2_osc_message_body_get(osc_urid, obj->atom.size, &obj->body,
		path, arguments);
}

static inline bool
lv2_osc_body_unroll(LV2_OSC_URID *osc_urid, uint32_t size, const LV2_Atom_Object_Body *body,
	LV2_OSC_Method method, void *data)
{
	if(body->otype == osc_urid->OSC_Bundle)
	{
		const LV2_Atom_Object *timetag = NULL;
		const LV2_Atom_Tuple *items = NULL;

		if(!lv2_osc_bundle_body_get(osc_urid, size, body, &timetag, &items))
			return false;

		LV2_OSC_Timetag tt;
		lv2_osc_timetag_get(osc_urid, &timetag->atom, &tt);

		LV2_ATOM_TUPLE_FOREACH(items, atom)
		{
			const LV2_Atom_Object *obj= (const LV2_Atom_Object *)atom;

			if(!lv2_osc_body_unroll(osc_urid, obj->atom.size, &obj->body, method, data))
				return false;
		}

		return true;
	}
	else if(body->otype == osc_urid->OSC_Message)
	{
		const LV2_Atom_String *path = NULL;
		const LV2_Atom_Tuple *arguments = NULL;

		if(!lv2_osc_message_body_get(osc_urid, size, body, &path, &arguments))
			return false;

		if(method)
			method(LV2_ATOM_BODY_CONST(path), arguments, data);

		return true;
	}

	return false;
}

static inline bool
lv2_osc_unroll(LV2_OSC_URID *osc_urid, const LV2_Atom_Object *obj,
	LV2_OSC_Method method, void *data)
{
	return lv2_osc_body_unroll(osc_urid, obj->atom.size, &obj->body, method, data);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif // LV2_OSC_UTIL_H
