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

#include "blend-editor.hpp"
#include "dock-style.hpp"
#include "destination.hpp"
#include "dialogs.hpp"
#include "manager.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDrag>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSpinBox>
#include <QToolButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr double MIN_SIZE = 32.0;
constexpr double SNAP_PX = 8.0;
constexpr double HANDLE_PX = 8.0;

const QColor OUTPUT_COLORS[] = {
	QColor(0x2E, 0xCC, 0x40), QColor(0x3D, 0x9B, 0xF5), QColor(0xFF, 0x95, 0x00),
	QColor(0xE0, 0x4F, 0xD0), QColor(0x2E, 0xD3, 0xC6), QColor(0xF5, 0xD0, 0x3D),
};

QColor OutputColor(int index)
{
	return OUTPUT_COLORS[size_t(index) % (sizeof(OUTPUT_COLORS) / sizeof(OUTPUT_COLORS[0]))];
}

/* Taille native d'un écran en pixels physiques (1920×1080 si aucun). */
void NativeSize(const std::string &screenName, uint32_t &w, uint32_t &h)
{
	w = 1920;
	h = 1080;
	if (QScreen *screen = Dialogs::FindScreen(screenName)) {
		const qreal ratio = screen->devicePixelRatio();
		w = uint32_t(std::lround(screen->geometry().width() * ratio));
		h = uint32_t(std::lround(screen->geometry().height() * ratio));
	}
}

} // namespace

/* ------------------------------------------------------------------ */
/* Liste des sorties                                                    */
/* ------------------------------------------------------------------ */

BlendOutputList::BlendOutputList(QWidget *parent) : QListWidget(parent)
{
	setDragEnabled(true);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setDragDropMode(QAbstractItemView::DragOnly);
}

void BlendOutputList::startDrag(Qt::DropActions)
{
	const int row = currentRow();
	if (row < 0)
		return;
	auto *mime = new QMimeData();
	mime->setData(MIME, QByteArray::number(row));
	auto *drag = new QDrag(this);
	drag->setMimeData(mime);
	drag->exec(Qt::CopyAction);
}

/* ------------------------------------------------------------------ */
/* Layout                                                               */
/* ------------------------------------------------------------------ */

BlendLayoutView::BlendLayoutView(QWidget *parent) : QWidget(parent)
{
	setAcceptDrops(true);
	setFocusPolicy(Qt::StrongFocus);
	setMouseTracking(true);
	setMinimumSize(480, 240);
}

void BlendLayoutView::SetConfig(BlendConfig *value)
{
	config = value;
	update();
}

void BlendLayoutView::SetSelected(int index)
{
	if (selected == index)
		return;
	selected = index;
	update();
}

QRectF BlendLayoutView::LayoutRect() const
{
	if (!config || !config->HasLayout())
		return QRectF();
	const double margin = 24.0;
	const double availW = std::max(1.0, width() - 2 * margin);
	const double availH = std::max(1.0, height() - 2 * margin);
	const double s = std::min(availW / config->width, availH / config->height);
	const double w = config->width * s;
	const double h = config->height * s;
	return QRectF((width() - w) / 2.0, (height() - h) / 2.0, w, h);
}

double BlendLayoutView::Scale() const
{
	const QRectF r = LayoutRect();
	return (config && config->width > 0) ? r.width() / config->width : 1.0;
}

QPointF BlendLayoutView::ToLayout(const QPointF &p) const
{
	const QRectF r = LayoutRect();
	const double s = Scale();
	return QPointF((p.x() - r.x()) / s, (p.y() - r.y()) / s);
}

QRectF BlendLayoutView::ToWidget(const BlendOutput &o) const
{
	const QRectF r = LayoutRect();
	const double s = Scale();
	return QRectF(r.x() + o.x * s, r.y() + o.y * s, o.w * s, o.h * s);
}

int BlendLayoutView::HitTest(const QPointF &p) const
{
	if (!config)
		return -1;
	/* La sélection d'abord, puis du dessus vers le dessous. */
	if (selected >= 0 && size_t(selected) < config->outputs.size() && config->outputs[size_t(selected)].placed &&
	    ToWidget(config->outputs[size_t(selected)]).contains(p))
		return selected;
	for (int i = int(config->outputs.size()) - 1; i >= 0; i--) {
		const BlendOutput &o = config->outputs[size_t(i)];
		if (o.placed && ToWidget(o).contains(p))
			return i;
	}
	return -1;
}

int BlendLayoutView::HandleAt(const QPointF &p) const
{
	if (!config || selected < 0 || size_t(selected) >= config->outputs.size())
		return -1;
	const BlendOutput &o = config->outputs[size_t(selected)];
	if (!o.placed)
		return -1;
	const QRectF r = ToWidget(o);
	const QPointF corners[4] = {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()};
	for (int i = 0; i < 4; i++) {
		if (std::abs(p.x() - corners[i].x()) <= HANDLE_PX && std::abs(p.y() - corners[i].y()) <= HANDLE_PX)
			return i;
	}
	return -1;
}

