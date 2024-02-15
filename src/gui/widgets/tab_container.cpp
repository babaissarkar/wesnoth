/*
	Copyright (C) 2024
	by babaissarkar(Subhraman Sarkar) <suvrax@gmail.com>
	Part of the Battle for Wesnoth Project https://www.wesnoth.org/

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY.

	See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "wesnoth-lib"

#include "gui/widgets/tab_container.hpp"

#include "gui/core/log.hpp"
#include "gettext.hpp"
#include "gui/core/window_builder/helper.hpp"
#include "gui/core/register_widget.hpp"
#include "gui/widgets/settings.hpp"
#include "wml_exception.hpp"

#define LOG_SCOPE_HEADER get_control_type() + " [" + id() + "] " + __func__
#define LOG_HEADER LOG_SCOPE_HEADER + ':'

namespace gui2
{

// ------------ WIDGET -----------{

REGISTER_WIDGET(tab_container)

tab_container::tab_container(const implementation::builder_tab_container& builder)
	: container_base(builder, type())
	, state_(ENABLED)
{
}

void tab_container::set_self_active(const bool active)
{
	state_ = active ? ENABLED : DISABLED;
}

bool tab_container::get_active() const
{
	return state_ != DISABLED;
}

unsigned tab_container::get_state() const
{
	return state_;
}

bool tab_container::can_wrap() const
{
	return true;
}

// }---------- DEFINITION ---------{

tab_container_definition::tab_container_definition(const config& cfg)
	: styled_widget_definition(cfg)
{
	DBG_GUI_P << "Parsing tab_container " << id;

	load_resolutions<resolution>(cfg);
}

tab_container_definition::resolution::resolution(const config& cfg)
	: resolution_definition(cfg), grid(nullptr)
{
	// Note the order should be the same as the enum state_t is tab_container.hpp.
	state.emplace_back(VALIDATE_WML_CHILD(cfg, "state_enabled", _("Missing required state for tab container control")));
	state.emplace_back(VALIDATE_WML_CHILD(cfg, "state_disabled", _("Missing required state for tab container control")));

	auto child = VALIDATE_WML_CHILD(cfg, "grid", _("No grid defined for tab container control"));
	grid = std::make_shared<builder_grid>(child);
}

// }---------- BUILDER -----------{

namespace implementation
{

builder_tab_container::builder_tab_container(const config& cfg)
	: implementation::builder_styled_widget(cfg)
{
}

std::unique_ptr<widget> builder_tab_container::build() const
{
	auto widget = std::make_unique<tab_container>(*this);

	const auto conf = widget->cast_config_to<tab_container_definition>();
	assert(conf);

	widget->init_grid(*conf->grid);
	widget->finalize_setup();

	DBG_GUI_G << "Window builder: placed tab_container '" << id
			  << "' with definition '" << definition << "'.";

	return widget;
}

} // namespace implementation

// }------------ END --------------

} // 
