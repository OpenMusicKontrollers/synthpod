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

#include <synthpod_ui_private.h>

void
_modlist_refresh(sp_ui_t *ui)
{
	// request module list
	{
		const size_t size = sizeof(transmit_module_list_t);
		transmit_module_list_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_module_list_fill(&ui->regs, &ui->forge, trans, size);
			_sp_ui_to_app_advance(ui, size);
		}
	}

	// request grid cols
	{
		const size_t size = sizeof(transmit_grid_cols_t);
		transmit_grid_cols_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_grid_cols_fill(&ui->regs, &ui->forge, trans, size, -1);
			_sp_ui_to_app_advance(ui, size);
		}
	}

	// request grid rows
	{
		const size_t size = sizeof(transmit_grid_rows_t);
		transmit_grid_rows_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_grid_rows_fill(&ui->regs, &ui->forge, trans, size, -1);
			_sp_ui_to_app_advance(ui, size);
		}
	}

	// request pane left 
	{
		const size_t size = sizeof(transmit_pane_left_t);
		transmit_pane_left_t *trans = _sp_ui_to_app_request(ui, size);
		if(trans)
		{
			_sp_transmit_pane_left_fill(&ui->regs, &ui->forge, trans, size, -1.f);
			_sp_ui_to_app_advance(ui, size);
		}
	}
}

static void
_mod_embedded_set(mod_t *mod, int state)
{
	sp_ui_t *ui = mod->ui;

	// set module embedded state
	const size_t size = sizeof(transmit_module_embedded_t);
	transmit_module_embedded_t *trans1 = _sp_ui_to_app_request(ui, size);
	if(trans1)
	{
		_sp_transmit_module_embedded_fill(&ui->regs, &ui->forge, trans1, size, mod->uid, state);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_modlist_activated(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	const Elm_Genlist_Item_Class *class = elm_genlist_item_item_class_get(itm);

	if(class == ui->listitc)
	{
		mod_t *mod = elm_object_item_data_get(itm);

		if(mod->std.grid)
		{
			elm_object_item_del(mod->std.grid);
			_mod_embedded_set(mod, 0);
		}
		else
		{
			mod->std.grid = elm_gengrid_item_append(ui->modgrid, ui->griditc, mod,
				NULL, NULL);
			_mod_embedded_set(mod, 1);
		}
	}
}

// only called upon user interaction
static void
_modlist_moved(void *data, Evas_Object *obj, void *event_info)
{
	Elm_Object_Item *itm = event_info;
	sp_ui_t *ui = data;

	Elm_Object_Item *first = elm_genlist_first_item_get(obj);
	Elm_Object_Item *last = elm_genlist_last_item_get(obj);

	if(!first || !last)
		return;

	// we must not move mod to top or end of list
	if(itm == first)
	{
		// promote system source to top of list
		Elm_Object_Item *source = elm_genlist_item_next_get(itm);
		if(source)
			elm_genlist_item_promote(source); // does not call _modlist_moved
	}
	else if(itm == last)
	{
		// demote system sink to end of list
		Elm_Object_Item *sink = elm_genlist_item_prev_get(itm);
		if(sink)
			elm_genlist_item_demote(sink); // does not call _modlist_moved
	}

	// get previous item
	Elm_Object_Item *prev = elm_genlist_item_prev_get(itm);
	if(!prev)
		return;

	mod_t *itm_mod = elm_object_item_data_get(itm);
	mod_t *prev_mod = elm_object_item_data_get(prev);

	if(!itm_mod || !prev_mod)
		return;

	// signal app
	size_t size = sizeof(transmit_module_move_t);
	transmit_module_move_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_move_fill(&ui->regs, &ui->forge, trans, size,
			itm_mod->uid, prev_mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}

	_patches_update(ui);
}

static void
_mod_link_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	mod->selected ^= 1; // toggle
	elm_layout_signal_emit(lay, mod->selected ? "link,on" : "link,off", "");

	_patches_update(ui);

	// signal app
	size_t size = sizeof(transmit_module_selected_t);
	transmit_module_selected_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_selected_fill(&ui->regs, &ui->forge, trans, size, mod->uid, mod->selected);
		_sp_ui_to_app_advance(ui, size);
	}
}

void
_mod_ui_toggle_raw(mod_t *mod, mod_ui_t *mod_ui)
{
	mod->mod_ui = mod_ui;

	mod_ui->driver->show(mod);
}

static void
_mod_ui_toggle_chosen(void *data, Evas_Object *obj, void *event_info)
{
	mod_ui_t *mod_ui = data;
	mod_t *mod = mod_ui->mod;

	_mod_ui_toggle_raw(mod, mod_ui);
}

