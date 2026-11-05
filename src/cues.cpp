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

#include "cues.hpp"
#include "manager.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QDateTime>
#include <QTimer>
#include <QUuid>

#include <algorithm>

CueEngine &CueEngine::Get()
{
	static CueEngine instance;
	return instance;
}

static qint64 NowMs()
{
	return QDateTime::currentMSecsSinceEpoch();
}

static QString Clock(qint64 ms)
{
	if (ms < 0)
		ms = 0;
	const qint64 s = ms / 1000;
	return QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'));
}

CueEngine::CueEngine()
{
	ticker.setInterval(250);
	connect(&ticker, &QTimer::timeout, this, &CueEngine::Tick);
	Manager &m = Manager::Get();
	/* Une autre scène choisie en Aperçu : la cue n'est plus armée. */
	connect(&m, &Manager::previewChanged, this, [this]() {
		if (arming || armedId.empty())
			return;
		const Cue *cue = Find(armedId);
		OBSSource preview = Manager::Get().PreviewScene();
		const char *uuid = preview ? obs_source_get_uuid(preview) : nullptr;
		if (!cue || cue->steps.empty() || !uuid || cue->steps.front().scene != uuid)
			Disarm();
	});
	/* Transition / Cut (boutons, raccourcis, Companion) : une cue armée
	 * démarre ; une cue en cours s'arrête et la prise normale a lieu. */
	m.takeInterceptor = [this](bool cut) {
		if (Running())
			Stop();
		if (!Manager::Get().CuesEnabled() || armedId.empty())
			return false;
		return Go(armedId, cut);
	};
}

void CueEngine::SetCues(const std::vector<Cue> &list)
{
	cues = list;
	if (!armedId.empty() && !Find(armedId))
		armedId.clear();
	emit cuesChanged();
	emit stateChanged();
}

const Cue *CueEngine::Find(const std::string &id) const
{
	for (const auto &c : cues) {
		if (c.id == id)
			return &c;
	}
	return nullptr;
}

const Cue *CueEngine::FindByName(const std::string &name) const
{
	for (const auto &c : cues) {
		if (c.name == name)
			return &c;
	}
	return nullptr;
}

