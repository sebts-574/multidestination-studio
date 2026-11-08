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

#include <obs.hpp>

#include <QWidget>

class QLabel;
class QSlider;
class QToolButton;

/* Lecteur pour la source média sélectionnée : lecture/pause, arrêt,
 * redémarrage, barre de position et temps. */
class MediaControls : public QWidget {
	Q_OBJECT

public:
	explicit MediaControls(QWidget *parent);

	void SetSource(obs_source_t *source);

private:
	void Poll();
	static QString FormatTime(int64_t ms);

	OBSWeakSource source;
	QLabel *nameLabel = nullptr;
	QLabel *timeLabel = nullptr;
	QSlider *slider = nullptr;
	QToolButton *playButton = nullptr;
	QToolButton *stopButton = nullptr;
	QToolButton *restartButton = nullptr;
	bool seeking = false;
};
