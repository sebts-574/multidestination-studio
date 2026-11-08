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

#pragma once

#include <obs.h>

/* Annulation (Ctrl+Z) des modifications faites dans Multidestination Studio.
 * On mémorise l'état d'une scène avant chaque modification. */
namespace Undo {

/* Placement, recadrage, visibilité et ordre de toutes les sources de la
 * scène, avant un déplacement, un redimensionnement, etc. */
void PushSceneState(obs_scene_t *scene);
/* Une source vient d'être ajoutée : l'annulation la retire. */
void PushAdded(obs_scene_t *scene, int64_t itemId);
/* Une source va être retirée : l'annulation la remet à sa place. */
void PushRemoved(obs_scene_t *scene, obs_sceneitem_t *item);

/* Annule la dernière modification encore applicable. */
bool UndoLast();
void Clear();

} // namespace Undo
