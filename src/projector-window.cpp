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

#include "projector-window.hpp"
#include "blend-render.hpp"
#include "destination.hpp"
#include "render-helpers.hpp"
#include "text.hpp"
#include "qt-display.hpp"
#ifdef Q_OS_MACOS
#include "macos-window.hpp"
#endif

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QGuiApplication>
#include <QKeyEvent>
#include <QTimer>
#include <QVBoxLayout>

ProjectorWindow::ProjectorWindow(Destination *destination, QScreen *screen, int index)
	: QWidget(nullptr, Qt::Window),
	  dest(destination),
	  target(screen),
	  outputIndex(index)
{
	setAttribute(Qt::WA_DeleteOnClose, true);
	QString title = QString("%1 — %2").arg(Tr("MDS.Projector")).arg(QString::fromStdString(dest->Name()));
	if (outputIndex >= 0) {
		blendRenderer = std::make_unique<BlendRenderer>();
		const BlendConfig config = dest->Blend();
		if (size_t(outputIndex) < config.outputs.size())
			title += " — " + QString::fromStdString(config.outputs[size_t(outputIndex)].name);
	}
	setWindowTitle(title);
	if (target) {
		const qreal ratio = target->devicePixelRatio();
		nativeW = uint32_t(target->geometry().width() * ratio);
		nativeH = uint32_t(target->geometry().height() * ratio);
	}

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);

	display = new OBSQTDisplay(this);
	display->SetDisplayBackgroundColor(Qt::black);
	layout->addWidget(display);

	connect(display, &OBSQTDisplay::DisplayCreated, this,
		[this](OBSQTDisplay *w) { obs_display_add_draw_callback(w->GetDisplay(), DrawCb, this); });

	/* Pas de curseur sur un écran de sortie. */
	if (target) {
		setCursor(Qt::BlankCursor);
		targetName = target->name();
	}

	/* Écran débranché puis rebranché (câble, veille de l'écran) : la
	 * sortie se cache au lieu d'atterrir sur un autre écran, puis revient
	 * seule sur son écran. */
	connect(qApp, &QGuiApplication::screenRemoved, this, &ProjectorWindow::ScreenRemoved);
	connect(qApp, &QGuiApplication::screenAdded, this, &ProjectorWindow::ScreenAdded);
}

void ProjectorWindow::ScreenRemoved(QScreen *screen)
{
	if (targetName.isEmpty() || screen != target)
		return;
	blog(LOG_INFO, "[multidestination-studio] output screen removed, projector hidden");
	target = nullptr;
	hide();
}

void ProjectorWindow::ScreenAdded(QScreen *screen)
{
	if (targetName.isEmpty() || target || !screen || screen->name() != targetName)
		return;
	blog(LOG_INFO, "[multidestination-studio] output screen back, projector restored");
	target = screen;
	const qreal ratio = target->devicePixelRatio();
	nativeW = uint32_t(target->geometry().width() * ratio);
	nativeH = uint32_t(target->geometry().height() * ratio);
	/* Laisse le système finir de configurer l'écran. */
	QTimer::singleShot(500, this, [this]() {
		if (target)
			Start();
	});
}

ProjectorWindow::~ProjectorWindow()
{
	if (display && display->GetDisplay())
		obs_display_remove_draw_callback(display->GetDisplay(), DrawCb, this);
}

void ProjectorWindow::Start()
{
	if (target) {
		/* Toujours au premier plan sur son écran de sortie, sauf si c'est
		 * l'écran de travail (on ne veut pas masquer OBS). Sans bord. */
		auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		const bool operatorScreen = main && main->screen() == target;
		setWindowFlag(Qt::FramelessWindowHint, true);
		setWindowFlag(Qt::WindowStaysOnTopHint, !operatorScreen);
		setAttribute(Qt::WA_ShowWithoutActivating, !operatorScreen);
		setGeometry(target->geometry());
#ifdef Q_OS_MACOS
		/* Pas de plein écran natif sur macOS : il crée un Space dédié
		 * qui masque les autres écrans et se cache dès qu'on clique
		 * dans OBS. Une fenêtre sans bord couvrant l'écran, présente sur
		 * tous les Spaces et au-dessus de la barre des menus, reste
		 * toujours visible (sauf sur l'écran de travail). */
		showNormal();
		macOutput = !operatorScreen;
		if (macOutput)
			MacOSWindow::MakeOutputWindow(this);
#else
		showFullScreen();
#endif
	} else {
		resize(960, 540);
		show();
	}
	setFocusPolicy(Qt::StrongFocus);
	/* Une sortie sur un autre écran ne prend pas le focus : la fenêtre de
	 * travail reste active (sur macOS, voler le focus faisait passer OBS
	 * à l'arrière-plan). Un clic sur la sortie la rend active (Échap). */
	if (!testAttribute(Qt::WA_ShowWithoutActivating)) {
		activateWindow();
		setFocus();
	}
}

void ProjectorWindow::DrawCb(void *data, uint32_t cx, uint32_t cy)
{
	auto *window = static_cast<ProjectorWindow *>(data);
	Destination *d = window->dest;
	if (!d || !d->Attached())
		return;
	if (window->blendRenderer) {
		window->blendRenderer->Draw(d, size_t(window->outputIndex), cx, cy, window->nativeW, window->nativeH);
		return;
	}
	DrawFitted(d->Width(), d->Height(), cx, cy, [d](float) { d->RenderProgram(); });
}

void ProjectorWindow::keyPressEvent(QKeyEvent *event)
{
	if (event->key() == Qt::Key_Escape) {
		close();
		return;
	}
	QWidget::keyPressEvent(event);
}

void ProjectorWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
	/* Double-clic : plein écran sur l'écran de sortie <-> fenêtre normale
	 * (avec bord, plus au premier plan). */
	if (target && windowFlags().testFlag(Qt::FramelessWindowHint)) {
		setWindowFlag(Qt::FramelessWindowHint, false);
		setWindowFlag(Qt::WindowStaysOnTopHint, false);
		showNormal();
#ifdef Q_OS_MACOS
		macOutput = false;
		MacOSWindow::MakeNormalWindow(this);
#endif
		resize(960, 540);
		unsetCursor();
		activateWindow();
	} else if (target) {
		Start();
		setCursor(Qt::BlankCursor);
	}
	QWidget::mouseDoubleClickEvent(event);
}

bool ProjectorWindow::event(QEvent *event)
{
	const bool result = QWidget::event(event);
#ifdef Q_OS_MACOS
	/* Qt réapplique son propre niveau de fenêtre à l'affichage et quand
	 * l'activation change : les réglages de sortie sont remis juste après. */
	switch (event->type()) {
	case QEvent::Show:
	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
	case QEvent::ApplicationStateChange:
	case QEvent::WindowStateChange:
		if (macOutput)
			QTimer::singleShot(0, this, [this]() {
				if (macOutput && isVisible())
					MacOSWindow::MakeOutputWindow(this);
			});
		break;
	default:
		break;
	}
#endif
	return result;
}
