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

#include <atomic>
#include <string>
#include <vector>

class QLabel;
class QProgressBar;
class QSlider;
class QToolButton;
class QVBoxLayout;

/* Une tranche de mixeur : nom, vumètre, volume, muet. */
class AudioStrip : public QWidget {
	Q_OBJECT

public:
	AudioStrip(obs_source_t *source, QWidget *parent);
	~AudioStrip() override;

	obs_source_t *Source() const { return source; }
	void Poll();

private:
	static void VolmeterCb(void *param, const float magnitude[MAX_AUDIO_CHANNELS],
			       const float peak[MAX_AUDIO_CHANNELS], const float inputPeak[MAX_AUDIO_CHANNELS]);

	OBSSource source;
	obs_fader_t *fader = nullptr;
	obs_volmeter_t *volmeter = nullptr;
	std::atomic<float> peakDb{-100.0f};

	QLabel *nameLabel = nullptr;
	QLabel *dbLabel = nullptr;
	QProgressBar *meter = nullptr;
	QSlider *slider = nullptr;
	QToolButton *muteButton = nullptr;
	QToolButton *monitorButton = nullptr;
	bool updating = false;
};

/* Mixeur audio. Mode scène : d'abord les sources actives, puis celles de la
 * scène sélectionnée, avec un bouton « Tout afficher ». Mode complet : toutes
 * les sources qui ont du son. */
class AudioMixer : public QWidget {
	Q_OBJECT

public:
	AudioMixer(QWidget *parent, bool showAll);

	void Refresh();
	void Clear();

private:
	std::vector<std::string> CurrentSourceIds() const;

	bool showAll;
	QVBoxLayout *stripsLayout = nullptr;
	std::vector<AudioStrip *> strips;
	std::vector<std::string> shownIds;
};
