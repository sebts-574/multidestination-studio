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

#include <functional>

#include "scene-editor.hpp"

#include <obs.hpp>

#include <QWidget>

#include <atomic>
#include <mutex>
#include <vector>

class Destination;
class EditDisplay;
class QLabel;
class QPushButton;

/* Une destination dans la colonne Aperçu ou Programme.
 * Aperçu : on édite la scène de la destination.
 * Programme : on peut éditer ce qui est à l'antenne si « Modifier le
 * programme » est activé. */
class TileWidget : public QWidget {
	Q_OBJECT

public:
	TileWidget(Destination *destination, bool program, QWidget *parent);
	~TileWidget() override;

	Destination *Dest() const { return dest; }
	bool IsProgram() const { return program; }
	/* Ouverture des propriétés d'une source (double-clic), fournie par la
	 * fenêtre du studio pour garder la fenêtre d'OBS derrière elle. */
	static inline std::function<void(obs_source_t *)> openProperties;

	/* Aperçu : scène de cette destination pour la scène sélectionnée. */
	void SetLinkedScene(obs_source_t *scene);
	OBSSource LinkedScene() const;
	/* Scène qu'on édite : la scène liée (Aperçu) ou la copie à l'antenne
	 * (Programme). */
	OBSSource EditScene() const;

	void SetEditable(bool value);
	void SetActive(bool active);
	bool Active() const { return active.load(); }
	void SetFocused(bool focused);
	/* Contours et sélection visibles (effacés quand on clique ailleurs). */
	void SetHighlighted(bool value) { highlighted = value; }
	/* Hauteur de l'en-tête (nom + boutons), fixée par la colonne. */
	void SetHeaderHeight(int height);
	static int HeaderHeightFor(int columnWidth);
	/* Texte « Absente de cette scène » partagé (thread de l'interface). */
	static void EnsureAbsentLabel();
	static void ReleaseShared();
	void SetSelectedItem(int64_t id);
	int64_t SelectedItem() const { return selectedId.load(); }
	void RefreshHeader();

	/* Événements transmis par l'affichage. */
	void MousePress(QMouseEvent *event);
	void MouseMove(QMouseEvent *event);
	void MouseRelease(QMouseEvent *event);
	void MouseDoubleClick(QMouseEvent *event);
	bool KeyPress(QKeyEvent *event);

protected:
	void resizeEvent(QResizeEvent *event) override;

signals:
	void activated(TileWidget *tile);
	void selectionChanged(TileWidget *tile, qint64 itemId);
	void itemsChanged(TileWidget *tile);
	void lockChanged(TileWidget *tile);
	void focusRequested(TileWidget *tile);
	void contextMenuRequested(TileWidget *tile, const QPoint &globalPos);

private:
	static void DrawCb(void *data, uint32_t cx, uint32_t cy);
	bool ToCanvas(const QPointF &widgetPos, float &x, float &y, float &screenScale) const;
	OBSSceneItem SelectedSceneItem() const;
	/* Texte des boutons Agrandir / Verrouiller adapté à la largeur. */
	void FitHeaderButtons();

	Destination *dest;
	bool program;
	bool focused = false;

	QWidget *header = nullptr;
	QLabel *headerLabel = nullptr;
	QString headerText; /* texte complet (le libellé peut être abrégé) */
	std::atomic<bool> absent{false};
	QPushButton *lockButton = nullptr;
	QPushButton *focusButton = nullptr;
	EditDisplay *display = nullptr;

	mutable std::mutex mutex;
	OBSWeakSource linkedScene;
	std::atomic<int64_t> selectedId{-1};
	std::atomic<bool> editable{false};
	std::atomic<bool> active{false};
	std::atomic<bool> highlighted{true};

	/* Glisser-déposer en cours. */
	enum class Drag { None, Move, Resize, Crop };
	Drag drag = Drag::None;
	bool dragMoved = false;
	int dragEdges = 0;
	float startX = 0.0f;
	float startY = 0.0f;
	SceneEditor::Box startBox;
	struct obs_sceneitem_crop startCrop = {};
	float startScaleX = 1.0f;
	float startScaleY = 1.0f;
};

/* Colonne de tuiles (une par destination affichée), empilées verticalement
 * et mises à l'échelle pour tenir dans la zone. Les tuiles existantes sont
 * réutilisées pour éviter les clignotements. */
class TileGrid : public QWidget {
	Q_OBJECT

public:
	TileGrid(bool program, QWidget *parent);

	/* Renvoie les tuiles nouvellement créées (à connecter). */
	std::vector<TileWidget *> SetDestinations(const std::vector<Destination *> &dests);
	const std::vector<TileWidget *> &Tiles() const { return tiles; }
	TileWidget *TileFor(Destination *dest) const;
	TileWidget *TileAt(const QPoint &globalPos) const;
	void Clear();

protected:
	void resizeEvent(QResizeEvent *event) override;

private:
	void Relayout();

	bool program;
	std::vector<TileWidget *> tiles;
	QLabel *emptyLabel = nullptr;
};