void BlendLayoutView::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing, true);
	painter.fillRect(rect(), palette().color(QPalette::Base).darker(130));

	const QRectF lr = LayoutRect();
	if (lr.isEmpty()) {
		painter.setPen(palette().color(QPalette::WindowText));
		painter.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap, Tr("MDS.BlendEmptyHint"));
		return;
	}

	painter.fillRect(lr, QColor(0x1B, 0x1B, 0x1B));
	painter.setPen(QPen(QColor(0x9A, 0x9A, 0x9A), 1.0));
	painter.drawRect(lr);
	painter.setPen(palette().color(QPalette::WindowText));
	painter.drawText(QRectF(lr.x(), lr.bottom() + 2, lr.width(), 18), Qt::AlignRight | Qt::AlignTop,
			 QString("%1 × %2").arg(config->width).arg(config->height));

	/* Sorties. */
	for (size_t i = 0; i < config->outputs.size(); i++) {
		const BlendOutput &o = config->outputs[i];
		if (!o.placed)
			continue;
		const QRectF r = ToWidget(o);
		QColor fill = OutputColor(int(i));
		fill.setAlpha(55);
		painter.fillRect(r, fill);

		/* Zones de fondu : dégradé vers l'extérieur. */
		const double s = Scale();
		auto ramp = [&](const QRectF &zone, const QPointF &from, const QPointF &to) {
			if (zone.width() < 1.0 || zone.height() < 1.0)
				return;
			QLinearGradient g(from, to);
			g.setColorAt(0.0, QColor(0, 0, 0, 170));
			g.setColorAt(1.0, QColor(0, 0, 0, 0));
			painter.fillRect(zone, g);
		};
		ramp(QRectF(r.left(), r.top(), o.left * s, r.height()), QPointF(r.left(), 0),
		     QPointF(r.left() + o.left * s, 0));
		ramp(QRectF(r.right() - o.right * s, r.top(), o.right * s, r.height()), QPointF(r.right(), 0),
		     QPointF(r.right() - o.right * s, 0));
		ramp(QRectF(r.left(), r.top(), r.width(), o.top * s), QPointF(0, r.top()),
		     QPointF(0, r.top() + o.top * s));
		ramp(QRectF(r.left(), r.bottom() - o.bottom * s, r.width(), o.bottom * s), QPointF(0, r.bottom()),
		     QPointF(0, r.bottom() - o.bottom * s));
	}

	/* Recouvrements hachurés. */
	for (size_t i = 0; i < config->outputs.size(); i++) {
		for (size_t j = i + 1; j < config->outputs.size(); j++) {
			const BlendOutput &a = config->outputs[i];
			const BlendOutput &b = config->outputs[j];
			if (!a.placed || !b.placed)
				continue;
			const QRectF inter = ToWidget(a).intersected(ToWidget(b));
			if (!inter.isEmpty())
				painter.fillRect(inter, QBrush(QColor(255, 255, 255, 70), Qt::BDiagPattern));
		}
	}

	for (size_t i = 0; i < config->outputs.size(); i++) {
		const BlendOutput &o = config->outputs[i];
		if (!o.placed)
			continue;
		const QRectF r = ToWidget(o);
		const bool isSelected = int(i) == selected;
		painter.setPen(QPen(OutputColor(int(i)), isSelected ? 2.5 : 1.5));
		painter.setBrush(Qt::NoBrush);
		painter.drawRect(r);

		painter.setPen(Qt::white);
		QString label = QString::fromStdString(o.name);
		label += QString("\n%1 × %2").arg(std::lround(o.w)).arg(std::lround(o.h));
		painter.drawText(r, Qt::AlignCenter, label);

		if (isSelected) {
			painter.setBrush(Qt::white);
			painter.setPen(QPen(OutputColor(int(i)), 1.0));
			for (const QPointF &c : {r.topLeft(), r.topRight(), r.bottomLeft(), r.bottomRight()})
				painter.drawRect(QRectF(c.x() - 4, c.y() - 4, 8, 8));
		}
	}

	/* Guides de centre pendant le déplacement. */
	if (drag == Drag::Move && dragStarted && (snapX || snapY)) {
		painter.setPen(QPen(QColor(0x2E, 0xCC, 0x40), 1.0, Qt::DashLine));
		if (snapX)
			painter.drawLine(QPointF(lr.center().x(), lr.top()), QPointF(lr.center().x(), lr.bottom()));
		if (snapY)
			painter.drawLine(QPointF(lr.left(), lr.center().y()), QPointF(lr.right(), lr.center().y()));
	}
}

void BlendLayoutView::mousePressEvent(QMouseEvent *event)
{
	setFocus();
	if (!config || event->button() != Qt::LeftButton)
		return;
	const QPointF p = event->position();

	handle = HandleAt(p);
	if (handle >= 0) {
		drag = Drag::Resize;
	} else {
		const int hit = HitTest(p);
		if (hit != selected) {
			selected = hit;
			emit selectionChanged(selected);
		}
		drag = hit >= 0 ? Drag::Move : Drag::None;
	}
	if (drag != Drag::None) {
		startPos = p;
		startOut = config->outputs[size_t(selected)];
		dragStarted = false;
	}
	update();
}