static void
_mod_ui_toggle(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;
	int x, y, w, h;

	evas_object_geometry_get(lay, &x, &y, &w, &h);

	Elm_Object_Item *itm;
	while((itm = elm_menu_first_item_get(ui->uimenu)))
		elm_object_item_del(itm);

	if(!mod->mod_ui) // show it!
	{
		Eina_List *l;
		mod_ui_t *mod_ui;
		if(eina_list_count(mod->mod_uis) == 1) // single UI
		{
			mod_ui = eina_list_data_get(mod->mod_uis);
			_mod_ui_toggle_chosen(mod_ui, ui->uimenu, NULL);
		}
		else if(eina_list_count(mod->mod_uis) > 1) // multiple UIs
		{
			EINA_LIST_FOREACH(mod->mod_uis, l, mod_ui)
			{
				const LilvNode *ui_uri = lilv_ui_get_uri(mod_ui->ui);
				const char *ui_uri_str = lilv_node_as_string(ui_uri);

				const char *ui_uri_fmt = (mod_ui->driver == &sbox_ui_driver)
					? "%s (sandboxed)"
					: "%s (native)";

				char *ui_uri_ptr = NULL;
				if(asprintf(&ui_uri_ptr, ui_uri_fmt, ui_uri_str) != -1)
					ui_uri_str = ui_uri_ptr;
				else
					ui_uri_ptr = NULL;

				elm_menu_item_add(ui->uimenu, NULL, NULL, ui_uri_str, _mod_ui_toggle_chosen, mod_ui);

				if(ui_uri_ptr)
					free(ui_uri_ptr);
			}
			elm_menu_move(ui->uimenu, x+w, y+h);
			evas_object_show(ui->uimenu);
		}
	}
	else // hide it!
	{
		mod->mod_ui->driver->hide(mod);
	}
}

static inline void
_mod_del_propagate(mod_t *mod)
{
	sp_ui_t *ui = mod->ui;

	size_t size = sizeof(transmit_module_del_t);
	transmit_module_del_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_del_fill(&ui->regs, &ui->forge, trans, size, mod->uid);
		_sp_ui_to_app_advance(ui, size);
	}
}

static void
_mod_close_click(void *data, Evas_Object *lay, const char *emission, const char *source)
{
	mod_t *mod = data;

	_mod_del_widgets(mod);
	_mod_del_propagate(mod);
}

static Evas_Object *
_modlist_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	/* FIXME implement me!
	Evas_Object *frame = elm_frame_add(obj);
	if(frame)
	{
		elm_object_text_set(frame, mod->name);
		evas_object_size_hint_weight_set(frame, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(frame, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_size_hint_min_set(frame, ELM_SCALE_SIZE(50), ELM_SCALE_SIZE(50));
		evas_object_show(frame);

		Evas_Object *hbox = elm_box_add(frame);
		if(hbox)
		{
			elm_box_homogeneous_set(hbox, EINA_TRUE);
			elm_box_horizontal_set(hbox, EINA_TRUE);
			evas_object_size_hint_weight_set(hbox, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
			evas_object_size_hint_align_set(hbox, EVAS_HINT_FILL, EVAS_HINT_FILL);
			evas_object_show(hbox);
			elm_object_content_set(frame, hbox);

			Evas_Object *lbl = elm_label_add(hbox);
			if(lbl)
			{
				elm_object_text_set(lbl, "<font=mono align=right>0.1%</font>");
				evas_object_size_hint_weight_set(lbl, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(lbl, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(lbl);
				elm_box_pack_end(hbox, lbl);
			}
		}
	}

	return frame;
	*/

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/modlist/module");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);

		elm_layout_text_set(lay, "elm.text", mod->name);

		char col [7];
		sprintf(col, "col,%02i", mod->col);
		elm_layout_signal_emit(lay, col, MODLIST_UI);

		// link
		elm_layout_signal_callback_add(lay, "link,toggle", "", _mod_link_toggle, mod);
		elm_layout_signal_emit(lay, mod->selected ? "link,on" : "link,off", "");

		// close
		if(!mod->system.source && !mod->system.sink)
		{
			elm_layout_signal_callback_add(lay, "close,click", "", _mod_close_click, mod);
			elm_layout_signal_emit(lay, "close,show", "");
		}
		else
		{
			// system mods cannot be removed
			elm_layout_signal_emit(lay, "close,hide", "");
		}

		// window
		//if(mod->show.ui || mod->kx.ui || mod->eo.ui || mod->x11.ui) //TODO also check for descriptor
		if(eina_list_count(mod->mod_uis) > 0)
		{
			elm_layout_signal_callback_add(lay, "ui,toggle", "", _mod_ui_toggle, mod);
			elm_layout_signal_emit(lay, "ui,show", "");
		}
		else
		{
			elm_layout_signal_emit(lay, "ui,hide", "");
		}
	} // lay

	return lay;
}

