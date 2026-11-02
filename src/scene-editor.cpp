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

#include "scene-editor.hpp"
#include "render-helpers.hpp"

#include <cmath>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <obs.hpp>

namespace SceneEditor {

static constexpr uint32_t SELECTION_COLOR = 0xFF2ECC40; /* vert */
static constexpr uint32_t OUTSIDE_COLOR = 0xFFFF9500;   /* orange : dépasse du cadre */
static constexpr uint32_t HANDLE_COLOR = 0xFFFFFFFF;
static constexpr uint32_t GUIDE_COLOR = 0xFF2ECC40;
static constexpr uint32_t PLACEHOLDER_COLOR = 0xB0FFFFFF;

static void AlignOffset(uint32_t align, float w, float h, float &ox, float &oy)
{
	ox = (align & OBS_ALIGN_LEFT) ? 0.0f : (align & OBS_ALIGN_RIGHT) ? w : w * 0.5f;
	oy = (align & OBS_ALIGN_TOP) ? 0.0f : (align & OBS_ALIGN_BOTTOM) ? h : h * 0.5f;
}

bool UsesBounds(obs_sceneitem_t *item)
{
	return obs_sceneitem_get_bounds_type(item) != OBS_BOUNDS_NONE;
}

/* Taille d'image mémorisée par source (UUID) : une vidéo inactive n'a plus
 * de taille dans OBS, mais garde la sienne ici et dans ses réglages privés
 * (enregistrés avec la collection). */
static constexpr const char *SIZE_W_KEY = "destinations_native_w";
static constexpr const char *SIZE_H_KEY = "destinations_native_h";
static std::mutex sizeMutex;
static std::unordered_map<std::string, std::pair<uint32_t, uint32_t>> sizeCache;

bool SourceSize(obs_source_t *source, uint32_t &w, uint32_t &h)
{
	if (!source)
		return false;
	w = obs_source_get_width(source);
	h = obs_source_get_height(source);
	const char *uuid = obs_source_get_uuid(source);
	const std::string key = uuid ? uuid : "";

	if (w > 0 && h > 0) {
		if (!key.empty()) {
			std::lock_guard<std::mutex> lock(sizeMutex);
			sizeCache[key] = {w, h};
		}
		return true;
	}
	if (key.empty())
		return false;

	{
		std::lock_guard<std::mutex> lock(sizeMutex);
		auto it = sizeCache.find(key);
		if (it != sizeCache.end()) {
			w = it->second.first;
			h = it->second.second;
			return true;
		}
	}

	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	w = uint32_t(obs_data_get_int(priv, SIZE_W_KEY));
	h = uint32_t(obs_data_get_int(priv, SIZE_H_KEY));
	if (w == 0 || h == 0)
		return false;
	std::lock_guard<std::mutex> lock(sizeMutex);
	sizeCache[key] = {w, h};
	return true;
}

void RememberSizes(obs_scene_t *scene)
{
	if (!scene)
		return;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *) {
			obs_source_t *source = obs_sceneitem_get_source(item);
			const uint32_t w = obs_source_get_width(source);
			const uint32_t h = obs_source_get_height(source);
			if (w == 0 || h == 0 || !(obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				return true;
			uint32_t cw = 0, ch = 0;
			SourceSize(source, cw, ch); /* met le cache à jour */
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			if (uint32_t(obs_data_get_int(priv, SIZE_W_KEY)) != w ||
			    uint32_t(obs_data_get_int(priv, SIZE_H_KEY)) != h) {
				obs_data_set_int(priv, SIZE_W_KEY, w);
				obs_data_set_int(priv, SIZE_H_KEY, h);
			}
			return true;
		},
		nullptr);
}

bool IsInactiveVideo(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	return (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) && obs_source_get_width(source) == 0;
}

bool IsPlaceholder(obs_sceneitem_t *item)
{
	if (UsesBounds(item) || !IsInactiveVideo(item))
		return false;
	uint32_t w, h;
	return !SourceSize(obs_sceneitem_get_source(item), w, h);
}

bool IsRotated(obs_sceneitem_t *item)
{
	return std::fabs(obs_sceneitem_get_rot(item)) > 0.01f;
}

bool CroppedSourceSize(obs_sceneitem_t *item, float &w, float &h)
{
	uint32_t sw = 0;
	uint32_t sh = 0;
	if (!SourceSize(obs_sceneitem_get_source(item), sw, sh))
		return false;

	struct obs_sceneitem_crop crop = {};
	obs_sceneitem_get_crop(item, &crop);
	w = float(int(sw) - crop.left - crop.right);
	h = float(int(sh) - crop.top - crop.bottom);
	return w > 0.0f && h > 0.0f;
}

bool GetBox(obs_sceneitem_t *item, Box &box)
{
	if (!item)
		return false;

	float w = 0.0f;
	float h = 0.0f;
	if (UsesBounds(item)) {
		struct vec2 bounds;
		obs_sceneitem_get_bounds(item, &bounds);
		w = bounds.x;
		h = bounds.y;
	} else if (IsPlaceholder(item)) {
		w = PLACEHOLDER_W;
		h = PLACEHOLDER_H;
	} else {
		if (!CroppedSourceSize(item, w, h))
			return false;
		struct vec2 scale;
		obs_sceneitem_get_scale(item, &scale);
		w *= std::fabs(scale.x);
		h *= std::fabs(scale.y);
	}
	if (w <= 0.0f || h <= 0.0f)
		return false;

	struct vec2 pos;
	obs_sceneitem_get_pos(item, &pos);
	float ox = 0.0f;
	float oy = 0.0f;
	AlignOffset(obs_sceneitem_get_alignment(item), w, h, ox, oy);

	box.x = pos.x - ox;
	box.y = pos.y - oy;
	box.w = w;
	box.h = h;
	return true;
}

void SetBox(obs_sceneitem_t *item, const Box &box)
{
	if (!item || box.w < 1.0f || box.h < 1.0f)
		return;

	if (IsPlaceholder(item) && std::fabs(box.w - PLACEHOLDER_W) < 0.5f && std::fabs(box.h - PLACEHOLDER_H) < 0.5f) {
		/* Simple déplacement : la vidéo gardera sa taille d'origine. */
		float ox = 0.0f;
		float oy = 0.0f;
		AlignOffset(obs_sceneitem_get_alignment(item), PLACEHOLDER_W, PLACEHOLDER_H, ox, oy);
		struct vec2 pos;
		vec2_set(&pos, box.x + ox, box.y + oy);
		obs_sceneitem_set_pos(item, &pos);
		return;
	}
	if (IsPlaceholder(item)) {
		/* Taille d'image encore inconnue : on passe en cadre (bounds),
		 * l'image s'y adaptera, en gardant ses proportions, dès qu'elle
		 * sera connue. */
		obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_SCALE_INNER);
		obs_sceneitem_set_bounds_alignment(item, OBS_ALIGN_CENTER);
		struct vec2 one;
		vec2_set(&one, 1.0f, 1.0f);
		obs_sceneitem_set_scale(item, &one);
	}

