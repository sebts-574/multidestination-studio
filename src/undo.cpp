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

#include "undo.hpp"
#include "scene-editor.hpp"

#include <obs.hpp>

#include <deque>
#include <vector>

namespace Undo {

namespace {

struct ItemState {
	int64_t id = -1;
	SceneEditor::Transform transform;
	bool visible = true;
	/* Groupe : ses sources (elles aussi déplaçables une à une). */
	std::vector<ItemState> children;
};

enum class Kind { SceneState, Added, Removed };

struct Entry {
	Kind kind = Kind::SceneState;
	OBSWeakSource scene;
	std::vector<ItemState> items; /* SceneState : de bas en haut */
	int64_t addedId = -1;         /* Added */
	OBSWeakSource removedSource;  /* Removed */
	ItemState removed;
	int removedOrder = 0;
};

constexpr size_t MAX_ENTRIES = 200;
std::deque<Entry> stack;

void Push(Entry &&entry)
{
	stack.push_back(std::move(entry));
	while (stack.size() > MAX_ENTRIES)
		stack.pop_front();
}

std::vector<ItemState> Capture(obs_scene_t *scene)
{
	std::vector<ItemState> items;
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *list = static_cast<std::vector<ItemState> *>(param);
			ItemState st;
			st.id = obs_sceneitem_get_id(item);
			st.transform = SceneEditor::GetTransform(item);
			st.visible = obs_sceneitem_visible(item);
			if (obs_sceneitem_is_group(item))
				st.children = Capture(obs_sceneitem_group_get_scene(item));
			list->push_back(st);
			return true;
		},
		&items);
	return items;
}

int OrderOf(obs_scene_t *scene, obs_sceneitem_t *target)
{
	struct Ctx {
		obs_sceneitem_t *target;
		int index;
		int found;
	} ctx{target, 0, 0};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<Ctx *>(param);
			if (item == c->target)
				c->found = c->index;
			c->index++;
			return true;
		},
		&ctx);
	return ctx.found;
}

bool Apply(const Entry &e)
{
	OBSSource sceneSource = OBSGetStrongRef(e.scene);
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	if (!scene)
		return false;

	switch (e.kind) {
	case Kind::SceneState: {
		bool any = false;
		int position = 0;
		for (const auto &st : e.items) {
			obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, st.id);
			if (!item)
				continue;
			/* Sources du groupe d'abord, puis le groupe lui-même (OBS
			 * recalcule le cadre du groupe autour de ses sources). */
			if (obs_sceneitem_is_group(item) && !st.children.empty()) {
				obs_scene_t *inner = obs_sceneitem_group_get_scene(item);
				obs_sceneitem_defer_group_resize_begin(item);
				for (const auto &child : st.children) {
					obs_sceneitem_t *c = obs_scene_find_sceneitem_by_id(inner, child.id);
					if (!c)
						continue;
					SceneEditor::SetTransform(c, child.transform);
					obs_sceneitem_set_visible(c, child.visible);
				}
				obs_sceneitem_defer_group_resize_end(item);
			}
			SceneEditor::SetTransform(item, st.transform);
			obs_sceneitem_set_visible(item, st.visible);
			obs_sceneitem_set_order_position(item, position++);
			any = true;
		}
		return any;
	}
	case Kind::Added: {
		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, e.addedId);
		if (!item)
			return false;
		obs_sceneitem_remove(item);
		return true;
	}
	case Kind::Removed: {
		OBSSource source = OBSGetStrongRef(e.removedSource);
		if (!source)
			return false;
		obs_sceneitem_t *item = obs_scene_add(scene, source);
		if (!item)
			return false;
		SceneEditor::SetTransform(item, e.removed.transform);
		obs_sceneitem_set_visible(item, e.removed.visible);
		obs_sceneitem_set_order_position(item, e.removedOrder);
		return true;
	}
	}
	return false;
}

} // namespace

void PushSceneState(obs_scene_t *scene)
{
	if (!scene)
		return;
	Entry e;
	e.kind = Kind::SceneState;
	e.scene = OBSGetWeakRef(obs_scene_get_source(scene));
	e.items = Capture(scene);
	Push(std::move(e));
}

void PushAdded(obs_scene_t *scene, int64_t itemId)
{
	if (!scene || itemId < 0)
		return;
	Entry e;
	e.kind = Kind::Added;
	e.scene = OBSGetWeakRef(obs_scene_get_source(scene));
	e.addedId = itemId;
	Push(std::move(e));
}

void PushRemoved(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (!scene || !item)
		return;
	Entry e;
	e.kind = Kind::Removed;
	e.scene = OBSGetWeakRef(obs_scene_get_source(scene));
	e.removedSource = OBSGetWeakRef(obs_sceneitem_get_source(item));
	e.removed.transform = SceneEditor::GetTransform(item);
	e.removed.visible = obs_sceneitem_visible(item);
	e.removedOrder = OrderOf(scene, item);
	Push(std::move(e));
}

bool UndoLast()
{
	/* Les entrées dont la scène ou la source a disparu sont sautées. */
	while (!stack.empty()) {
		Entry e = std::move(stack.back());
		stack.pop_back();
		if (Apply(e))
			return true;
	}
	return false;
}

void Clear()
{
	stack.clear();
}

} // namespace Undo
