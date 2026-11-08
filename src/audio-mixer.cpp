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

#include "audio-mixer.hpp"
#include "destination.hpp"
#include "manager.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>

#include <QGridLayout>
#include <QDialog>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QProgressBar>
#include <QScrollArea>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>

static constexpr float METER_MIN_DB = -60.0f;

/* ------------------------------------------------------------------ */
/* Tranche                                                              */
/* ------------------------------------------------------------------ */

AudioStrip::AudioStrip(obs_source_t *src, QWidget *parent) : QWidget(parent), source(src)
{
	auto *grid = new QGridLayout(this);
	grid->setContentsMargins(2, 2, 2, 6);
	grid->setHorizontalSpacing(6);
	grid->setVerticalSpacing(2);

	nameLabel = new QLabel(QString::fromUtf8(obs_source_get_name(source)), this);
	dbLabel = new QLabel(this);
	dbLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	grid->addWidget(nameLabel, 0, 0, 1, 2);
	grid->addWidget(dbLabel, 0, 2, 1, 2);

	meter = new QProgressBar(this);
	meter->setRange(0, 1000);
	meter->setTextVisible(false);
	meter->setFixedHeight(6);
	grid->addWidget(meter, 1, 0, 1, 4);

	slider = new QSlider(Qt::Horizontal, this);
	slider->setRange(0, 1000);
	grid->addWidget(slider, 2, 0, 1, 2);

	muteButton = new QToolButton(this);
	muteButton->setCheckable(true);
	muteButton->setToolTip(Tr("MDS.Mute"));
	grid->addWidget(muteButton, 2, 2);

	/* Écoute (monitoring) : comme « Propriétés audio avancées » d'OBS. */
	monitorButton = new QToolButton(this);
	monitorButton->setText("🎧");
	monitorButton->setToolTip(Tr("MDS.Monitoring"));
	monitorButton->setPopupMode(QToolButton::InstantPopup);
	auto *monitorMenu = new QMenu(monitorButton);
	const std::pair<obs_monitoring_type, const char *> types[] = {
		{OBS_MONITORING_TYPE_NONE, "MDS.MonitorOff"},
		{OBS_MONITORING_TYPE_MONITOR_ONLY, "MDS.MonitorOnly"},
		{OBS_MONITORING_TYPE_MONITOR_AND_OUTPUT, "MDS.MonitorAndOutput"},
	};
	for (const auto &[type, key] : types) {
		QAction *a = monitorMenu->addAction(Tr(key));
		a->setCheckable(true);
		a->setData(int(type));
		const obs_monitoring_type t = type;
		connect(a, &QAction::triggered, this, [this, t]() { obs_source_set_monitoring_type(source, t); });
	}
	connect(monitorMenu, &QMenu::aboutToShow, this, [this, monitorMenu]() {
		const int current = int(obs_source_get_monitoring_type(source));
		for (QAction *a : monitorMenu->actions())
			a->setChecked(a->data().toInt() == current);
	});
	monitorButton->setMenu(monitorMenu);
	grid->addWidget(monitorButton, 2, 3);
	grid->setColumnStretch(0, 1);

	fader = obs_fader_create(OBS_FADER_LOG);
	obs_fader_attach_source(fader, source);
	volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_attach_source(volmeter, source);
	obs_volmeter_add_callback(volmeter, VolmeterCb, this);

	connect(slider, &QSlider::valueChanged, this, [this](int value) {
		if (updating)
			return;
		obs_fader_set_deflection(fader, float(value) / 1000.0f);
		Poll();
	});
	connect(muteButton, &QToolButton::toggled, this, [this](bool on) {
		if (updating)
			return;
		obs_source_set_muted(source, on);
		Poll();
	});

	Poll();
}

AudioStrip::~AudioStrip()
{
	obs_volmeter_remove_callback(volmeter, VolmeterCb, this);
	obs_volmeter_detach_source(volmeter);
	obs_volmeter_destroy(volmeter);
	obs_fader_detach_source(fader);
	obs_fader_destroy(fader);
}

