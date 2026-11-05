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

#include "destination.hpp"

#include <QCoreApplication>
#include <QTimer>

#include <algorithm>
#include "scene-editor.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <cstring>

/* Le canvas sert à la vidéo uniquement : pas de MIX_AUDIO, pour éviter
 * qu'une source présente dans plusieurs destinations soit mixée plusieurs
 * fois. L'audio reste géré par le mixeur principal d'OBS. */
static constexpr uint32_t DESTINATION_CANVAS_FLAGS = ACTIVATE | SCENE_REF;

static std::string SourceUuid(obs_source_t *source)
{
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	return uuid ? uuid : "";
}

Destination *Destination::CreateMain()
{
	auto *dest = new Destination();
	dest->isMain = true;
	dest->name = Tr("MDS.DefaultDestination").toStdString();
	return dest;
}

Destination::Destination(std::string name_) : name(std::move(name_)) {}

Destination::~Destination()
{
	Detach();
}

std::string Destination::Id() const
{
	if (isMain)
		return MAIN_ID;
	std::lock_guard<std::mutex> lock(mutex);
	const char *uuid = canvas ? obs_canvas_get_uuid(canvas) : nullptr;
	return uuid ? uuid : "";
}

void Destination::SetName(const std::string &newName)
{
	if (isMain || newName.empty())
		return;
	name = newName;
	if (canvas)
		obs_canvas_set_name(canvas, name.c_str());
	/* Le nom réel peut avoir été dédoublonné par OBS. */
	if (canvas && obs_canvas_get_name(canvas))
		name = obs_canvas_get_name(canvas);
}

/* ------------------------------------------------------------------ */
/* Canvas                                                               */
/* ------------------------------------------------------------------ */

bool Destination::Attach(uint32_t w, uint32_t h, const std::string &uuidHint)
{
	if (isMain)
		return true;

	if (w > 0 && h > 0) {
		width = w;
		height = h;
	}

	if (!canvas) {
		obs_canvas_t *found = nullptr;

		struct obs_frontend_canvas_list list = {};
		obs_frontend_get_canvases(&list);
		/* Chargement : par UUID (stable), puis par nom. Nouvelle
		 * destination (pas d'UUID) : jamais par nom, sinon elle
		 * reprendrait le canvas d'une autre destination ou d'une
		 * destination perdue, avec toutes ses scènes. */
		const int passes = uuidHint.empty() ? 0 : 2;
		for (int pass = 0; pass < passes && !found; pass++) {
			for (size_t i = 0; i < list.canvases.num; i++) {
				obs_canvas_t *c = list.canvases.array[i];
				if (obs_canvas_removed(c))
					continue;
				const char *key = pass == 0 ? obs_canvas_get_uuid(c) : obs_canvas_get_name(c);
				const std::string &wanted = pass == 0 ? uuidHint : name;
				if (key && !wanted.empty() && wanted == key) {
					found = obs_canvas_get_ref(c);
					break;
				}
			}
		}
		obs_frontend_canvas_list_free(&list);

		if (!found)
			found = obs_frontend_add_canvas(name.c_str(), nullptr, DESTINATION_CANVAS_FLAGS);

		if (!found) {
			obs_log(LOG_ERROR, "[%s] impossible de créer le canvas", name.c_str());
			return false;
		}

		{
			std::lock_guard<std::mutex> lock(mutex);
			canvas = found;
		}
		if (obs_canvas_get_name(found))
			name = obs_canvas_get_name(found);
	}

	StartVideo();

	if (!transition)
		CreateTransitionFrom(nullptr);

	obs_log(LOG_INFO, "[%s] destination prête (%ux%u)", name.c_str(), width, height);
	return true;
}

void Destination::StartVideo()
{
	if (!canvas)
		return;

	struct obs_video_info ovi = {};
	if (!obs_get_video_info(&ovi))
		return;

	struct obs_video_info current = {};
	if (obs_canvas_has_video(canvas) && obs_canvas_get_video_info(canvas, &current) &&
	    current.base_width == width && current.base_height == height && current.fps_num == ovi.fps_num &&
	    current.fps_den == ovi.fps_den)
		return;

	/* Même cadence et même format couleur que le canvas principal,
	 * résolution propre à la destination. */
	ovi.base_width = width;
	ovi.base_height = height;
	ovi.output_width = width;
	ovi.output_height = height;

	if (!obs_canvas_reset_video(canvas, &ovi))
		obs_log(LOG_WARNING, "[%s] la vidéo du canvas n'a pas pu démarrer", name.c_str());

	if (transition)
		obs_transition_set_size(transition, width, height);
}