void BlendLayoutView::mouseMoveEvent(QMouseEvent *event)
{
	const QPointF p = event->position();
	if (drag == Drag::None || !config || selected < 0 || size_t(selected) >= config->outputs.size()) {
		const int h = HandleAt(p);
		if (h == 0 || h == 3)
			setCursor(Qt::SizeFDiagCursor);
		else if (h == 1 || h == 2)
			setCursor(Qt::SizeBDiagCursor);
		else
			setCursor(HitTest(p) >= 0 ? Qt::SizeAllCursor : Qt::ArrowCursor);
		return;
	}

	const double s = Scale();
	const double dx = (p.x() - startPos.x()) / s;
	const double dy = (p.y() - startPos.y()) / s;
	if (!dragStarted) {
		if (std::abs(p.x() - startPos.x()) < 2 && std::abs(p.y() - startPos.y()) < 2)
			return;
		dragStarted = true;
		emit aboutToChange();
	}

	BlendOutput &o = config->outputs[size_t(selected)];
	const double W = config->width;
	const double H = config->height;

	if (drag == Drag::Move) {
		o.x = startOut.x + dx;
		o.y = startOut.y + dy;
		snapX = snapY = false;
		if (!(event->modifiers() & Qt::ControlModifier)) {
			const double tol = SNAP_PX / s;
			if (std::abs(o.x + o.w / 2 - W / 2) < tol) {
				o.x = W / 2 - o.w / 2;
				snapX = true;
			}
			if (std::abs(o.y + o.h / 2 - H / 2) < tol) {
				o.y = H / 2 - o.h / 2;
				snapY = true;
			}
		}
		o.x = std::clamp(o.x, 0.0, W - o.w);
		o.y = std::clamp(o.y, 0.0, H - o.h);
	} else {
		/* Coin opposé fixe. */
		const bool left = handle == 0 || handle == 2;
		const bool top = handle == 0 || handle == 1;
		const double ax = left ? startOut.x + startOut.w : startOut.x;
		const double ay = top ? startOut.y + startOut.h : startOut.y;
		const double maxW = left ? ax : W - ax;
		const double maxH = top ? ay : H - ay;

		double w = startOut.w + (left ? -dx : dx);
		double h = startOut.h + (top ? -dy : dy);
		/* Proportions verrouillées (réglage de la sortie) ; Maj fait
		 * l'inverse le temps du glisser. */
		const bool keep = o.lockAspect != bool(event->modifiers() & Qt::ShiftModifier);
		if (keep) {
			const double aspect = startOut.w / std::max(1.0, startOut.h);
			w = std::max(w, h * aspect);
			w = std::clamp(w, MIN_SIZE, std::min(maxW, maxH * aspect));
			h = w / aspect;
		} else {
			w = std::clamp(w, MIN_SIZE, maxW);
			h = std::clamp(h, MIN_SIZE, maxH);
		}
		o.w = w;
		o.h = h;
		o.x = left ? ax - w : ax;
		o.y = top ? ay - h : ay;
	}
	config->ComputeAutoBlend();
	emit changed();
	update();
}

void BlendLayoutView::mouseReleaseEvent(QMouseEvent *)
{
	drag = Drag::None;
	snapX = snapY = false;
	update();
}

void BlendLayoutView::keyPressEvent(QKeyEvent *event)
{
	if (!config || selected < 0 || size_t(selected) >= config->outputs.size() ||
	    !config->outputs[size_t(selected)].placed) {
		QWidget::keyPressEvent(event);
		return;
	}
	BlendOutput &o = config->outputs[size_t(selected)];
	const double step = (event->modifiers() & Qt::ShiftModifier) ? 10.0 : 1.0;
	double dx = 0.0;
	double dy = 0.0;
	switch (event->key()) {
	case Qt::Key_Left:
		dx = -step;
		break;
	case Qt::Key_Right:
		dx = step;
		break;
	case Qt::Key_Up:
		dy = -step;
		break;
	case Qt::Key_Down:
		dy = step;
		break;
	case Qt::Key_Delete:
	case Qt::Key_Backspace:
		emit aboutToChange();
		o.placed = false;
		config->ComputeAutoBlend();
		emit changed();
		update();
		return;
	default:
		QWidget::keyPressEvent(event);
		return;
	}
	emit aboutToChange();
	o.x = std::clamp(o.x + dx, 0.0, double(config->width) - o.w);
	o.y = std::clamp(o.y + dy, 0.0, double(config->height) - o.h);
	config->ComputeAutoBlend();
	emit changed();
	update();
}

void BlendLayoutView::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasFormat(BlendOutputList::MIME) && config && config->HasLayout())
		event->acceptProposedAction();
}

void BlendLayoutView::dragMoveEvent(QDragMoveEvent *event)
{
	if (event->mimeData()->hasFormat(BlendOutputList::MIME) && LayoutRect().contains(event->position()))
		event->acceptProposedAction();
	else
		event->ignore();
}

