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

#include "manager.hpp"
#include "cues.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>
#include <util/config-file.h>

#include <QTimer>

#include <cstring>

Manager &Manager::Get()
{
	static Manager instance;
	return instance;
}

Manager::Manager()
{
	destinations.emplace_back(Destination::CreateMain());
	LoadSettings();
}

std::vector<Destination *> Manager::Destinations() const
{
	std::vector<Destination *> list;
	for (auto &d : destinations)
		list.push_back(d.get());
	return list;
}

std::vector<Destination *> Manager::ActiveDestinations() const
{
	std::vector<Destination *> list;
	for (auto &d : destinations) {
		if (!d->Disabled())
			list.push_back(d.get());
	}
	return list;
}

void Manager::SetMainDisabled(bool disabled)
{
	if (!Main() || Main()->Disabled() == disabled)
		return;
	Main()->SetDisabled(disabled);
	emit destinationsChanged();
	emit scenesChanged();
}

/* Nom de canvas libre : « Nom », sinon « Nom (2) », « Nom (3) »… */
static std::string UniqueCanvasName(const std::string &name)
{
	std::string candidate = name;
	for (int n = 2;; n++) {
		obs_canvas_t *existing = obs_get_canvas_by_name(candidate.c_str());
		if (!existing)
			return candidate;
		obs_canvas_release(existing);
		candidate = name + " (" + std::to_string(n) + ")";
	}
}

/* Canvas d'une destination dont les données ont été perdues (versions
 * antérieures à la 1.0.1) : OBS a gardé le canvas et ses scènes liées. On
 * les reprend comme destinations pour ne rien perdre. */
void Manager::AdoptOrphanCanvases()
{
	std::set<std::string> used;
	for (auto &d : destinations)
		used.insert(d->Id());

	struct obs_frontend_canvas_list list = {};
	obs_frontend_get_canvases(&list);
	std::vector<std::pair<std::string, std::string>> orphans; /* uuid, nom */
	for (size_t i = 0; i < list.canvases.num; i++) {
		obs_canvas_t *c = list.canvases.array[i];
		const char *uuid = obs_canvas_get_uuid(c);
		if (obs_canvas_removed(c) || !uuid || used.count(uuid))
			continue;
		bool ours = false;
		obs_canvas_enum_scenes(
			c,
			[](void *param, obs_source_t *scene) {
				OBSDataAutoRelease priv = obs_source_get_private_settings(scene);
				const char *link = obs_data_get_string(priv, Destination::LINK_KEY);
				if ((link && *link) || obs_data_get_bool(priv, Destination::LIVE_KEY)) {
					*static_cast<bool *>(param) = true;
					return false;
				}
				return true;
			},
			&ours);
		if (ours) {
			const char *name = obs_canvas_get_name(c);
			orphans.emplace_back(uuid, name ? name : "");
		}
	}
	obs_frontend_canvas_list_free(&list);

	for (auto &[uuid, name] : orphans) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "id", uuid.c_str());
		obs_data_set_string(item, "name", name.c_str());
		auto dest = std::make_unique<Destination>(name);
		dest->Load(item);
		if (dest->Attached()) {
			obs_log(LOG_INFO, "destination « %s » retrouvée", name.c_str());
			destinations.push_back(std::move(dest));
		}
	}
}

Destination *Manager::Add(const std::string &requestedName, uint32_t width, uint32_t height, bool addToExisting)
{
	const std::string name = UniqueCanvasName(requestedName);
	auto dest = std::make_unique<Destination>(name);
	if (!dest->Attach(width, height))
		return nullptr;

	Destination *raw = dest.get();
	destinations.push_back(std::move(dest));

	if (addToExisting) {
		creatingScene = true;
		for (auto &scene : MainScenes())
			raw->AddCoverage(scene);
		creatingScene = false;
	}

	obs_log(LOG_INFO, "destination « %s » créée (%u×%u), %s", name.c_str(), width, height,
		addToExisting ? "ajoutée aux scènes existantes" : "dans aucune scène existante");
	emit destinationsChanged();
	emit scenesChanged();
	return raw;
}

void Manager::Remove(Destination *dest)
{
	if (!dest || dest->IsMain())
		return;

	emit aboutToRemove(dest);
	for (auto it = destinations.begin(); it != destinations.end(); ++it) {
		if (it->get() == dest) {
			dest->DestroyCanvas();
			destinations.erase(it);
			break;
		}
	}
	emit destinationsChanged();
}

