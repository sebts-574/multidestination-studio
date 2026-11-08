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

#include "media-controls.hpp"
#include "text.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

MediaControls::MediaControls(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(4, 4, 4, 4);

	nameLabel = new QLabel(this);
	layout->addWidget(nameLabel);

	auto *row = new QHBoxLayout();
	restartButton = new QToolButton(this);
	restartButton->setText("⏮");
	restartButton->setToolTip(Tr("MDS.MediaRestart"));
	playButton = new QToolButton(this);
	playButton->setToolTip(Tr("MDS.MediaPlayPause"));
	stopButton = new QToolButton(this);
	stopButton->setText("⏹");
	stopButton->setToolTip(Tr("MDS.MediaStop"));
	row->addWidget(restartButton);
	row->addWidget(playButton);
	row->addWidget(stopButton);

	slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, 1000);
	row->addWidget(slider, 1);

	timeLabel = new QLabel(this);
	row->addWidget(timeLabel);
	layout->addLayout(row);
	layout->addStretch(1);

	connect(restartButton, &QToolButton::clicked, this, [this]() {
		OBSSource s = OBSGetStrongRef(source);
		if (s)
			obs_source_media_restart(s);
	});
	connect(playButton, &QToolButton::clicked, this, [this]() {
		OBSSource s = OBSGetStrongRef(source);
		if (!s)
			return;
		const obs_media_state state = obs_source_media_get_state(s);
		if (state == OBS_MEDIA_STATE_PLAYING)
			obs_source_media_play_pause(s, true);
		else if (state == OBS_MEDIA_STATE_PAUSED)
			obs_source_media_play_pause(s, false);
		else
			obs_source_media_restart(s);
	});
	connect(stopButton, &QToolButton::clicked, this, [this]() {
		OBSSource s = OBSGetStrongRef(source);
		if (s)
			obs_source_media_stop(s);
	});
	connect(slider, &QSlider::sliderPressed, this, [this]() { seeking = true; });
	connect(slider, &QSlider::sliderReleased, this, [this]() {
		seeking = false;
		OBSSource s = OBSGetStrongRef(source);
		if (!s)
			return;
		const int64_t duration = obs_source_media_get_duration(s);
		if (duration > 0)
			obs_source_media_set_time(s, duration * slider->value() / 1000);
	});

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &MediaControls::Poll);
	timer->start(200);

	SetSource(nullptr);
}

QString MediaControls::FormatTime(int64_t ms)
{
	if (ms < 0)
		ms = 0;
	const int64_t total = ms / 1000;
	const int64_t h = total / 3600;
	const int64_t m = (total / 60) % 60;
	const int64_t sec = total % 60;
	if (h > 0)
		return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
	return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(sec, 2, 10, QChar('0'));
}

void MediaControls::SetSource(obs_source_t *src)
{
	const bool media = src && (obs_source_get_output_flags(src) & OBS_SOURCE_CONTROLLABLE_MEDIA);
	source = media ? OBSGetWeakRef(src) : OBSWeakSource();

	nameLabel->setText(media ? QString::fromUtf8(obs_source_get_name(src)) : Tr("MDS.NoMediaSelected"));
	for (QWidget *w : {static_cast<QWidget *>(restartButton), static_cast<QWidget *>(playButton),
			   static_cast<QWidget *>(stopButton), static_cast<QWidget *>(slider)})
		w->setEnabled(media);
	Poll();
}

void MediaControls::Poll()
{
	OBSSource s = OBSGetStrongRef(source);
	if (!s) {
		playButton->setText("▶");
		timeLabel->setText("--:-- / --:--");
		if (!seeking)
			slider->setValue(0);
		return;
	}

	const obs_media_state state = obs_source_media_get_state(s);
	playButton->setText(state == OBS_MEDIA_STATE_PLAYING ? "⏸" : "▶");

	const int64_t duration = obs_source_media_get_duration(s);
	const int64_t time = obs_source_media_get_time(s);
	timeLabel->setText(FormatTime(time) + " / " + FormatTime(duration));
	if (!seeking && duration > 0)
		slider->setValue(int(time * 1000 / duration));
}
