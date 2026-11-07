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

/* Géométrie des sources dans une scène, en pixels du canvas, sans rotation.
 * Indépendant de l'alignement OBS de chaque source : la position stockée
 * par OBS est celle du point d'alignement ; on travaille sur la boîte. */
namespace SceneEditor {

struct Box {
	float x = 0.0f;
	float y = 0.0f;
	float w = 0.0f;
	float h = 0.0f;

	bool Contains(float px, float py) const { return px >= x && px <= x + w && py >= y && py <= y + h; }
};

/* Poignées : combinaison de bords. */
enum Edge : int {
	EDGE_NONE = 0,
	EDGE_LEFT = 1 << 0,
	EDGE_RIGHT = 1 << 1,
	EDGE_TOP = 1 << 2,
	EDGE_BOTTOM = 1 << 3,
};

bool GetBox(obs_sceneitem_t *item, Box &box);
/* Redimensionne (échelle ou cadre selon la source) et place la boîte. */
void SetBox(obs_sceneitem_t *item, const Box &box);
bool IsRotated(obs_sceneitem_t *item);
bool UsesBounds(obs_sceneitem_t *item);

/* Taille de la source après recadrage, en pixels source. */
bool CroppedSourceSize(obs_sceneitem_t *item, float &w, float &h);

/* Source visible la plus haute sous le point, -1 si aucune. prefer : une
 * source déjà sélectionnée (y compris dans un groupe) gardée si le point est
 * dessus. */
int64_t HitTest(obs_scene_t *scene, float x, float y, int64_t prefer = -1);

/* Élément par id, au premier niveau ou dans un groupe de la scène. */
obs_sceneitem_t *FindItem(obs_scene_t *scene, int64_t id);
/* Boîte en pixels du canvas, aussi pour un élément d'un groupe (converti
 * par la transformation du groupe). top : la scène qui contient le groupe. */
bool GetWorldBox(obs_scene_t *top, obs_sceneitem_t *item, Box &box);
void SetWorldBox(obs_scene_t *top, obs_sceneitem_t *item, const Box &box);
/* Échelle à l'écran (élément × groupe). */
void WorldScale(obs_scene_t *top, obs_sceneitem_t *item, float &sx, float &sy);

/* Poignée sous le point (tolérance en pixels canvas). */
int HandleAt(const Box &box, float x, float y, float tolerance);

/* Actions de menu. */
void FitToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void StretchToCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void CenterInCanvas(obs_sceneitem_t *item, uint32_t cw, uint32_t ch);
void ResetTransform(obs_sceneitem_t *item);

/* Transformation complète (position, échelle, rotation, cadre, recadrage)
 * pour copier / coller. */
struct Transform {
	struct obs_transform_info info = {};
	struct obs_sceneitem_crop crop = {};
};
Transform GetTransform(obs_sceneitem_t *item);
void SetTransform(obs_sceneitem_t *item, const Transform &t);

/* Recopie sources, placement, visibilité et ordre d'une scène dans une
 * autre. */
void CopyLayout(obs_scene_t *from, obs_scene_t *to);

/* Taille du cadre provisoire d'une vidéo dont l'image n'a jamais été vue
 * (ni lue ni mémorisée). */
constexpr float PLACEHOLDER_W = 320.0f;
constexpr float PLACEHOLDER_H = 180.0f;
bool IsPlaceholder(obs_sceneitem_t *item);
/* Vidéo sans image en ce moment (inactive, pas encore lue). */
bool IsInactiveVideo(obs_sceneitem_t *item);
/* Taille d'image d'une source : réelle, sinon la dernière connue (une vidéo
 * inactive n'a plus de taille dans OBS). */
bool SourceSize(obs_source_t *source, uint32_t &w, uint32_t &h);
/* Mémorise dans les réglages de chaque source la taille de son image
 * (thread de l'interface). */
void RememberSizes(obs_scene_t *scene);

/* Dessin de la sélection et des guides de centrage (thread graphique,
 * coordonnées canvas). */
void DrawSelection(obs_scene_t *scene, int64_t itemId, float screenScale, uint32_t canvasW, uint32_t canvasH);

} // namespace SceneEditor
