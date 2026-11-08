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

#include <QDialog>

#include <functional>

class Destination;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QVBoxLayout;

/* « Gérer les destinations » : toutes les destinations dans une seule
 * fenêtre, avec pour chacune son nom, sa résolution, son écran de sortie,
 * son verrou et les scènes dans lesquelles elle apparaît. */
class ManageDestinationsDialog : public QDialog {
	Q_OBJECT

public:
	explicit ManageDestinationsDialog(QWidget *parent);

	/* Actions réalisées par la fenêtre Studio. */
	std::function<void()> onNew;
	std::function<void(Destination *)> onBlending;
	std::function<void(Destination *)> onProject;
	std::function<void(Destination *)> onDelete;

	void Refresh();

private:
	Destination *Current() const;
	void LoadCurrent();
	void Apply();
	void MarkDirty();

	QListWidget *list = nullptr;
	QWidget *details = nullptr;
	QLineEdit *nameEdit = nullptr;
	QLabel *nameFixed = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;
	QLabel *resolutionNote = nullptr;
	QComboBox *screenCombo = nullptr;
	QCheckBox *lockBox = nullptr;
	QVBoxLayout *scenesLayout = nullptr;
	QWidget *scenesBox = nullptr;
	QLabel *scenesSummary = nullptr;
	QPushButton *applyButton = nullptr;
	QPushButton *blendingButton = nullptr;
	QPushButton *projectButton = nullptr;
	QPushButton *deleteButton = nullptr;
	bool loading = false;
};