void
_modlist_clear(sp_ui_t *ui, bool clear_system_ports, bool propagate)
{
	if(!ui || !ui->modlist)
		return;

	// iterate over all registered modules
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(ui->modlist);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->listitc) // is not a parent mod item 
			continue; // skip 

		mod_t *mod = elm_object_item_data_get(itm);

		if(!clear_system_ports && (mod->system.source || mod->system.sink) )
			continue; // skip

		_mod_del_widgets(mod);
		if(propagate)
			_mod_del_propagate(mod);
	}
}

static Evas_Object * 
_modlist_psets_content_get(void *data, Evas_Object *obj, const char *part)
{
	if(!data) // mepty item
		return NULL;

	mod_t *mod = data;

	if(strcmp(part, "elm.swallow.content"))
		return NULL;

	Evas_Object *lay = elm_layout_add(obj);
	if(lay)
	{
		elm_layout_file_set(lay, SYNTHPOD_DATA_DIR"/synthpod.edj",
			"/synthpod/group/theme");
		char col [7];
		sprintf(col, "col,%02i", mod->col);
		elm_layout_signal_emit(lay, col, "/synthpod/group/ui");
		elm_object_part_text_set(lay, "elm.text", "Presets");
		evas_object_size_hint_weight_set(lay, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
		evas_object_size_hint_align_set(lay, EVAS_HINT_FILL, EVAS_HINT_FILL);
		evas_object_show(lay);
	}

	return lay;
}

static char * 
_modlist_bank_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* bank = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		//lilv_world_load_resource(ui->world, bank); //FIXME
		LilvNode *label = lilv_world_get(ui->world, bank,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}
		//lilv_world_unload_resource(ui->world, bank); //FIXME

		return lbl;
	}

	return NULL;
}

static char * 
_modlist_pset_label_get(void *data, Evas_Object *obj, const char *part)
{
	const LilvNode* preset = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.text"))
	{
		char *lbl = NULL;

		//lilv_world_load_resource(ui->world, preset); //FIXME
		LilvNode *label = lilv_world_get(ui->world, preset,
			ui->regs.rdfs.label.node, NULL);
		if(label)
		{
			const char *label_str = lilv_node_as_string(label);
			if(label_str)
				lbl = strdup(label_str);
			lilv_node_free(label);
		}
		//lilv_world_unload_resource(ui->world, preset); //FIXME

		return lbl;
	}

	return NULL;
}

static void
_pset_markup(void *data, Evas_Object *obj, char **txt)
{
	// intercept enter
	if(!strcmp(*txt, "<tab/>") || !strcmp(*txt, " "))
	{
		free(*txt);
		*txt = strdup("_"); //TODO check
	}
}

static void
_pset_changed(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;

	const char *chunk = elm_entry_entry_get(obj);
	char *utf8 = elm_entry_markup_to_utf8(chunk);

	if(mod->pset_label)
		free(mod->pset_label);

	mod->pset_label = strdup(utf8); //TODO check
	free(utf8);
}

static void
_pset_clicked(void *data, Evas_Object *obj, void *event_info)
{
	mod_t *mod = data;
	sp_ui_t *ui = mod->ui;

	if(!mod->pset_label)
		return;

	// signal app
	size_t size = sizeof(transmit_module_preset_save_t)
		+ lv2_atom_pad_size(strlen(mod->pset_label) + 1);
	transmit_module_preset_save_t *trans = _sp_ui_to_app_request(ui, size);
	if(trans)
	{
		_sp_transmit_module_preset_save_fill(&ui->regs, &ui->forge, trans, size, mod->uid, mod->pset_label);
		_sp_ui_to_app_advance(ui, size);
	}

	// reset pset_label
	free(mod->pset_label);
	mod->pset_label = strdup("unknown"); //TODO check

	// contract parent list item
	for(Elm_Object_Item *itm = elm_genlist_first_item_get(mod->std.list);
		itm != NULL;
		itm = elm_genlist_item_next_get(itm))
	{
		const Elm_Genlist_Item_Class *itc = elm_genlist_item_item_class_get(itm);
		if(itc != ui->psetitc) // is not a parent preset item
			continue; // skip 

		if(elm_object_item_data_get(itm) != mod) // does not belong to this module
			continue; // skip

		evas_object_smart_callback_call(mod->std.list, "contract,request", itm);
		break;
	}
}

