/*
	Copyright (C) 2025
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

#pragma once

#include "gui/core/canvas.hpp"
#include "color.hpp"
#include "config.hpp"
#include "font/attributes.hpp"
#include "font/sdl_ttf_compat.hpp"
#include "font/standard_colors.hpp"
#include "font/text.hpp"
#include "formula/function.hpp"
#include "gettext.hpp"
#include "gui/widgets/helper.hpp"
#include "gui/widgets/rich_label.hpp"
#include "log.hpp"
#include "picture.hpp" // We want the file in src/
#include "sdl/point.hpp"
#include "sdl/rect.hpp"
#include "serialization/markup.hpp"
#include "serialization/string_utils.hpp"
#include "serialization/unicode.hpp"
#include "serialization/utf8_exception.hpp"
#include "video.hpp"

#include <boost/multi_array.hpp>

static lg::log_domain log_rich_label("gui/widget/rich_label");
#define DBG_GUI_RL LOG_STREAM(debug, log_rich_label)

#define LINK_DEBUG_BORDER false

namespace gui2
{

namespace
{
using namespace std::string_literals;

/** Possible formatting tags, must be the same as those in gui2::text_shape::draw */
const std::array format_tags{ "bold"s, "b"s, "italic"s, "i"s, "underline"s, "u"s };
}

/**
 * A small struct to pass around important layout styling parameters.
 * `max_width` is not passed via this as it gets changed in various places
 * during recursive calls and is thus not a global constant parameter.
 */
struct layout_info
{
public:
	int padding;
	int font_size;
	std::string align;
	std::string font_family;
	std::string font_style;
	color_t text_color_enabled;
	color_t text_color_disabled; // TODO not used yet
	color_t link_color;
	std::map<std::string, color_t> predef_colors; // TODO should the 3 colors above be dumped here?
};

std::pair<config, point> generate_layout(
    const config& parsed_text,
    const point& origin,
    const unsigned init_width,
    std::vector<std::pair<rect, std::string>> links,
    const layout_info& info,
    const bool finalize = false);

struct item
{
private:
    point origin_;
    point size_;

protected:
	int max_width_;

public:
	item() = delete;
    explicit item(const int max_width)
        : origin_(0, 0)
		, max_width_(max_width)
    {}

    virtual ~item() {};

    void set_origin(const point& origin) { origin_ = origin; }
	void set_origin(const int x, const int y) { origin_ = point(x, y); }
    point origin() const { return origin_; }
    virtual point size() const { return {0,0}; };
};

struct text: public item
{
	explicit text(const int max_width, const layout_info& info)
		: item(max_width)
		, text_()
		, align_(info.align)
		, font_family_(info.font_family)
		, font_size_(info.font_size)
		, font_style_(info.font_style)
		, text_color_enabled_(info.text_color_enabled)
		, text_attributes_()
	{};

	t_string get_text() const
	{
		return text_;
	}

	std::pair<size_t, size_t> set_text(const std::string& text)
	{
		text_ = text;
		return { 0, text_.size() };
	}

	std::pair<size_t, size_t> add_text(const std::string& text)
	{
		size_t start = text_.size();
		text_ += text;
		return { start, text_.size() };
	}

	void add_attribute(
		const std::string& attr_name,
		size_t start,
		size_t end,
		const std::string& extra_data)
	{
		text_attributes_.add_child("attribute", config{
			"name"  , attr_name,
			"start" , start,
			"end"   , end == 0 ? text_.str().size() : end,
			"value" , extra_data
		});
	}

	/**
	 * To calculate the bounds of text, we need to set
	 * the width to which the text will wrap to first.
	 * This function does that.
	 */
	void set_max_width(const int width)
	{
		max_width_ = width;
	}

	int get_max_width() const
	{
		return max_width_;
	}

