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

#include <string>
#include <vector>

/*
 * Output blending
 * ---------------
 * Une destination « blendée » est un layout (par ex. 2000×700) découpé en
 * plusieurs sorties physiques (vidéoprojecteurs). Chaque sortie affiche une
 * zone du layout ; les zones se recouvrent et le recouvrement est fondu
 * (edge blending) pour que la jonction soit invisible.
 */
struct BlendOutput {
	std::string name;
	std::string screen; /* nom de l'écran de sortie (QScreen) */
	bool placed = false;

	/* Zone du layout affichée par cette sortie, en pixels du layout. */
	double x = 0.0;
	double y = 0.0;
	double w = 0.0;
	double h = 0.0;
	/* Proportions gardées quand on redimensionne (souris ou champs). */
	bool lockAspect = true;

	/* Largeur des zones de fondu sur chaque bord, en pixels du layout.
	 * En automatique : calculées à partir des recouvrements. */
	bool autoBlend = true;
	double left = 0.0;
	double right = 0.0;
	double top = 0.0;
	double bottom = 0.0;

	double gamma = 2.2;      /* correction gamma du fondu (projecteurs : ~2.2) */
	double curve = 2.0;      /* forme de la rampe (1 = linéaire, 2 = douce) */
	double blackLevel = 0.0; /* relevé du noir hors recouvrement (0 – 0,3) */

	/* Corner pin (keystone) : décalage de chaque coin, en pixels de la
	 * sortie. Ordre : haut-gauche x,y ; haut-droite x,y ; bas-gauche x,y ;
	 * bas-droite x,y. */
	double pin[8] = {0, 0, 0, 0, 0, 0, 0, 0};
};

struct BlendConfig {
	uint32_t width = 0; /* taille du layout = taille de la destination */
	uint32_t height = 0;
	std::vector<BlendOutput> outputs;
	bool testPattern = false;

	bool HasLayout() const { return width > 0 && height > 0; }
	bool Active() const;

	/* Fondus automatiques à partir des recouvrements entre sorties. */
	void ComputeAutoBlend();
	/* Nouvelle taille de layout : les sorties suivent proportionnellement. */
	void Resize(uint32_t newWidth, uint32_t newHeight);
	/* Garde une sortie dans le layout (réduite si trop grande). */
	void ClampInside(BlendOutput &o) const;
	/* Place une sortie à sa taille native, centrée sur un point. */
	void Place(BlendOutput &o, double centerX, double centerY, uint32_t nativeW, uint32_t nativeH) const;

	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);
	std::string ToJson() const;
};