static Evas_Object * 
_modlist_pset_content_get(void *data, Evas_Object *obj, const char *part)
{
	mod_t *mod = data;
	sp_ui_t *ui = evas_object_data_get(obj, "ui");
	if(!ui)
		return NULL;

	if(!strcmp(part, "elm.swallow.content"))
	{
		Evas_Object *hbox = elm_box_add(obj);
		if(hbox)
		{
			elm_box_horizontal_set(hbox, EINA_TRUE);
			elm_box_homogeneous_set(hbox, EINA_FALSE);
			elm_box_padding_set(hbox, 5, 0);
			evas_object_show(hbox);

			Evas_Object *entry = elm_entry_add(hbox);
			if(entry)
			{
				elm_entry_single_line_set(entry, EINA_TRUE);
				elm_entry_entry_set(entry, mod->pset_label);
				elm_entry_editable_set(entry, EINA_TRUE);
				elm_entry_markup_filter_append(entry, _pset_markup, mod);
				evas_object_smart_callback_add(entry, "changed,user", _pset_changed, mod);
				evas_object_size_hint_weight_set(entry, EVAS_HINT_EXPAND, EVAS_HINT_EXPAND);
				evas_object_size_hint_align_set(entry, EVAS_HINT_FILL, EVAS_HINT_FILL);
				evas_object_show(entry);
				elm_box_pack_end(hbox, entry);
			}

			Evas_Object *but = elm_button_add(hbox);
			if(but)
			{
				elm_object_text_set(but, "+");
				evas_object_smart_callback_add(but, "clicked", _pset_clicked, mod);
				evas_object_size_hint_align_set(but, 0.f, EVAS_HINT_FILL);
				evas_object_show(but);
				elm_box_pack_start(hbox, but);
			}
		}

		return hbox;
	}

	return NULL;
}

static void
_modlist_del(void *data, Evas_Object *obj)
{
	mod_t *mod = data;
	mod_ui_t *mod_ui = mod->mod_ui;
	sp_ui_t *ui = mod->ui;

	if(mod_ui)
		mod_ui->driver->hide(mod);

	_sp_ui_mod_del(ui, mod);
}

void
_modlist_itc_add(sp_ui_t *ui)
{
	ui->listitc = elm_genlist_item_class_new();
	if(ui->listitc)
	{
		ui->listitc->item_style = "full";
		ui->listitc->func.text_get = NULL;
		ui->listitc->func.content_get = _modlist_content_get;
		ui->listitc->func.state_get = NULL;
		ui->listitc->func.del = _modlist_del;
	}

	ui->psetbnkitc = elm_genlist_item_class_new();
	if(ui->psetbnkitc)
	{
		ui->psetbnkitc->item_style = "default";
		ui->psetbnkitc->func.text_get = _modlist_bank_label_get;
		ui->psetbnkitc->func.content_get = NULL;
		ui->psetbnkitc->func.state_get = NULL;
		ui->psetbnkitc->func.del = NULL;
	}

	ui->psetitmitc = elm_genlist_item_class_new();
	if(ui->psetitmitc)
	{
		ui->psetitmitc->item_style = "default";
		ui->psetitmitc->func.text_get = _modlist_pset_label_get;
		ui->psetitmitc->func.content_get = NULL;
		ui->psetitmitc->func.state_get = NULL;
		ui->psetitmitc->func.del = NULL;
	}

	ui->psetsaveitc = elm_genlist_item_class_new();
	if(ui->psetsaveitc)
	{
		ui->psetsaveitc->item_style = "full";
		ui->psetsaveitc->func.text_get = NULL;
		ui->psetsaveitc->func.content_get = _modlist_pset_content_get;
		ui->psetsaveitc->func.state_get = NULL;
		ui->psetsaveitc->func.del = NULL;
	}

	ui->moditc = elm_genlist_item_class_new();
	if(ui->moditc)
	{
		ui->moditc->item_style = "full";
		ui->moditc->func.text_get = NULL;
		ui->moditc->func.content_get = _modlist_content_get;
		ui->moditc->func.state_get = NULL;
		ui->moditc->func.del = NULL;
	}

	ui->psetitc = elm_genlist_item_class_new();
	if(ui->psetitc)
	{
		ui->psetitc->item_style = "full";
		ui->psetitc->func.text_get = NULL;
		ui->psetitc->func.content_get = _modlist_psets_content_get;
		ui->psetitc->func.state_get = NULL;
		ui->psetitc->func.del = NULL;

		elm_genlist_item_class_ref(ui->psetitc);
	}
}

void
_modlist_set_callbacks(sp_ui_t *ui)
{
	evas_object_smart_callback_add(ui->modlist, "activated",
		_modlist_activated, ui);
	evas_object_smart_callback_add(ui->modlist, "moved",
		_modlist_moved, ui);
}