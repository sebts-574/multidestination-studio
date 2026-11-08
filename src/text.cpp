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

#include "text.hpp"
#include "default-strings.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <util/text-lookup.h>

#include <cstring>

static lookup_t *forcedLookup = nullptr;

void SetUiLanguage(const std::string &locale)
{
	if (forcedLookup) {
		text_lookup_destroy(forcedLookup);
		forcedLookup = nullptr;
	}
	if (locale.empty())
		return;

	char *path = obs_module_file(("locale/" + locale + ".ini").c_str());
	if (path)
		forcedLookup = text_lookup_create(path);
	bfree(path);
	if (!forcedLookup)
		obs_log(LOG_WARNING, "langue %s introuvable, langue d'OBS utilisée", locale.c_str());
}

QString Tr(const char *key)
{
	const char *out = nullptr;
	if (forcedLookup && text_lookup_getstr(forcedLookup, key, &out) && out)
		return QString::fromUtf8(out);
	if (obs_module_get_string(key, &out) && out)
		return QString::fromUtf8(out);

	static bool warned = false;
	if (!warned) {
		warned = true;
		obs_log(LOG_WARNING, "fichiers de langue introuvables, textes anglais intégrés utilisés");
	}

	for (const auto &entry : DEFAULT_STRINGS) {
		if (strcmp(entry.key, key) == 0)
			return QString::fromUtf8(entry.value);
	}
	return QString::fromUtf8(key);
}
