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

#include "blend.hpp"

#include <obs.hpp>

#include <mutex>
#include <string>
#include <utility>
#include <vector>

/*
 * Modèle
 * ------
 * - Les scènes OBS (canvas principal) sont la référence : une « scène » du
 *   plugin = une scène OBS.
 * - Chaque destination supplémentaire a son propre canvas. Pour chaque scène
 *   OBS qu'elle couvre, elle possède une scène « liée » dans son canvas, avec
 *   ses propres sources et placements. Le lien est stocké dans les réglages
 *   privés de la scène liée (clé LINK_KEY = UUID de la scène OBS).
 * - La destination par défaut est le canvas principal d'OBS lui-même. Une
 *   scène OBS la couvre sauf si elle porte le drapeau EXCLUDE_MAIN_KEY.
 */
class Destination {
public:
	static constexpr const char *MAIN_ID = "main";
	static constexpr const char *LINK_KEY = "destinations_link";
	static constexpr const char *EXCLUDE_MAIN_KEY = "destinations_exclude_main";
	static constexpr const char *LIVE_KEY = "destinations_live_copy";

	/* Destination par défaut = canvas principal d'OBS. */
	static Destination *CreateMain();
	/* Destination supplémentaire avec son propre canvas. */
	explicit Destination(std::string name);
	~Destination();

	Destination(const Destination &) = delete;
	Destination &operator=(const Destination &) = delete;

	bool IsMain() const { return isMain; }
	std::string Id() const;
	const std::string &Name() const { return name; }
	void SetName(const std::string &newName);

	/* Trouve le canvas (par UUID puis par nom) ou le crée, puis démarre sa
	 * vidéo. Sans effet sur la destination par défaut. */
	bool Attach(uint32_t width, uint32_t height, const std::string &uuidHint = "");
	void Detach();
	/* Supprime définitivement le canvas et ses scènes. */
	void DestroyCanvas();
	bool Attached() const;

	uint32_t Width() const;
	uint32_t Height() const;

	/* Destination par défaut « désactivée et masquée » : elle n'apparaît
	 * plus nulle part, mais ses scènes sont mémorisées. */
	bool Disabled() const { return disabled; }
	void SetDisabled(bool value) { disabled = value; }

	/* Verrouillée : ignorée par le TAKE global (reste sur son Program). */
	bool Locked() const { return locked; }
	void SetLocked(bool value) { locked = value; }

	/* --- Scènes liées --- */
	bool Covers(obs_source_t *mainScene) const;
	/* Scène à rendre / à prendre pour cette destination (la scène OBS
	 * elle-même pour la destination par défaut). Vide si non couverte. */
	OBSSource LinkedScene(obs_source_t *mainScene) const;
	bool AddCoverage(obs_source_t *mainScene);
	void RemoveCoverage(obs_source_t *mainScene);
	/* Renomme les scènes liées et supprime celles dont la scène OBS a
	 * disparu. */
	void SyncWithMainScenes(const std::vector<OBSSource> &mainScenes);

	/* --- Program --- */
	/* Passe la scène liée à l'antenne. Les destinations supplémentaires
	 * utilisent une copie de la transition d'OBS (templ) ; la destination
	 * par défaut utilise directement celle d'OBS. */
	void Take(obs_source_t *linkedScene, bool cut, obs_source_t *templ, int durationMs);
	/* Groupes de la scène à l'antenne détachés de ceux de l'Aperçu
	 * (destination par défaut, copie d'OBS en mode Studio). */
	void IsolateLiveGroups();
	/* Fondu vers le noir (destinations supplémentaires). */
	void FadeToBlack(obs_source_t *templ, int durationMs);
	/* Transition manuelle (barre T) : début, position 0…1, fin (finie ou
	 * annulée). Destinations supplémentaires seulement. */
	void BeginManual(obs_source_t *linkedScene, obs_source_t *templ);
	void SetManual(float t);
	void EndManual(bool finished);
	std::string ProgramSceneName() const;
	/* Ce qui est réellement à l'antenne : une copie privée de la scène,
	 * faite au moment de la transition. Vide si rien n'est à l'antenne. */
	OBSSource LiveScene() const;
	/* Scène d'origine de ce qui est à l'antenne. */
	OBSSource ProgramOriginal() const;
	/* « Modifier le PGM » : recopie la copie à l'antenne dans la scène. */
	void KeepLiveEdits();

	/* --- Écran de sortie physique (nom QScreen, vide = aucun) --- */
	const std::string &OutputScreen() const { return outputScreen; }
	void SetOutputScreen(const std::string &screen) { outputScreen = screen; }

	/* --- Output blending (destinations supplémentaires uniquement) --- */
	bool IsBlended() const;
	BlendConfig Blend() const;
	void SetBlend(const BlendConfig &config);
	/* Réglages en cours d'édition, affichés sur les sorties ouvertes tant
	 * que l'éditeur est ouvert. nullptr : revient aux réglages enregistrés. */
	void SetBlendPreview(const BlendConfig *config);
	/* Thread graphique : réglages d'une sortie à afficher. */
	bool GetRenderOutput(size_t index, BlendOutput &out, uint32_t &layoutW, uint32_t &layoutH,
			     bool &testPattern) const;

	/* --- Rendu (thread graphique) --- */
	void RenderProgram();

	/* --- Persistance --- */
	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);

private:
	Destination() = default;

	OBSSource FindLinked(const std::string &mainUuid) const;
	void CreateTransitionFrom(obs_source_t *templ);
	OBSSource MakeLiveCopy(obs_source_t *linkedScene);
	void PurgeRetiredCopies();
	void RemoveStaleCopies();
	void StartVideo();

	bool isMain = false;
	std::string name;
	bool locked = false;
	bool disabled = false;

	obs_canvas_t *canvas = nullptr;
	uint32_t width = 1920;
	uint32_t height = 1080;

	/* Protège uniquement ce que lit le thread graphique. Ne jamais appeler
	 * libobs en tenant ce verrou. */
	mutable std::mutex mutex;
	OBSWeakSource programScene;
	OBSSource liveCopy;
	std::vector<OBSSource> retired;
	OBSSource manualCopy;
	OBSWeakSource manualScene;
	OBSSource transition;
	std::string transitionSignature; /* id + réglages de la transition copiée */
	std::string outputScreen;
	BlendConfig blend;
	BlendConfig blendPreview;
	bool previewingBlend = false;
};