/* ------------------------------------------------------------------ */
/* Scènes communes                                                      */
/* ------------------------------------------------------------------ */

std::vector<OBSSource> Manager::MainScenes()
{
	std::vector<OBSSource> scenes;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_scenes(&list);
	for (size_t i = 0; i < list.sources.num; i++)
		scenes.emplace_back(list.sources.array[i]);
	obs_frontend_source_list_free(&list);
	return scenes;
}

OBSSource Manager::CreateScene(const std::string &name, const std::set<Destination *> &coverage)
{
	if (name.empty())
		return nullptr;

	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing)
		return nullptr;

	creatingScene = true;
	obs_scene_t *scene = obs_scene_create(name.c_str());
	creatingScene = false;
	if (!scene)
		return nullptr;
	OBSSource source = obs_scene_get_source(scene);
	/* L'interface d'OBS prend sa propre référence en ajoutant la scène à
	 * sa liste (comme obs-websocket). */
	obs_scene_release(scene);

	SetCoverage(source, coverage);
	return source;
}

/* Recopie les éléments d'une scène dans une autre (vide) : mêmes sources (les
 * groupes sont partagés, comme dans OBS), même placement, visibilité,
 * rognage, mélange et filtrage. */
static void CopySceneItems(obs_scene_t *from, obs_scene_t *to)
{
	std::vector<obs_sceneitem_t *> items;
	obs_scene_enum_items(
		from,
		[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
			static_cast<std::vector<obs_sceneitem_t *> *>(param)->push_back(item);
			return true;
		},
		&items);
	for (obs_sceneitem_t *item : items) {
		obs_sceneitem_t *copy = obs_scene_add(to, obs_sceneitem_get_source(item));
		if (!copy)
			continue;
		struct obs_transform_info info = {};
		obs_sceneitem_get_info2(item, &info);
		obs_sceneitem_set_info2(copy, &info);
		struct obs_sceneitem_crop crop = {};
		obs_sceneitem_get_crop(item, &crop);
		obs_sceneitem_set_crop(copy, &crop);
		obs_sceneitem_set_visible(copy, obs_sceneitem_visible(item));
		obs_sceneitem_set_locked(copy, obs_sceneitem_locked(item));
		obs_sceneitem_set_blending_method(copy, obs_sceneitem_get_blending_method(item));
		obs_sceneitem_set_blending_mode(copy, obs_sceneitem_get_blending_mode(item));
		obs_sceneitem_set_scale_filter(copy, obs_sceneitem_get_scale_filter(item));
	}
}

OBSSource Manager::DuplicateScene(obs_source_t *original, const std::string &name,
				  const std::set<Destination *> &coverage)
{
	obs_scene_t *from = obs_scene_from_source(original);
	if (!from || name.empty())
		return nullptr;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing)
		return nullptr;

	creatingScene = true;
	obs_scene_t *scene = obs_scene_duplicate(from, name.c_str(), OBS_SCENE_DUP_REFS);
	creatingScene = false;
	if (!scene)
		return nullptr;
	OBSSource source = obs_scene_get_source(scene);
	obs_scene_release(scene);

	SetCoverage(source, coverage);
	creatingScene = true;
	for (auto &d : destinations) {
		if (d->IsMain() || d->Disabled() || !coverage.count(d.get()))
			continue;
		OBSSource src = d->LinkedScene(original);
		OBSSource dst = d->LinkedScene(source);
		if (src && dst && src.Get() != dst.Get())
			CopySceneItems(obs_scene_from_source(src), obs_scene_from_source(dst));
	}
	creatingScene = false;
	emit scenesChanged();
	return source;
}

void Manager::SetCoverage(obs_source_t *mainScene, const std::set<Destination *> &coverage)
{
	if (!mainScene)
		return;
	creatingScene = true;
	for (auto &d : destinations) {
		/* Destination désactivée : ses scènes restent mémorisées. */
		if (d->Disabled())
			continue;
		const bool wanted = coverage.count(d.get()) > 0;
		if (wanted && !d->Covers(mainScene))
			d->AddCoverage(mainScene);
		else if (!wanted && d->Covers(mainScene))
			d->RemoveCoverage(mainScene);
	}
	creatingScene = false;
	emit scenesChanged();
	emit previewChanged();
}