	operator config() const
	{
		config cfg;
		cfg["text"] = text_;
		cfg["color"] = text_color_enabled_.to_rgba_string();
		cfg["font_family"] = font_family_;
		cfg["font_size"] = font_size_;
		cfg["font_style"] = font_style_;
		cfg["text_alignment"] = align_;
		cfg["x"] = origin().x;
		cfg["y"] = origin().y;
		cfg["w"] = "(text_width)";
		cfg["h"] = "(text_height)";
		cfg["maximum_width"] = max_width_;
		cfg["parse_text_as_formula"] = false;
		cfg.append(text_attributes_);
		return cfg;
	}

	point size() const override
	{
		wfl::action_function_symbol_table functions;
		wfl::map_formula_callable variables;
		variables.add("text", wfl::variant(text_));
		variables.add("width", wfl::variant(max_width_));
		variables.add("text_wrap_mode", wfl::variant(PANGO_ELLIPSIZE_NONE));
		variables.add("fake_draw", wfl::variant(true));
		text_shape(*this, functions).draw(variables);
		return {
			variables.query_value("text_width").as_int(),
			variables.query_value("text_height").as_int()
		};
	}

private:
	t_string text_;
	const std::string align_;
	const std::string font_family_;
	const int font_size_;
	const std::string font_style_;
	const color_t text_color_enabled_;

	config text_attributes_;

	// font::attribute_list text_attributes_;
};

struct image: public item
{
	explicit image(
		const std::string& src,
		// const bool floating,
		const std::string& align,
		const int max_width
		// const int padding
	)
		: item(max_width)
		// , padding_(padding)
		// , floating_(floating)
		, src_(src)
		, align_(align)
    {
		// TODO should trigger warning/abort when src_ is empty
    }

	operator config()
	{
		config cfg;
		cfg["name"] = src_;
		cfg["x"] = origin().x;
		cfg["y"] = origin().y;
		cfg["w"] = "(image_width)";
		cfg["h"] = "(image_height)";
		// TODO WIP
		return cfg;
	}

	point size() const override
	{
		return ::image::get_size(::image::locator{src_});
	}

private:
	// const int padding_;
	// const bool floating_;
	const std::string src_;
	const std::string align_;
};

struct table: public item
{
	explicit table(const config& cfg, const layout_info& info, const int max_width)
		: item(max_width)
		, cfg_(cfg)
		, info_(info)
		, rows_(cfg.child_count("row"))
		, columns_(cfg.mandatory_child("row").child_count("col"))
		, row_heights_(rows_, 0)
		, col_widths_(columns_, 0)
		, cell_sizes_(boost::extents[rows_][columns_])
	{
		if (columns_ == 0) {
			columns_ = 1;
		}
		max_cell_width_ = max_width/columns_;
	}

	std::array<int, 2> get_padding(const config::attribute_value& val) const
	{
		if(val.blank()) {
			return std::array{ info_.padding, info_.padding };
		} else {
			auto paddings = utils::split(val.str(), ' ');
			return std::array{ std::stoi(paddings[0]), std::stoi(paddings[1]) };
		}
	}

