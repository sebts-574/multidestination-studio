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

#include <QString>

#include <functional>
#include <set>
#include <string>
#include <vector>

class Destination;
class QComboBox;
class QDialog;
class QScreen;
class QString;
class QWidget;

namespace Dialogs {

/* Nom et destinations couvertes d'une scène. isNew : scène qui vient d'être
 * créée (toutes les destinations cochées par défaut). */
bool EditScene(QWidget *parent, obs_source_t *scene, std::string &name, std::set<Destination *> &coverage, bool isNew);

struct DestinationSettings {
	std::string name;
	uint32_t width = 1920;
	uint32_t height = 1080;
	std::string outputScreen;   /* nom de l'écran, vide = aucun */
	bool addToExisting = false; /* création uniquement */
	bool openBlending = false;  /* « Output blending… » demandé */
};

/* Nouvelle destination (dest == nullptr) ou paramètres d'une destination. */
bool EditDestination(QWidget *parent, Destination *dest, DestinationSettings &settings);

/* « Êtes-vous sûr ? » avant de supprimer une destination. */
bool ConfirmDeleteDestination(QWidget *parent, Destination *dest);

/* Réglage : après une transition, l'ancien Program revient en Aperçu
 * (swap = true) ou la scène passée à l'antenne reste en Aperçu. */
bool EditAfterTakeSetting(QWidget *parent, bool &swap);
/* Réglage : toutes les destinations affichées (showAll = true) ou
 * seulement celles de la scène sélectionnée. */
bool EditDisplaySetting(QWidget *parent, bool &showAll);

/* Réglage de démarrage : interrupteur « Launch Multidestination Studio on
 * startup », appliqué immédiatement. */
void EditStartupSetting(QWidget *parent);

/* Fenêtre de réglage à interrupteurs (appliqués dès le clic), avec une
 * note explicative au-dessus. */
struct SwitchOption {
	QString text;
	bool value = false;
	std::function<void(bool)> apply;
};
void EditSwitchSetting(QWidget *parent, const QString &title, const QString &note,
		       const std::vector<SwitchOption> &options);

/* Langue du plugin : "" = celle d'OBS, sinon un code de langue. */
bool EditLanguageSetting(QWidget *parent, std::string &locale);

/* Écran physique par son nom, ou nullptr. */
QScreen *FindScreen(const std::string &name);
QString ScreenLabel(QScreen *screen, int index);
/* Liste des écrans (« Aucun » en premier) ; data = nom de l'écran. */
QComboBox *ScreenCombo(const std::string &current, QWidget *parent);

/* Entrée dans un champ valide la valeur sans fermer la fenêtre : la
 * touche n'active un bouton que s'il a le focus. */
void GuardEnterKey(QDialog *dialog);

} // namespace Dialogs