void Manager::RenameScene(obs_source_t *mainScene, const std::string &name)
{
	if (!mainScene || name.empty())
		return;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
	if (existing)
		return;
	obs_source_set_name(mainScene, name.c_str());
	Sync();
}

void Manager::RemoveScene(obs_source_t *mainScene)
{
	if (!mainScene)
		return;
	OBSSource keep = mainScene;
	for (auto &d : destinations) {
		if (!d->IsMain())
			d->RemoveCoverage(keep);
	}
	if (obs_weak_source_references_source(previewScene, keep)) {
		previewScene = nullptr;
		emit previewChanged();
	}
	obs_source_remove(keep);
	emit scenesChanged();
}

void Manager::Sync()
{
	if (!ready)
		return;

	auto scenes = MainScenes();
	for (auto &d : destinations)
		d->SyncWithMainScenes(scenes);

	OBSSource preview = PreviewScene();
	bool previewAlive = false;
	for (auto &s : scenes) {
		if (s.Get() == preview.Get())
			previewAlive = true;
	}
	if (preview && !previewAlive) {
		previewScene = nullptr;
		emit previewChanged();
	}
	if (!preview && !scenes.empty()) {
		previewScene = OBSGetWeakRef(scenes.front().Get());
		emit previewChanged();
	}
	emit scenesChanged();
}

/* ------------------------------------------------------------------ */
/* Preview / Program                                                    */
/* ------------------------------------------------------------------ */

void Manager::SetPreviewScene(obs_source_t *mainScene)
{
	if (obs_weak_source_references_source(previewScene, mainScene))
		return;
	previewScene = mainScene ? OBSGetWeakRef(mainScene) : OBSWeakSource();
	emit previewChanged();
}

void Manager::TakeScene(obs_source_t *mainScene, bool cut, obs_source_t *transition, int durationMs, bool skipMain)
{
	if (!mainScene)
		return;
	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	obs_source_t *templ = transition ? transition : current.Get();
	const int duration = durationMs >= 0 ? durationMs : obs_frontend_get_transition_duration();

	taking = true;
	for (auto &d : destinations) {
		if (d->Locked() || (skipMain && d->IsMain()))
			continue;
		OBSSource linked = d->LinkedScene(mainScene);
		if (linked)
			d->Take(linked, cut, templ, duration);
	}
	taking = false;

	programScene = OBSGetWeakRef(mainScene);
	emit programChanged();
}

void Manager::Take(bool cut, obs_source_t *transition, int durationMs)
{
	OBSSource preview = PreviewScene();
	if (!preview)
		return;
	if (takeInterceptor && takeInterceptor(cut, transition, durationMs))
		return;
	emit userTake();

	OBSSource oldProgram = ProgramScene();
	TakeScene(preview, cut, transition, durationMs);

	/* Comme OBS : l'ancien Program revient en Preview (réglable). */
	if (swapAfterTake && oldProgram && oldProgram.Get() != preview.Get())
		SetPreviewScene(oldProgram);
}

void Manager::OnObsProgramChanged()
{
	if (!ready)
		return;

	/* Un changement de scène fait dans OBS n'agit que sur la destination
	 * par défaut ; on suit seulement le Program pour l'affichage. */
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	if (current && blackScene && current.Get() == blackScene.Get()) {
		emit programChanged();
		return;
	}
	if (current && !taking && !obs_weak_source_references_source(programScene, current)) {
		programScene = OBSGetWeakRef(current.Get());
		emit userTake();
	}
	/* Prise faite dans OBS : la copie à l'antenne garde ses propres
	 * groupes (après la mise en place de la transition). */
	if (Destination *main = Main()) {
		main->IsolateLiveGroups();
		QTimer::singleShot(0, this, [this]() {
			if (Destination *m = Main())
				m->IsolateLiveGroups();
		});
	}
	emit programChanged();
}

static OBSSource FrontendTransitionById(const char *id)
{
	OBSSource found;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num && !found; i++) {
		if (strcmp(obs_source_get_id(list.sources.array[i]), id) == 0)
			found = list.sources.array[i];
	}
	obs_frontend_source_list_free(&list);
	return found;
}

/* Fondu au noir : la destination par défaut passe à une scène vide (avec le
 * fondu d'OBS), les autres font un fondu vers rien. L'Aperçu ne change pas. */