	if (UsesBounds(item)) {
		struct vec2 bounds;
		vec2_set(&bounds, box.w, box.h);
		obs_sceneitem_set_bounds(item, &bounds);
	} else {
		float cw = 0.0f;
		float ch = 0.0f;
		if (!CroppedSourceSize(item, cw, ch))
			return;
		struct vec2 oldScale;
		obs_sceneitem_get_scale(item, &oldScale);
		struct vec2 scale;
		vec2_set(&scale, std::copysign(box.w / cw, oldScale.x == 0.0f ? 1.0f : oldScale.x),
			 std::copysign(box.h / ch, oldScale.y == 0.0f ? 1.0f : oldScale.y));
		obs_sceneitem_set_scale(item, &scale);
	}

	float ox = 0.0f;
	float oy = 0.0f;
	AlignOffset(obs_sceneitem_get_alignment(item), box.w, box.h, ox, oy);
	struct vec2 pos;
	vec2_set(&pos, box.x + ox, box.y + oy);
	obs_sceneitem_set_pos(item, &pos);
}

int64_t HitTest(obs_scene_t *scene, float x, float y)
{
	struct Ctx {
		float x, y;
		int64_t id;
	} ctx{x, y, -1};

	/* L'énumération va du bas vers le haut : la dernière trouvée est la
	 * plus haute. */
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<Ctx *>(param);
			if (!obs_sceneitem_visible(item) || obs_sceneitem_locked(item))
				return true;
			Box box;
			if (GetBox(item, box) && box.Contains(c->x, c->y))
				c->id = obs_sceneitem_get_id(item);
			return true;
		},
		&ctx);
	return ctx.id;
}