void BlendLayoutView::dropEvent(QDropEvent *event)
{
	bool ok = false;
	const int index = event->mimeData()->data(BlendOutputList::MIME).toInt(&ok);
	if (!ok)
		return;
	const QPointF p = ToLayout(event->position());
	event->acceptProposedAction();
	emit dropRequested(index, p.x(), p.y());
}

/* ------------------------------------------------------------------ */
/* Fenêtre Output blending                                              */
/* ------------------------------------------------------------------ */

BlendEditor::BlendEditor(QWidget *parent, Destination *destination, const std::string &defaultDestName)
	: QDialog(parent),
	  dest(destination),
	  defaultName(defaultDestName)
{
	if (dest)
		config = dest->Blend();
	if (dest && !config.HasLayout()) {
		/* Destination existante pas encore blendée : son layout = sa
		 * résolution. */
		config.width = dest->Width();
		config.height = dest->Height();
	}

	setWindowTitle(dest ? Tr("MDS.OutputBlendingTitle").arg(QString::fromStdString(dest->Name()))
			    : Tr("MDS.OutputBlending"));
	setWindowModality(Qt::WindowModal);
	resize(1100, 680);

	BuildUi();
	RefreshList();
	RefreshParams();
	savedState = State();
	RefreshButtons();

	auto *undo = new QShortcut(QKeySequence::Undo, this);
	connect(undo, &QShortcut::activated, this, &BlendEditor::Undo);
}

BlendEditor::~BlendEditor()
{
	if (dest)
		dest->SetBlendPreview(nullptr);
}

