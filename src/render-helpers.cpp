/*
Multidestination Studio
Copyright (C) 2026 sebts

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

#include "render-helpers.hpp"

#include <graphics/matrix4.h>

FitRect ComputeFit(uint32_t srcW, uint32_t srcH, uint32_t cx, uint32_t cy)
{
	FitRect r;
	if (srcW == 0 || srcH == 0 || cx == 0 || cy == 0)
		return r;

	const double windowAspect = double(cx) / double(cy);
	const double baseAspect = double(srcW) / double(srcH);

	if (windowAspect > baseAspect) {
		r.scale = float(double(cy) / double(srcH));
		r.cx = int(double(cy) * baseAspect);
		r.cy = int(cy);
	} else {
		r.scale = float(double(cx) / double(srcW));
		r.cx = int(cx);
		r.cy = int(double(cx) / baseAspect);
	}
	r.x = (int(cx) - r.cx) / 2;
	r.y = (int(cy) - r.cy) / 2;
	return r;
}

void DrawFitted(uint32_t srcW, uint32_t srcH, uint32_t cx, uint32_t cy, const std::function<void(float)> &draw)
{
	const FitRect fit = ComputeFit(srcW, srcH, cx, cy);
	if (fit.cx <= 0 || fit.cy <= 0)
		return;

	gs_viewport_push();
	gs_projection_push();

	gs_ortho(0.0f, float(srcW), 0.0f, float(srcH), -100.0f, 100.0f);
	gs_set_viewport(fit.x, fit.y, fit.cx, fit.cy);

	draw(fit.scale);

	gs_projection_pop();
	gs_viewport_pop();
}

void DrawSolidRect(float x, float y, float w, float h, uint32_t argb)
{
	if (w <= 0.0f || h <= 0.0f)
		return;

	gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
	gs_eparam_t *color = gs_effect_get_param_by_name(solid, "color");

	gs_blend_state_push();
	gs_enable_blending(true);
	gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);

	gs_effect_set_color(color, argb);

	gs_matrix_push();
	gs_matrix_translate3f(x, y, 0.0f);
	gs_matrix_scale3f(w, h, 1.0f);
	while (gs_effect_loop(solid, "Solid"))
		gs_draw_sprite(nullptr, 0, 1, 1);
	gs_matrix_pop();

	gs_blend_state_pop();
}

void DrawOutline(float x, float y, float w, float h, float t, uint32_t argb)
{
	DrawSolidRect(x - t, y - t, w + 2.0f * t, t, argb);
	DrawSolidRect(x - t, y + h, w + 2.0f * t, t, argb);
	DrawSolidRect(x - t, y, t, h, argb);
	DrawSolidRect(x + w, y, t, h, argb);
}