	void calculate_cell_sizes()
	{
		point pos;
		point origin(this->origin());
		int row_idx = 0, col_idx = 0;

		// optimal col width calculation
		for(const config& row : cfg_.child_range("row")) {
			pos.x = origin.x;
			col_idx = 0;

			// order: top padding|bottom padding
			std::array<int, 2> row_paddings = get_padding(row["padding"]);

			pos.y += row_paddings[0];
			for(const config& col : row.child_range("col")) {
				// DBG_GUI_RL << "table cell origin (pre-layout): " << pos.x << ", " << pos.y;
				config col_cfg;
				col_cfg.append_children(col);
				config& col_txt_cfg = col_cfg.add_child("text");
				col_txt_cfg.append_attributes(col);

				// order: left padding|right padding
				std::array<int, 2> col_paddings = get_padding(col["padding"]);
				int cell_width = max_cell_width_ - col_paddings[0] - col_paddings[1];

				pos.x += col_paddings[0];

				// store calculation results for cell sizes
				cell_sizes_[row_idx][col_idx] = generate_layout(col_cfg, pos, max_cell_width_, {}, info_).second;

				// column post-processing
				row_heights_[row_idx] = std::max(row_heights_[row_idx], cell_sizes_[row_idx][col_idx].y);
				if (!cfg_["width"].empty()) {
					col_widths_[col_idx] = cell_width;
				}
				col_widths_[col_idx] = std::max(col_widths_[col_idx], cell_sizes_[row_idx][col_idx].x);
				if (cfg_["width"].empty()) {
					col_widths_[col_idx] = std::min(col_widths_[col_idx], cell_width);
				}

				// DBG_GUI_RL << "table row " << row_idx << " height: " << row_heights_[row_idx]
				//            << "col " << col_idx << " width: " << col_widths_[col_idx];

				pos.x += cell_width;
				pos.x += col_paddings[1];
				col_idx++;
			}

			pos.y += row_heights_[row_idx] + row_paddings[1];
			row_idx++;
		}
	}

	point size() const override
	{
		return {0, 0};
	}

private:
	config cfg_;
	layout_info info_;
	int rows_, columns_;
	int max_cell_width_;
	std::vector<int> row_heights_, col_widths_;
	boost::multi_array<point, 2> cell_sizes_;
};


/**
 * A correction to allow inline image to stay at the same height
 * as the text following it.
 */
inline unsigned baseline_correction(unsigned img_height) {
	unsigned text_height = font::get_text_renderer().get_size().y;
	return (text_height > img_height) ? (text_height - img_height)/2 : 0;
}