void BlendEditor::BuildUi()
{
	auto *root = new QVBoxLayout(this);
	auto *splitter = new QSplitter(Qt::Horizontal, this);
	root->addWidget(splitter, 1);

	/* Colonne gauche : sorties. */
	auto *left = new QWidget(splitter);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	leftLayout->addWidget(new QLabel(Tr("MDS.BlendOutputs"), left));
	list = new BlendOutputList(left);
	leftLayout->addWidget(list, 1);
	auto *newButton = new QPushButton(Tr("MDS.BlendNewOutput"), left);
	leftLayout->addWidget(newButton);
	removeButton = new QPushButton(Tr("MDS.BlendRemoveOutput"), left);
	leftLayout->addWidget(removeButton);
	hint = new QLabel(Tr("MDS.BlendDragHint"), left);
	hint->setWordWrap(true);
	hint->setStyleSheet("color: palette(window-text);");
	leftLayout->addWidget(hint);
	connect(newButton, &QPushButton::clicked, this, &BlendEditor::AddOutput);
	connect(removeButton, &QPushButton::clicked, this, &BlendEditor::RemoveOutput);

	/* Centre : layout. */
	auto *center = new QWidget(splitter);
	auto *centerLayout = new QVBoxLayout(center);
	centerLayout->setContentsMargins(0, 0, 0, 0);
	auto *sizeRow = new QHBoxLayout();
	sizeRow->addWidget(new QLabel(Tr("MDS.BlendLayoutSize"), center));
	layoutW = new QSpinBox(center);
	layoutW->setRange(16, 16384);
	layoutH = new QSpinBox(center);
	layoutH->setRange(16, 16384);
	sizeRow->addWidget(layoutW);
	sizeRow->addWidget(new QLabel("×", center));
	sizeRow->addWidget(layoutH);
	sizeRow->addStretch(1);
	testPattern = new QCheckBox(Tr("MDS.BlendTestPattern"), center);
	sizeRow->addWidget(testPattern);
	centerLayout->addLayout(sizeRow);
	view = new BlendLayoutView(center);
	view->SetConfig(&config);
	centerLayout->addWidget(view, 1);
	auto *viewHint = new QLabel(Tr("MDS.BlendViewHint"), center);
	viewHint->setWordWrap(true);
	viewHint->setStyleSheet("color: palette(window-text);");
	centerLayout->addWidget(viewHint);

	connect(layoutW, &QSpinBox::editingFinished, this,
		[this]() { SetLayoutSize(uint32_t(layoutW->value()), config.height); });
	connect(layoutH, &QSpinBox::editingFinished, this,
		[this]() { SetLayoutSize(config.width, uint32_t(layoutH->value())); });
	connect(testPattern, &QCheckBox::toggled, this, [this](bool on) {
		if (updating)
			return;
		PushUndo();
		config.testPattern = on;
		Changed();
	});

	/* Colonne droite : réglages de la sortie sélectionnée. */
	auto *scroll = new QScrollArea(splitter);
	scroll->setWidgetResizable(true);
	params = new QGroupBox(Tr("MDS.BlendOutput"));
	scroll->setWidget(params);
	auto *form = new QFormLayout(params);

	outName = new QLineEdit(params);
	form->addRow(Tr("MDS.Name"), outName);
	outScreen = Dialogs::ScreenCombo("", params);
	form->addRow(Tr("MDS.OutputScreen"), outScreen);

	auto spin = [this](int min, int max) {
		auto *s = new QSpinBox(params);
		s->setRange(min, max);
		s->setKeyboardTracking(false);
		return s;
	};
	posX = spin(0, 16384);
	posY = spin(0, 16384);
	sizeW = spin(int(MIN_SIZE), 16384);
	sizeH = spin(int(MIN_SIZE), 16384);

	/* Zone de la sortie : taille (avec verrou des proportions entre largeur
	 * et hauteur), position, et coin opposé (lecture seule). */
	auto *region = new QGroupBox(params);
	auto *regionGrid = new QGridLayout(region);
	regionGrid->setColumnStretch(1, 1);
	regionGrid->setColumnStretch(3, 1);
	lockAspect = new QToolButton(region);
	lockAspect->setCheckable(true);
	lockAspect->setAutoRaise(true);
	lockAspect->setToolTip(Tr("MDS.BlendLockAspect"));
	auto updateLockIcon = [this]() {
		lockAspect->setIcon(DockStyle::LockIcon(lockAspect->isChecked(),
							lockAspect->palette().color(QPalette::ButtonText)));
	};
	connect(lockAspect, &QToolButton::toggled, this, updateLockIcon);
	updateLockIcon();
	endX = new QLabel(region);
	endY = new QLabel(region);
	for (QLabel *l : {endX, endY}) {
		l->setAlignment(Qt::AlignCenter);
		l->setTextInteractionFlags(Qt::TextSelectableByMouse);
	}
	auto rowLabel = [region](const QString &text) {
		auto *l = new QLabel(text, region);
		l->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
		return l;
	};
	regionGrid->addWidget(rowLabel(Tr("MDS.BlendSize")), 0, 0);
	regionGrid->addWidget(sizeW, 0, 1);
	regionGrid->addWidget(lockAspect, 0, 2, Qt::AlignCenter);
	regionGrid->addWidget(sizeH, 0, 3);
	regionGrid->addWidget(rowLabel(Tr("MDS.BlendPosition")), 1, 0);
	regionGrid->addWidget(posX, 1, 1);
	regionGrid->addWidget(posY, 1, 3);
	regionGrid->addWidget(rowLabel(Tr("MDS.BlendEnd")), 2, 0);
	regionGrid->addWidget(endX, 2, 1);
	regionGrid->addWidget(endY, 2, 3);
	form->addRow(region);

	/* Fondus des bords, disposés comme les bords de la sortie : haut, gauche
	 * et droite, bas ; automatique en dessous. */
	auto *edges = new QGroupBox(Tr("MDS.BlendEdges"), params);
	auto *edgeGrid = new QGridLayout(edges);
	const char *edgeKeys[4] = {"MDS.BlendLeft", "MDS.BlendRight", "MDS.BlendTop", "MDS.BlendBottom"};
	const int edgeRow[4] = {1, 1, 0, 2};
	const int edgeCol[4] = {0, 2, 1, 1};
	for (int i = 0; i < 4; i++) {
		edge[i] = spin(0, 16384);
		auto *cell = new QWidget(edges);
		auto *cellLayout = new QVBoxLayout(cell);
		cellLayout->setContentsMargins(0, 0, 0, 0);
		cellLayout->setSpacing(2);
		auto *label = new QLabel(Tr(edgeKeys[i]), cell);
		label->setAlignment(Qt::AlignCenter);
		cellLayout->addWidget(label);
		cellLayout->addWidget(edge[i]);
		edgeGrid->addWidget(cell, edgeRow[i], edgeCol[i]);
	}
	for (int c = 0; c < 3; c++)
		edgeGrid->setColumnStretch(c, 1);
	autoBlend = new QCheckBox(Tr("MDS.BlendAuto"), edges);
	autoBlend->setToolTip(Tr("MDS.BlendAutoTip"));
	edgeGrid->addWidget(autoBlend, 3, 0, 1, 3, Qt::AlignCenter);
	form->addRow(edges);

	gamma = new QDoubleSpinBox(params);
	gamma->setRange(0.5, 4.0);
	gamma->setSingleStep(0.05);
	gamma->setDecimals(2);
	gamma->setKeyboardTracking(false);
	gamma->setToolTip(Tr("MDS.BlendGammaTip"));
	form->addRow(Tr("MDS.BlendGamma"), gamma);
	curve = new QDoubleSpinBox(params);
	curve->setRange(1.0, 6.0);
	curve->setSingleStep(0.1);
	curve->setDecimals(2);
	curve->setKeyboardTracking(false);
	curve->setToolTip(Tr("MDS.BlendCurveTip"));
	form->addRow(Tr("MDS.BlendCurve"), curve);
	blackLevel = new QDoubleSpinBox(params);
	blackLevel->setRange(0.0, 30.0);
	blackLevel->setSingleStep(0.5);
	blackLevel->setDecimals(1);
	blackLevel->setSuffix(" %");
	blackLevel->setKeyboardTracking(false);
	blackLevel->setToolTip(Tr("MDS.BlendBlackLevelTip"));
	form->addRow(Tr("MDS.BlendBlackLevel"), blackLevel);

	auto *pinBox = new QGroupBox(Tr("MDS.BlendCornerPin"), params);
	pinBox->setToolTip(Tr("MDS.BlendCornerPinTip"));
	auto *pinGrid = new QGridLayout(pinBox);
	const char *cornerKeys[4] = {"MDS.BlendTopLeft", "MDS.BlendTopRight", "MDS.BlendBottomLeft",
				     "MDS.BlendBottomRight"};
	for (int c = 0; c < 4; c++) {
		pinGrid->addWidget(new QLabel(Tr(cornerKeys[c]), pinBox), c, 0);
		for (int k = 0; k < 2; k++) {
			auto *s = spin(-4000, 4000);
			s->setPrefix(k == 0 ? "x " : "y ");
			pin[c * 2 + k] = s;
			pinGrid->addWidget(s, c, 1 + k);
		}
	}
	auto *resetPin = new QPushButton(Tr("MDS.BlendResetPin"), pinBox);
	pinGrid->addWidget(resetPin, 4, 0, 1, 3);
	form->addRow(pinBox);

	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setStretchFactor(2, 0);
	splitter->setSizes({200, 600, 300});

	/* Liaisons des réglages. */
	connect(list, &QListWidget::currentRowChanged, this, [this](int row) {
		if (updating)
			return;
		view->SetSelected(row);
		RefreshParams();
	});
	connect(list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *item) {
		const int row = list->row(item);
		if (row >= 0 && size_t(row) < config.outputs.size() && !config.outputs[size_t(row)].placed &&
		    config.HasLayout())
			PlaceOutput(row, config.width / 2.0, config.height / 2.0);
	});
	connect(view, &BlendLayoutView::selectionChanged, this, [this](int index) {
		updating = true;
		list->setCurrentRow(index);
		updating = false;
		RefreshParams();
	});
	connect(view, &BlendLayoutView::aboutToChange, this, &BlendEditor::PushUndo);
	connect(view, &BlendLayoutView::changed, this, &BlendEditor::Changed);
	connect(view, &BlendLayoutView::dropRequested, this, &BlendEditor::PlaceOutput);

	connect(outName, &QLineEdit::editingFinished, this, [this]() {
		const std::string text = outName->text().trimmed().toStdString();
		if (Current() && !text.empty() && text != Current()->name)
			ApplyParam([&](BlendOutput &o) { o.name = text; });
	});
	connect(outScreen, &QComboBox::currentIndexChanged, this, [this](int) {
		const std::string screen = outScreen->currentData().toString().toStdString();
		if (!Current() || screen == Current()->screen)
			return;
		ApplyParam([&](BlendOutput &o) {
			o.screen = screen;
			/* Nouvelle forme d'écran : on garde la largeur. */
			uint32_t nw, nh;
			NativeSize(screen, nw, nh);
			if (o.placed) {
				o.h = o.w * double(nh) / double(nw);
				config.ClampInside(o);
			}
		});
	});
	connect(posX, &QSpinBox::valueChanged, this, [this](int v) { ApplyParam([&](BlendOutput &o) { o.x = v; }); });
	connect(posY, &QSpinBox::valueChanged, this, [this](int v) { ApplyParam([&](BlendOutput &o) { o.y = v; }); });
	connect(sizeW, &QSpinBox::valueChanged, this, [this](int v) {
		ApplyParam([&](BlendOutput &o) {
			const double aspect = o.w / std::max(1.0, o.h);
			o.w = v;
			if (o.lockAspect)
				o.h = v / aspect;
		});
	});
	connect(sizeH, &QSpinBox::valueChanged, this, [this](int v) {
		ApplyParam([&](BlendOutput &o) {
			const double aspect = o.w / std::max(1.0, o.h);
			o.h = v;
			if (o.lockAspect)
				o.w = v * aspect;
		});
	});
	connect(lockAspect, &QToolButton::toggled, this, [this](bool on) {
		if (Current() && Current()->lockAspect != on)
			ApplyParam([&](BlendOutput &o) { o.lockAspect = on; });
	});
	connect(autoBlend, &QCheckBox::toggled, this,
		[this](bool on) { ApplyParam([&](BlendOutput &o) { o.autoBlend = on; }); });
	for (int i = 0; i < 4; i++) {
		connect(edge[i], &QSpinBox::valueChanged, this, [this, i](int v) {
			ApplyParam([&](BlendOutput &o) {
				double *fields[4] = {&o.left, &o.right, &o.top, &o.bottom};
				*fields[i] = v;
			});
		});
	}
	connect(gamma, &QDoubleSpinBox::valueChanged, this,
		[this](double v) { ApplyParam([&](BlendOutput &o) { o.gamma = v; }); });
	connect(curve, &QDoubleSpinBox::valueChanged, this,
		[this](double v) { ApplyParam([&](BlendOutput &o) { o.curve = v; }); });
	connect(blackLevel, &QDoubleSpinBox::valueChanged, this,
		[this](double v) { ApplyParam([&](BlendOutput &o) { o.blackLevel = v / 100.0; }); });
	for (int i = 0; i < 8; i++) {
		connect(pin[i], &QSpinBox::valueChanged, this,
			[this, i](int v) { ApplyParam([&](BlendOutput &o) { o.pin[i] = v; }); });
	}
	connect(resetPin, &QPushButton::clicked, this,
		[this]() { ApplyParam([](BlendOutput &o) { std::fill(std::begin(o.pin), std::end(o.pin), 0.0); }); });

	/* Bas : nom de la destination et boutons. */
	auto *bottom = new QHBoxLayout();
	bottom->addWidget(new QLabel(Tr("MDS.BlendDestinationName"), this));
	nameEdit = new QLineEdit(this);
	nameEdit->setPlaceholderText(QString::fromStdString(defaultName));
	if (dest)
		nameEdit->setText(QString::fromStdString(dest->Name()));
	nameEdit->setMinimumWidth(220);
	bottom->addWidget(nameEdit);
	connect(nameEdit, &QLineEdit::textChanged, this, [this]() { RefreshButtons(); });

	if (!dest) {
		addToExisting = new QCheckBox(Tr("MDS.AddToExistingScenes"), this);
		addToExisting->setToolTip(Tr("MDS.AddToExistingScenesTip"));
		if (Manager::Get().ShowAllDestinations())
			addToExisting->setToolTip(Tr("MDS.AddToExistingScenesTip") + "\n\n" +
						  Tr("MDS.AddToExistingShowAllNote"));
		bottom->addWidget(addToExisting);
	}
	bottom->addStretch(1);

	if (dest) {
		auto *openOutputs = new QPushButton(Tr("MDS.BlendOpenOutputs"), this);
		openOutputs->setToolTip(Tr("MDS.BlendOpenOutputsTip"));
		bottom->addWidget(openOutputs);
		connect(openOutputs, &QPushButton::clicked, this, [this]() {
			dest->SetBlendPreview(&config);
			if (onOpenOutputs)
				onOpenOutputs(dest);
		});
		saveButton = new QPushButton(Tr("MDS.SaveChanges"), this);
		auto *closeButton = new QPushButton(Tr("MDS.Close"), this);
		bottom->addWidget(saveButton);
		bottom->addWidget(closeButton);
		connect(saveButton, &QPushButton::clicked, this, &BlendEditor::Save);
		connect(closeButton, &QPushButton::clicked, this, &BlendEditor::reject);
	} else {
		auto *cancel = new QPushButton(Tr("MDS.Cancel"), this);
		createButton = new QPushButton(Tr("MDS.SaveAndCreate").replace("&", "&&"), this);
		bottom->addWidget(cancel);
		bottom->addWidget(createButton);
		connect(cancel, &QPushButton::clicked, this, &BlendEditor::reject);
		connect(createButton, &QPushButton::clicked, this, &BlendEditor::Create);
	}
	root->addLayout(bottom);
	Dialogs::GuardEnterKey(this);
}

