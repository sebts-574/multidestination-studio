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

#pragma once

#include "destination.hpp"

#include <QObject>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

/* Propriétaire des destinations et des opérations sur les scènes communes.
 * Vit sur le thread de l'interface. */
class Manager : public QObject {
	Q_OBJECT

public:
	static Manager &Get();

	/* Destination par défaut en premier. */
	std::vector<Destination *> Destinations() const;
	Destination *Main() const { return destinations.empty() ? nullptr : destinations.front().get(); }
	/* Destinations utilisables (sans la destination par défaut si elle est
	 * désactivée). */
	std::vector<Destination *> ActiveDestinations() const;
	/* Nouvelle destination ; addToExisting : l'ajouter aussi à toutes les
	 * scènes déjà créées. */
	Destination *Add(const std::string &name, uint32_t width, uint32_t height, bool addToExisting);
	void SetMainDisabled(bool disabled);
	void Remove(Destination *dest);
	void NotifyDestinationsChanged() { emit destinationsChanged(); }

	/* --- Scènes communes (= scènes OBS) --- */
	static std::vector<OBSSource> MainScenes();
	OBSSource CreateScene(const std::string &name, const std::set<Destination *> &coverage);
	void SetCoverage(obs_source_t *mainScene, const std::set<Destination *> &coverage);
	void RenameScene(obs_source_t *mainScene, const std::string &name);
	void RemoveScene(obs_source_t *mainScene);
	void Sync();
	/* Dossiers de la liste des scènes (organisation seulement) : tableau
	 * d'entrées {"scene": uuid} ou {"folder": nom, "expanded": bool,
	 * "scenes": [uuid…]}, dans l'ordre d'affichage. Enregistré avec la
	 * collection. */
	obs_data_array_t *SceneTree() const { return sceneTree; }
	void SetSceneTree(obs_data_array_t *tree) { sceneTree = tree; }

	/* --- Preview / Program globaux --- */
	void SetPreviewScene(obs_source_t *mainScene);
	OBSSource PreviewScene() const { return OBSGetStrongRef(previewScene); }
	OBSSource ProgramScene() const { return OBSGetStrongRef(programScene); }
	/* Transition : chaque destination couverte et non verrouillée passe à
	 * sa scène liée avec la transition d'OBS. Les destinations que la
	 * scène ne couvre pas gardent leur Program. Le Preview reçoit ensuite
	 * l'ancien Program, comme dans OBS, sauf
	 * si le réglage SwapAfterTake est désactivé. */
	/* transition / durationMs : transition d'OBS et durée à utiliser à la
	 * place des réglages courants (transitions rapides, cues). */
	void Take(bool cut, obs_source_t *transition = nullptr, int durationMs = -1);
	/* Passe une scène donnée à l'antenne sans toucher à l'Aperçu.
	 * skipMain : la destination par défaut y est déjà (changement fait dans
	 * OBS, par exemple depuis Companion). */
	void TakeScene(obs_source_t *mainScene, bool cut, obs_source_t *transition = nullptr, int durationMs = -1,
		       bool skipMain = false);
	/* Appelé avant Take : renvoie true s'il a pris la main (cue armée). */
	std::function<bool(bool cut)> takeInterceptor;
	void OnObsProgramChanged();

	/* --- Transitions rapides (barre entre Aperçu et Programme) --- */
	struct QuickTransition {
		std::string transition; /* uuid de la transition d'OBS (change à chaque lancement) */
		std::string name;       /* son nom : c'est lui qui sert après un redémarrage */
		int durationMs = 300;
	};
	/* Transition d'OBS d'une transition rapide, ou nullptr si elle n'existe plus. */
	static OBSSource ResolveQuickTransition(const QuickTransition &q);

	/* --- Barre T (transition manuelle), position 0…TBarMax --- */
	static constexpr int TBarMax = 1023;
	void TBarMove(int position);
	/* Renvoie true si la barre doit revenir à 0 (transition finie ou annulée). */
	bool TBarRelease(int position);
	bool TBarActive() const { return tbarActive; }
	const std::vector<QuickTransition> &QuickTransitions() const { return quickTransitions; }
	void SetQuickTransitions(const std::vector<QuickTransition> &list);
	/* Sans reconstruire les boutons (durée changée depuis leur menu). */
	void SetQuickTransitionsQuietly(const std::vector<QuickTransition> &list) { quickTransitions = list; }
	static OBSSource FindTransition(const std::string &uuidOrName);

	/* --- Cycle de vie de la collection de scènes --- */
	void Save(obs_data_t *data) const;
	void SetPendingLoad(obs_data_t *data)
	{
		pendingLoad = data;
		snapshot = data;
	}
	void ApplyLoad();
	void Reset();

	/* Création de scènes par le plugin lui-même (scènes liées, copies à
	 * l'antenne) : à ne pas traiter comme une nouvelle scène d'OBS. */
	bool CreatingScene() const { return creatingScene || taking; }

	/* --- Réglages (configuration utilisateur d'OBS, communs à toutes les
	 * collections) --- */
	/* Après une transition : true = l'ancien Program revient en Aperçu
	 * (comme OBS), false = la scène passée à l'antenne reste en Aperçu. */
	bool SwapAfterTake() const { return swapAfterTake; }
	void SetSwapAfterTake(bool value);
	/* true = toutes les destinations restent affichées ; false = seulement
	 * celles de la scène sélectionnée. */
	bool ShowAllDestinations() const { return showAllDestinations; }
	void SetShowAllDestinations(bool value);
	/* Langue du plugin ("" = celle d'OBS). Prise en compte au prochain
	 * démarrage d'OBS. */
	const std::string &Language() const { return language; }
	/* Ouvrir Multidestination Studio au démarrage d'OBS (désactivé par
	 * défaut). */
	bool LaunchOnStartup() const { return launchOnStartup; }
	void SetLaunchOnStartup(bool value);
	/* Ouvrir toutes les destinations sur leur sortie au démarrage d'OBS
	 * (désactivé par défaut). */
	bool OpenOutputsOnStartup() const { return openOutputsOnStartup; }
	void SetOpenOutputsOnStartup(bool value);
	void SetLanguage(const std::string &value);
	/* Fonction Cues (désactivée par défaut). */
	bool CuesEnabled() const { return cuesEnabled; }
	void SetCuesEnabled(bool value);
	bool Ready() const { return ready; }

signals:
	void aboutToReset();
	void loaded();
	void destinationsChanged();
	void aboutToRemove(Destination *dest);
	void scenesChanged();
	void previewChanged();
	void programChanged();
	void settingsChanged();
	void quickTransitionsChanged();

private:
	Manager();

	std::vector<std::unique_ptr<Destination>> destinations;
	OBSWeakSource previewScene;
	OBSWeakSource programScene;
	OBSData pendingLoad;
	OBSDataArray sceneTree;
	/* Dernières données de la collection en cours (voir Save). */
	OBSData snapshot;
	bool ready = false;
	bool taking = false;
	bool creatingScene = false;
	bool swapAfterTake = true;
	bool showAllDestinations = true;
	std::string language;
	bool launchOnStartup = false;
	bool openOutputsOnStartup = false;
	bool cuesEnabled = false;
	std::vector<QuickTransition> quickTransitions;
	bool tbarActive = false;
	bool tbarMain = false;
	OBSWeakSource tbarScene;
	OBSWeakSource tbarOldProgram;

	void LoadSettings();
	void AdoptOrphanCanvases();
	void SaveSettings() const;
};
