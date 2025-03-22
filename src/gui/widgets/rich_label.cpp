/*
	Copyright (C) 2024 - 2025
	by Subhraman Sarkar (babaissarkar) <suvrax@gmail.com>
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

#include "gui/widgets/rich_label.hpp"

#include "gui/core/log.hpp"
#include "gui/core/widget_definition.hpp"
#include "gui/core/register_widget.hpp"
#include "gui/widgets/settings.hpp"

#include "cursor.hpp"
#include "font/constants.hpp"
#include "font/sdl_ttf_compat.hpp"
#include "gettext.hpp"
#include "serialization/markup.hpp"
#include "serialization/string_utils.hpp"
#include "serialization/unicode.hpp"
#include "sound.hpp"
#include "wml_exception.hpp"

#include <boost/format.hpp>
#include <functional>
#include <string>
#include <utility>

#define DBG_GUI_RL LOG_STREAM(debug, log_rich_label)

namespace gui2
{

// ------------ WIDGET -----------{

REGISTER_WIDGET(rich_label)

rich_label::rich_label(const implementation::builder_rich_label& builder)
	: styled_widget(builder, type())
	, state_(ENABLED)
	, can_wrap_(true)
	, link_aware_(builder.link_aware)
	, link_color_(font::YELLOW_COLOR)
	, predef_colors_()
	, font_size_(font::SIZE_NORMAL)
	, can_shrink_(true)
	, text_alpha_(ALPHA_OPAQUE)
	, init_w_(builder.width(get_screen_size_variables()))
	, size_(0, 0)
	, padding_(builder.padding)
{
	const auto conf = cast_config_to<rich_label_definition>();
	assert(conf);
	text_color_enabled_ = conf->text_color_enabled;
	text_color_disabled_ = conf->text_color_disabled;
	font_family_ = conf->font_family;
	font_size_ = conf->font_size;
	font_style_ = conf->font_style;
	link_color_ = conf->link_color;
	predef_colors_.insert(conf->colors.begin(), conf->colors.end());
	set_text_alignment(builder.text_alignment);
	set_label(builder.label_string);
}

color_t rich_label::get_color(const std::string& color)
{
	const auto iter = predef_colors_.find(color);
	return (iter != predef_colors_.end()) ? iter->second : font::string_to_color(color);
}

wfl::map_formula_callable rich_label::setup_text_renderer(config text_cfg, unsigned width) const
{
	// Set up fake render to calculate text position
	static wfl::action_function_symbol_table functions;
	wfl::map_formula_callable variables;
	variables.add("text", wfl::variant(text_cfg["text"]));
	variables.add("width", wfl::variant(width));
	variables.add("text_wrap_mode", wfl::variant(PANGO_ELLIPSIZE_NONE));
	variables.add("fake_draw", wfl::variant(true));
	gui2::text_shape{text_cfg, functions}.draw(variables);
	return variables;
}

point rich_label::get_text_size(config& text_cfg, unsigned width) const
{
	wfl::map_formula_callable variables = setup_text_renderer(text_cfg, width);
	return {
		variables.query_value("text_width").as_int(),
		variables.query_value("text_height").as_int()
	};
}

point rich_label::get_image_size(config& img_cfg) const
{
	static wfl::action_function_symbol_table functions;
	wfl::map_formula_callable variables;
	variables.add("fake_draw", wfl::variant(true));
	gui2::image_shape{img_cfg, functions}.draw(variables);
	return {
		variables.query_value("image_width").as_int(),
		variables.query_value("image_height").as_int()
	};
}

void rich_label::add_attribute(
	config& curr_item,
	const std::string& attr_name,
	size_t start,
	size_t end,
	const std::string& extra_data)
{
	curr_item.add_child("attribute", config{
		"name"  , attr_name,
		"start" , start,
		"end"   , end == 0 ? curr_item["text"].str().size() : end,
		"value" , extra_data
	});
}

std::pair<size_t, size_t> rich_label::add_text_with_attribute(
	config& curr_item,
	const std::string& text,
	const std::string& attr_name,
	const std::string& extra_data)
{
	const auto [start, end] = add_text(curr_item, text);
	add_attribute(curr_item, attr_name, start, end, extra_data);
	return { start, end };
}

void rich_label::add_link(
	config& curr_item,
	const std::string& name,
	const std::string& dest,
	const point& origin,
	int img_width)
{
	// TODO algorithm needs to be text_alignment independent

	DBG_GUI_RL << "add_link: " << name << "->" << dest;
	DBG_GUI_RL << "origin: " << origin;
	DBG_GUI_RL << "width=" << img_width;

	point t_start, t_end;

	setup_text_renderer(curr_item, init_w_ - origin.x - img_width);
	t_start = origin + get_xy_from_offset(utf8::size(curr_item["text"].str()));
	DBG_GUI_RL << "link text start:" << t_start;

	std::string link_text = name.empty() ? dest : name;
	add_text_with_attribute(curr_item, link_text, "color", link_color_.to_hex_string());

	setup_text_renderer(curr_item, init_w_ - origin.x - img_width);
	t_end.x = origin.x + get_xy_from_offset(utf8::size(curr_item["text"].str())).x;
	DBG_GUI_RL << "link text end:" << t_end;

	// TODO link after right aligned images

	// Add link
	if (t_end.x > t_start.x) {
		rect link_rect{ t_start, point{t_end.x - t_start.x, font::get_max_height(font_size_) }};
		links_.emplace_back(link_rect, dest);

		DBG_GUI_RL << "added link at rect: " << link_rect;

	} else {
		//link straddles two lines, break into two rects
		point t_size(size_.x - t_start.x - (origin.x == 0 ? img_width : 0), t_end.y - t_start.y);
		point link_start2(origin.x, t_start.y + 1.3*font::get_max_height(font_size_));
		point t_size2(t_end.x, t_end.y - t_start.y);

		rect link_rect{ t_start, point{ t_size.x, font::get_max_height(font_size_) } };
		rect link_rect2{ link_start2, point{ t_size2.x, font::get_max_height(font_size_) } };

		links_.emplace_back(link_rect, dest);
		links_.emplace_back(link_rect2, dest);

		DBG_GUI_RL << "added link at rect 1: " << link_rect;
		DBG_GUI_RL << "added link at rect 2: " << link_rect2;
	}
}

size_t rich_label::get_split_location(std::string_view text, const point& pos)
{
	size_t len = get_offset_from_xy(pos);
	len = (len > text.size()-1) ? text.size()-1 : len;

	// break only at word boundary
	char c;
	while(!std::isspace(c = text[len])) {
		len--;
		if (len == 0) {
			break;
		}
	}

	return len;
}

std::vector<std::string> rich_label::split_in_width(
	const std::string &s,
	const int font_size,
	const unsigned width)
{
	std::vector<std::string> res;
	try {
		const std::string& first_line = font::pango_word_wrap(s, font_size, width, -1, 1, true);
		res.push_back(first_line);
		if(s.size() > first_line.size()) {
			res.push_back(s.substr(first_line.size()));
		}
	} catch (utf8::invalid_utf8_exception&) {
		throw markup::parse_error(_("corrupted original file"));
	}

	return res;
}

void rich_label::set_dom(const config& dom) {
	std::tie(shapes_, size_) = generate_layout(dom, point(0,0), init_w_, links_, padding_, true);
}

void rich_label::set_label(const t_string& text) {
	set_dom(markup::parse_text(text));
}

void rich_label::default_text_config(
	config* txt_ptr,
	const point& pos,
	const int max_width,
	const t_string& text)
{
	if (txt_ptr != nullptr) {
		(*txt_ptr)["text"] = text;
		(*txt_ptr)["color"] = text_color_enabled_.to_rgba_string();
		(*txt_ptr)["font_family"] = font_family_;
		(*txt_ptr)["font_size"] = font_size_;
		(*txt_ptr)["font_style"] = font_style_;
		(*txt_ptr)["text_alignment"] = encode_text_alignment(get_text_alignment());
		(*txt_ptr)["x"] = pos.x;
		(*txt_ptr)["y"] = pos.y;
		(*txt_ptr)["w"] = "(text_width)";
		(*txt_ptr)["h"] = "(text_height)";
		(*txt_ptr)["maximum_width"] = max_width;
		(*txt_ptr)["parse_text_as_formula"] = false;
	}
}

void rich_label::update_canvas()
{
	for(canvas& tmp : get_canvases()) {
		tmp.set_shapes(shapes_, true);
		tmp.set_variable("width", wfl::variant(init_w_));
		tmp.set_variable("padding", wfl::variant(padding_));
		// Disable ellipsization so that text wrapping can work
		tmp.set_variable("text_wrap_mode", wfl::variant(PANGO_ELLIPSIZE_NONE));
		tmp.set_variable("text_alpha", wfl::variant(text_alpha_));
	}
}

void rich_label::set_text_alpha(unsigned short alpha)
{
	if(alpha != text_alpha_) {
		text_alpha_ = alpha;
		update_canvas();
		queue_redraw();
	}
}

void rich_label::set_active(const bool active)
{
	if(get_active() != active) {
		set_state(active ? ENABLED : DISABLED);
	}
}

void rich_label::set_link_aware(bool link_aware)
{
	if(link_aware != link_aware_) {
		link_aware_ = link_aware;
		update_canvas();
		queue_redraw();
	}
}

void rich_label::set_link_color(const color_t& color)
{
	if(color != link_color_) {
		link_color_ = color;
		update_canvas();
		queue_redraw();
	}
}

void rich_label::set_state(const state_t state)
{
	if(state != state_) {
		state_ = state;
		queue_redraw();
	}
}

void rich_label::register_link_callback(std::function<void(std::string)> link_handler)
{
	if(!link_aware_) {
		return;
	}

	connect_signal<event::LEFT_BUTTON_CLICK>(
		std::bind(&rich_label::signal_handler_left_button_click, this, std::placeholders::_3));
	connect_signal<event::MOUSE_MOTION>(
		std::bind(&rich_label::signal_handler_mouse_motion, this, std::placeholders::_3, std::placeholders::_5));
	connect_signal<event::MOUSE_LEAVE>(
		std::bind(&rich_label::signal_handler_mouse_leave, this, std::placeholders::_3));
	link_handler_ = std::move(link_handler);
}


void rich_label::signal_handler_left_button_click(bool& handled)
{
	DBG_GUI_E << "rich_label click";

	if(!get_link_aware()) {
		return; // without marking event as "handled"
	}

	point mouse = get_mouse_position() - get_origin();

	DBG_GUI_RL << "(mouse) " << mouse;
	DBG_GUI_RL << "link count :" << links_.size();

	for (const auto& entry : links_) {
		DBG_GUI_RL << "link " << entry.first;

		if (entry.first.contains(mouse)) {
			DBG_GUI_RL << "Clicked link! dst = " << entry.second;
			sound::play_UI_sound(settings::sound_button_click);
			if (link_handler_) {
				link_handler_(entry.second);
			} else {
				DBG_GUI_RL << "No registered link handler found";
			}

		}
	}

	handled = true;
}

void rich_label::signal_handler_mouse_motion(bool& handled, const point& coordinate)
{
	DBG_GUI_E << "rich_label mouse motion";

	if(!get_link_aware()) {
		return; // without marking event as "handled"
	}

	point mouse = coordinate - get_origin();

	for (const auto& entry : links_) {
		if (entry.first.contains(mouse)) {
			update_mouse_cursor(true);
			handled = true;
			return;
		}
	}

	update_mouse_cursor(false);
}

void rich_label::signal_handler_mouse_leave(bool& handled)
{
	DBG_GUI_E << "rich_label mouse leave";

	if(!get_link_aware()) {
		return; // without marking event as "handled"
	}

	// We left the widget, so just unconditionally reset the cursor
	update_mouse_cursor(false);

	handled = true;
}

void rich_label::update_mouse_cursor(bool enable)
{
	// Someone else may set the mouse cursor for us to something unusual (e.g.
	// the WAIT cursor) so we ought to mess with that only if it's set to
	// NORMAL or HYPERLINK.

	if(enable && cursor::get() == cursor::NORMAL) {
		cursor::set(cursor::HYPERLINK);
	} else if(!enable && cursor::get() == cursor::HYPERLINK) {
		cursor::set(cursor::NORMAL);
	}
}

// }---------- DEFINITION ---------{

rich_label_definition::rich_label_definition(const config& cfg)
	: styled_widget_definition(cfg)
{
	DBG_GUI_P << "Parsing rich_label " << id;

	load_resolutions<resolution>(cfg);
}

rich_label_definition::resolution::resolution(const config& cfg)
	: resolution_definition(cfg)
	, text_color_enabled(color_t::from_rgba_string(cfg["text_font_color_enabled"].str()))
	, text_color_disabled(color_t::from_rgba_string(cfg["text_font_color_disabled"].str()))
	, link_color(cfg["link_color"].empty() ? font::YELLOW_COLOR : color_t::from_rgba_string(cfg["link_color"].str()))
	, font_family(cfg["text_font_family"].str())
	, font_size(cfg["text_font_size"].to_int(font::SIZE_NORMAL))
	, font_style(cfg["text_font_style"].str("normal"))
	, colors()
{
	if(auto colors_cfg = cfg.optional_child("colors")) {
		for (const auto& [name, value] : colors_cfg->attribute_range()) {
			colors.try_emplace(name, color_t::from_rgba_string(value.str()));
		}
	}

	// Note the order should be the same as the enum state_t is rich_label.hpp.
	state.emplace_back(VALIDATE_WML_CHILD(cfg, "state_enabled", missing_mandatory_wml_tag("rich_label_definition][resolution", "state_enabled")));
	state.emplace_back(VALIDATE_WML_CHILD(cfg, "state_disabled", missing_mandatory_wml_tag("rich_label_definition][resolution", "state_disabled")));
}

// }---------- BUILDER -----------{

namespace implementation
{

builder_rich_label::builder_rich_label(const config& cfg)
	: builder_styled_widget(cfg)
	, text_alignment(decode_text_alignment(cfg["text_alignment"]))
	, link_aware(cfg["link_aware"].to_bool(true))
	, width(cfg["width"], 500)
	, padding(cfg["padding"].to_int(5))
{
}

std::unique_ptr<widget> builder_rich_label::build() const
{
	DBG_GUI_G << "Window builder: placed rich_label '" << id << "' with definition '"
			  << definition << "'.";

	return std::make_unique<rich_label>(*this);
}

} // namespace implementation

// }------------ END --------------

} // namespace gui2
