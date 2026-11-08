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

#include <obs-frontend-api.h>

#include <QString>
#include <QStringList>

class QWidget;

/* Un « show » est une collection de scènes d'OBS, avec les réglages du
 * plugin qu'elle contient (destinations, cues, transitions rapides…).
 *
 * OBS enregistre la collection tout seul (à chaque modification et à la
 * fermeture). Pour que « Ne pas enregistrer » ait un sens, on garde en
 * mémoire le fichier du show tel qu'il a été chargé ou enregistré ; si
 * l'utilisateur renonce à ses modifications, ce fichier est réécrit après
 * l'enregistrement automatique d'OBS. */
namespace Show {

enum class Answer { Clean, Saved, Discarded, Cancelled };

/* À appeler depuis les événements d'OBS. */
void OnFrontendEvent(enum obs_frontend_event event);

QString CurrentName();
QStringList Names();
bool IsDirty();

/* Enregistre l'état actuel dans le show chargé. */
void Save();

/* Demande s'il faut enregistrer les modifications (Enregistrer / Ne pas
 * enregistrer / Annuler). Rien n'est demandé si rien n'a changé. */
Answer AskToSave(QWidget *parent);

void New(QWidget *parent);
void SaveAs(QWidget *parent);
void Load(QWidget *parent, const QString &name);
void Import(QWidget *parent);
void Export(QWidget *parent);

} // namespace Show
