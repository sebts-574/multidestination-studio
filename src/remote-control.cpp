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

#include "remote-control.hpp"
#include "cues.hpp"
#include "manager.hpp"
#include "obs-websocket-api.h"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QCoreApplication>
#include <QDateTime>
#include <QMetaObject>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <functional>
#include <string>

namespace {

obs_websocket_vendor vendor = nullptr;

/* Les requêtes arrivent sur un fil d'obs-websocket : tout se fait sur le
 * fil de l'interface. */
void OnUi(const std::function<void()> &fn)
{
	if (QThread::currentThread() == qApp->thread()) {
		fn();
		return;
	}
	QMetaObject::invokeMethod(qApp, fn, Qt::BlockingQueuedConnection);
}

OBSSource FindScene(obs_data_t *request)
{
	const char *uuid = obs_data_get_string(request, "sceneUuid");
	const char *name = obs_data_get_string(request, "sceneName");
	for (auto &s : Manager::MainScenes()) {
		if ((uuid && *uuid && strcmp(obs_source_get_uuid(s), uuid) == 0) ||
		    (name && *name && strcmp(obs_source_get_name(s), name) == 0))
			return s;
	}
	return nullptr;
}

void Fail(obs_data_t *response, const char *error)
{
	obs_data_set_bool(response, "ok", false);
	obs_data_set_string(response, "error", error);
}

void Ok(obs_data_t *response)
{
	obs_data_set_bool(response, "ok", true);
}

obs_data_array_t *GroupsState();
void EmitStateChanged();

const char *SceneName(const OBSSource &s)
{
	return s ? obs_source_get_name(s) : "";
}

void GetState(obs_data_t *, obs_data_t *response, void *)
{
	OnUi([response]() {
		Manager &m = Manager::Get();
		obs_data_set_string(response, "previewScene", SceneName(m.PreviewScene()));
		obs_data_set_string(response, "programScene", SceneName(m.ProgramScene()));
		OBSDataArrayAutoRelease scenes = obs_data_array_create();
		for (auto &s : Manager::MainScenes()) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "sceneName", obs_source_get_name(s));
			obs_data_set_string(item, "sceneUuid", obs_source_get_uuid(s));
			obs_data_array_push_back(scenes, item);
		}
		obs_data_set_array(response, "scenes", scenes);
		OBSDataArrayAutoRelease transitions = obs_data_array_create();
		struct obs_frontend_source_list list = {};
		obs_frontend_get_transitions(&list);
		for (size_t i = 0; i < list.sources.num; i++) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "transitionName", obs_source_get_name(list.sources.array[i]));
			obs_data_array_push_back(transitions, item);
		}
		obs_frontend_source_list_free(&list);
		obs_data_set_array(response, "transitions", transitions);
		obs_data_set_bool(response, "swapAfterTake", m.SwapAfterTake());
		obs_data_set_bool(response, "showAllDestinations", m.ShowAllDestinations());
		obs_data_set_bool(response, "cuesEnabled", m.CuesEnabled());
		const CueEngine &engine = CueEngine::Get();
		const Cue *running = engine.Find(engine.RunningId());
		const Cue *armed = engine.Find(engine.ArmedId());
		obs_data_set_string(response, "runningCue", running ? running->name.c_str() : "");
		obs_data_set_string(response, "previewCue", armed ? armed->name.c_str() : "");
		OBSDataArrayAutoRelease cues = obs_data_array_create();
		for (const auto &c : CueEngine::Get().Cues()) {
			OBSDataAutoRelease item = obs_data_create();
			obs_data_set_string(item, "cueName", c.name.c_str());
			obs_data_array_push_back(cues, item);
		}
		obs_data_set_array(response, "cues", cues);
		/* Avancement de la cue en cours (retour pendant la lecture d'un
		 * média, temps restant d'une attente). */
		obs_data_set_int(response, "cueStep", engine.Running() ? engine.RunningStep() + 1 : 0);
		obs_data_set_int(response, "cueSteps", running ? int64_t(running->steps.size()) : 0);
		obs_data_set_string(response, "cuePhase", engine.PhaseName());
		obs_data_set_int(response, "cueRemaining", engine.RemainingMs());
		int64_t mediaTime = 0, mediaDuration = 0;
		engine.MediaTimes(mediaTime, mediaDuration);
		obs_data_set_int(response, "cueMediaTime", mediaTime);
		obs_data_set_int(response, "cueMediaDuration", mediaDuration);
		OBSDataArrayAutoRelease groups = GroupsState();
		obs_data_set_array(response, "groups", groups);
		Ok(response);
	});
}

