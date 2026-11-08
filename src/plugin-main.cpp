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

#include "blend-render.hpp"
#include "cues.hpp"
#include "manager.hpp"
#include "remote-control.hpp"
#include "show.hpp"
#include "frame-source.hpp"
#include "studio-window.hpp"
#include "tile-grid.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QAction>
#include <QBoxLayout>
#include <QMainWindow>
#include <QMetaObject>
#include <QPointer>
#include <QPushButton>
#include <QTimer>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

static const char *SAVE_KEY = "destinations_v2";

static QPointer<StudioWindow> studio;
static QPointer<QPushButton> controlsButton;
static obs_hotkey_id studioHotkey = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id takeHotkey = OBS_INVALID_HOTKEY_ID;
static obs_hotkey_id cutHotkey = OBS_INVALID_HOTKEY_ID;

static void ToggleStudio()
{
	if (studio)
		studio->Toggle();
}

/* ------------------------------------------------------------------ */
/* Bouton dans le dock Commandes d'OBS                                  */
/* ------------------------------------------------------------------ */

/* Le dock Commandes n'est pas accessible par l'API des plugins : on retrouve
 * le bouton « Mode Studio » par son nom interne et on insère le nôtre juste
 * après, avec le même style. Si OBS change ces noms, le menu Outils et le
 * raccourci restent disponibles. */
static void AddControlsButton()
{
	if (controlsButton)
		return;
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return;

	auto *modeSwitch = mainWindow->findChild<QPushButton *>("modeSwitch");
	QWidget *container = modeSwitch ? modeSwitch->parentWidget() : nullptr;
	auto *layout = container ? qobject_cast<QBoxLayout *>(container->layout()) : nullptr;
	if (!layout) {
		obs_log(LOG_WARNING, "dock Commandes introuvable : utiliser le menu Outils");
		return;
	}

	auto *button = new QPushButton("Multidestination Studio", container);
	button->setObjectName("multidestinationStudioButton");
	button->setSizePolicy(modeSwitch->sizePolicy());
	button->setMinimumSize(modeSwitch->minimumSize());
	button->setToolTip(Tr("MDS.OpenStudioTip"));
	QObject::connect(button, &QPushButton::clicked, []() { ToggleStudio(); });
	layout->insertWidget(layout->indexOf(modeSwitch) + 1, button);
	controlsButton = button;
}

/* ------------------------------------------------------------------ */
/* Sauvegarde dans la collection de scènes                             */
/* ------------------------------------------------------------------ */

static void SaveHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	OBSDataArrayAutoRelease array = obs_hotkey_save(id);
	obs_data_set_array(data, key, array);
}

static void LoadHotkey(obs_data_t *data, const char *key, obs_hotkey_id id)
{
	if (id == OBS_INVALID_HOTKEY_ID)
		return;
	OBSDataArrayAutoRelease array = obs_data_get_array(data, key);
	obs_hotkey_load(id, array);
}

static void SaveLoad(obs_data_t *save_data, bool saving, void *)
{
	if (saving) {
		OBSDataAutoRelease obj = obs_data_create();
		Manager::Get().Save(obj);
		SaveHotkey(obj, "hotkey_studio", studioHotkey);
		SaveHotkey(obj, "hotkey_take", takeHotkey);
		SaveHotkey(obj, "hotkey_cut", cutHotkey);
		if (studio)
			obs_data_set_string(obj, "studio_layout", studio->SaveLayout().toBase64().constData());
		obs_data_set_obj(save_data, SAVE_KEY, obj);
		return;
	}

	/* Chargement : appliqué quand OBS a fini de charger les canvases. */
	OBSDataAutoRelease obj = obs_data_get_obj(save_data, SAVE_KEY);
	Manager::Get().SetPendingLoad(obj);
	if (obj) {
		LoadHotkey(obj, "hotkey_studio", studioHotkey);
		LoadHotkey(obj, "hotkey_take", takeHotkey);
		LoadHotkey(obj, "hotkey_cut", cutHotkey);
		if (studio)
			studio->RestoreLayout(QByteArray::fromBase64(obs_data_get_string(obj, "studio_layout")));
	}
}

/* ------------------------------------------------------------------ */
/* Événements                                                           */
/* ------------------------------------------------------------------ */

static void FrontendEvent(enum obs_frontend_event event, void *)
{
	Manager &m = Manager::Get();
	Show::OnFrontendEvent(event);
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
		if (studio)
			studio->OnShowChanged();
		break;
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (studio)
			studio->OnShowChanged();
		AddControlsButton();
		m.ApplyLoad();
		/* Réglage « Launch Multidestination Studio on startup ». */
		if (m.LaunchOnStartup() && studio)
			QTimer::singleShot(500, studio.data(), []() {
				if (studio && !studio->isVisible())
					studio->Open();
			});
		/* Réglage « ouvrir les destinations sur leur sortie ». */
		if (m.OpenOutputsOnStartup() && studio)
			QTimer::singleShot(800, studio.data(), []() {
				if (studio)
					studio->OpenAllOutputs();
			});
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		if (studio)
			studio->OnShowChanged();
		m.ApplyLoad();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		m.Reset();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		m.Sync();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		m.OnObsProgramChanged();
		break;
	case OBS_FRONTEND_EVENT_TRANSITION_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_TRANSITION_DURATION_CHANGED:
		if (studio)
			studio->RefreshTransitions();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		if (studio)
			studio->Shutdown();
		TileWidget::ReleaseShared();
		m.Reset();
		/* Fenêtre indépendante (sans parent) : détruite ici, avant l'arrêt
		 * d'OBS, puisque la fermeture de la fenêtre d'OBS ne l'emporte plus. */
		if (event == OBS_FRONTEND_EVENT_EXIT && studio) {
			studio->hide();
			delete studio.data();
		}
		break;
	default:
		break;
	}
}

