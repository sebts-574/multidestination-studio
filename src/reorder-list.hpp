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
#include <QGuiApplication>
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
/* Dessin des lignes :
 * - icônes cliquables à droite (œil, cadenas), par-dessus la ligne : la
 *   sélection reste d'un seul tenant, comme dans OBS ;
 * - flèche d'ouverture des dossiers / groupes dans la ligne elle-même, à
 *   gauche de l'icône (pas de colonne de branches à part). */
class TrailingIconDelegate : public QStyledItemDelegate {
public:
	explicit TrailingIconDelegate(QTreeWidget *tree) : QStyledItemDelegate(tree), view(tree) {}
	static constexpr int IconRole = Qt::UserRole + 20;
	static constexpr int ContainerRole = Qt::UserRole + 30;
	static constexpr int ArrowWidth = 16;
	int count = 0;

	static QRect IconRect(const QRect &row, int i, int count)
	{
		const int size = 16;
		const int step = 24;
		const int x = row.right() - 6 - (count - i) * step + (step - size) / 2;
		return QRect(x, row.center().y() - size / 2 + 1, size, size);
	}

	/* Zone de la flèche (début de la ligne, après la marge intérieure). */
	static QRect ArrowRect(const QRect &row)
	{
		return QRect(row.left() + 2, row.top(), ArrowWidth + 6, row.height());
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

protected:
	void initStyleOption(QStyleOptionViewItem *option, const QModelIndex &index) const override
	{
		QStyledItemDelegate::initStyleOption(option, index);
		if (index.column() != 0 || !index.data(ContainerRole).toBool())
			return;
		/* Flèche + icône dans une seule image. */
		const bool open = view && view->isExpanded(index);
		const QColor color = option->palette.color(QPalette::Text);
		const QIcon icon = option->icon;
		const int w = icon.isNull() ? ArrowWidth : ArrowWidth + 20;
		QPixmap pm(w * 2, 32);
		pm.setDevicePixelRatio(2.0);
		pm.fill(Qt::transparent);
		QPainter p(&pm);
		p.setRenderHint(QPainter::Antialiasing);
		p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		if (open) {
			p.drawLine(QPointF(4, 6.5), QPointF(8, 10.5));
			p.drawLine(QPointF(8, 10.5), QPointF(12, 6.5));
		} else {
			p.drawLine(QPointF(6.5, 4), QPointF(10.5, 8));
			p.drawLine(QPointF(10.5, 8), QPointF(6.5, 12));
		}
		if (!icon.isNull())
			icon.paint(&p, QRect(ArrowWidth + 2, 0, 16, 16));
		p.end();
		option->icon = QIcon(pm);
		option->decorationSize = QSize(w, 16);
		option->features |= QStyleOptionViewItem::HasDecoration;
	}

private:
	QTreeWidget *view = nullptr;
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
		setIndentation(22);
		setUniformRowHeights(true);
		setExpandsOnDoubleClick(false);
		setAllColumnsShowFocus(true);
		setEditTriggers(QAbstractItemView::EditKeyPressed);
		/* Pas de colonne de branches : la flèche des dossiers est dans la
		 * ligne, et les éléments hors dossier ne sont pas décalés. */
		setRootIsDecorated(false);
		delegate = new TrailingIconDelegate(this);
		setItemDelegate(delegate);
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
		delegate->count = count;
		trailingIcons = count;
	}
	static constexpr int ContainerRole = TrailingIconDelegate::ContainerRole;
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
		/* Flèche d'un dossier : ouvrir / fermer sans changer la sélection. */
		if (item && event->button() == Qt::LeftButton && item->data(0, ContainerRole).toBool() &&
		    TrailingIconDelegate::ArrowRect(visualRect(indexFromItem(item, 0)))
			    .contains(event->position().toPoint())) {
			item->setExpanded(!item->isExpanded());
			event->accept();
			return;
		}
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
		const bool modifiers = RealModifiers(event->modifiers()) & (Qt::ShiftModifier | Qt::ControlModifier);
		setDragEnabled(item && !modifiers);
		pressed = event->button() == Qt::LeftButton;
		if (const Qt::KeyboardModifiers real = RealModifiers(event->modifiers()); real != event->modifiers()) {
			QMouseEvent copy(event->type(), event->position(), event->scenePosition(),
					 event->globalPosition(), event->button(), event->buttons(), real,
					 event->pointingDevice());
			QTreeWidget::mousePressEvent(&copy);
			event->setAccepted(copy.isAccepted());
			return;
		}
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

	/* Suppr / Retour arrière vont à la liste, avant les raccourcis de la
	 * fenêtre ou du menu (sur macOS, le menu les prenait : Suppr dans les
	 * cues demandait de supprimer une source). */
	bool event(QEvent *event) override
	{
		if (event->type() == QEvent::ShortcutOverride && onDelete &&
		    state() != QAbstractItemView::EditingState) {
			auto *key = static_cast<QKeyEvent *>(event);
			if ((key->key() == Qt::Key_Delete || key->key() == Qt::Key_Backspace) &&
			    !(key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
				event->accept();
				return true;
			}
		}
		return QTreeWidget::event(event);
	}

	/* Ctrl / Maj d'après l'état réel du clavier : un modificateur resté
	 * « collé » (vu sur macOS après un raccourci ou un changement
	 * d'application) faisait une sélection multiple sur un simple clic. */
	static Qt::KeyboardModifiers RealModifiers(Qt::KeyboardModifiers reported)
	{
		return reported & QGuiApplication::queryKeyboardModifiers();
	}

	QItemSelectionModel::SelectionFlags selectionCommand(const QModelIndex &index,
							     const QEvent *event = nullptr) const override
	{
		if (event && (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseMove ||
			      event->type() == QEvent::MouseButtonRelease)) {
			const auto *me = static_cast<const QMouseEvent *>(event);
			const Qt::KeyboardModifiers real = RealModifiers(me->modifiers());
			if (real != me->modifiers()) {
				QMouseEvent copy(me->type(), me->position(), me->scenePosition(), me->globalPosition(),
						 me->button(), me->buttons(), real, me->pointingDevice());
				return QTreeWidget::selectionCommand(index, &copy);
			}
		}
		return QTreeWidget::selectionCommand(index, event);
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
	TrailingIconDelegate *delegate = nullptr;
};
