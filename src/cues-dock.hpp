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

#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class OrganizerTree;
class QTreeWidgetItem;

/* Dock « Cues » (fonction optionnelle) : listes de scènes passées à
 * l'antenne l'une après l'autre, avec pré-délai, post-délai et transition
 * par scène. Cliquer sur une cue montre sa première scène en Aperçu ;
 * Transition ou Cut la lancent. */
class CuesPanel : public QWidget {
	Q_OBJECT

public:
	explicit CuesPanel(QWidget *parent);
	/* Scènes sélectionnées dans le dock Scènes (pour une nouvelle cue). */
	std::function<std::vector<std::string>()> selectedScenes;

private:
	void Refresh();
	void UpdateState();
	void NewCue();
	void EditCue(const std::string &id);
	void RemoveSelected();
	void MoveCue(int delta);
	void MoveRows(QTreeWidgetItem *target, int position);
	std::string CurrentCueId() const;

	OrganizerTree *tree = nullptr;
	bool refreshing = false;
};
