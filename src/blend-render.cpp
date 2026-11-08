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

#include "blend-render.hpp"
#include "blend.hpp"
#include "destination.hpp"
#include "render-helpers.hpp"

#include <obs-module.h>
#include <plugin-support.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace {

std::string effectPath;
gs_effect_t *effect = nullptr;
bool effectFailed = false;

/* Grille de déformation du corner pin : 8 × 8 quadrilatères = 384 sommets
 * (limite du mode immédiat de libobs : 512). */
constexpr int GRID = 8;

gs_effect_t *Effect()
{
	if (!effect && !effectFailed && !effectPath.empty()) {
		char *errors = nullptr;
		effect = gs_effect_create_from_file(effectPath.c_str(), &errors);
		if (!effect) {
			effectFailed = true;
			obs_log(LOG_ERROR, "blend.effect : %s", errors ? errors : "?");
		}
		bfree(errors);
	}
	return effect;
}

struct Point {
	float x;
	float y;
};

Point Bilinear(const Point c[4], float u, float v)
{
	const float topX = c[0].x + (c[1].x - c[0].x) * u;
	const float topY = c[0].y + (c[1].y - c[0].y) * u;
	const float botX = c[2].x + (c[3].x - c[2].x) * u;
	const float botY = c[2].y + (c[3].y - c[2].y) * u;
	return {topX + (botX - topX) * v, topY + (botY - topY) * v};
}

} // namespace

void BlendRenderer::Init()
{
	char *path = obs_module_file("effects/blend.effect");
	effectPath = path ? path : "";
	bfree(path);
}

void BlendRenderer::Shutdown()
{
	obs_enter_graphics();
	gs_effect_destroy(effect);
	effect = nullptr;
	effectFailed = false;
	obs_leave_graphics();
}

BlendRenderer::~BlendRenderer()
{
	if (!texrender)
		return;
	obs_enter_graphics();
	gs_texrender_destroy(texrender);
	obs_leave_graphics();
}

void BlendRenderer::Draw(Destination *dest, size_t index, uint32_t cx, uint32_t cy, uint32_t outW, uint32_t outH)
{
	BlendOutput o;
	uint32_t layoutW = 0;
	uint32_t layoutH = 0;
	bool pattern = false;
	if (!dest || !dest->GetRenderOutput(index, o, layoutW, layoutH, pattern) || o.w < 1.0 || o.h < 1.0)
		return;

	gs_effect_t *fx = Effect();
	if (!fx)
		return;

	/* 1. Program de la destination dans une texture (taille du canvas ;
	 * pendant l'édition, le layout peut différer jusqu'à l'enregistrement). */
	const uint32_t canvasW = dest->Width();
	const uint32_t canvasH = dest->Height();
	if (canvasW == 0 || canvasH == 0)
		return;
	if (!texrender)
		texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_texrender_reset(texrender);
	if (gs_texrender_begin(texrender, canvasW, canvasH)) {
		struct vec4 clear;
		vec4_zero(&clear);
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, float(canvasW), 0.0f, float(canvasH), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_reset_blend_state();
		dest->RenderProgram();
		gs_blend_state_pop();
		gs_texrender_end(texrender);
	}
	gs_texture_t *tex = gs_texrender_get_texture(texrender);
	if (!tex)
		return;

	/* 2. Zone de la sortie, déformée par le corner pin, avec fondus. */
	if (outW == 0 || outH == 0) {
		outW = uint32_t(std::lround(o.w));
		outH = uint32_t(std::lround(o.h));
	}

	struct vec4 region;
	vec4_set(&region, float(o.x / layoutW), float(o.y / layoutH), float(o.w / layoutW), float(o.h / layoutH));
	struct vec4 edges;
	vec4_set(&edges, float(o.left / o.w), float(o.right / o.w), float(o.top / o.h), float(o.bottom / o.h));
	struct vec2 size;
	vec2_set(&size, float(layoutW), float(layoutH));

	gs_effect_set_texture(gs_effect_get_param_by_name(fx, "image"), tex);
	gs_effect_set_vec4(gs_effect_get_param_by_name(fx, "region"), &region);
	gs_effect_set_vec4(gs_effect_get_param_by_name(fx, "edges"), &edges);
	gs_effect_set_float(gs_effect_get_param_by_name(fx, "blend_gamma"), float(std::max(0.1, o.gamma)));
	gs_effect_set_float(gs_effect_get_param_by_name(fx, "curve"), float(std::max(0.1, o.curve)));
	gs_effect_set_float(gs_effect_get_param_by_name(fx, "black_level"), float(o.blackLevel));
	gs_effect_set_float(gs_effect_get_param_by_name(fx, "test_pattern"), pattern ? 1.0f : 0.0f);
	gs_effect_set_vec2(gs_effect_get_param_by_name(fx, "layout_size"), &size);

	const float w = float(outW);
	const float h = float(outH);
	const Point corners[4] = {
		{float(o.pin[0]), float(o.pin[1])},
		{w + float(o.pin[2]), float(o.pin[3])},
		{float(o.pin[4]), h + float(o.pin[5])},
		{w + float(o.pin[6]), h + float(o.pin[7])},
	};
	const float u0 = float(o.x / layoutW);
	const float v0 = float(o.y / layoutH);
	const float du = float(o.w / layoutW);
	const float dv = float(o.h / layoutH);

	DrawFitted(outW, outH, cx, cy, [&](float) {
		gs_blend_state_push();
		gs_enable_blending(false);
		while (gs_effect_loop(fx, "Draw")) {
			gs_render_start(true);
			for (int j = 0; j < GRID; j++) {
				for (int i = 0; i < GRID; i++) {
					const float ua = float(i) / GRID;
					const float ub = float(i + 1) / GRID;
					const float va = float(j) / GRID;
					const float vb = float(j + 1) / GRID;
					const float us[6] = {ua, ub, ua, ub, ub, ua};
					const float vs[6] = {va, va, vb, va, vb, vb};
					for (int k = 0; k < 6; k++) {
						const Point p = Bilinear(corners, us[k], vs[k]);
						gs_texcoord(u0 + us[k] * du, v0 + vs[k] * dv, 0);
						gs_vertex2f(p.x, p.y);
					}
				}
			}
			gs_render_stop(GS_TRIS);
		}
		gs_blend_state_pop();
	});
}