BlendOutput *BlendEditor::Current()
{
	const int index = view->Selected();
	if (index < 0 || size_t(index) >= config.outputs.size())
		return nullptr;
	return &config.outputs[size_t(index)];
}

void BlendEditor::RefreshList()
{
	updating = true;
	list->clear();
	for (size_t i = 0; i < config.outputs.size(); i++) {
		const BlendOutput &o = config.outputs[i];
		QString text = QString::fromStdString(o.name);
		if (QScreen *screen = Dialogs::FindScreen(o.screen))
			text += "  —  " + screen->name();
		auto *item = new QListWidgetItem(text, list);
		QPixmap swatch(12, 12);
		swatch.fill(OutputColor(int(i)));
		item->setIcon(QIcon(swatch));
		if (!o.placed) {
			QFont f = item->font();
			f.setItalic(true);
			item->setFont(f);
			item->setToolTip(Tr("MDS.BlendNotPlaced"));
		}
	}
	list->setCurrentRow(view->Selected());
	updating = false;
}

void BlendEditor::RefreshParams()
{
	updating = true;
	BlendOutput *o = Current();
	params->setEnabled(o != nullptr);
	removeButton->setEnabled(o != nullptr);

	const bool hasLayout = config.HasLayout() && !config.outputs.empty();
	layoutW->setEnabled(hasLayout);
	layoutH->setEnabled(hasLayout);
	layoutW->setValue(int(config.width));
	layoutH->setValue(int(config.height));
	testPattern->setChecked(config.testPattern);
	hint->setVisible(!config.outputs.empty() && !config.Active());

	if (o) {
		outName->setText(QString::fromStdString(o->name));
		int screenIndex = outScreen->findData(QString::fromStdString(o->screen));
		outScreen->setCurrentIndex(screenIndex >= 0 ? screenIndex : 0);
		for (QSpinBox *s : {posX, posY, sizeW, sizeH})
			s->setEnabled(o->placed);
		posX->setValue(int(std::lround(o->x)));
		posY->setValue(int(std::lround(o->y)));
		sizeW->setValue(int(std::lround(o->w)));
		sizeH->setValue(int(std::lround(o->h)));
		lockAspect->setEnabled(o->placed);
		lockAspect->setChecked(o->lockAspect);
		endX->setText(o->placed ? QString::number(std::lround(o->x + o->w)) : QString());
		endY->setText(o->placed ? QString::number(std::lround(o->y + o->h)) : QString());
		autoBlend->setChecked(o->autoBlend);
		const double values[4] = {o->left, o->right, o->top, o->bottom};
		for (int i = 0; i < 4; i++) {
			edge[i]->setValue(int(std::lround(values[i])));
			edge[i]->setEnabled(!o->autoBlend);
		}
		gamma->setValue(o->gamma);
		curve->setValue(o->curve);
		blackLevel->setValue(o->blackLevel * 100.0);
		for (int i = 0; i < 8; i++)
			pin[i]->setValue(int(std::lround(o->pin[i])));
	}
	updating = false;
}

