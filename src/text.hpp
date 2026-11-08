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

#include <QString>

#include <string>

/* Texte traduit. Si les fichiers de langue sont introuvables (plugin mal
 * installé), on affiche le texte anglais intégré, jamais la clé brute. */
QString Tr(const char *key);

/* Langue de l'interface du plugin, indépendante de celle d'OBS :
 * "" = suivre OBS, sinon "en-US", "fr-FR" ou "nb-NO". */
void SetUiLanguage(const std::string &locale);