int HandleAt(const Box &box, float x, float y, float tol)
{
	const bool nearL = std::fabs(x - box.x) <= tol;
	const bool nearR = std::fabs(x - (box.x + box.w)) <= tol;
	const bool nearT = std::fabs(y - box.y) <= tol;
	const bool nearB = std::fabs(y - (box.y + box.h)) <= tol;
	const bool nearCX = std::fabs(x - (box.x + box.w * 0.5f)) <= tol;
	const bool nearCY = std::fabs(y - (box.y + box.h * 0.5f)) <= tol;

	int edge = EDGE_NONE;
	if (nearL && (nearT || nearB || nearCY))
		edge |= EDGE_LEFT;
	if (nearR && (nearT || nearB || nearCY))
		edge |= EDGE_RIGHT;
	if (nearT && (nearL || nearR || nearCX))
		edge |= EDGE_TOP;
	if (nearB && (nearL || nearR || nearCX))
		edge |= EDGE_BOTTOM;
	return edge;
}

void FitToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch)
{
	if (UsesBounds(item)) {
		SetBox(item, Box{0.0f, 0.0f, float(cw), float(ch)});
		return;
	}
	if (IsPlaceholder(item)) {
		/* Taille inconnue : cadre plein canvas, l'image s'y adaptera. */
		SetBox(item, Box{0.0f, 0.0f, float(cw), float(ch)});
		return;
	}
	float w = 0.0f;
	float h = 0.0f;
	if (!CroppedSourceSize(item, w, h))
		return;
	const float s = std::fmin(float(cw) / w, float(ch) / h);
	const float bw = w * s;
	const float bh = h * s;
	SetBox(item, Box{(float(cw) - bw) * 0.5f, (float(ch) - bh) * 0.5f, bw, bh});
}

void StretchToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch)
{
	SetBox(item, Box{0.0f, 0.0f, float(cw), float(ch)});
}

void CenterInCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch)
{
	Box box;
	if (!GetBox(item, box))
		return;
	box.x = (float(cw) - box.w) * 0.5f;
	box.y = (float(ch) - box.h) * 0.5f;
	SetBox(item, box);
}

void ResetTransform(obs_sceneitem_t *item)
{
	struct obs_sceneitem_crop crop = {};
	obs_sceneitem_set_crop(item, &crop);
	obs_sceneitem_set_bounds_type(item, OBS_BOUNDS_NONE);
	obs_sceneitem_set_rot(item, 0.0f);
	obs_sceneitem_set_alignment(item, OBS_ALIGN_TOP | OBS_ALIGN_LEFT);
	struct vec2 scale;
	vec2_set(&scale, 1.0f, 1.0f);
	obs_sceneitem_set_scale(item, &scale);
	struct vec2 pos;
	vec2_set(&pos, 0.0f, 0.0f);
	obs_sceneitem_set_pos(item, &pos);
}

Transform GetTransform(obs_sceneitem_t *item)
{
	Transform t;
	obs_sceneitem_get_info2(item, &t.info);
	obs_sceneitem_get_crop(item, &t.crop);
	return t;
}

void SetTransform(obs_sceneitem_t *item, const Transform &t)
{
	obs_sceneitem_set_info2(item, &t.info);
	obs_sceneitem_set_crop(item, &t.crop);
}

