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

class Destination;

/* Rendu d'une sortie blendée (thread graphique). Chaque fenêtre de sortie
 * possède son propre BlendRenderer. */
class BlendRenderer {
public:
	BlendRenderer() = default;
	~BlendRenderer();

	BlendRenderer(const BlendRenderer &) = delete;
	BlendRenderer &operator=(const BlendRenderer &) = delete;

	/* Dessine la sortie « index » de la destination dans une zone cx × cy.
	 * outW × outH : taille native de la sortie (corner pin en pixels de la
	 * sortie) ; 0 = taille de la zone du layout. */
	void Draw(Destination *dest, size_t index, uint32_t cx, uint32_t cy, uint32_t outW, uint32_t outH);

	/* Chemin de l'effet : à appeler au chargement du module. */
	static void Init();
	/* Libère l'effet partagé : à appeler au déchargement du module. */
	static void Shutdown();

private:
	gs_texrender_t *texrender = nullptr;
};