/* Une scène créée dans OBS (bouton +, duplication, obs-websocket…) : on
 * demande ses destinations, hors du traitement d'OBS en cours. */
static void SourceCreated(void *, calldata_t *cd)
{
	obs_source_t *source = static_cast<obs_source_t *>(calldata_ptr(cd, "source"));
	if (!source || !obs_source_is_scene(source))
		return;

	Manager &m = Manager::Get();
	if (!m.Ready() || m.CreatingScene())
		return;

	/* Seulement les scènes du canvas principal (pas nos scènes liées).
	 * Selon le moment du signal, le canvas peut ne pas être encore
	 * renseigné : une scène sans canvas est une scène principale. */
	obs_canvas_t *canvas = obs_source_get_canvas(source);
	obs_canvas_t *main = obs_get_main_canvas();
	const bool isMain = !canvas || canvas == main;
	obs_canvas_release(canvas);
	obs_canvas_release(main);
	if (!isMain)
		return;

	OBSWeakSource weak = OBSGetWeakRef(source);
	QMetaObject::invokeMethod(
		studio.data(),
		[weak]() {
			OBSSource scene = OBSGetStrongRef(weak);
			if (scene && studio)
				studio->AskCoverage(scene);
		},
		Qt::QueuedConnection);
}

/* ------------------------------------------------------------------ */
/* Raccourcis                                                           */
/* ------------------------------------------------------------------ */

static void RunOnUi(void (*fn)())
{
	if (!studio)
		return;
	QMetaObject::invokeMethod(studio.data(), [fn]() { fn(); }, Qt::QueuedConnection);
}

static void StudioHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() { ToggleStudio(); });
}

static void TakeHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() { Manager::Get().Take(false); });
}

static void CutHotkey(void *, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	if (pressed)
		RunOnUi([]() { Manager::Get().Take(true); });
}

/* ------------------------------------------------------------------ */

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "plugin chargé (version %s)", PLUGIN_VERSION);
	BlendRenderer::Init();
	FrameSource::Register();
	return true;
}

void obs_module_post_load(void)
{
	/* Réglages (dont la langue du plugin) avant tout texte de l'interface. */
	Manager::Get();
	CueEngine::Get();
	RemoteControl::Register();

	/* Sans parent, comme un projecteur d'OBS : la fenêtre a sa propre entrée
	 * dans la barre des tâches (le Dock et Mission Control sur macOS), donc
	 * on la retrouve après l'avoir réduite. */
	studio = new StudioWindow(nullptr);

	/* Accès de secours par le menu Outils. */
	auto *toolsAction = static_cast<QAction *>(obs_frontend_add_tools_menu_qaction("Multidestination Studio"));
	QObject::connect(toolsAction, &QAction::triggered, []() { ToggleStudio(); });

	const QByteArray studioName = Tr("MDS.HotkeyStudio").toUtf8();
	const QByteArray takeName = Tr("MDS.HotkeyTransition").toUtf8();
	const QByteArray cutName = Tr("MDS.HotkeyCut").toUtf8();
	studioHotkey = obs_hotkey_register_frontend("multidestination_studio.studio", studioName.constData(),
						    StudioHotkey, nullptr);
	takeHotkey =
		obs_hotkey_register_frontend("multidestination_studio.take", takeName.constData(), TakeHotkey, nullptr);
	cutHotkey =
		obs_hotkey_register_frontend("multidestination_studio.cut", cutName.constData(), CutHotkey, nullptr);

	signal_handler_connect(obs_get_signal_handler(), "source_create", SourceCreated, nullptr);
	obs_frontend_add_event_callback(FrontendEvent, nullptr);
	obs_frontend_add_save_callback(SaveLoad, nullptr);
}

void obs_module_unload(void)
{
	obs_frontend_remove_save_callback(SaveLoad, nullptr);
	obs_frontend_remove_event_callback(FrontendEvent, nullptr);
	signal_handler_disconnect(obs_get_signal_handler(), "source_create", SourceCreated, nullptr);

	for (obs_hotkey_id id : {studioHotkey, takeHotkey, cutHotkey}) {
		if (id != OBS_INVALID_HOTKEY_ID)
			obs_hotkey_unregister(id);
	}

	/* La fenêtre et le bouton appartiennent à la fenêtre principale. */
	studio = nullptr;
	BlendRenderer::Shutdown();
	obs_log(LOG_INFO, "plugin déchargé");
}
