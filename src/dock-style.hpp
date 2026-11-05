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

#include <QIcon>
#include <QString>

#include <functional>

class QAction;
class QToolBar;
class QTreeWidget;
class QWidget;

/* Apparence des docks comme ceux d'OBS : barre d'outils du bas avec les
 * icônes du thème d'OBS (classes icon-plus, icon-trash…), lignes
 * arrondies, icônes des types de sources. */
namespace DockStyle {

QToolBar *Toolbar(QWidget *parent);
/* iconClass : classe du thème d'OBS ("icon-plus"…) ; fallback : icône si le
 * thème ne la fournit pas. */
QAction *AddAction(QToolBar *bar, const char *iconClass, const QIcon &fallback, const QString &tip,
		   const std::function<void()> &fn);
void StyleTree(QTreeWidget *tree, bool trailingIcons = false);
QWidget *Panel(QWidget *list, QToolBar *bar, QWidget *parent);

/* Icône exposée par la fenêtre principale d'OBS (groupIcon, sceneIcon…). */
QIcon MainIcon(const char *property);
QIcon SourceIcon(obs_source_t *source);
/* Œil et cadenas du thème d'OBS (dessinés si le thème ne les a pas). */
QIcon VisibilityIcon(bool visible, const QColor &color);
QIcon LockIcon(bool locked, const QColor &color);
/* « + » de secours (le thème d'OBS fournit le sien). */
QIcon PlusIcon();

} // namespace DockStyle
