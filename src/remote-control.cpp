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
#include <QMetaObject>
#include <QThread>
#include <QTimer>

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
		if (!CueEngine::Get().Go(id, obs_data_get_bool(request, "cut")))
			return Fail(response, "no cue to run");
		Ok(response);
	});
}

void StopCue(obs_data_t *, obs_data_t *response, void *)
{
	OnUi([response]() {
		CueEngine::Get().Stop();
		Ok(response);
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
	obs_log(LOG_INFO, "pilotage obs-websocket prêt (fournisseur « multidestination-studio »)");
}
