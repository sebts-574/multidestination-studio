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

#include <functional>

/* Placement d'une image source (w × h) centrée, à l'échelle, dans une zone
 * cx × cy (pixels écran). */
struct FitRect {
	int x = 0;
	int y = 0;
	int cx = 0;
	int cy = 0;
	float scale = 1.0f; /* pixels écran par pixel source */
};

FitRect ComputeFit(uint32_t srcW, uint32_t srcH, uint32_t cx, uint32_t cy);

/* Prépare viewport + projection pour dessiner en coordonnées source, appelle
 * draw(scale), puis restaure. Thread graphique uniquement. */
void DrawFitted(uint32_t srcW, uint32_t srcH, uint32_t cx, uint32_t cy, const std::function<void(float)> &draw);

/* Primitives en coordonnées courantes (thread graphique). Couleur 0xAARRGGBB. */
void DrawSolidRect(float x, float y, float w, float h, uint32_t argb);
void DrawOutline(float x, float y, float w, float h, float thickness, uint32_t argb);
