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

#include <QObject>
#include <QString>
#include <QTimer>
#include <atomic>
#include <functional>
#include <string>
#include <vector>

/* Une étape de cue : une scène passée à l'antenne après un pré-délai, avec
 * sa transition, puis un post-délai avant l'étape suivante. */
struct CueStep {
	std::string scene;      /* uuid de la scène commune */
	int preWaitMs = 0;      /* avant de passer la scène */
	int postWaitMs = 0;     /* après le début de la transition */
	std::string transition; /* "" = transition courante, "cut" = coupure, sinon uuid */
	int durationMs = 0;     /* 0 = durée courante d'OBS */
	/* Passer à la scène suivante quand le média de la scène est fini (à la
	 * place du post-délai). */
	bool nextOnMediaEnd = false;
};

struct Cue {
	std::string id;
	std::string name;
	std::vector<CueStep> steps;
};

/* Cues (fonction optionnelle) : enregistrées avec la collection de scènes.
 * Une cue « armée » montre sa première scène en Aperçu ; Transition ou Cut
 * la lancent, et ses scènes passent ensuite à l'antenne l'une après
 * l'autre. */
class CueEngine : public QObject {
	Q_OBJECT

public:
	static CueEngine &Get();

	const std::vector<Cue> &Cues() const { return cues; }
	void SetCues(const std::vector<Cue> &list);
	const Cue *Find(const std::string &id) const;
	const Cue *FindByName(const std::string &name) const;
	static std::string NewId();

	/* Arme une cue : sa première scène passe en Aperçu. */
	void Arm(const std::string &id);
	void Disarm();
	const std::string &ArmedId() const { return armedId; }

	/* Lance une cue (la cue armée si id est vide). La première scène part
	 * avec la transition de ce qui a lancé la cue : transition (nullptr =
	 * celle d'OBS) et durée (< 0 = celle d'OBS) ; ses propres réglages de
	 * transition ne servent pas. */
	bool Go(const std::string &id, bool cut, obs_source_t *transition = nullptr, int durationMs = -1);
	/* Arrête la cue sans rien changer à l'antenne. */
	void Stop();
	/* Bouton « Arrêter la cue » : arrêt avec un fondu au noir. */
	void StopWithFade();
	bool Running() const { return !runningId.empty(); }
	const std::string &RunningId() const { return runningId; }
	int RunningStep() const { return runningStep; }
	/* État de l'étape en cours (attente, média : temps écoulé / durée). */
	QString StatusText() const;
	/* Pour Companion : phase ("pre_wait", "post_wait", "media", ""), temps
	 * restant et, pour un média, position et durée (ms). */
	const char *PhaseName() const;
	qint64 RemainingMs() const;
	bool MediaTimes(int64_t &time, int64_t &duration) const;

	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);
	void Reset();

signals:
	void cuesChanged();
	void stateChanged();
	/* L'état affiché de l'étape en cours a changé (≈ 4 fois / s). */
	void progressChanged();

private:
	CueEngine();
	void RunStep();
	void Finish();
	void Schedule(int ms, std::function<void()> fn);
	void StartPhase(int phase, int ms);
	void WatchMedia(obs_source_t *scene);
	void OnMediaEnded(int gen);
	static void MediaEndedSignal(void *param, calldata_t *data);
	void Tick();
	void Advance();

	std::vector<Cue> cues;
	std::string armedId;
	bool arming = false;

	std::string runningId;
	int runningStep = -1;
	bool firstCut = false;
	OBSWeakSource firstTransition;
	int firstDurationMs = -1;
	/* Dernière scène de la dernière cue finie (« Arrêter » la ferme en
	 * fondu tant qu'elle est à l'antenne). */
	OBSWeakSource lastCueScene;
	int generation = 0;

	/* Suivi de l'étape en cours. */
	enum Phase { PhaseNone, PhasePreWait, PhaseOnAir, PhasePostWait, PhaseMedia };
	int phase = PhaseNone;
	qint64 phaseStart = 0;
	int phaseLength = 0;
	OBSWeakSource media;
	/* Gardé tant que son signal est branché (le gestionnaire de signaux
	 * vit avec la source). */
	OBSSource mediaHeld;
	OBSSignal mediaEnded;
	std::atomic<int> mediaGeneration{0};
	bool mediaStarted = false;
	QTimer ticker;
};
