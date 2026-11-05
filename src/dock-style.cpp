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

#include "dock-style.hpp"

#include <obs-frontend-api.h>

#include <QAction>
#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace DockStyle {

QToolBar *Toolbar(QWidget *parent)
{
	auto *bar = new QToolBar(parent);
	bar->setIconSize(QSize(16, 16));
	bar->setFloatable(false);
	bar->setMovable(false);
	bar->setToolButtonStyle(Qt::ToolButtonIconOnly);
	/* Règles « OBSDock QToolBar » du thème d'OBS. */
	bar->setStyleSheet("QToolBar { padding: 1px; margin: 0px; border: none; border-top: 1px solid palette(mid);"
			   " spacing: 0px; background: transparent; }"
			   "QToolBar::separator { background: transparent; width: 4px; border: none; }"
			   "QToolButton { background: transparent; border: 1px solid transparent; border-radius: 4px;"
			   " margin: 1px; margin-top: 2px; padding: 4px; }"
			   "QToolButton:hover { border-color: palette(light); }");
	return bar;
}

QAction *AddAction(QToolBar *bar, const char *iconClass, const QIcon &fallback, const QString &tip,
		   const std::function<void()> &fn)
{
	QAction *action = bar->addAction(fallback, tip);
	action->setToolTip(tip);
	if (iconClass)
		action->setProperty("class", iconClass);
	QObject::connect(action, &QAction::triggered, bar, [fn]() { fn(); });
	/* Comme OBS : le thème lit la classe sur le bouton. */
	if (QWidget *button = bar->widgetForAction(action)) {
		if (iconClass)
			button->setProperty("class", iconClass);
		button->style()->unpolish(button);
		button->style()->polish(button);
	}
	return action;
}

void StyleTree(QTreeWidget *tree, bool trailingIcons)
{
	const QColor highlight = tree->palette().color(QPalette::Highlight);
	const QColor hover = highlight.lighter(125);
	tree->setFrameShape(QFrame::NoFrame);
	tree->setIconSize(QSize(16, 16));
	tree->setStyleSheet(
		QString("QTreeView { border: none; outline: 0; padding: 2px; }"
			"QTreeView::item { padding: 4px 5px; %1 border: 1px solid transparent;"
			" border-radius: 4px; color: palette(text); }"
			"QTreeView::item:selected { background-color: %2; color: palette(highlighted-text); }"
			"QTreeView::item:hover, QTreeView::item:selected:hover { background-color: %3; }")
			.arg(trailingIcons ? "padding-right: 54px;" : "")
			.arg(highlight.name())
			.arg(hover.name()));
}

QWidget *Panel(QWidget *list, QToolBar *bar, QWidget *parent)
{
	auto *panel = new QWidget(parent);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(1, 0, 1, 1);
	layout->setSpacing(0);
	layout->addWidget(list, 1);
	layout->addWidget(bar);
	return panel;
}

QIcon MainIcon(const char *property)
{
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!main)
		return QIcon();
	return main->property(property).value<QIcon>();
}

QIcon SourceIcon(obs_source_t *source)
{
	if (!source)
		return QIcon();
	if (obs_source_is_group(source))
		return MainIcon("groupIcon");
	if (obs_source_is_scene(source))
		return MainIcon("sceneIcon");
	const char *property = "defaultIcon";
	switch (obs_source_get_icon_type(obs_source_get_id(source))) {
	case OBS_ICON_TYPE_IMAGE:
		property = "imageIcon";
		break;
	case OBS_ICON_TYPE_COLOR:
		property = "colorIcon";
		break;
	case OBS_ICON_TYPE_SLIDESHOW:
		property = "slideshowIcon";
		break;
	case OBS_ICON_TYPE_AUDIO_INPUT:
		property = "audioInputIcon";
		break;
	case OBS_ICON_TYPE_AUDIO_OUTPUT:
		property = "audioOutputIcon";
		break;
	case OBS_ICON_TYPE_DESKTOP_CAPTURE:
		property = "desktopCapIcon";
		break;
	case OBS_ICON_TYPE_WINDOW_CAPTURE:
		property = "windowCapIcon";
		break;
	case OBS_ICON_TYPE_GAME_CAPTURE:
		property = "gameCapIcon";
		break;
	case OBS_ICON_TYPE_CAMERA:
		property = "cameraIcon";
		break;
	case OBS_ICON_TYPE_TEXT:
		property = "textIcon";
		break;
	case OBS_ICON_TYPE_MEDIA:
		property = "mediaIcon";
		break;
	case OBS_ICON_TYPE_BROWSER:
		property = "browserIcon";
		break;
	case OBS_ICON_TYPE_PROCESS_AUDIO_OUTPUT:
		property = "audioProcessOutputIcon";
		break;
	default:
		break;
	}
	return MainIcon(property);
}

static QIcon PaintedIcon(const QColor &color, const std::function<void(QPainter &)> &draw)
{
	QPixmap pm(32, 32);
	pm.setDevicePixelRatio(2.0);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(QPen(color, 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	p.setBrush(Qt::NoBrush);
	draw(p);
	p.end();
	return QIcon(pm);
}

/* Fichier d'icône du thème d'OBS (préfixe « theme: » d'OBS), si présent. */
static QIcon ThemeFile(const char *path)
{
	return QFile::exists(path) ? QIcon(path) : QIcon();
}

QIcon VisibilityIcon(bool visible, const QColor &base)
{
	QIcon themed = ThemeFile(visible ? "theme:Dark/visible.svg" : ":res/images/invisible.svg");
	if (!themed.isNull())
		return themed;
	QColor color = base;
	if (!visible)
		color.setAlphaF(0.35f);
	return PaintedIcon(color, [visible, color](QPainter &p) {
		QPainterPath eye;
		eye.moveTo(1.5, 8);
		eye.quadTo(8, 1.5, 14.5, 8);
		eye.quadTo(8, 14.5, 1.5, 8);
		p.drawPath(eye);
		if (visible) {
			p.setBrush(color);
			p.drawEllipse(QPointF(8, 8), 2.2, 2.2);
		} else {
			p.drawLine(QPointF(3, 13), QPointF(13, 3));
		}
	});
}

QIcon LockIcon(bool locked, const QColor &base)
{
	QIcon themed = ThemeFile(locked ? "theme:Dark/locked.svg" : ":res/images/unlocked.svg");
	if (!themed.isNull())
		return themed;
	QColor color = base;
	if (!locked)
		color.setAlphaF(0.3f);
	return PaintedIcon(color, [locked, color](QPainter &p) {
		if (locked)
			p.drawArc(QRectF(5, 2.5, 6, 8), 0, 180 * 16);
		else
			p.drawArc(QRectF(8, 1.5, 6, 8), 0, 180 * 16);
		p.setBrush(color);
		p.drawRoundedRect(QRectF(3.5, 8, 9, 6.5), 1.2, 1.2);
	});
}

QIcon PlusIcon()
{
	const QColor color = QApplication::palette().color(QPalette::ButtonText);
	return PaintedIcon(color, [](QPainter &p) {
		p.drawLine(QPointF(8, 3), QPointF(8, 13));
		p.drawLine(QPointF(3, 8), QPointF(13, 8));
	});
}

} // namespace DockStyle