void AudioStrip::VolmeterCb(void *param, const float[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			    const float[MAX_AUDIO_CHANNELS])
{
	auto *strip = static_cast<AudioStrip *>(param);
	float maxDb = -100.0f;
	for (int i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (std::isfinite(peak[i]))
			maxDb = std::max(maxDb, peak[i]);
	}
	/* Garder le pic jusqu'à la lecture suivante. */
	float current = strip->peakDb.load();
	while (maxDb > current && !strip->peakDb.compare_exchange_weak(current, maxDb)) {
	}
}

void AudioStrip::Poll()
{
	updating = true;

	const float deflection = obs_fader_get_deflection(fader);
	const int sliderValue = int(std::lround(deflection * 1000.0f));
	if (!slider->isSliderDown() && slider->value() != sliderValue)
		slider->setValue(sliderValue);

	const float db = obs_fader_get_db(fader);
	dbLabel->setText(std::isfinite(db) ? QString("%1 dB").arg(double(db), 0, 'f', 1) : QString("-inf dB"));

	const obs_monitoring_type monitoring = obs_source_get_monitoring_type(source);
	monitorButton->setText(monitoring == OBS_MONITORING_TYPE_NONE ? "🎧" : "🎧•");

	const bool muted = obs_source_muted(source);
	if (muteButton->isChecked() != muted)
		muteButton->setChecked(muted);
	muteButton->setText(muted ? Tr("MDS.Muted") : Tr("MDS.Mute"));

	const char *name = obs_source_get_name(source);
	if (name && nameLabel->text() != QString::fromUtf8(name))
		nameLabel->setText(QString::fromUtf8(name));

	const float peak = peakDb.exchange(-100.0f);
	const float level = std::clamp((peak - METER_MIN_DB) / -METER_MIN_DB, 0.0f, 1.0f);
	meter->setValue(muted ? 0 : int(std::lround(level * 1000.0f)));

	updating = false;
}

/* ------------------------------------------------------------------ */
/* Mixeur                                                               */
/* ------------------------------------------------------------------ */

AudioMixer::AudioMixer(QWidget *parent, bool showAll_) : QWidget(parent), showAll(showAll_)
{
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(0, 0, 0, 0);

	auto *scroll = new QScrollArea(this);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	auto *content = new QWidget(scroll);
	stripsLayout = new QVBoxLayout(content);
	stripsLayout->setContentsMargins(4, 4, 4, 4);
	stripsLayout->addStretch(1);
	scroll->setWidget(content);
	outer->addWidget(scroll, 1);

	if (!showAll) {
		auto *all = new QPushButton(Tr("MDS.ViewAll"), this);
		connect(all, &QPushButton::clicked, this, [this]() {
			QDialog dialog(this);
			dialog.setWindowTitle(Tr("MDS.AllAudioSources"));
			dialog.resize(460, 520);
			auto *l = new QVBoxLayout(&dialog);
			auto *mixer = new AudioMixer(&dialog, true);
			l->addWidget(mixer);
			mixer->Refresh();
			dialog.exec();
		});
		outer->addWidget(all);
	}

	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!isVisible())
			return;
		for (auto *s : strips)
			s->Poll();
	});
	timer->start(50);

	/* Sources ajoutées / supprimées / renommées dans OBS. */
	auto *listTimer = new QTimer(this);
	connect(listTimer, &QTimer::timeout, this, [this]() {
		if (isVisible() && CurrentSourceIds() != shownIds)
			Refresh();
	});
	listTimer->start(1000);
}

static bool HasAudio(obs_source_t *source)
{
	return obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
	       (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO);
}

/* Sources d'une scène, scènes imbriquées comprises. */
static void CollectSceneSources(obs_scene_t *scene, std::vector<OBSSource> &out, int depth)
{
	if (!scene || depth > 4)
		return;
	struct Ctx {
		std::vector<OBSSource> *out;
		int depth;
	} ctx{&out, depth};
	obs_scene_enum_items(
		scene,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			auto *c = static_cast<Ctx *>(param);
			obs_source_t *src = obs_sceneitem_get_source(item);
			if (obs_source_is_scene(src) || obs_source_is_group(src))
				CollectSceneSources(obs_scene_from_source(src) ? obs_scene_from_source(src)
									       : obs_group_from_source(src),
						    *c->out, c->depth + 1);
			else
				c->out->emplace_back(src);
			return true;
		},
		&ctx);
}

std::vector<std::string> AudioMixer::CurrentSourceIds() const
{
	std::vector<std::string> ids;
	std::set<std::string> seen;
	auto add = [&](obs_source_t *src) {
		if (!src || !HasAudio(src))
			return;
		const std::string uuid = obs_source_get_uuid(src);
		if (seen.insert(uuid).second)
			ids.push_back(uuid);
	};

	std::vector<OBSSource> all;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			static_cast<std::vector<OBSSource> *>(param)->emplace_back(source);
			return true;
		},
		&all);

	if (showAll) {
		for (auto &s : all)
			add(s);
		return ids;
	}

	/* D'abord les sources actives (à l'antenne quelque part). */
	for (auto &s : all) {
		if (obs_source_active(s))
			add(s);
	}
	/* Puis celles de la scène sélectionnée, sur toutes les destinations. */
	OBSSource preview = Manager::Get().PreviewScene();
	if (preview) {
		for (auto *d : Manager::Get().ActiveDestinations()) {
			OBSSource linked = d->LinkedScene(preview);
			std::vector<OBSSource> inScene;
			CollectSceneSources(linked ? obs_scene_from_source(linked) : nullptr, inScene, 0);
			for (auto &s : inScene)
				add(s);
		}
	}
	return ids;
}

void AudioMixer::Clear()
{
	for (auto *s : strips)
		delete s;
	strips.clear();
	shownIds.clear();
}

void AudioMixer::Refresh()
{
	Clear();
	shownIds = CurrentSourceIds();
	for (const auto &id : shownIds) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(id.c_str());
		if (!source)
			continue;
		auto *strip = new AudioStrip(source, this);
		stripsLayout->insertWidget(stripsLayout->count() - 1, strip);
		strips.push_back(strip);
	}
}