void Manager::FadeToBlack(int durationMs)
{
	const int duration = durationMs >= 0 ? durationMs : obs_frontend_get_transition_duration();
	/* Sans « Fondu » dans la liste d'OBS, les destinations supplémentaires
	 * créent quand même un fondu (transition par défaut). */
	OBSSource fade = FrontendTransitionById("fade_transition");
	if (!blackScene) {
		obs_scene_t *black = obs_scene_create_private("Multidestination Studio - black");
		blackScene = obs_scene_get_source(black);
		obs_scene_release(black);
	}

	taking = true;
	for (auto &d : destinations) {
		if (d->Locked() || d->Disabled())
			continue;
		if (d->IsMain()) {
			/* La destination par défaut passe par la transition d'OBS : il
			 * en faut une de sa liste. */
			d->Take(blackScene, duration <= 0 || !fade, fade, duration);
		} else {
			d->FadeToBlack(fade, duration);
		}
	}
	taking = false;
	programScene = OBSWeakSource();
	emit programChanged();
}

OBSSource Manager::ResolveQuickTransition(const QuickTransition &q)
{
	OBSSource tr = FindTransition(q.transition);
	if (!tr)
		tr = FindTransition(q.name);
	return tr;
}

/* Barre T : la destination par défaut passe par la barre T d'OBS (avec la
 * scène d'Aperçu du studio en aperçu d'OBS) ; les autres destinations font
 * une transition manuelle sur leur canvas. Comme OBS : Cut et Stinger sont
 * remplacés par un fondu. */
void Manager::TBarMove(int position)
{
	const float t = float(position) / float(TBarMax + 1);
	if (!tbarActive) {
		OBSSource preview = PreviewScene();
		if (!preview || position <= 0)
			return;
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		OBSSource templ = current.Get();
		const char *id = templ ? obs_source_get_id(templ) : "";
		if (!templ || strcmp(id, "cut_transition") == 0 || strcmp(id, "obs_stinger_transition") == 0) {
			struct obs_frontend_source_list list = {};
			obs_frontend_get_transitions(&list);
			for (size_t i = 0; i < list.sources.num; i++) {
				if (strcmp(obs_source_get_id(list.sources.array[i]), "fade_transition") == 0) {
					templ = list.sources.array[i];
					break;
				}
			}
			obs_frontend_source_list_free(&list);
		}
		OBSSource old = ProgramScene();
		tbarOldProgram = old ? OBSGetWeakRef(old.Get()) : OBSWeakSource();
		tbarScene = OBSGetWeakRef(preview.Get());
		tbarMain = false;
		taking = true;
		for (auto &d : destinations) {
			if (d->Locked())
				continue;
			OBSSource linked = d->LinkedScene(preview);
			if (!linked)
				continue;
			if (d->IsMain()) {
				if (d->Disabled() || !obs_frontend_preview_program_mode_active())
					continue;
				obs_frontend_set_current_preview_scene(linked);
				tbarMain = true;
			} else {
				d->BeginManual(linked, templ);
			}
		}
		taking = false;
		tbarActive = true;
		emit userTake();
	}
	taking = true;
	if (tbarMain)
		obs_frontend_set_tbar_position(position);
	for (auto &d : destinations) {
		if (!d->IsMain())
			d->SetManual(t);
	}
	taking = false;
}

bool Manager::TBarRelease(int position)
{
	if (!tbarActive)
		return true;
	const int clamp = (TBarMax + 1) / 10;
	const bool finished = TBarMax - position <= clamp;
	const bool cancelled = position <= clamp;
	if (!finished && !cancelled)
		return false; /* comme OBS : la transition reste en suspens */

	taking = true;
	if (tbarMain)
		obs_frontend_release_tbar();
	for (auto &d : destinations) {
		if (!d->IsMain())
			d->EndManual(finished);
	}
	taking = false;
	tbarActive = false;
	tbarMain = false;

	if (finished) {
		OBSSource scene = OBSGetStrongRef(tbarScene);
		OBSSource old = OBSGetStrongRef(tbarOldProgram);
		if (scene) {
			programScene = OBSGetWeakRef(scene.Get());
			emit programChanged();
			if (swapAfterTake && old && old.Get() != scene.Get())
				SetPreviewScene(old);
		}
	}
	return true;
}

void Manager::SetQuickTransitions(const std::vector<QuickTransition> &list)
{
	quickTransitions = list;
	emit quickTransitionsChanged();
}