std::string CueEngine::NewId()
{
	return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

void CueEngine::Arm(const std::string &id)
{
	const Cue *cue = Find(id);
	if (!cue)
		return;
	armedId = id;
	if (!cue->steps.empty()) {
		OBSSourceAutoRelease first = obs_get_source_by_uuid(cue->steps.front().scene.c_str());
		if (first) {
			arming = true;
			Manager::Get().SetPreviewScene(first);
			arming = false;
		}
	}
	emit stateChanged();
}

void CueEngine::Disarm()
{
	if (armedId.empty())
		return;
	armedId.clear();
	emit stateChanged();
}

void CueEngine::Schedule(int ms, std::function<void()> fn)
{
	const int gen = generation;
	QTimer::singleShot(std::max(0, ms), this, [this, gen, fn]() {
		if (gen == generation)
			fn();
	});
}

bool CueEngine::Go(const std::string &id, bool cut)
{
	const Cue *cue = Find(id.empty() ? armedId : id);
	if (!cue || cue->steps.empty())
		return false;
	Stop();
	runningId = cue->id;
	runningStep = 0;
	firstCut = cut;
	OBSSource program = Manager::Get().ProgramScene();
	programBefore = program ? OBSGetWeakRef(program.Get()) : OBSWeakSource();
	armedId.clear();
	obs_log(LOG_INFO, "cue « %s » lancée (%zu scène(s))", cue->name.c_str(), cue->steps.size());
	ticker.start();
	emit stateChanged();
	RunStep();
	return true;
}

void CueEngine::StartPhase(int value, int ms)
{
	phase = value;
	phaseStart = NowMs();
	phaseLength = ms;
	emit progressChanged();
}

void CueEngine::RunStep()
{
	const Cue *cue = Find(runningId);
	if (!cue || runningStep < 0 || size_t(runningStep) >= cue->steps.size()) {
		Finish();
		return;
	}
	const CueStep step = cue->steps[size_t(runningStep)];
	emit stateChanged();
	StartPhase(PhasePreWait, step.preWaitMs);
	Schedule(step.preWaitMs, [this, step]() {
		OBSSourceAutoRelease scene = obs_get_source_by_uuid(step.scene.c_str());
		int busyMs = 0;
		if (scene) {
			bool cut = step.transition == "cut" || (runningStep == 0 && firstCut);
			OBSSource transition = cut ? OBSSource() : Manager::FindTransition(step.transition);
			OBSSourceAutoRelease current = obs_frontend_get_current_transition();
			obs_source_t *used = transition ? transition.Get() : current.Get();
			if (used && obs_transition_fixed(used))
				cut = true;
			const int duration = step.durationMs > 0 ? step.durationMs : -1;
			Manager::Get().TakeScene(scene, cut, transition, duration);
			if (!cut)
				busyMs = duration > 0 ? duration : obs_frontend_get_transition_duration();
		}
		/* « Fin du média » : la scène suivante part quand le média de la
		 * scène est terminé (suivi dans Tick). */
		if (step.nextOnMediaEnd && scene) {
			WatchMedia(scene);
			if (OBSGetStrongRef(media)) {
				StartPhase(PhaseMedia, 0);
				return;
			}
			obs_log(LOG_INFO, "cue : aucun média dans « %s », post-délai utilisé",
				obs_source_get_name(scene));
		}
		/* L'étape suivante attend au moins la fin de la transition : une
		 * transition ne peut pas en interrompre une autre. */
		const int wait = std::max(step.postWaitMs, busyMs > 0 ? busyMs + 60 : 0);
		StartPhase(PhasePostWait, wait);
		Schedule(wait, [this]() { Advance(); });
	});
}

void CueEngine::Advance()
{
	media = nullptr;
	runningStep++;
	RunStep();
}

/* Média le plus long de la scène (éléments visibles, groupes compris). */
void CueEngine::WatchMedia(obs_source_t *scene)
{
	media = nullptr;
	mediaStarted = false;
	struct Search {
		OBSSource best;
		int64_t duration = -1;
	} search;
	auto visit = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto *found = static_cast<Search *>(param);
		if (!obs_sceneitem_visible(item))
			return true;
		obs_source_t *src = obs_sceneitem_get_source(item);
		if (obs_sceneitem_is_group(item)) {
			obs_scene_enum_items(
				obs_sceneitem_group_get_scene(item),
				[](obs_scene_t *, obs_sceneitem_t *child, void *p) {
					auto *f = static_cast<Search *>(p);
					obs_source_t *c = obs_sceneitem_get_source(child);
					if (obs_sceneitem_visible(child) &&
					    (obs_source_get_output_flags(c) & OBS_SOURCE_CONTROLLABLE_MEDIA)) {
						const int64_t d = obs_source_media_get_duration(c);
						if (d > f->duration || !f->best) {
							f->best = c;
							f->duration = d;
						}
					}
					return true;
				},
				found);
			return true;
		}
		if (obs_source_get_output_flags(src) & OBS_SOURCE_CONTROLLABLE_MEDIA) {
			const int64_t d = obs_source_media_get_duration(src);
			if (d > found->duration || !found->best) {
				found->best = src;
				found->duration = d;
			}
		}
		return true;
	};
	obs_scene_enum_items(obs_scene_from_source(scene), visit, &search);
	if (search.best)
		media = OBSGetWeakRef(search.best.Get());
}

void CueEngine::Tick()
{
	if (!Running())
		return;
	if (phase == PhaseMedia) {
		OBSSource src = OBSGetStrongRef(media);
		if (!src) {
			Advance();
			return;
		}
		const obs_media_state state = obs_source_media_get_state(src);
		if (state == OBS_MEDIA_STATE_PLAYING || state == OBS_MEDIA_STATE_PAUSED ||
		    state == OBS_MEDIA_STATE_BUFFERING || state == OBS_MEDIA_STATE_OPENING)
			mediaStarted = true;
		const bool finished = state == OBS_MEDIA_STATE_ENDED || state == OBS_MEDIA_STATE_STOPPED ||
				      state == OBS_MEDIA_STATE_ERROR;
		/* Un média qui ne démarre jamais (déjà fini, pas de redémarrage)
		 * ne bloque pas la cue plus de 5 s. */
		if ((finished && mediaStarted) || (!mediaStarted && NowMs() - phaseStart > 5000)) {
			Advance();
			return;
		}
	}
	emit progressChanged();
}

