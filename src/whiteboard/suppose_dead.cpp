/*
	Copyright (C) 2011 - 2025
	by Tommy Schmitz
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

/**
 * @file
 */

#include "whiteboard/suppose_dead.hpp"

#include "whiteboard/visitor.hpp"

#include "config.hpp"
#include "display.hpp"
#include "draw.hpp"
#include "play_controller.hpp"
#include "resources.hpp"
#include "units/unit.hpp"
#include "units/map.hpp"

namespace wb
{

std::ostream& operator<<(std::ostream &s, const suppose_dead_ptr& sup_d)
{
	assert(sup_d);
	return sup_d->print(s);
}

std::ostream& operator<<(std::ostream &s, const suppose_dead_const_ptr& sup_d)
{
	assert(sup_d);
	return sup_d->print(s);
}

std::ostream& suppose_dead::print(std::ostream &s) const
{
	s << "Suppose-dead for unit " << get_unit()->name() << " [" << get_unit()->id() << "] "
			<< "at (" << loc_ << ")";
	return s;
}

suppose_dead::suppose_dead(std::size_t team_index, bool hidden, const unit& curr_unit, const map_location& loc)
	: action(team_index,hidden)
	, unit_underlying_id_(curr_unit.underlying_id())
	, unit_id_(curr_unit.id())
	, loc_(loc)
{
	this->init();
}

suppose_dead::suppose_dead(const config& cfg, bool hidden)
	: action(cfg,hidden)
	, unit_underlying_id_(0)
	, unit_id_()
	, loc_(cfg.mandatory_child("loc_")["x"],cfg.mandatory_child("loc_")["y"], wml_loc())
{
	// Construct and validate unit_
	unit_map::iterator unit_itor = resources::gameboard->units().find(cfg["unit_"].to_size_t());
	if(unit_itor == resources::gameboard->units().end())
		throw action::ctor_err("suppose_dead: Invalid underlying_id");

	unit_underlying_id_ = unit_itor->underlying_id();
	unit_id_ = unit_itor->id();

	this->init();
}

void suppose_dead::init()
{
	display::get_singleton()->invalidate(loc_);
}

suppose_dead::~suppose_dead()
{
	//invalidate hex so that skull indicator is properly cleared
	if(display::get_singleton())
		display::get_singleton()->invalidate(loc_);
}

unit_ptr suppose_dead::get_unit() const
{
	unit_map::iterator itor = resources::gameboard->units().find(unit_underlying_id_);
	if (itor.valid())
		return itor.get_shared_ptr();
	else
		return unit_ptr();
}

void suppose_dead::accept(visitor& v)
{
	v.visit(shared_from_this());
}

void suppose_dead::execute(bool& success, bool& complete)
	{success = false;   complete = true;}

void suppose_dead::apply_temp_modifier(unit_map& unit_map)
{
	// Remove the unit
	const unit_const_ptr removed_unit = unit_map.extract(loc_);
	DBG_WB << "Suppose dead: Temporarily removing unit " << removed_unit->name() << " [" << removed_unit->id()
			<< "] from (" << loc_ << ")";

	// Just check to make sure we removed the unit we expected to remove
	assert(get_unit().get() == removed_unit.get());
}

void suppose_dead::remove_temp_modifier(unit_map& unit_map)
{
	// Just check to make sure the hex is empty
	unit_map::iterator unit_it = resources::gameboard->units().find(loc_);
	assert(unit_it == resources::gameboard->units().end());

	// Restore the unit
	unit_map.insert(get_unit());
}

void suppose_dead::draw_hex(const map_location& hex)
{
	if (hex != loc_) {
		return;
	}

	//@todo: Possibly use a different layer
	const drawing_layer layer = drawing_layer::arrows;

	display::get_singleton()->drawing_buffer_add(
		layer, loc_, [tex = image::get_texture(image::locator{"whiteboard/suppose_dead.png"}, image::HEXED)](const rect& d) {
			draw::blit(tex, d);
		});
}

void suppose_dead::redraw()
{
	display::get_singleton()->invalidate(loc_);
}

action::error suppose_dead::check_validity() const
{
	if(!get_source_hex().valid()) {
		return INVALID_LOCATION;
	}
	//Check that the unit still exists in the source hex
	unit_map::const_iterator unit_it = resources::gameboard->units().find(get_source_hex());
	if(unit_it == resources::gameboard->units().end()) {
		return NO_UNIT;
	}
	//check if the unit in the source hex has the same unit id as before,
	//i.e. that it's the same unit
	if(unit_id_ != unit_it->id()) {
		return UNIT_CHANGED;
	}

	return OK;
}

config suppose_dead::to_config() const
{
	config final_cfg = action::to_config();

	final_cfg["type"]="suppose_dead";
	final_cfg["unit_"]=static_cast<int>(unit_underlying_id_);
	final_cfg["unit_id_"]=unit_id_;

	config loc_cfg;
	loc_cfg["x"]=loc_.wml_x();
	loc_cfg["y"]=loc_.wml_y();
	final_cfg.add_child("loc_", std::move(loc_cfg));

	return final_cfg;
}

} // end namespace wb