void BlendEditor::RefreshButtons()
{
	if (saveButton)
		saveButton->setEnabled(Dirty());
	if (createButton)
		createButton->setEnabled(config.Active());
}

void BlendEditor::ApplyParam(const std::function<void(BlendOutput &)> &change)
{
	if (updating)
		return;
	BlendOutput *o = Current();
	if (!o)
		return;
	PushUndo();
	change(*o);
	if (o->placed)
		config.ClampInside(*o);
	Changed();
}

void BlendEditor::Changed()
{
	config.ComputeAutoBlend();
	if (dest)
		dest->SetBlendPreview(&config);
	view->update();
	RefreshList();
	RefreshParams();
	RefreshButtons();
}

void BlendEditor::PushUndo()
{
	undoStack.push_back(config);
	if (undoStack.size() > 100)
		undoStack.erase(undoStack.begin());
}

void BlendEditor::Undo()
{
	if (undoStack.empty())
		return;
	config = undoStack.back();
	undoStack.pop_back();
	if (view->Selected() >= int(config.outputs.size()))
		view->SetSelected(int(config.outputs.size()) - 1);
	Changed();
}

std::string BlendEditor::DestinationName() const
{
	const std::string name = nameEdit->text().trimmed().toStdString();
	return name.empty() ? defaultName : name;
}

