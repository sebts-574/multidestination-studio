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
#include <QPainter>
#include <QPersistentModelIndex>
#include <QStyledItemDelegate>
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
/* Icônes cliquables à droite de la ligne (œil, cadenas), dessinées par-dessus
 * la ligne : la sélection reste d'un seul tenant, comme dans OBS. */
class TrailingIconDelegate : public QStyledItemDelegate {
public:
	using QStyledItemDelegate::QStyledItemDelegate;
	static constexpr int IconRole = Qt::UserRole + 20;
	int count = 0;

	static QRect IconRect(const QRect &row, int i, int count)
	{
		const int size = 16;
		const int step = 24;
		const int x = row.right() - 6 - (count - i) * step + (step - size) / 2;
		return QRect(x, row.center().y() - size / 2 + 1, size, size);
	}

	void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
	{
		QStyledItemDelegate::paint(painter, option, index);
		if (index.column() != 0)
			return;
		for (int i = 0; i < count; i++) {
			const QIcon icon = index.data(IconRole + i).value<QIcon>();
			if (!icon.isNull())
				icon.paint(painter, IconRect(option.rect, i, count));
		}
	}
};

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

	/* Icônes cliquables à droite (rôle TrailingIconDelegate::IconRole + i). */
	void SetTrailingIcons(int count)
	{
		auto *delegate = new TrailingIconDelegate(this);
		delegate->count = count;
		trailingIcons = count;
		setItemDelegate(delegate);
	}
	std::function<void(QTreeWidgetItem *item, int icon)> onIconClicked;

	std::function<void()> onDelete;
	/* Bouton de la souris relâché (aussi après un glisser). */
	std::function<void()> onReleased;
	bool MousePressed() const { return pressed; }
	/* target : ligne visée (nullptr = zone vide) ; position :
	 * QAbstractItemView::DropIndicatorPosition. */
	std::function<void(QTreeWidgetItem *target, int position)> onMove;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		/* Sur un élément (sélectionné ou non) : on le déplace. Depuis une
		 * zone vide : sélection de plusieurs éléments. Ctrl / Maj : pas de
		 * glisser, sélection seulement. */
		QTreeWidgetItem *item = itemAt(event->position().toPoint());
		if (item && trailingIcons > 0 && event->button() == Qt::LeftButton && onIconClicked) {
			const QRect rect = visualRect(indexFromItem(item, 0));
			for (int i = 0; i < trailingIcons; i++) {
				if (TrailingIconDelegate::IconRect(rect, i, trailingIcons)
					    .adjusted(-4, -4, 4, 4)
					    .contains(event->position().toPoint())) {
					onIconClicked(item, i);
					event->accept();
					return;
				}
			}
		}
		const bool modifiers = event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier);
		setDragEnabled(item && !modifiers);
		pressed = event->button() == Qt::LeftButton;
		QTreeWidget::mousePressEvent(event);
	}

	void mouseReleaseEvent(QMouseEvent *event) override
	{
		QTreeWidget::mouseReleaseEvent(event);
		setDragEnabled(true);
		Released();
	}

	void startDrag(Qt::DropActions actions) override
	{
		QTreeWidget::startDrag(actions);
		/* Le glisser a consommé le relâchement du bouton. */
		Released();
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

	void Released()
	{
		if (!pressed)
			return;
		pressed = false;
		/* Après un éventuel dépôt (lui aussi différé). */
		QTimer::singleShot(0, this, [this]() {
			if (onReleased)
				onReleased();
		});
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

private:
	bool pressed = false;
	int trailingIcons = 0;
};
