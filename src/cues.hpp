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

#include <obs.hpp>

#include <QObject>
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

	/* Lance une cue (la cue armée si id est vide). */
	bool Go(const std::string &id, bool cut);
	void Stop();
	bool Running() const { return !runningId.empty(); }
	const std::string &RunningId() const { return runningId; }
	int RunningStep() const { return runningStep; }

	void Save(obs_data_t *data) const;
	void Load(obs_data_t *data);
	void Reset();

signals:
	void cuesChanged();
	void stateChanged();

private:
	CueEngine();
	void RunStep();
	void Finish();
	void Schedule(int ms, std::function<void()> fn);

	std::vector<Cue> cues;
	std::string armedId;
	bool arming = false;

	std::string runningId;
	int runningStep = -1;
	bool firstCut = false;
	OBSWeakSource programBefore;
	int generation = 0;
};