void Destination::Detach()
{
	obs_canvas_t *old = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		old = canvas;
		canvas = nullptr;
		programScene = nullptr;
		liveCopy = nullptr;
	}
	retired.clear();
	if (old) {
		obs_canvas_set_channel(old, 0, nullptr);
		obs_canvas_release(old);
	}
	transition = nullptr;
}

void Destination::DestroyCanvas()
{
	if (isMain)
		return;
	obs_canvas_t *c = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		c = canvas ? obs_canvas_get_ref(canvas) : nullptr;
	}
	Detach();
	if (c) {
		obs_frontend_remove_canvas(c);
		obs_canvas_release(c);
	}
}

bool Destination::Attached() const
{
	if (isMain)
		return true;
	std::lock_guard<std::mutex> lock(mutex);
	return canvas != nullptr;
}

uint32_t Destination::Width() const
{
	if (isMain) {
		struct obs_video_info ovi = {};
		return obs_get_video_info(&ovi) ? ovi.base_width : 1920;
	}
	return width;
}

uint32_t Destination::Height() const
{
	if (isMain) {
		struct obs_video_info ovi = {};
		return obs_get_video_info(&ovi) ? ovi.base_height : 1080;
	}
	return height;
}

/* ------------------------------------------------------------------ */
/* Scènes liées                                                         */
/* ------------------------------------------------------------------ */

OBSSource Destination::FindLinked(const std::string &mainUuid) const
{
	if (!canvas || mainUuid.empty())
		return nullptr;

	struct Ctx {
		const std::string *uuid;
		OBSSource found;
	} ctx{&mainUuid, nullptr};

	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *c = static_cast<Ctx *>(param);
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			const char *link = obs_data_get_string(priv, LINK_KEY);
			if (link && *c->uuid == link) {
				c->found = source;
				return false;
			}
			return true;
		},
		&ctx);
	return ctx.found;
}

bool Destination::Covers(obs_source_t *mainScene) const
{
	if (!mainScene)
		return false;
	if (isMain) {
		if (disabled)
			return false;
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		return !obs_data_get_bool(priv, EXCLUDE_MAIN_KEY);
	}
	return FindLinked(SourceUuid(mainScene)) != nullptr;
}

OBSSource Destination::LinkedScene(obs_source_t *mainScene) const
{
	if (!mainScene)
		return nullptr;
	if (isMain)
		return Covers(mainScene) ? OBSSource(mainScene) : OBSSource();
	return FindLinked(SourceUuid(mainScene));
}

bool Destination::AddCoverage(obs_source_t *mainScene)
{
	if (!mainScene)
		return false;

	if (isMain) {
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		obs_data_set_bool(priv, EXCLUDE_MAIN_KEY, false);
		return true;
	}

	if (!canvas)
		return false;
	if (FindLinked(SourceUuid(mainScene)))
		return true;

	const char *sceneName = obs_source_get_name(mainScene);
	obs_scene_t *scene = obs_canvas_scene_create(canvas, sceneName ? sceneName : "Scene");
	if (!scene)
		return false;

	obs_source_t *source = obs_scene_get_source(scene);
	OBSDataAutoRelease priv = obs_source_get_private_settings(source);
	obs_data_set_string(priv, LINK_KEY, SourceUuid(mainScene).c_str());

	/* Le canvas (SCENE_REF) garde sa propre référence. */
	obs_scene_release(scene);
	return true;
}

void Destination::RemoveCoverage(obs_source_t *mainScene)
{
	if (!mainScene)
		return;

	if (isMain) {
		OBSDataAutoRelease priv = obs_source_get_private_settings(mainScene);
		obs_data_set_bool(priv, EXCLUDE_MAIN_KEY, true);
		return;
	}

	OBSSource linked = FindLinked(SourceUuid(mainScene));
	if (!linked)
		return;

	bool wasProgram = false;
	{
		std::lock_guard<std::mutex> lock(mutex);
		wasProgram = obs_weak_source_references_source(programScene, linked);
		if (wasProgram)
			programScene = nullptr;
	}
	if (wasProgram && transition) {
		obs_transition_force_stop(transition);
		obs_transition_set(transition, nullptr);
	}
	obs_source_remove(linked);
}

