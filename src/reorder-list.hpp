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

#include <QDragLeaveEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPersistentModelIndex>
#include <QTimer>
#include <QTreeWidget>

#include <functional>

/* Liste des scènes / sources, avec dossiers (scènes) ou groupes (sources),
 * au comportement proche d'OBS :
 * - Ctrl (Cmd sur Mac) + clic ajoute ou retire un élément de la sélection,
 *   Maj + clic sélectionne tout ce qui se trouve entre deux éléments ;
 * - cliquer-glisser depuis un élément non sélectionné sélectionne plusieurs
 *   éléments, depuis un élément déjà sélectionné les déplace ;
 * - F2 renomme ; Suppr / Retour arrière appelle onDelete ;
 * - un dépôt à la souris n'est pas appliqué par Qt : onMove reçoit la ligne
 *   visée et la position (au-dessus, dedans, en dessous), et la fenêtre
 *   applique le déplacement elle-même (scènes d'OBS, groupes...). */
class OrganizerTree : public QTreeWidget {
public:
	explicit OrganizerTree(QWidget *parent, int columns = 1) : QTreeWidget(parent)
	{
		setHeaderHidden(true);
		setColumnCount(columns);
		setSelectionMode(QAbstractItemView::ExtendedSelection);
		setSelectionBehavior(QAbstractItemView::SelectRows);
		setDragDropMode(QAbstractItemView::InternalMove);
		setDefaultDropAction(Qt::MoveAction);
		setDropIndicatorShown(true);
		setAutoExpandDelay(600);
		setIndentation(14);
		setUniformRowHeights(true);
		setExpandsOnDoubleClick(false);
		setAllColumnsShowFocus(true);
		setEditTriggers(QAbstractItemView::EditKeyPressed);
		setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
		if (columns > 1) {
			header()->setStretchLastSection(false);
			header()->setSectionResizeMode(0, QHeaderView::Stretch);
			for (int c = 1; c < columns; c++) {
				header()->setSectionResizeMode(c, QHeaderView::Fixed);
				header()->resizeSection(c, 24);
			}
		}
	}

	/* Positions de dépôt (valeurs de QAbstractItemView, protégées). */
	static constexpr int DropOn = 0;
	static constexpr int DropAbove = 1;
	static constexpr int DropBelow = 2;
	static constexpr int DropViewport = 3;
	static_assert(int(OnItem) == DropOn && int(AboveItem) == DropAbove && int(BelowItem) == DropBelow &&
		      int(OnViewport) == DropViewport);

	std::function<void()> onDelete;
	/* target : ligne visée (nullptr = zone vide) ; position :
	 * QAbstractItemView::DropIndicatorPosition. */
	std::function<void(QTreeWidgetItem *target, int position)> onMove;

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
		if (state() != QAbstractItemView::EditingState &&
		    (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) && onDelete) {
			onDelete();
			event->accept();
			return;
		}
		QTreeWidget::keyPressEvent(event);
	}

	void dropEvent(QDropEvent *event) override
	{
		if (event->source() != this) {
			QTreeWidget::dropEvent(event);
			return;
		}
		const QPersistentModelIndex index(indexAt(event->position().toPoint()));
		const int position = int(dropIndicatorPosition());
		/* Qt ne déplace rien lui-même : pas d'éléments perdus. */
		event->setDropAction(Qt::IgnoreAction);
		event->accept();
		QDragLeaveEvent leave;
		QTreeWidget::dragLeaveEvent(&leave);
		/* Appliqué après la fin du glisser (Qt utilise encore la
		 * sélection pendant ce temps). */
		QTimer::singleShot(0, this, [this, index, position]() {
			if (!onMove)
				return;
			/* Ligne disparue entre-temps : on ne devine pas. */
			if (position != int(QAbstractItemView::OnViewport) && !index.isValid())
				return;
			onMove(index.isValid() ? itemFromIndex(index) : nullptr, position);
		});
	}
};