OBSSource Manager::FindTransition(const std::string &uuidOrName)
{
	OBSSource found;
	if (uuidOrName.empty())
		return found;
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num && !found; i++) {
		obs_source_t *tr = list.sources.array[i];
		const char *uuid = obs_source_get_uuid(tr);
		if (uuid && uuidOrName == uuid)
			found = tr;
	}
	for (size_t i = 0; i < list.sources.num && !found; i++) {
		obs_source_t *tr = list.sources.array[i];
		const char *n = obs_source_get_name(tr);
		if (n && uuidOrName == n)
			found = tr;
	}
	obs_frontend_source_list_free(&list);
	return found;
}

/* ------------------------------------------------------------------ */
/* Cycle de vie                                                         */
/* ------------------------------------------------------------------ */

void Manager::Save(obs_data_t *data) const
{
	/* Pas (ou plus) chargé : OBS enregistre la collection pendant sa
	 * fermeture, après que les destinations ont été libérées. On réécrit
	 * alors les dernières données connues au lieu d'une liste vide. */
	if (!ready) {
		if (snapshot)
			obs_data_apply(data, snapshot);
		return;
	}

	OBSDataArrayAutoRelease list = obs_data_array_create();
	for (auto &d : destinations) {
		OBSDataAutoRelease item = obs_data_create();
		d->Save(item);
		obs_data_array_push_back(list, item);
	}
	obs_data_set_array(data, "list", list);

	OBSSource preview = PreviewScene();
	const char *uuid = preview ? obs_source_get_uuid(preview) : nullptr;
	obs_data_set_string(data, "preview_scene", uuid ? uuid : "");
	if (sceneTree)
		obs_data_set_array(data, "scene_tree", sceneTree);

	OBSDataArrayAutoRelease quick = obs_data_array_create();
	for (const auto &q : quickTransitions) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "transition", q.transition.c_str());
		obs_data_set_string(item, "transition_name", q.name.c_str());
		obs_data_set_int(item, "duration", q.durationMs);
		obs_data_array_push_back(quick, item);
	}
	obs_data_set_array(data, "quick_transitions", quick);
	CueEngine::Get().Save(data);
}

void Manager::ApplyLoad()
{
	/* OBS envoie « collection chargée » puis « chargement terminé » au
	 * démarrage : le second appel, sans nouvelles données, ne doit pas
	 * vider les destinations déjà chargées. */
	if (ready && !pendingLoad) {
		Sync();
		return;
	}

	/* Les destinations supplémentaires de la collection précédente ont
	 * déjà été détachées par Reset(). */
	while (destinations.size() > 1)
		destinations.pop_back();

	OBSData data = pendingLoad;
	pendingLoad = nullptr;

	ready = true;

	if (data) {
		/* Les copies « à l'antenne » recréées au chargement ne doivent pas
		 * ouvrir la fenêtre de couverture. */
		creatingScene = true;
		OBSDataArrayAutoRelease list = obs_data_get_array(data, "list");
		const size_t count = obs_data_array_count(list);
		for (size_t i = 0; i < count; i++) {
			OBSDataAutoRelease item = obs_data_array_item(list, i);
			if (obs_data_get_bool(item, "main")) {
				Main()->Load(item);
				continue;
			}
			auto dest = std::make_unique<Destination>(obs_data_get_string(item, "name"));
			dest->Load(item);
			if (dest->Attached())
				destinations.push_back(std::move(dest));
		}
		AdoptOrphanCanvases();
		creatingScene = false;

		OBSDataArrayAutoRelease tree = obs_data_get_array(data, "scene_tree");
		sceneTree = tree.Get();
		OBSSourceAutoRelease preview = obs_get_source_by_uuid(obs_data_get_string(data, "preview_scene"));
		previewScene = preview ? OBSGetWeakRef(preview.Get()) : OBSWeakSource();

		quickTransitions.clear();
		OBSDataArrayAutoRelease quick = obs_data_get_array(data, "quick_transitions");
		for (size_t i = 0; i < obs_data_array_count(quick); i++) {
			OBSDataAutoRelease item = obs_data_array_item(quick, i);
			QuickTransition q;
			q.transition = obs_data_get_string(item, "transition");
			q.name = obs_data_get_string(item, "transition_name");
			/* Anciennes entrées sans nom : leur uuid ne survit pas au
			 * redémarrage d'OBS, on les abandonne. */
			if (q.name.empty())
				continue;
			q.durationMs = int(obs_data_get_int(item, "duration"));
			quickTransitions.push_back(q);
		}
		CueEngine::Get().Load(data);
	}

	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	programScene = current ? OBSGetWeakRef(current.Get()) : OBSWeakSource();

	Sync();
	obs_log(LOG_INFO, "%zu destination(s) chargée(s)", destinations.size());
	emit loaded();
	emit quickTransitionsChanged();
	emit destinationsChanged();
	emit previewChanged();
	emit programChanged();
}