void Destination::SyncWithMainScenes(const std::vector<OBSSource> &mainScenes)
{
	if (isMain || !canvas)
		return;

	struct Entry {
		OBSSource source;
		std::string link;
	};
	std::vector<Entry> linked;
	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<Entry> *>(param);
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			const char *link = obs_data_get_string(priv, LINK_KEY);
			if (link && *link)
				list->push_back({OBSSource(source), link});
			return true;
		},
		&linked);

	for (auto &entry : linked) {
		obs_source_t *main = nullptr;
		for (auto &s : mainScenes) {
			if (SourceUuid(s) == entry.link) {
				main = s;
				break;
			}
		}

		if (!main) {
			/* La scène OBS a été supprimée : la scène liée aussi. */
			bool wasProgram = false;
			{
				std::lock_guard<std::mutex> lock(mutex);
				wasProgram = obs_weak_source_references_source(programScene, entry.source);
				if (wasProgram)
					programScene = nullptr;
			}
			if (wasProgram && transition)
				obs_transition_set(transition, nullptr);
			obs_source_remove(entry.source);
			continue;
		}

		const char *mainName = obs_source_get_name(main);
		const char *linkedName = obs_source_get_name(entry.source);
		if (mainName && linkedName && strcmp(mainName, linkedName) != 0)
			obs_source_set_name(entry.source, mainName);
	}
}

/* ------------------------------------------------------------------ */
/* Program                                                              */
/* ------------------------------------------------------------------ */

static OBSSource FindFrontendTransition(const char *id)
{
	OBSSource found;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		const char *tid = obs_source_get_id(list.sources.array[i]);
		if (tid && strcmp(tid, id) == 0) {
			found = list.sources.array[i];
			break;
		}
	}
	obs_frontend_source_list_free(&list);
	return found;
}

void Destination::Take(obs_source_t *linkedScene, bool cut, obs_source_t *templ, int durationMs)
{
	if (!linkedScene)
		return;

	if (isMain) {
		/* La destination par défaut passe par OBS : sa transition, son
		 * Program, son stream et son enregistrement. */
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		OBSSource cutTransition = cut ? FindFrontendTransition("cut_transition") : OBSSource();
		if (cut && cutTransition && current && current.Get() != cutTransition.Get()) {
			obs_frontend_set_current_transition(cutTransition);
			obs_frontend_set_current_scene(linkedScene);
			obs_frontend_set_current_transition(current);
			return;
		}
		/* Transition ou durée imposée (transition rapide, cue) : OBS
		 * l'utilise le temps de la transition, puis on remet ses réglages
		 * (sauf si quelqu'un les a changés entre-temps). */
		const int obsDuration = obs_frontend_get_transition_duration();
		const bool otherTransition = !cut && templ && current && templ != current.Get();
		const bool otherDuration = !cut && durationMs > 0 && durationMs != obsDuration;
		if (otherTransition)
			obs_frontend_set_current_transition(templ);
		if (otherDuration)
			obs_frontend_set_transition_duration(durationMs);
		obs_frontend_set_current_scene(linkedScene);
		if (otherTransition || otherDuration) {
			OBSSource previous = current.Get();
			OBSSource used = templ;
			const int restoreAfter = std::max(durationMs, obsDuration) + 250;
			QTimer::singleShot(restoreAfter, qApp,
					   [previous, used, otherTransition, otherDuration, obsDuration, durationMs]() {
						   OBSSourceAutoRelease now = obs_frontend_get_current_transition();
						   if (otherTransition && now.Get() == used.Get())
							   obs_frontend_set_current_transition(previous);
						   if (otherDuration &&
						       obs_frontend_get_transition_duration() == durationMs)
							   obs_frontend_set_transition_duration(obsDuration);
					   });
		}
		return;
	}

	CreateTransitionFrom(templ);
	if (!transition)
		return;

	/* Copie de la scène (mêmes sources, même placement) : c'est elle qui
	 * passe à l'antenne, pour que les retouches faites en Aperçu n'y
	 * arrivent pas. Elle est créée dans le canvas de la destination : OBS
	 * calcule les positions par rapport à la taille de la scène, une copie
	 * hors canvas prendrait la taille du canvas principal. */
	OBSSource copy = MakeLiveCopy(linkedScene);
	if (!copy)
		return;

	const bool immediate = cut || obs_transition_fixed(transition) || durationMs <= 0;
	if (immediate) {
		obs_transition_force_stop(transition);
		obs_transition_set(transition, copy);
	} else if (!obs_transition_start(transition, OBS_TRANSITION_MODE_AUTO, uint32_t(durationMs), copy)) {
		obs_source_remove(copy);
		return; /* une transition est déjà en cours */
	}

	OBSSource previous;
	{
		std::lock_guard<std::mutex> lock(mutex);
		programScene = OBSGetWeakRef(linkedScene);
		previous = liveCopy;
		liveCopy = copy;
	}
	if (previous)
		retired.push_back(previous);
	PurgeRetiredCopies();
}