inline std::vector<std::string> split_in_width(
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

inline color_t get_color(const layout_info& info, const std::string& color)
{
	const auto iter = info.predef_colors.find(color);
	return (iter != info.predef_colors.end()) ? iter->second : font::string_to_color(color);
}

inline int get_offset_from_xy(const point& position)
{
	return font::get_text_renderer().xy_to_index(position);
}

inline point get_xy_from_offset(const unsigned offset)
{
	return font::get_text_renderer().get_cursor_position(offset);
}

inline size_t get_split_location(std::string_view text, const point& pos)
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

inline void add_link(
	text& txt,
	const std::string& name,
	const std::string& dest,
	std::vector<std::pair<rect, std::string>>& links,
	const layout_info& info,
	int img_width)
{
	// TODO algorithm needs to be text_alignment independent
	// TODO link after right aligned images
	point origin = txt.origin();
	const int max_width = txt.get_max_width();

	DBG_GUI_RL << "add_link: " << name << "->" << dest;
	DBG_GUI_RL << "origin: " << origin;
	DBG_GUI_RL << "width=" << img_width;

	point t_start, t_end;

	txt.set_max_width(max_width - origin.x - img_width);
	t_start = origin + get_xy_from_offset(utf8::size(txt.get_text()));
	DBG_GUI_RL << "link text start:" << t_start;

	std::string link_text = name.empty() ? dest : name;
	const auto [start, end] = txt.add_text(link_text);
	txt.add_attribute("color", start, end, info.link_color.to_hex_string());

	t_end.x = origin.x + get_xy_from_offset(utf8::size(txt.get_text())).x;
	DBG_GUI_RL << "link text end:" << t_end;

	int text_height = font::get_max_height(info.font_size);

	// Add link
	if (t_end.x > t_start.x) {
		rect link_rect{ t_start, point(t_end.x - t_start.x, text_height)};
		links.emplace_back(link_rect, dest);

		DBG_GUI_RL << "added link at rect: " << link_rect;

	} else {
		//link straddles two lines, break into two rects

		point t_size(max_width - t_start.x - (origin.x == 0 ? img_width : 0), t_end.y - t_start.y);
		point link_start2(origin.x, t_start.y + 1.3*text_height);
		point t_size2(t_end.x, t_end.y - t_start.y);

		rect link_rect{ t_start, point{ t_size.x, text_height } };
		rect link_rect2{ link_start2, point{ t_size2.x, text_height } };

		links.emplace_back(link_rect, dest);
		links.emplace_back(link_rect2, dest);

		DBG_GUI_RL << "added link at rect 1: " << link_rect;
		DBG_GUI_RL << "added link at rect 2: " << link_rect2;
	}
}

/**
 * Given a parsed config from help markup,
 * layout it into a config that can be understood by canvas
 */
inline std::pair<config, point> generate_layout(
    const config& parsed_text,
    const point& origin,
    const unsigned init_width,
    std::vector<std::pair<rect, std::string>> links,
    const layout_info& info,
    const bool finalize)
{
	// Initial width
	DBG_GUI_RL << "Initial width: " << init_width;

	// Initialization
	int x = 0;
	int prev_blk_height = origin.y;
	int text_height = 0;
	int h = 0;
	int w = 0;

	if (finalize) {
		links.clear();
	}

	config text_dom;

	config* remaining_item = nullptr;
	item* curr_item = nullptr;

	bool is_text = false;
	bool is_image = false;
	bool is_float = false;
	bool wrap_mode = false;
	bool new_text_block = false;

	point pos(origin);
	point float_pos, float_size;
	point img_size;

	DBG_GUI_RL << parsed_text.debug();

	for(const auto [key, child] : parsed_text.all_children_view()) {
		if(key == "img") {
			prev_blk_height += text_height;
			text_height = 0;

			////////////// New Code /////////////////
			image img(child["src"], child["align"].str("left"), init_width);
			curr_item = &img;
			img.set_origin(pos);
			////////////////////////////////////////

			std::string align = child["align"].str("left");
			const point& curr_img_size = img.size();

			if (child["float"].to_bool(false)) {

				if (align == "right") {
					float_pos.x = init_width - curr_img_size.x;
				} else if (align == "middle" || align == "center") {
					// works for single image only
					float_pos.x = float_size.x + (init_width - curr_img_size.x)/2;
				}

				if (is_image) {
					float_pos.y += float_size.y;
				}

				img.set_origin(float_pos.x, pos.y + float_pos.y);

				x = (align == "left") ? float_size.x : 0;
				float_size.x = curr_img_size.x + info.padding;
				float_size.y += curr_img_size.y + info.padding;

				wrap_mode = true;
				is_float = true;

			} else {

				int img_x = pos.x;
				if (align == "right") {
					img_x = init_width - curr_img_size.x - pos.x;
				} else if (align == "middle" || align == "center") {
					// works for single image only
					img_x = pos.x + (init_width - curr_img_size.x)/2;
				}
				img.set_origin(img_x, pos.y);

				img_size.x += curr_img_size.x + info.padding;
				img_size.y = std::max(img_size.y, curr_img_size.y);

				x = img_size.x;
				pos.x = origin.x + img_size.x;

				if (!is_image || is_float) {
					prev_blk_height += curr_img_size.y;
					float_size.y -= curr_img_size.y;
				}

				is_float = false;
			}

			w = std::max(w, x);

			text_dom.add_child("image", img);

			is_image = true;
			is_text = false;
			new_text_block = true;

			DBG_GUI_RL << "image: src=" << child["src"] << ", size=" << img_size;
			DBG_GUI_RL << "wrap mode: " << wrap_mode << ", floating: " << is_float;

		} else if(key == "table") {
			if (curr_item == nullptr) {
				text txt(init_width, info);
				curr_item = &txt;
				new_text_block = false;
			}

			// table doesn't support floating images alongside
			img_size = point(0,0);
			float_size = point(0,0);
			x = origin.x;
			prev_blk_height += text_height + info.padding;
			text_height = 0;
			pos = point(origin.x, prev_blk_height + info.padding);

			////////////// New Code /////////////////
			int max_width = child["width"] == "fill" ? init_width : child["width"].to_int(init_width);
			table t(child, info, max_width);
			t.set_origin(pos);
			t.calculate_cell_sizes();
			/////////////////////////////////////////

			// init table vars
			unsigned col_idx = 0, row_idx = 0;
			unsigned rows = child.child_count("row");
			unsigned columns = 1;
			if (rows > 0) {
				columns = child.mandatory_child("row").child_count("col");
			}
			columns = (columns == 0) ? 1 : columns;
			int init_cell_width;
			if (child["width"] == "fill") {
				init_cell_width = init_width/columns;
			} else {
				init_cell_width = child["width"].to_int(init_width)/columns;
			}
			std::vector<int> col_widths(columns, 0);
			std::vector<int> row_heights(rows, 0);

			is_text = false;
			new_text_block = true;
			is_image = false;

			DBG_GUI_RL << "start table : " << "row= " << rows << " col=" << columns
			           << " width=" << init_cell_width*columns;

			const auto get_padding = [padding = info.padding](const config::attribute_value& val) {
				if(val.blank()) {
					return std::array{ padding, padding };
				} else {
					auto paddings = utils::split(val.str(), ' ');
					return std::array{ std::stoi(paddings[0]), std::stoi(paddings[1]) };
				}
			};

			std::array<int, 2> row_paddings;
			boost::multi_array<point, 2> cell_sizes(boost::extents[rows][columns]);

			// optimal col width calculation
			for(const config& row : child.child_range("row")) {
				pos.x = origin.x;
				col_idx = 0;

				// order: top padding|bottom padding
				row_paddings = get_padding(row["padding"]);

				pos.y += row_paddings[0];
				for(const config& col : row.child_range("col")) {
					DBG_GUI_RL << "table cell origin (pre-layout): " << pos.x << ", " << pos.y;
					config col_cfg;
					col_cfg.append_children(col);
					config& col_txt_cfg = col_cfg.add_child("text");
					col_txt_cfg.append_attributes(col);

					// order: left padding|right padding
					std::array<int, 2> col_paddings = get_padding(col["padding"]);
					int cell_width = init_cell_width - col_paddings[0] - col_paddings[1];

					pos.x += col_paddings[0];
					// attach data
					cell_sizes[row_idx][col_idx] = generate_layout(col_cfg, pos, init_cell_width, links, info).second;

					// column post-processing
					row_heights[row_idx] = std::max(row_heights[row_idx], cell_sizes[row_idx][col_idx].y);
					if (!child["width"].empty()) {
						col_widths[col_idx] = cell_width;
					}
					col_widths[col_idx] = std::max(col_widths[col_idx], cell_sizes[row_idx][col_idx].x);
					if (child["width"].empty()) {
						col_widths[col_idx] = std::min(col_widths[col_idx], cell_width);
					}

					DBG_GUI_RL << "table row " << row_idx << " height: " << row_heights[row_idx]
					           << "col " << col_idx << " width: " << col_widths[col_idx];

					pos.x += cell_width;
					pos.x += col_paddings[1];
					col_idx++;
				}

				pos.y += row_heights[row_idx] + row_paddings[1];
				row_idx++;
			}

			// table layouting
			row_idx = 0;
			pos = point(origin.x, prev_blk_height);
			for(const config& row : child.child_range("row")) {
				pos.x = origin.x;
				col_idx = 0;

				if (!row["bgcolor"].blank()) {
					config bg_base;
					config& bgbox = bg_base.add_child("rectangle");
					bgbox["x"] = origin.x;
					bgbox["y"] = pos.y;
					bgbox["w"] = std::accumulate(col_widths.begin(), col_widths.end(), 0) + 2*(row_paddings[0] + row_paddings[1])*columns;
					bgbox["h"] = row_paddings[0] + row_heights[row_idx] + row_paddings[1];
					bgbox["fill_color"] = get_color(info, row["bgcolor"].str()).to_rgba_string();
					text_dom.append(std::move(bg_base));
				}

				pos.y += row_paddings[0];

				for(const config& col : row.child_range("col")) {
					DBG_GUI_RL << "table row " << row_idx << " height: " << row_heights[row_idx]
					           << "col " << col_idx << " width: " << col_widths[col_idx];
					DBG_GUI_RL << "cell origin: " << pos;

					config col_cfg;
					col_cfg.append_children(col);
					config& col_txt_cfg = col_cfg.add_child("text");
					col_txt_cfg.append_attributes(col);

					// order: left padding|right padding
					std::array<int, 2> col_paddings = get_padding(col["padding"]);

					pos.x += col_paddings[0];

					// set position according to alignment keys
					point text_pos(pos);
					if (row["valign"] == "center" || row["valign"] == "middle") {
						text_pos.y += (row_heights[row_idx] - cell_sizes[row_idx][col_idx].y)/2;
					} else if (row["valign"] == "bottom") {
						text_pos.y += row_heights[row_idx] - cell_sizes[row_idx][col_idx].y;
					}
					if (col["halign"] == "center" || col["halign"] == "middle") {
						text_pos.x += (col_widths[col_idx] - cell_sizes[row_idx][col_idx].x)/2;
					} else if (col["halign"] == "right") {
						text_pos.x += col_widths[col_idx] - cell_sizes[row_idx][col_idx].x;
					}

					// attach data
					auto [table_elem, size] = generate_layout(col_cfg, text_pos, col_widths[col_idx], links, info);
					text_dom.append(std::move(table_elem));
					pos.x += col_widths[col_idx];
					pos.x += col_paddings[1];

					auto [_, end_cfg] = text_dom.all_children_view().back();
					end_cfg["maximum_width"] = col_widths[col_idx];

					DBG_GUI_RL << "jump to next column";

					if (!is_image) {
						new_text_block = true;
					}
					is_image = false;
					col_idx++;
				}

				pos.y += row_heights[row_idx];
				pos.y += row_paddings[1];
				DBG_GUI_RL << "row height: " << row_heights[row_idx];
				row_idx++;
			}

			w = std::max(w, pos.x);
			prev_blk_height = pos.y;
			text_height = 0;
			pos.x = origin.x;

			is_image = false;
			is_text = false;

			x = origin.x;

		} else if(key == "break" || key == "br") {

			if (curr_item == nullptr) {
				text t(init_width, info);
				curr_item = &t;
				t.set_origin(pos);
				new_text_block = false;
			}

			// TODO correct height update
			if (is_image && !is_float) {
				prev_blk_height += text_height + info.padding;
				text_height = 0;
				pos = point(origin.x, prev_blk_height);
			} else if (text* t = dynamic_cast<text*>(curr_item)) {
				t->add_text("\n");
			}


			x = origin.x;
			is_image = false;
			img_size = point(0,0);

			DBG_GUI_RL << "linebreak";

			if (!is_image) {
				new_text_block = true;
			}
			is_text = false;

		} else {

			std::string line = child["text"];

			if (!finalize && line.empty()) {
				continue;
			}

			config part2_cfg;
			if (is_image && (!is_float)) {
				if (!line.empty() && line.at(0) == '\n') {

					// Text following inline image starts with linebreak
					x = origin.x;
					prev_blk_height += info.padding;
					pos = point(origin.x, prev_blk_height);
					line = line.substr(1);

				} else if (!line.empty() && line.at(0) != '\n') {

					// Text following inline image does not start with linebreak
					// Add y correction to previous image so that it aligns with the line of text
					if (text* t = dynamic_cast<text*>(curr_item)) {
						t->set_origin(t->origin().x, pos.y + baseline_correction(img_size.y));
					}

					// Break the text into two parts:
					// the first part is a single line of text that fit in the area after the image
					// the rest goes on a new paragraph just below the image
					// -------------
					// |   Inline  | as much of text you can fit in a single line goes here...
					// |   Image   |
					// -------------
					// rest goes here.....
					std::vector<std::string> parts = split_in_width(line, info.font_size, (init_width-x));
					// First line
					if (!parts.front().empty()) {
						line = parts.front();
					}

					std::string& part2 = parts.back();
					if (!part2.empty() && parts.size() > 1) {
						part2 = (part2[0] == '\n') ? part2.substr(1) : part2;
						part2_cfg.add_child("text")["text"] = parts.back();
						part2_cfg = generate_layout(part2_cfg, point(origin.x, prev_blk_height), init_width, links, info, false).first;
						remaining_item = &part2_cfg;
					}

					if (parts.size() == 1) {
						prev_blk_height -= img_size.y;
					}

				} else {
					prev_blk_height -= img_size.y;
				}
			}

			if (curr_item == nullptr || new_text_block) {
				text t(init_width - pos.x - float_size.x, info);
				curr_item = &t;
				t.set_origin(pos);
				new_text_block = false;
			}

			text* t = dynamic_cast<text*>(curr_item);
			if (!t) break;

			// }---------- TEXT TAGS -----------{
			// TODO set correct width
			// int tmp_h = get_text_size(*curr_item, init_width - (x == 0 ? float_size.x : x)).y;
			t->set_max_width(init_width - (x == 0 ? float_size.x : x));
			int tmp_h = t->size().y;

			if (is_text && key == "text") {
				t->add_text("\n\n");
			}
			is_text = false;

			if(key == "ref") {

				add_link(*t, line, child["dst"], links, info, float_size.x);
				is_image = false;

				DBG_GUI_RL << "ref: dst=" << child["dst"];

			} else if(std::find(format_tags.begin(), format_tags.end(), key) != format_tags.end()) {
				// TODO only the formatting tags here support nesting atm
				const auto [start, end] = t->add_text(line);
				t->add_attribute(key, start, end, "");
				config parsed_children = generate_layout(child, point(x, prev_blk_height), init_width, links, info).first;

				for (const auto [parsed_key, parsed_cfg] : parsed_children.all_children_view()) {
					if (parsed_key == "text") {
						const auto [start, end] = t->add_text(parsed_cfg["text"]);
						for (const config& attr : parsed_cfg.child_range("attribute")) {
							t->add_attribute(attr["name"], start + attr["start"].to_int(), start + attr["end"].to_int(), attr["value"]);
						}
						t->add_attribute(key, start, end, "");
					} else {
						text_dom.add_child(parsed_key, parsed_cfg);
					}
				}

				is_image = false;

				DBG_GUI_RL << key << ": text=" << gui2::debug_truncate(line);

			} else if(key == "header" || key == "h") {

				const auto [start, end] = t->add_text(line);
				t->add_attribute("weight", start, end, "heavy");
				t->add_attribute("color", start, end, font::string_to_color("white").to_hex_string());
				t->add_attribute("size", start, end, std::to_string(font::SIZE_TITLE - 2));

				is_image = false;

				DBG_GUI_RL << "h: text=" << line;

			} else if(key == "character_entity") {
				line = "&" + child["name"].str() + ";";

				const auto [start, end] = t->add_text(line);
				t->add_attribute("face", start, end, "monospace");
				t->add_attribute("color", start, end, font::string_to_color("red").to_hex_string());

				is_image = false;

				DBG_GUI_RL << "entity: text=" << line;

			} else if(key == "span" || key == "format") {

				const auto [start, end] = t->add_text(line);
				DBG_GUI_RL << "span/format: text=" << line;
				DBG_GUI_RL << "attributes:";

				for (const auto& [key, value] : child.attribute_range()) {
					if (key != "text") {
						t->add_attribute(key, start, end, value);
						DBG_GUI_RL << key << "=" << value;
					}
				}

				is_image = false;

			} else if (key == "text") {

				DBG_GUI_RL << "text: text=" << gui2::debug_truncate(line) << "...";

				t->add_text(line);
				t->set_max_width(init_width - (x == 0 ? float_size.x : x));
				point text_size = t->size();
				text_size.x -= x;

				is_text = true;

				if (wrap_mode && (float_size.y > 0) && (text_size.y > float_size.y)) {
					DBG_GUI_RL << "wrap start";

					size_t len = get_split_location(t->get_text(), point(init_width - float_size.x, float_size.y * video::get_pixel_scale()));
					DBG_GUI_RL << "wrap around area: " << float_size;

					// first part of the text
					std::string removed_part = t->get_text().str().substr(len+1);
					t->set_text(t->get_text().str().substr(0, len));
					float_size = point(0,0);

					// Height update
					t->set_max_width(init_width - float_size.x);
					int ah = t->size().y;
					if (tmp_h > ah) {
						tmp_h = 0;
					}
					text_height += ah - tmp_h;

					prev_blk_height += text_height + 0.3*font::get_max_height(info.font_size);
					pos = point(origin.x, prev_blk_height);

					DBG_GUI_RL << "wrap: " << prev_blk_height << "," << text_height;
					text_height = 0;

					// New text block
					x = origin.x;
					wrap_mode = false;

					// rest of the text
					text t(init_width - pos.x - float_size.x, info);
					t.set_origin(pos);
					tmp_h = t.size().y;
					const auto [start, end] = t.add_text(line);
					t.add_attribute(key, start, end, "");
					curr_item = &t;

				} else if ((float_size.y > 0) && (text_size.y < float_size.y)) {
					//TODO padding?
					// text height less than floating image's height, don't split
					DBG_GUI_RL << "no wrap";
					pos.y += text_size.y;
				}

				if (!wrap_mode) {
					float_size = point(0,0);
				}

				is_image = false;
			}

			t->set_max_width(x == 0 ? float_size.x : x);
			point size = t->size();
			// update text size and widget height
			if (tmp_h > size.y) {
				tmp_h = 0;
			}
			w = std::max(w, x + size.x);

			text_height += size.y - tmp_h;
			pos.y += size.y - tmp_h;

			if (remaining_item) {
				x = origin.x;
				pos = point(origin.x, pos.y + img_size.y);
				text_dom.append(*remaining_item);
				remaining_item = nullptr;
				// TODO curr_item pointer did not get changed
			}
		}

		if (!is_image && !wrap_mode && img_size.y > 0) {
			img_size = point(0,0);
		}

		DBG_GUI_RL << "X: " << x;
		DBG_GUI_RL << "Prev block height: " << prev_blk_height << " Current text block height: " << text_height;
		DBG_GUI_RL << "Height: " << h;
		h = text_height + prev_blk_height;
		DBG_GUI_RL << "-----------";
	} // for loop ends

	if (w == 0) {
		w = init_width;
	}

	// DEBUG: draw boxes around links
	#if LINK_DEBUG_BORDER
	if (finalize) {
		for (const auto& entry : links_) {
			config& link_rect_cfg = text_dom.add_child("rectangle");
			link_rect_cfg["x"] = entry.first.x;
			link_rect_cfg["y"] = entry.first.y;
			link_rect_cfg["w"] = entry.first.w;
			link_rect_cfg["h"] = entry.first.h;
			link_rect_cfg["border_thickness"] = 1;
			link_rect_cfg["border_color"] = "255, 180, 0, 255";
		}
	}
	#endif

	// TODO float and a mix of floats and images and tables
	h = std::max(img_size.y, h);

	DBG_GUI_RL << "Width: " << w << " Height: " << h << " Origin: " << origin;
	return { text_dom, point(w, h - origin.y) };
} // function ends

} // end namespace gui2