void Manager::Reset()
{
	emit aboutToReset();
	if (ready) {
		OBSDataAutoRelease last = obs_data_create();
		Save(last);
		snapshot = last.Get();
	}
	ready = false;
	while (destinations.size() > 1) {
		destinations.back()->Detach();
		destinations.pop_back();
	}
	previewScene = nullptr;
	programScene = nullptr;
	sceneTree = nullptr;
	quickTransitions.clear();
	blackScene = nullptr;
	CueEngine::Get().Reset();
}

/* ------------------------------------------------------------------ */
/* Réglages                                                             */
/* ------------------------------------------------------------------ */

static constexpr const char *SETTINGS_SECTION = "MultidestinationStudio";

void Manager::LoadSettings()
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;
	config_set_default_bool(config, SETTINGS_SECTION, "SwapAfterTake", true);
	config_set_default_bool(config, SETTINGS_SECTION, "ShowAllDestinations", true);
	config_set_default_bool(config, SETTINGS_SECTION, "LaunchOnStartup", false);
	launchOnStartup = config_get_bool(config, SETTINGS_SECTION, "LaunchOnStartup");
	config_set_default_bool(config, SETTINGS_SECTION, "OpenOutputsOnStartup", false);
	openOutputsOnStartup = config_get_bool(config, SETTINGS_SECTION, "OpenOutputsOnStartup");
	swapAfterTake = config_get_bool(config, SETTINGS_SECTION, "SwapAfterTake");
	const char *lang = config_get_string(config, SETTINGS_SECTION, "Language");
	language = lang ? lang : "";
	SetUiLanguage(language);
	obs_log(LOG_INFO, "langue d'OBS : %s, langue du plugin : %s", obs_get_locale(),
		language.empty() ? "celle d'OBS" : language.c_str());
	showAllDestinations = config_get_bool(config, SETTINGS_SECTION, "ShowAllDestinations");
	config_set_default_bool(config, SETTINGS_SECTION, "CuesEnabled", false);
	cuesEnabled = config_get_bool(config, SETTINGS_SECTION, "CuesEnabled");
}

void Manager::SaveSettings() const
{
	config_t *config = obs_frontend_get_user_config();
	if (!config)
		return;
	config_set_bool(config, SETTINGS_SECTION, "SwapAfterTake", swapAfterTake);
	config_set_bool(config, SETTINGS_SECTION, "ShowAllDestinations", showAllDestinations);
	config_set_string(config, SETTINGS_SECTION, "Language", language.c_str());
	config_set_bool(config, SETTINGS_SECTION, "LaunchOnStartup", launchOnStartup);
	config_set_bool(config, SETTINGS_SECTION, "OpenOutputsOnStartup", openOutputsOnStartup);
	config_set_bool(config, SETTINGS_SECTION, "CuesEnabled", cuesEnabled);
	config_save_safe(config, "tmp", nullptr);
}

void Manager::SetSwapAfterTake(bool value)
{
	if (swapAfterTake == value)
		return;
	swapAfterTake = value;
	SaveSettings();
	emit settingsChanged();
}

void Manager::SetShowAllDestinations(bool value)
{
	if (showAllDestinations == value)
		return;
	showAllDestinations = value;
	SaveSettings();
	emit settingsChanged();
}

void Manager::SetLanguage(const std::string &value)
{
	if (language == value)
		return;
	language = value;
	SaveSettings();
}

void Manager::SetLaunchOnStartup(bool value)
{
	if (launchOnStartup == value)
		return;
	launchOnStartup = value;
	SaveSettings();
}

void Manager::SetCuesEnabled(bool value)
{
	if (cuesEnabled == value)
		return;
	cuesEnabled = value;
	if (!value)
		CueEngine::Get().Stop();
	SaveSettings();
	emit settingsChanged();
}

void Manager::SetOpenOutputsOnStartup(bool value)
{
	if (openOutputsOnStartup == value)
		return;
	openOutputsOnStartup = value;
	SaveSettings();
}