void Destination::BeginManual(obs_source_t *linkedScene, obs_source_t *templ)
{
	if (isMain || !linkedScene)
		return;
	CreateTransitionFrom(templ);
	if (!transition)
		return;
	OBSSource copy = MakeLiveCopy(linkedScene);
	if (!copy)
		return;
	obs_transition_set_manual_torque(transition, 8.0f, 0.05f);
	if (!obs_transition_start(transition, OBS_TRANSITION_MODE_MANUAL, 0, copy)) {
		obs_source_remove(copy);
		return;
	}
	manualCopy = copy;
	manualScene = OBSGetWeakRef(linkedScene);
}

void Destination::SetManual(float t)
{
	if (manualCopy && transition)
		obs_transition_set_manual_time(transition, t);
}

void Destination::EndManual(bool finished)
{
	if (!manualCopy || !transition) {
		manualCopy = nullptr;
		return;
	}
	obs_transition_set_manual_time(transition, finished ? 1.0f : 0.0f);
	if (finished) {
		OBSSource scene = OBSGetStrongRef(manualScene);
		OBSSource previous;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (scene)
				programScene = OBSGetWeakRef(scene.Get());
			previous = liveCopy;
			liveCopy = manualCopy;
		}
		if (previous)
			retired.push_back(previous);
	} else {
		retired.push_back(manualCopy);
	}
	manualCopy = nullptr;
	manualScene = nullptr;
	PurgeRetiredCopies();
}

OBSSource Destination::MakeLiveCopy(obs_source_t *linkedScene)
{
	if (!canvas || !linkedScene)
		return nullptr;
	const char *n = obs_source_get_name(linkedScene);
	const std::string copyName = std::string(n ? n : "") + " [live]";
	obs_scene_t *dup =
		obs_scene_duplicate(obs_scene_from_source(linkedScene), copyName.c_str(), OBS_SCENE_DUP_REFS);
	if (!dup)
		return nullptr;
	OBSSource copy = obs_scene_get_source(dup);
	obs_scene_release(dup);

	/* La copie n'est pas une scène liée : on retire le lien recopié et on
	 * la marque pour pouvoir la nettoyer. */
	OBSDataAutoRelease priv = obs_source_get_private_settings(copy);
	obs_data_erase(priv, LINK_KEY);
	obs_data_set_bool(priv, LIVE_KEY, true);
	return copy;
}

/* Supprime les anciennes copies dès que la transition ne les affiche plus. */
void Destination::PurgeRetiredCopies()
{
	OBSSourceAutoRelease a = transition ? obs_transition_get_source(transition, OBS_TRANSITION_SOURCE_A) : nullptr;
	OBSSourceAutoRelease b = transition ? obs_transition_get_source(transition, OBS_TRANSITION_SOURCE_B) : nullptr;
	for (auto it = retired.begin(); it != retired.end();) {
		if (it->Get() == a.Get() || it->Get() == b.Get()) {
			++it;
			continue;
		}
		obs_source_remove(*it);
		it = retired.erase(it);
	}
}

