/*
Multidestination Studio
Copyright (C) 2026 Sébastien Tombre

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "blend.hpp"

#include <obs.hpp>

#include <algorithm>

bool BlendConfig::Active() const
{
	if (!HasLayout())
		return false;
	for (const auto &o : outputs) {
		if (o.placed)
			return true;
	}
	return false;
}

void BlendConfig::ComputeAutoBlend()
{
	for (size_t i = 0; i < outputs.size(); i++) {
		BlendOutput &a = outputs[i];
		if (!a.placed || !a.autoBlend)
			continue;
		a.left = a.right = a.top = a.bottom = 0.0;

		for (size_t j = 0; j < outputs.size(); j++) {
			const BlendOutput &b = outputs[j];
			if (i == j || !b.placed)
				continue;
			const double ix0 = std::max(a.x, b.x);
			const double ix1 = std::min(a.x + a.w, b.x + b.w);
			const double iy0 = std::max(a.y, b.y);
			const double iy1 = std::min(a.y + a.h, b.y + b.h);
			const double ow = ix1 - ix0;
			const double oh = iy1 - iy0;
			if (ow <= 0.5 || oh <= 0.5)
				continue;

			/* Bande verticale = voisins gauche/droite ; bande
			 * horizontale = voisins haut/bas. */
			if (ow <= oh) {
				if (b.x + b.w * 0.5 > a.x + a.w * 0.5)
					a.right = std::max(a.right, ow);
				else
					a.left = std::max(a.left, ow);
			} else {
				if (b.y + b.h * 0.5 > a.y + a.h * 0.5)
					a.bottom = std::max(a.bottom, oh);
				else
					a.top = std::max(a.top, oh);
			}
		}
	}
}

void BlendConfig::Resize(uint32_t newWidth, uint32_t newHeight)
{
	if (newWidth == 0 || newHeight == 0)
		return;
	if (HasLayout()) {
		/* Positions : proportionnelles sur chaque axe. Tailles : même
		 * facteur sur les deux axes pour garder la forme de l'écran. */
		const double sx = double(newWidth) / double(width);
		const double sy = double(newHeight) / double(height);
		const double s = std::min(sx, sy);
		for (auto &o : outputs) {
			const double cx = (o.x + o.w * 0.5) * sx;
			const double cy = (o.y + o.h * 0.5) * sy;
			o.w *= s;
			o.h *= s;
			o.x = cx - o.w * 0.5;
			o.y = cy - o.h * 0.5;
			o.left *= s;
			o.right *= s;
			o.top *= s;
			o.bottom *= s;
		}
	}
	width = newWidth;
	height = newHeight;
	for (auto &o : outputs)
		ClampInside(o);
	ComputeAutoBlend();
}

void BlendConfig::ClampInside(BlendOutput &o) const
{
	if (!HasLayout() || o.w <= 0.0 || o.h <= 0.0)
		return;
	/* Trop grande : réduite en gardant sa forme. */
	const double fit = std::min({1.0, double(width) / o.w, double(height) / o.h});
	o.w *= fit;
	o.h *= fit;
	o.x = std::clamp(o.x, 0.0, double(width) - o.w);
	o.y = std::clamp(o.y, 0.0, double(height) - o.h);
}

void BlendConfig::Place(BlendOutput &o, double centerX, double centerY, uint32_t nativeW, uint32_t nativeH) const
{
	o.w = nativeW > 0 ? double(nativeW) : 1920.0;
	o.h = nativeH > 0 ? double(nativeH) : 1080.0;
	o.x = centerX - o.w * 0.5;
	o.y = centerY - o.h * 0.5;
	o.placed = true;
	ClampInside(o);
}

void BlendConfig::Save(obs_data_t *data) const
{
	obs_data_set_int(data, "width", width);
	obs_data_set_int(data, "height", height);
	obs_data_set_bool(data, "test_pattern", testPattern);

	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (const auto &o : outputs) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "name", o.name.c_str());
		obs_data_set_string(item, "screen", o.screen.c_str());
		obs_data_set_bool(item, "placed", o.placed);
		obs_data_set_double(item, "x", o.x);
		obs_data_set_double(item, "y", o.y);
		obs_data_set_double(item, "w", o.w);
		obs_data_set_double(item, "h", o.h);
		obs_data_set_bool(item, "auto_blend", o.autoBlend);
		obs_data_set_bool(item, "lock_aspect", o.lockAspect);
		obs_data_set_double(item, "left", o.left);
		obs_data_set_double(item, "right", o.right);
		obs_data_set_double(item, "top", o.top);
		obs_data_set_double(item, "bottom", o.bottom);
		obs_data_set_double(item, "gamma", o.gamma);
		obs_data_set_double(item, "curve", o.curve);
		obs_data_set_double(item, "black_level", o.blackLevel);
		OBSDataArrayAutoRelease pin = obs_data_array_create();
		for (double v : o.pin) {
			OBSDataAutoRelease p = obs_data_create();
			obs_data_set_double(p, "v", v);
			obs_data_array_push_back(pin, p);
		}
		obs_data_set_array(item, "pin", pin);
		obs_data_array_push_back(array, item);
	}
	obs_data_set_array(data, "outputs", array);
}

void BlendConfig::Load(obs_data_t *data)
{
	*this = BlendConfig();
	if (!data)
		return;
	width = uint32_t(obs_data_get_int(data, "width"));
	height = uint32_t(obs_data_get_int(data, "height"));
	testPattern = obs_data_get_bool(data, "test_pattern");

	OBSDataArrayAutoRelease array = obs_data_get_array(data, "outputs");
	const size_t count = obs_data_array_count(array);
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease item = obs_data_array_item(array, i);
		BlendOutput o;
		obs_data_set_default_bool(item, "auto_blend", true);
		obs_data_set_default_double(item, "gamma", 2.2);
		obs_data_set_default_double(item, "curve", 2.0);
		o.name = obs_data_get_string(item, "name");
		o.screen = obs_data_get_string(item, "screen");
		o.placed = obs_data_get_bool(item, "placed");
		o.x = obs_data_get_double(item, "x");
		o.y = obs_data_get_double(item, "y");
		o.w = obs_data_get_double(item, "w");
		o.h = obs_data_get_double(item, "h");
		o.autoBlend = obs_data_get_bool(item, "auto_blend");
		o.lockAspect = !obs_data_has_user_value(item, "lock_aspect") || obs_data_get_bool(item, "lock_aspect");
		o.left = obs_data_get_double(item, "left");
		o.right = obs_data_get_double(item, "right");
		o.top = obs_data_get_double(item, "top");
		o.bottom = obs_data_get_double(item, "bottom");
		o.gamma = obs_data_get_double(item, "gamma");
		o.curve = obs_data_get_double(item, "curve");
		o.blackLevel = obs_data_get_double(item, "black_level");
		OBSDataArrayAutoRelease pin = obs_data_get_array(item, "pin");
		for (size_t k = 0; k < 8 && k < obs_data_array_count(pin); k++) {
			OBSDataAutoRelease p = obs_data_array_item(pin, k);
			o.pin[k] = obs_data_get_double(p, "v");
		}
		outputs.push_back(o);
	}
}

std::string BlendConfig::ToJson() const
{
	OBSDataAutoRelease data = obs_data_create();
	Save(data);
	const char *json = obs_data_get_json(data);
	return json ? json : "";
}
