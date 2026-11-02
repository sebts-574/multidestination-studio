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

#include <QPointer>
#include <QScreen>
#include <QWidget>

#include <atomic>
#include <memory>

class Destination;
class BlendRenderer;
class OBSQTDisplay;

/* Sortie Program d'une destination, en plein écran sur un écran donné
 * (ou en fenêtre si aucun écran). Échap ferme le projecteur.
 * outputIndex >= 0 : une sortie d'une destination blendée (sa zone du
 * layout, avec fondus et corner pin). */
class ProjectorWindow : public QWidget {
	Q_OBJECT

public:
	ProjectorWindow(Destination *destination, QScreen *screen, int outputIndex = -1);
	~ProjectorWindow() override;

	void Start();
	QScreen *TargetScreen() const { return target; }
	/* Écran choisi à l'ouverture (même s'il est débranché). */
	bool WantsScreen() const { return !targetName.isEmpty(); }
	int OutputIndex() const { return outputIndex; }

protected:
	void keyPressEvent(QKeyEvent *event) override;
	void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
	void ScreenRemoved(QScreen *screen);
	void ScreenAdded(QScreen *screen);
	static void DrawCb(void *data, uint32_t cx, uint32_t cy);

	Destination *dest;
	QPointer<QScreen> target;
	QString targetName;
	OBSQTDisplay *display = nullptr;
	int outputIndex = -1;
	std::unique_ptr<BlendRenderer> blendRenderer;
	/* Taille native de l'écran de sortie (pixels physiques). */
	std::atomic<uint32_t> nativeW{0};
	std::atomic<uint32_t> nativeH{0};
};