/* Copies restées dans le canvas d'une session précédente. */
void Destination::RemoveStaleCopies()
{
	if (!canvas)
		return;
	OBSSource keep;
	{
		std::lock_guard<std::mutex> lock(mutex);
		keep = liveCopy;
	}
	std::vector<OBSSource> stale;
	obs_canvas_enum_scenes(
		canvas,
		[](void *param, obs_source_t *source) {
			auto *list = static_cast<std::vector<OBSSource> *>(param);
			OBSDataAutoRelease priv = obs_source_get_private_settings(source);
			if (obs_data_get_bool(priv, LIVE_KEY))
				list->emplace_back(source);
			return true;
		},
		&stale);
	for (auto &s : stale) {
		if (s.Get() != keep.Get())
			obs_source_remove(s);
	}
}

OBSSource Destination::ProgramOriginal() const
{
	if (isMain) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		return OBSSource(current.Get());
	}
	std::lock_guard<std::mutex> lock(mutex);
	return OBSGetStrongRef(programScene);
}

/* Retouches faites directement dans le Programme : on les recopie dans la
 * scène d'origine pour qu'elles restent après la transition suivante. */
void Destination::KeepLiveEdits()
{
	OBSSource live = LiveScene();
	OBSSource original = ProgramOriginal();
	if (!live || !original || live.Get() == original.Get())
		return;
	SceneEditor::CopyLayout(obs_scene_from_source(live), obs_scene_from_source(original));
}

std::string Destination::ProgramSceneName() const
{
	if (isMain) {
		OBSSourceAutoRelease current = obs_frontend_get_current_scene();
		const char *n = current ? obs_source_get_name(current) : nullptr;
		return n ? n : "";
	}
	std::lock_guard<std::mutex> lock(mutex);
	OBSSource s = OBSGetStrongRef(programScene);
	const char *n = s ? obs_source_get_name(s) : nullptr;
	return n ? n : "";
}

OBSSource Destination::LiveScene() const
{
	if (isMain) {
		/* OBS en mode Studio duplique lui-même la scène à l'antenne : on
		 * prend ce qu'affiche réellement sa transition. */
		OBSSourceAutoRelease tr = obs_get_output_source(0);
		if (!tr || obs_source_get_type(tr) != OBS_SOURCE_TYPE_TRANSITION)
			return nullptr;
		OBSSourceAutoRelease active = obs_transition_get_active_source(tr);
		return OBSSource(active.Get());
	}
	std::lock_guard<std::mutex> lock(mutex);
	return liveCopy;
}

/* ------------------------------------------------------------------ */
/* Transitions                                                          */
/* ------------------------------------------------------------------ */

static std::string TransitionSignature(obs_source_t *templ)
{
	if (!templ)
		return "fade_transition";
	std::string sig = obs_source_get_id(templ);
	OBSDataAutoRelease settings = obs_source_get_settings(templ);
	const char *json = settings ? obs_data_get_json(settings) : nullptr;
	if (json)
		sig += json;
	return sig;
}

/* Copie privée de la transition d'OBS (même type, mêmes réglages), recréée
 * seulement si elle a changé depuis la dernière fois. */
void Destination::CreateTransitionFrom(obs_source_t *templ)
{
	if (isMain)
		return;

	const std::string sig = TransitionSignature(templ);
	if (transition && sig == transitionSignature)
		return;

	const std::string trName = name + " - transition";
	OBSDataAutoRelease settings = templ ? obs_source_get_settings(templ) : nullptr;
	const char *id = templ ? obs_source_get_id(templ) : "fade_transition";
	OBSSourceAutoRelease next = obs_source_create_private(id, trName.c_str(), settings);
	if (!next) {
		obs_log(LOG_WARNING, "[%s] transition « %s » indisponible, fondu utilisé", name.c_str(), id);
		next = obs_source_create_private("fade_transition", trName.c_str(), nullptr);
		if (!next)
			return;
	}

	obs_transition_set_size(next, width, height);

	OBSSource previous = transition;
	if (previous) {
		obs_transition_force_stop(previous);
		obs_transition_swap_begin(next, previous);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, next);
		obs_transition_swap_end(next, previous);
	} else {
		OBSSource program;
		{
			std::lock_guard<std::mutex> lock(mutex);
			program = OBSGetStrongRef(programScene);
		}
		obs_transition_set(next, program);
		if (canvas)
			obs_canvas_set_channel(canvas, 0, next);
	}

	transition = next.Get();
	transitionSignature = sig;
}