QString CueEngine::StatusText() const
{
	if (!Running())
		return QString();
	const qint64 elapsed = NowMs() - phaseStart;
	switch (phase) {
	case PhasePreWait:
		return QString("⏱ %1").arg(Clock(phaseLength - elapsed + 999));
	case PhasePostWait:
		return QString("● %1").arg(Clock(phaseLength - elapsed + 999));
	case PhaseMedia: {
		OBSSource src = OBSGetStrongRef(media);
		if (!src)
			return QString();
		const int64_t t = obs_source_media_get_time(src);
		const int64_t d = obs_source_media_get_duration(src);
		return QString("▶ %1 / %2").arg(Clock(t)).arg(Clock(d));
	}
	default:
		return QString();
	}
}

void CueEngine::Finish()
{
	runningId.clear();
	runningStep = -1;
	phase = PhaseNone;
	media = nullptr;
	ticker.stop();
	/* Comme une prise normale : l'ancien Program revient en Aperçu. */
	OBSSource before = OBSGetStrongRef(programBefore);
	if (Manager::Get().SwapAfterTake() && before)
		Manager::Get().SetPreviewScene(before);
	emit stateChanged();
}

void CueEngine::Stop()
{
	generation++;
	phase = PhaseNone;
	media = nullptr;
	ticker.stop();
	if (runningId.empty())
		return;
	runningId.clear();
	runningStep = -1;
	emit stateChanged();
}

void CueEngine::Save(obs_data_t *data) const
{
	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (const auto &c : cues) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "id", c.id.c_str());
		obs_data_set_string(item, "name", c.name.c_str());
		OBSDataArrayAutoRelease steps = obs_data_array_create();
		for (const auto &s : c.steps) {
			OBSDataAutoRelease st = obs_data_create();
			obs_data_set_string(st, "scene", s.scene.c_str());
			obs_data_set_int(st, "pre_wait", s.preWaitMs);
			obs_data_set_int(st, "post_wait", s.postWaitMs);
			obs_data_set_string(st, "transition", s.transition.c_str());
			obs_data_set_int(st, "duration", s.durationMs);
			obs_data_set_bool(st, "next_on_media_end", s.nextOnMediaEnd);
			obs_data_array_push_back(steps, st);
		}
		obs_data_set_array(item, "steps", steps);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(data, "cues", list);
}

void CueEngine::Load(obs_data_t *data)
{
	Stop();
	cues.clear();
	armedId.clear();
	OBSDataArrayAutoRelease list = obs_data_get_array(data, "cues");
	for (size_t i = 0; i < obs_data_array_count(list); i++) {
		OBSDataAutoRelease item = obs_data_array_item(list, i);
		Cue c;
		c.id = obs_data_get_string(item, "id");
		if (c.id.empty())
			c.id = NewId();
		c.name = obs_data_get_string(item, "name");
		OBSDataArrayAutoRelease steps = obs_data_get_array(item, "steps");
		for (size_t k = 0; k < obs_data_array_count(steps); k++) {
			OBSDataAutoRelease st = obs_data_array_item(steps, k);
			CueStep s;
			s.scene = obs_data_get_string(st, "scene");
			s.preWaitMs = int(obs_data_get_int(st, "pre_wait"));
			s.postWaitMs = int(obs_data_get_int(st, "post_wait"));
			s.transition = obs_data_get_string(st, "transition");
			s.durationMs = int(obs_data_get_int(st, "duration"));
			s.nextOnMediaEnd = obs_data_get_bool(st, "next_on_media_end");
			c.steps.push_back(s);
		}
		cues.push_back(c);
	}
	emit cuesChanged();
	emit stateChanged();
}

void CueEngine::Reset()
{
	Stop();
	cues.clear();
	armedId.clear();
	emit cuesChanged();
	emit stateChanged();
}
