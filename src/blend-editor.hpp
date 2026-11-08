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

#include <QDialog>
#include <QListWidget>
#include <QWidget>

#include <functional>
#include <string>
#include <vector>

class Destination;
class QCheckBox;
class QToolButton;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

/* Liste des sorties : on les glisse sur le layout. */
class BlendOutputList : public QListWidget {
	Q_OBJECT

public:
	static constexpr const char *MIME = "application/x-mds-blend-output";
	explicit BlendOutputList(QWidget *parent);

protected:
	void startDrag(Qt::DropActions actions) override;
};

/* Layout de la destination : on y place et règle les sorties à la souris,
 * comme dans les vues Aperçu / Programme. Les sorties gardent la forme de
 * leur écran et ne peuvent pas sortir du layout. */
class BlendLayoutView : public QWidget {
	Q_OBJECT

public:
	explicit BlendLayoutView(QWidget *parent);

	void SetConfig(BlendConfig *config);
	void SetSelected(int index);
	int Selected() const { return selected; }

signals:
	void aboutToChange(); /* une fois par geste (pour annuler) */
	void changed();
	void selectionChanged(int index);
	void dropRequested(int index, double layoutX, double layoutY);

protected:
	void paintEvent(QPaintEvent *event) override;
	void mousePressEvent(QMouseEvent *event) override;
	void mouseMoveEvent(QMouseEvent *event) override;
	void mouseReleaseEvent(QMouseEvent *event) override;
	void keyPressEvent(QKeyEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dragMoveEvent(QDragMoveEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private:
	QRectF LayoutRect() const;
	double Scale() const;
	QPointF ToLayout(const QPointF &widgetPos) const;
	QRectF ToWidget(const BlendOutput &o) const;
	int HitTest(const QPointF &widgetPos) const;
	int HandleAt(const QPointF &widgetPos) const;

	BlendConfig *config = nullptr;
	int selected = -1;

	enum class Drag { None, Move, Resize };
	Drag drag = Drag::None;
	bool dragStarted = false;
	int handle = -1;
	QPointF startPos;
	BlendOutput startOut;
	bool snapX = false;
	bool snapY = false;
};

/* Fenêtre « Output blending » : création d'une destination blendée
 * (dest == nullptr) ou modification de ses réglages. */
class BlendEditor : public QDialog {
	Q_OBJECT

public:
	BlendEditor(QWidget *parent, Destination *dest, const std::string &defaultName);
	~BlendEditor() override;

	std::string DestinationName() const;
	const BlendConfig &Config() const { return config; }
	bool AddToExisting() const;

	/* Création : renvoie true si la destination a été créée. */
	std::function<bool(BlendEditor *)> onCreate;
	/* Modification : applique les réglages à la destination. */
	std::function<void(BlendEditor *)> onSave;
	/* Ouvre les sorties de la destination (aperçu en direct). */
	std::function<void(Destination *)> onOpenOutputs;

public slots:
	void reject() override;

private:
	void BuildUi();
	void RefreshList();
	void RefreshParams();
	void RefreshButtons();
	void ApplyParam(const std::function<void(BlendOutput &)> &change);
	void Changed();
	void PushUndo();
	void Undo();
	bool Dirty() const;
	std::string State() const;
	void Save();
	void Create();
	void AddOutput();
	void RemoveOutput();
	void PlaceOutput(int index, double x, double y);
	void SetLayoutSize(uint32_t w, uint32_t h);
	BlendOutput *Current();

	Destination *dest;
	std::string defaultName;
	BlendConfig config;
	std::string savedState;
	std::vector<BlendConfig> undoStack;
	bool updating = false;

	QLineEdit *nameEdit = nullptr;
	QCheckBox *addToExisting = nullptr;
	BlendOutputList *list = nullptr;
	BlendLayoutView *view = nullptr;
	QSpinBox *layoutW = nullptr;
	QSpinBox *layoutH = nullptr;
	QCheckBox *testPattern = nullptr;
	QLabel *hint = nullptr;

	QGroupBox *params = nullptr;
	QLineEdit *outName = nullptr;
	QComboBox *outScreen = nullptr;
	QSpinBox *posX = nullptr;
	QSpinBox *posY = nullptr;
	QSpinBox *sizeW = nullptr;
	QSpinBox *sizeH = nullptr;
	QCheckBox *autoBlend = nullptr;
	QToolButton *lockAspect = nullptr;
	QLabel *endX = nullptr;
	QLabel *endY = nullptr;
	QSpinBox *edge[4] = {};
	QDoubleSpinBox *gamma = nullptr;
	QDoubleSpinBox *curve = nullptr;
	QDoubleSpinBox *blackLevel = nullptr;
	QSpinBox *pin[8] = {};
	QPushButton *removeButton = nullptr;

	QPushButton *saveButton = nullptr;
	QPushButton *createButton = nullptr;
};