void CopyLayout(obs_scene_t *from, obs_scene_t *to)
{
	if (!from || !to || from == to)
		return;

	struct Item {
		OBSSource source;
		Transform transform;
		bool visible;
		obs_sceneitem_t *item;
	};
	auto collect = [](obs_scene_t *scene) {
		std::vector<Item> items;
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *it, void *param) {
				auto *list = static_cast<std::vector<Item> *>(param);
				list->push_back({OBSSource(obs_sceneitem_get_source(it)), GetTransform(it),
						 obs_sceneitem_visible(it), it});
				return true;
			},
			&items);
		return items;
	};

	std::vector<Item> src = collect(from);
	std::vector<Item> dst = collect(to);

	/* Les groupes d'OBS ne se partagent pas entre scènes : la copie à
	 * l'antenne a ses propres groupes. Deux groupes à la même place sont
	 * donc considérés comme le même, et leur contenu est recopié. */
	auto isGroup = [](const Item &it) {
		return obs_sceneitem_is_group(it.item);
	};
	auto same = [&](const Item &a, const Item &b) {
		return a.source.Get() == b.source.Get() || (isGroup(a) && isGroup(b));
	};
	auto copyItem = [&](const Item &from, obs_sceneitem_t *to) {
		SetTransform(to, from.transform);
		obs_sceneitem_set_visible(to, from.visible);
		if (obs_sceneitem_is_group(from.item) && obs_sceneitem_is_group(to) &&
		    obs_sceneitem_get_source(from.item) != obs_sceneitem_get_source(to))
			CopyLayout(obs_sceneitem_group_get_scene(from.item), obs_sceneitem_group_get_scene(to));
	};

	bool sameSources = src.size() == dst.size();
	for (size_t i = 0; sameSources && i < src.size(); i++)
		sameSources = same(src[i], dst[i]);

	if (sameSources) {
		for (size_t i = 0; i < src.size(); i++)
			copyItem(src[i], dst[i].item);
		return;
	}

	/* Sources ajoutées ou retirées : on reconstruit dans le même ordre.
	 * Les groupes existants sont gardés (dans l'ordre où ils
	 * apparaissent) au lieu d'être recréés. */
	std::vector<obs_sceneitem_t *> keptGroups;
	for (auto &d : dst) {
		if (isGroup(d))
			keptGroups.push_back(d.item);
		else
			obs_sceneitem_remove(d.item);
	}
	size_t nextGroup = 0;
	std::vector<obs_sceneitem_t *> order;
	for (auto &it : src) {
		obs_sceneitem_t *target = nullptr;
		if (isGroup(it)) {
			if (nextGroup < keptGroups.size())
				target = keptGroups[nextGroup++];
			else
				continue; /* un groupe ne peut pas être ajouté à une autre scène */
		} else {
			target = obs_scene_add(to, it.source);
		}
		if (!target)
			continue;
		copyItem(it, target);
		order.push_back(target);
	}
	for (; nextGroup < keptGroups.size(); nextGroup++)
		obs_sceneitem_remove(keptGroups[nextGroup]);
	for (size_t i = 0; i < order.size(); i++)
		obs_sceneitem_set_order_position(order[i], int(i));
}

void DrawSelection(obs_scene_t *scene, int64_t itemId, float screenScale, uint32_t canvasW, uint32_t canvasH)
{
	if (!scene || screenScale <= 0.0f)
		return;

	struct Ctx {
		int64_t id;
		float px; /* un pixel écran en unités canvas */
		float cw;
		float ch;
	} ctx{itemId, 1.0f / screenScale, float(canvasW), float(canvasH)};

	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<Ctx *>(param);
			if (obs_sceneitem_get_id(item) != c->id) {
				/* Vidéo inactive (sans image) : cadre toujours visible. */
				Box ph;
				if (obs_sceneitem_visible(item) && IsInactiveVideo(item) && GetBox(item, ph))
					DrawOutline(ph.x, ph.y, ph.w, ph.h, c->px, PLACEHOLDER_COLOR);
				return true;
			}

			Box box;
			if (!GetBox(item, box))
				return true;

			/* Guides quand la source est centrée sur le canvas. */
			const float tol = 0.5f;
			if (std::fabs(box.x + box.w * 0.5f - c->cw * 0.5f) <= tol)
				DrawSolidRect(c->cw * 0.5f - 0.5f * c->px, 0.0f, c->px, c->ch, GUIDE_COLOR);
			if (std::fabs(box.y + box.h * 0.5f - c->ch * 0.5f) <= tol)
				DrawSolidRect(0.0f, c->ch * 0.5f - 0.5f * c->px, c->cw, c->px, GUIDE_COLOR);

			/* Orange si la source dépasse du cadre (sans la limiter). */
			const float eps = 0.5f;
			const bool outside = box.x < -eps || box.y < -eps || box.x + box.w > c->cw + eps ||
					     box.y + box.h > c->ch + eps;
			const uint32_t color = outside ? OUTSIDE_COLOR : SELECTION_COLOR;

			DrawOutline(box.x, box.y, box.w, box.h, 2.0f * c->px, color);

			const float hs = 8.0f * c->px;
			const float xs[3] = {box.x, box.x + box.w * 0.5f, box.x + box.w};
			const float ys[3] = {box.y, box.y + box.h * 0.5f, box.y + box.h};
			for (int ix = 0; ix < 3; ix++) {
				for (int iy = 0; iy < 3; iy++) {
					if (ix == 1 && iy == 1)
						continue;
					DrawSolidRect(xs[ix] - hs * 0.5f - c->px, ys[iy] - hs * 0.5f - c->px,
						      hs + 2.0f * c->px, hs + 2.0f * c->px, color);
					DrawSolidRect(xs[ix] - hs * 0.5f, ys[iy] - hs * 0.5f, hs, hs, HANDLE_COLOR);
				}
			}
			return true;
		},
		&ctx);
}

} // namespace SceneEditor
