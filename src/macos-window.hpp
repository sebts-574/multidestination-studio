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

class QWidget;

/* macOS : fenêtres de sortie (projection d'une destination sur son écran).
 * Sans effet sur les autres systèmes. */
namespace MacOSWindow {
/* Fenêtre de sortie : présente sur tous les Spaces et fixe (ne suit pas les
 * changements de Space, pas d'animation), au-dessus de la barre des menus
 * et du Dock pour couvrir tout l'écran, jamais en plein écran natif, hors
 * du cycle des fenêtres (Cmd+`). */
void MakeOutputWindow(QWidget *window);
/* Retour à une fenêtre normale (sortie passée en fenêtre). */
void MakeNormalWindow(QWidget *window);
} // namespace MacOSWindow
