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

#include "frame-source.hpp"
#include "text.hpp"

#include <algorithm>
#include <cstring>
#include <string>

namespace {

struct Frame {
	uint32_t width = 1920;
	uint32_t height = 1080;
};

const char *GetName(void *)
{
	static std::string name;
	name = Tr("MDS.FrameSourceType").toStdString();
	return name.c_str();
}

void Update(void *data, obs_data_t *settings)
{
	auto *f = static_cast<Frame *>(data);
	f->width = uint32_t(std::max<long long>(1, obs_data_get_int(settings, "width")));
	f->height = uint32_t(std::max<long long>(1, obs_data_get_int(settings, "height")));
}

void *Create(obs_data_t *settings, obs_source_t *)
{
	auto *f = new Frame;
	Update(f, settings);
	return f;
}

void Destroy(void *data)
{
	delete static_cast<Frame *>(data);
}

uint32_t Width(void *data)
{
	return static_cast<Frame *>(data)->width;
}

uint32_t Height(void *data)
{
	return static_cast<Frame *>(data)->height;
}

void Defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "width", 1920);
	obs_data_set_default_int(settings, "height", 1080);
}

obs_properties_t *Properties(void *)
{
	obs_properties_t *props = obs_properties_create();
	const std::string w = Tr("MDS.FrameWidth").toStdString();
	const std::string h = Tr("MDS.FrameHeight").toStdString();
	obs_properties_add_int(props, "width", w.c_str(), 1, 16384, 1);
	obs_properties_add_int(props, "height", h.c_str(), 1, 16384, 1);
	return props;
}

/* Rien à l'image : le cadre n'apparaît que dans le studio. */
void Render(void *, gs_effect_t *) {}

} // namespace

namespace FrameSource {

void Register()
{
	struct obs_source_info info = {};
	info.id = ID;
	info.type = OBS_SOURCE_TYPE_INPUT;
	/* Absent du menu « + » d'OBS : un cadre se crée depuis le studio. */
	info.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_CAP_DISABLED;
	info.get_name = GetName;
	info.create = Create;
	info.destroy = Destroy;
	info.update = Update;
	info.get_width = Width;
	info.get_height = Height;
	info.get_defaults = Defaults;
	info.get_properties = Properties;
	info.video_render = Render;
	info.icon_type = OBS_ICON_TYPE_CUSTOM;
	obs_register_source(&info);
}

bool Is(obs_source_t *source)
{
	const char *id = source ? obs_source_get_unversioned_id(source) : nullptr;
	return id && strcmp(id, ID) == 0;
}

bool IsItem(obs_sceneitem_t *item)
{
	return item && Is(obs_sceneitem_get_source(item));
}

} // namespace FrameSource