bool BlendEditor::AddToExisting() const
{
	return addToExisting && addToExisting->isChecked();
}

std::string BlendEditor::State() const
{
	return DestinationName() + "\n" + config.ToJson();
}

bool BlendEditor::Dirty() const
{
	return State() != savedState;
}

void BlendEditor::Save()
{
	if (!dest || !Dirty())
		return;
	if (onSave)
		onSave(this);
	nameEdit->setText(QString::fromStdString(dest->Name()));
	savedState = State();
	RefreshButtons();
}

void BlendEditor::Create()
{
	if (!config.Active())
		return;
	if (onCreate && onCreate(this))
		QDialog::accept();
}

void BlendEditor::reject()
{
	if (dest && Dirty()) {
		QMessageBox box(this);
		box.setIcon(QMessageBox::Question);
		box.setWindowTitle(Tr("MDS.OutputBlending"));
		box.setText(Tr("MDS.UnsavedChanges"));
		QPushButton *save = box.addButton(Tr("MDS.SaveChanges"), QMessageBox::AcceptRole);
		box.addButton(Tr("MDS.DiscardChanges"), QMessageBox::DestructiveRole);
		box.setDefaultButton(save);
		box.exec();
		if (box.clickedButton() == save)
			Save();
	}
	if (dest)
		dest->SetBlendPreview(nullptr);
	QDialog::reject();
}

void BlendEditor::AddOutput()
{
	PushUndo();
	BlendOutput o;
	o.name = QString("%1 %2").arg(Tr("MDS.BlendOutputName")).arg(config.outputs.size() + 1).toStdString();

	/* Premier écran pas encore utilisé. */
	for (QScreen *screen : QGuiApplication::screens()) {
		const std::string name = screen->name().toStdString();
		const bool used = std::any_of(config.outputs.begin(), config.outputs.end(),
					      [&](const BlendOutput &other) { return other.screen == name; });
		if (!used) {
			o.screen = name;
			break;
		}
	}

	/* Premier ajout : le layout prend la taille de l'écran. */
	if (!config.HasLayout()) {
		uint32_t w, h;
		NativeSize(o.screen, w, h);
		config.width = w;
		config.height = h;
	}
	config.outputs.push_back(o);
	view->SetSelected(int(config.outputs.size()) - 1);
	Changed();
}

void BlendEditor::RemoveOutput()
{
	const int index = view->Selected();
	if (index < 0 || size_t(index) >= config.outputs.size())
		return;
	PushUndo();
	config.outputs.erase(config.outputs.begin() + index);
	view->SetSelected(std::min(index, int(config.outputs.size()) - 1));
	Changed();
}

void BlendEditor::PlaceOutput(int index, double x, double y)
{
	if (index < 0 || size_t(index) >= config.outputs.size() || !config.HasLayout())
		return;
	PushUndo();
	BlendOutput &o = config.outputs[size_t(index)];
	uint32_t w, h;
	NativeSize(o.screen, w, h);
	config.Place(o, x, y, w, h);
	view->SetSelected(index);
	Changed();
}

void BlendEditor::SetLayoutSize(uint32_t w, uint32_t h)
{
	if (updating || (w == config.width && h == config.height))
		return;
	PushUndo();
	config.Resize(w, h);
	Changed();
}