/* ------------------------------------------------------------------ */
/* Rendu                                                                */
/* ------------------------------------------------------------------ */

void Destination::RenderProgram()
{
	if (isMain) {
		obs_render_main_texture();
		return;
	}

	obs_canvas_t *c = nullptr;
	{
		std::lock_guard<std::mutex> lock(mutex);
		c = canvas ? obs_canvas_get_ref(canvas) : nullptr;
	}
	if (!c)
		return;
	obs_render_canvas_texture(c);
	obs_canvas_release(c);
}

/* ------------------------------------------------------------------ */
/* Output blending                                                      */
/* ------------------------------------------------------------------ */

bool Destination::IsBlended() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return !isMain && blend.Active();
}

BlendConfig Destination::Blend() const
{
	std::lock_guard<std::mutex> lock(mutex);
	return blend;
}

void Destination::SetBlend(const BlendConfig &config)
{
	std::lock_guard<std::mutex> lock(mutex);
	blend = config;
}

void Destination::SetBlendPreview(const BlendConfig *config)
{
	std::lock_guard<std::mutex> lock(mutex);
	previewingBlend = config != nullptr;
	if (config)
		blendPreview = *config;
}

bool Destination::GetRenderOutput(size_t index, BlendOutput &out, uint32_t &layoutW, uint32_t &layoutH,
				  bool &testPattern) const
{
	std::lock_guard<std::mutex> lock(mutex);
	const BlendConfig &config = previewingBlend ? blendPreview : blend;
	if (index >= config.outputs.size() || !config.outputs[index].placed || !config.HasLayout())
		return false;
	out = config.outputs[index];
	layoutW = config.width;
	layoutH = config.height;
	testPattern = config.testPattern;
	return true;
}

/* ------------------------------------------------------------------ */
/* Persistance                                                          */
/* ------------------------------------------------------------------ */

void Destination::Save(obs_data_t *data) const
{
	obs_data_set_string(data, "id", Id().c_str());
	obs_data_set_bool(data, "main", isMain);
	obs_data_set_bool(data, "locked", locked);
	obs_data_set_string(data, "output_screen", outputScreen.c_str());
	obs_data_set_bool(data, "disabled", disabled);
	if (isMain)
		return;

	obs_data_set_string(data, "name", name.c_str());
	{
		OBSDataAutoRelease blendData = obs_data_create();
		Blend().Save(blendData);
		obs_data_set_obj(data, "blend", blendData);
	}
	obs_data_set_int(data, "width", width);
	obs_data_set_int(data, "height", height);

	OBSSource program;
	{
		std::lock_guard<std::mutex> lock(mutex);
		program = OBSGetStrongRef(programScene);
	}
	OBSDataAutoRelease priv = program ? obs_source_get_private_settings(program) : nullptr;
	obs_data_set_string(data, "program_link", priv ? obs_data_get_string(priv, LINK_KEY) : "");
}

void Destination::Load(obs_data_t *data)
{
	locked = obs_data_get_bool(data, "locked");
	outputScreen = obs_data_get_string(data, "output_screen");
	disabled = isMain && obs_data_get_bool(data, "disabled");
	if (isMain)
		return;

	obs_data_set_default_int(data, "width", 1920);
	obs_data_set_default_int(data, "height", 1080);

	name = obs_data_get_string(data, "name");
	{
		OBSDataAutoRelease blendData = obs_data_get_obj(data, "blend");
		BlendConfig config;
		config.Load(blendData);
		SetBlend(config);
	}

	Attach(uint32_t(obs_data_get_int(data, "width")), uint32_t(obs_data_get_int(data, "height")),
	       obs_data_get_string(data, "id"));

	OBSSource program = FindLinked(obs_data_get_string(data, "program_link"));
	if (program && transition) {
		OBSSource copy = MakeLiveCopy(program);
		obs_transition_set(transition, copy ? copy.Get() : program.Get());
		std::lock_guard<std::mutex> lock(mutex);
		programScene = OBSGetWeakRef(program.Get());
		liveCopy = copy;
	}
	RemoveStaleCopies();
}
