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

#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QTreeWidget>

#include <functional>

/* Liste des scènes / sources, avec dossiers (scènes) ou groupes (sources) :
 * - cliquer-glisser depuis un élément non sélectionné sélectionne plusieurs
 *   éléments ;
 * - cliquer-glisser depuis un élément déjà sélectionné les déplace ;
 * - Suppr / Retour arrière appelle onDelete (avec confirmation) ;
 * - onDropped est appelé après un déplacement à la souris. */
class OrganizerTree : public QTreeWidget {
public:
	explicit OrganizerTree(QWidget *parent) : QTreeWidget(parent)
	{
		setHeaderHidden(true);
		setColumnCount(1);
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setDragDropMode(QAbstractItemView::InternalMove);
		setDefaultDropAction(Qt::MoveAction);
		setIndentation(16);
		setUniformRowHeights(true);
		setExpandsOnDoubleClick(false);
	}

	std::function<void()> onDelete;
	std::function<void()> onDropped;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		QTreeWidgetItem *item = itemAt(event->position().toPoint());
		const bool modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
		setDragEnabled(item && item->isSelected() && !modifiers);
		QTreeWidget::mousePressEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		QTreeWidget::mouseReleaseEvent(event);
		setDragEnabled(true);
	}

	void keyPressEvent(QKeyEvent *event) override
	{
		if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && onDelete) {
			onDelete();
			event->accept();
			return;
		}
		QTreeWidget::keyPressEvent(event);
	}

	void dropEvent(QDropEvent *event) override
	{
		QTreeWidget::dropEvent(event);
		if (onDropped)
			QTimer::singleShot(0, this, [this]() {
				if (onDropped)
					onDropped();
			});
	}
};