void SetPreviewScene(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		OBSSource scene = FindScene(request);
		if (!scene)
			return Fail(response, "scene not found");
		Manager::Get().SetPreviewScene(scene);
		Ok(response);
	});
}

/* Transition et durée facultatives : {"transitionName": …, "duration": ms}. */
void ReadTransition(obs_data_t *request, OBSSource &transition, int &duration)
{
	const char *name = obs_data_get_string(request, "transitionName");
	transition = (name && *name) ? Manager::FindTransition(name) : OBSSource();
	duration = obs_data_has_user_value(request, "duration") ? int(obs_data_get_int(request, "duration")) : -1;
}

void SetProgramScene(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		OBSSource scene = FindScene(request);
		if (!scene)
			return Fail(response, "scene not found");
		OBSSource transition;
		int duration = -1;
		ReadTransition(request, transition, duration);
		/* Prise faite par l'utilisateur : une cue en cours s'arrête. */
		emit Manager::Get().userTake();
		Manager::Get().TakeScene(scene, obs_data_get_bool(request, "cut"), transition, duration);
		Ok(response);
	});
}

void Transition(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		OBSSource transition;
		int duration = -1;
		ReadTransition(request, transition, duration);
		Manager::Get().Take(false, transition, duration);
		Ok(response);
	});
}

void Cut(obs_data_t *, obs_data_t *response, void *)
{
	OnUi([response]() {
		Manager::Get().Take(true);
		Ok(response);
	});
}

const Cue *FindCue(obs_data_t *request)
{
	const char *name = obs_data_get_string(request, "cueName");
	return (name && *name) ? CueEngine::Get().FindByName(name) : nullptr;
}

void SetPreviewCue(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		if (!Manager::Get().CuesEnabled())
			return Fail(response, "cues are disabled");
		const Cue *cue = FindCue(request);
		if (!cue)
			return Fail(response, "cue not found");
		CueEngine::Get().Arm(cue->id);
		Ok(response);
	});
}

void GoCue(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		if (!Manager::Get().CuesEnabled())
			return Fail(response, "cues are disabled");
		const Cue *cue = FindCue(request);
		const std::string id = cue ? cue->id : std::string();
		/* Transition de la première scène (comme une transition rapide). */
		OBSSource transition;
		int duration = -1;
		ReadTransition(request, transition, duration);
		if (!CueEngine::Get().Go(id, obs_data_get_bool(request, "cut"), transition, duration))
			return Fail(response, "no cue to run");
		Ok(response);
	});
}

void StopCue(obs_data_t *, obs_data_t *response, void *)
{
	OnUi([response]() {
		CueEngine::Get().StopWithFade();
		Ok(response);
	});
}

/* ---- Sélecteur de groupes ----
 * Un groupe = un nom : les groupes de même nom (le même groupe référencé
 * dans plusieurs scènes, ou un groupe recréé à l'identique dans une autre
 * destination) sont pilotés ensemble, leurs sources étant reconnues par
 * leur nom. Afficher une source cache les autres sources du groupe. */

/* Tous les groupes des scènes communes et des scènes des destinations. */
std::vector<obs_sceneitem_t *> AllGroupItems()
{
	std::vector<obs_source_t *> scenes;
	for (auto &s : Manager::MainScenes()) {
		scenes.push_back(s);
		for (Destination *d : Manager::Get().Destinations()) {
			if (OBSSource linked = d->LinkedScene(s); linked && linked.Get() != s.Get())
				scenes.push_back(linked);
		}
	}
	std::vector<obs_sceneitem_t *> groups;
	std::vector<obs_source_t *> seen;
	for (obs_source_t *sc : scenes) {
		obs_scene_t *scene = obs_scene_from_source(sc);
		if (!scene)
			continue;
		struct Ctx {
			std::vector<obs_sceneitem_t *> *groups;
			std::vector<obs_source_t *> *seen;
		} ctx{&groups, &seen};
		obs_scene_enum_items(
			scene,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				auto *c = static_cast<Ctx *>(param);
				obs_source_t *src = obs_sceneitem_get_source(item);
				if (!obs_sceneitem_is_group(item) ||
				    std::find(c->seen->begin(), c->seen->end(), src) != c->seen->end())
					return true;
				c->seen->push_back(src);
				c->groups->push_back(item);
				return true;
			},
			&ctx);
	}
	return groups;
}

