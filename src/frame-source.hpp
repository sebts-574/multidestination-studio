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

#pragma once

#include <obs.h>

/* Cadre vide (« frame ») : une source invisible d'une taille donnée, placée
 * dans un groupe. Le groupe prend sa taille ; les fichiers déposés dessus
 * dans le studio s'y placent, ajustés au cadre en gardant leurs
 * proportions. Le cadre n'est dessiné que dans le studio. */
namespace FrameSource {

constexpr const char *ID = "mds_frame";

void Register();
bool Is(obs_source_t *source);
bool IsItem(obs_sceneitem_t *item);

} // namespace FrameSource