/* Sources d'un groupe, du haut vers le bas (comme la liste des sources). */
std::vector<obs_sceneitem_t *> GroupChildren(obs_sceneitem_t *group)
{
	std::vector<obs_sceneitem_t *> children;
	obs_scene_enum_items(
		obs_sceneitem_group_get_scene(group),
		[](obs_scene_t *, obs_sceneitem_t *child, void *param) {
			auto *list = static_cast<std::vector<obs_sceneitem_t *> *>(param);
			list->insert(list->begin(), child);
			return true;
		},
		&children);
	return children;
}

const char *ItemName(obs_sceneitem_t *item)
{
	const char *n = obs_source_get_name(obs_sceneitem_get_source(item));
	return n ? n : "";
}

obs_data_array_t *GroupsState()
{
	obs_data_array_t *list = obs_data_array_create();
	std::vector<std::string> names;
	for (obs_sceneitem_t *group : AllGroupItems()) {
		const std::string name = ItemName(group);
		if (std::find(names.begin(), names.end(), name) != names.end())
			continue;
		names.push_back(name);
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "groupName", name.c_str());
		OBSDataArrayAutoRelease sources = obs_data_array_create();
		std::string current;
		for (obs_sceneitem_t *child : GroupChildren(group)) {
			OBSDataAutoRelease src = obs_data_create();
			obs_data_set_string(src, "sourceName", ItemName(child));
			obs_data_set_bool(src, "visible", obs_sceneitem_visible(child));
			obs_data_array_push_back(sources, src);
			if (current.empty() && obs_sceneitem_visible(child))
				current = ItemName(child);
		}
		obs_data_set_array(item, "sources", sources);
		obs_data_set_string(item, "current", current.c_str());
		obs_data_array_push_back(list, item);
	}
	return list;
}

/* Affiche la source « sourceName » dans tous les groupes « groupName » et
 * cache les autres. */
bool ShowInGroup(const std::string &groupName, const std::string &sourceName)
{
	bool found = false;
	for (obs_sceneitem_t *group : AllGroupItems()) {
		if (groupName != ItemName(group))
			continue;
		for (obs_sceneitem_t *child : GroupChildren(group)) {
			const bool show = sourceName == ItemName(child);
			found = found || show;
			if (obs_sceneitem_visible(child) != show)
				obs_sceneitem_set_visible(child, show);
		}
	}
	return found;
}

void SelectGroupSource(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		const std::string group = obs_data_get_string(request, "groupName");
		const std::string source = obs_data_get_string(request, "sourceName");
		if (!ShowInGroup(group, source))
			return Fail(response, "group or source not found");
		EmitStateChanged();
		Ok(response);
	});
}

/* Source suivante du groupe (après la première visible ; retour au début
 * après la dernière). {"direction": -1} pour la précédente. */
void CycleGroup(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		const std::string groupName = obs_data_get_string(request, "groupName");
		const int direction = obs_data_get_int(request, "direction") < 0 ? -1 : 1;
		for (obs_sceneitem_t *group : AllGroupItems()) {
			if (groupName != ItemName(group))
				continue;
			const auto children = GroupChildren(group);
			if (children.empty())
				return Fail(response, "group is empty");
			int current = -1;
			for (size_t i = 0; i < children.size(); i++) {
				if (obs_sceneitem_visible(children[i])) {
					current = int(i);
					break;
				}
			}
			const int n = int(children.size());
			const int next = current < 0 ? (direction > 0 ? 0 : n - 1) : (current + direction + n) % n;
			const std::string name = ItemName(children[size_t(next)]);
			ShowInGroup(groupName, name);
			obs_data_set_string(response, "current", name.c_str());
			EmitStateChanged();
			return Ok(response);
		}
		Fail(response, "group not found");
	});
}

/* Réglages : {"value": bool} ; sans valeur, bascule. */
bool ReadToggle(obs_data_t *request, bool current)
{
	return obs_data_has_user_value(request, "value") ? obs_data_get_bool(request, "value") : !current;
}

void SetAfterTransition(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		Manager::Get().SetSwapAfterTake(ReadToggle(request, Manager::Get().SwapAfterTake()));
		obs_data_set_bool(response, "swapAfterTake", Manager::Get().SwapAfterTake());
		Ok(response);
	});
}

void SetDestinationsShown(obs_data_t *request, obs_data_t *response, void *)
{
	OnUi([request, response]() {
		Manager::Get().SetShowAllDestinations(ReadToggle(request, Manager::Get().ShowAllDestinations()));
		obs_data_set_bool(response, "showAllDestinations", Manager::Get().ShowAllDestinations());
		Ok(response);
	});
}

/* Un seul « StateChanged » par passage de la boucle d'événements : le
 * client relit l'état avec GetState. */
bool statePending = false;

void EmitStateChanged()
{
	if (!vendor || statePending)
		return;
	statePending = true;
	QTimer::singleShot(0, qApp, []() {
		statePending = false;
		if (!vendor)
			return;
		OBSDataAutoRelease data = obs_data_create();
		obs_websocket_vendor_emit_event(vendor, "StateChanged", data);
	});
}

void EmitScene(const char *event, const OBSSource &scene)
{
	if (!vendor)
		return;
	OBSDataAutoRelease data = obs_data_create();
	obs_data_set_string(data, "sceneName", SceneName(scene));
	obs_websocket_vendor_emit_event(vendor, event, data);
}

} // namespace

void RemoteControl::Register()
{
	vendor = obs_websocket_register_vendor("multidestination-studio");
	if (!vendor) {
		obs_log(LOG_INFO, "obs-websocket absent : pilotage par Companion indisponible");
		return;
	}
	const std::pair<const char *, obs_websocket_request_callback_function> requests[] = {
		{"GetState", GetState},
		{"SetPreviewScene", SetPreviewScene},
		{"SetProgramScene", SetProgramScene},
		{"Transition", Transition},
		{"Cut", Cut},
		{"SetPreviewCue", SetPreviewCue},
		{"GoCue", GoCue},
		{"StopCue", StopCue},
		{"SetAfterTransition", SetAfterTransition},
		{"SetDestinationsShown", SetDestinationsShown},
		{"CycleGroup", CycleGroup},
		{"SelectGroupSource", SelectGroupSource},
	};
	for (const auto &r : requests)
		obs_websocket_vendor_register_request(vendor, r.first, r.second, nullptr);

	Manager &m = Manager::Get();
	QObject::connect(&m, &Manager::previewChanged, &m,
			 []() { EmitScene("PreviewSceneChanged", Manager::Get().PreviewScene()); });
	QObject::connect(&m, &Manager::programChanged, &m,
			 []() { EmitScene("ProgramSceneChanged", Manager::Get().ProgramScene()); });
	for (auto signal : {&Manager::previewChanged, &Manager::programChanged, &Manager::settingsChanged,
			    &Manager::scenesChanged, &Manager::loaded, &Manager::quickTransitionsChanged})
		QObject::connect(&m, signal, &m, []() { EmitStateChanged(); });
	for (auto signal : {&CueEngine::cuesChanged, &CueEngine::stateChanged})
		QObject::connect(&CueEngine::Get(), signal, &m, []() { EmitStateChanged(); });
	/* Avancement d'une cue : au plus deux fois par seconde. */
	QObject::connect(&CueEngine::Get(), &CueEngine::progressChanged, &m, []() {
		static qint64 last = 0;
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		if (now - last < 500)
			return;
		last = now;
		EmitStateChanged();
	});
	/* Sources des groupes affichées ou cachées ailleurs (Studio, OBS) :
	 * vérifié chaque seconde, l'état n'est envoyé que s'il a changé. */
	auto *watch = new QTimer(qApp);
	watch->setInterval(1000);
	QObject::connect(watch, &QTimer::timeout, &m, []() {
		if (!Manager::Get().Ready())
			return;
		static std::string previous;
		OBSDataAutoRelease holder = obs_data_create();
		OBSDataArrayAutoRelease groups = GroupsState();
		obs_data_set_array(holder, "groups", groups);
		const std::string json = obs_data_get_json(holder);
		if (json != previous) {
			previous = json;
			EmitStateChanged();
		}
	});
	watch->start();
	obs_log(LOG_INFO, "pilotage obs-websocket prêt (fournisseur « multidestination-studio »)");
}
