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

#include "cues-dock.hpp"
#include "cues.hpp"
#include "dialogs.hpp"
#include "dock-style.hpp"
#include "manager.hpp"
#include "reorder-list.hpp"
#include "text.hpp"

#include <obs-frontend-api.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStyle>
#include <QTableWidget>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <set>

namespace {

constexpr int ROW_CUE = 1;
constexpr int ROW_STEP = 2;

QString SceneNameOf(const std::string &uuid)
{
	OBSSourceAutoRelease s = obs_get_source_by_uuid(uuid.c_str());
	return s ? QString::fromUtf8(obs_source_get_name(s)) : Tr("MDS.CueMissingScene");
}

QString TransitionLabel(const CueStep &step)
{
	if (step.transition == "cut")
		return Tr("MDS.Cut");
	QString name = Tr("MDS.CueDefault");
	if (!step.transition.empty()) {
		if (OBSSource tr = Manager::FindTransition(step.transition))
			name = QString::fromUtf8(obs_source_get_name(tr));
	}
	if (step.durationMs > 0)
		name += QString(" %1 ms").arg(step.durationMs);
	return name;
}

QString Seconds(int ms)
{
	return QString::number(ms / 1000.0, 'f', 1) + " s";
}

QString StepText(size_t index, const CueStep &step)
{
	const QString after = step.nextOnMediaEnd
				      ? Tr("MDS.CueMediaEndShort")
				      : QString("%1 %2").arg(Tr("MDS.CuePostWaitShort")).arg(Seconds(step.postWaitMs));
	/* Première scène : transition de ce qui lance la cue. */
	const QString transition = index == 0 ? Tr("MDS.CueFirstTransitionShort") : TransitionLabel(step);
	return QString("%1. %2   ·   %3 %4 · %5 · %6")
		.arg(index + 1)
		.arg(SceneNameOf(step.scene))
		.arg(Tr("MDS.CuePreWaitShort"))
		.arg(Seconds(step.preWaitMs))
		.arg(transition)
		.arg(after);
}

/* ---- Fenêtre d'édition d'une cue ---- */

enum Column { COL_SCENE, COL_PRE, COL_TRANSITION, COL_DURATION, COL_MEDIA, COL_POST, COL_COUNT };

QComboBox *SceneCombo(const std::string &current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	for (auto &s : Manager::MainScenes()) {
		combo->addItem(QString::fromUtf8(obs_source_get_name(s)), QString::fromUtf8(obs_source_get_uuid(s)));
		if (current == obs_source_get_uuid(s))
			combo->setCurrentIndex(combo->count() - 1);
	}
	return combo;
}

/* « Default » puis toutes les transitions d'OBS (UserRole + 1 : durée
 * fixe, comme Cut). */
QComboBox *TransitionCombo(const std::string &current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	combo->addItem(Tr("MDS.CueDefault"), QString());
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		const bool cut = strcmp(obs_source_get_id(tr), "cut_transition") == 0;
		combo->addItem(QString::fromUtf8(obs_source_get_name(tr)),
			       cut ? QString("cut") : QString::fromUtf8(obs_source_get_uuid(tr)));
		combo->setItemData(combo->count() - 1, cut || obs_transition_fixed(tr), Qt::UserRole + 1);
	}
	obs_frontend_source_list_free(&list);
	const int index = combo->findData(QString::fromStdString(current));
	combo->setCurrentIndex(index >= 0 ? index : 0);
	return combo;
}

/* Durée selon la transition, comme le dock d'OBS : Default -> durée par
 * défaut (non modifiable) ; transition fixe (Cut) -> grisée ; autre ->
 * modifiable (300 ms proposés). */
void UpdateDurationField(QComboBox *transition, QSpinBox *duration)
{
	const QString data = transition->currentData().toString();
	const bool fixed = transition->currentData(Qt::UserRole + 1).toBool();
	if (data.isEmpty()) {
		duration->setSpecialValueText(Tr("MDS.CueDefault"));
		duration->setValue(0);
		duration->setEnabled(false);
	} else if (fixed) {
		duration->setSpecialValueText(QString());
		if (duration->value() == 0)
			duration->setValue(obs_frontend_get_transition_duration());
		duration->setEnabled(false);
	} else {
		duration->setSpecialValueText(QString());
		if (duration->value() == 0)
			duration->setValue(300);
		duration->setEnabled(true);
	}
}

QDoubleSpinBox *SecondsSpin(int ms, QWidget *parent)
{
	auto *spin = new QDoubleSpinBox(parent);
	spin->setRange(0.0, 3600.0);
	spin->setDecimals(1);
	spin->setSingleStep(0.5);
	spin->setSuffix(" s");
	spin->setValue(ms / 1000.0);
	return spin;
}

class CueEditor : public QDialog {
public:
	CueEditor(QWidget *parent, const Cue &cue) : QDialog(parent)
	{
		setWindowTitle(Tr("MDS.CueEditTitle"));
		resize(860, 460);
		auto *root = new QVBoxLayout(this);
		auto *form = new QFormLayout();
		name = new QLineEdit(QString::fromStdString(cue.name), this);
		form->addRow(Tr("MDS.Name"), name);
		root->addLayout(form);

		table = new QTableWidget(0, COL_COUNT, this);
		table->setHorizontalHeaderLabels({Tr("MDS.CueScene"), Tr("MDS.CuePreWait"), Tr("MDS.CueTransition"),
						  Tr("MDS.CueDuration"), Tr("MDS.CueMediaEnd"), Tr("MDS.CuePostWait")});
		table->horizontalHeader()->setSectionResizeMode(COL_SCENE, QHeaderView::Stretch);
		table->horizontalHeader()->setSectionResizeMode(COL_TRANSITION, QHeaderView::Stretch);
		table->verticalHeader()->setVisible(true);
		table->setSelectionBehavior(QAbstractItemView::SelectRows);
		table->setSelectionMode(QAbstractItemView::SingleSelection);
		root->addWidget(table, 1);
		Populate(cue.steps);

		auto *rowButtons = new QHBoxLayout();
		auto *add = new QPushButton(Tr("MDS.CueAddScene"), this);
		auto *remove = new QPushButton(Tr("MDS.CueRemoveScene"), this);
		auto *up = new QPushButton(Tr("MDS.MoveUp"), this);
		auto *down = new QPushButton(Tr("MDS.MoveDown"), this);
		rowButtons->addWidget(add);
		rowButtons->addWidget(remove);
		rowButtons->addStretch(1);
		rowButtons->addWidget(up);
		rowButtons->addWidget(down);
		root->addLayout(rowButtons);
		connect(add, &QPushButton::clicked, this, [this]() {
			auto steps = Collect();
			CueStep step;
			auto scenes = Manager::MainScenes();
			if (!scenes.empty())
				step.scene = obs_source_get_uuid(scenes.front());
			steps.push_back(step);
			Populate(steps);
			table->selectRow(int(steps.size()) - 1);
		});
		connect(remove, &QPushButton::clicked, this, [this]() {
			const int row = table->currentRow();
			auto steps = Collect();
			if (row < 0 || size_t(row) >= steps.size())
				return;
			steps.erase(steps.begin() + row);
			Populate(steps);
		});
		auto move = [this](int delta) {
			const int row = table->currentRow();
			auto steps = Collect();
			const int target = row + delta;
			if (row < 0 || target < 0 || size_t(target) >= steps.size())
				return;
			std::swap(steps[size_t(row)], steps[size_t(target)]);
			Populate(steps);
			table->selectRow(target);
		};
		connect(up, &QPushButton::clicked, this, [move]() { move(-1); });
		connect(down, &QPushButton::clicked, this, [move]() { move(1); });

		auto *note = new QLabel(Tr("MDS.CueEditNote"), this);
		note->setWordWrap(true);
		root->addWidget(note);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
		buttons->button(QDialogButtonBox::Ok)->setText(Tr("MDS.Save"));
		buttons->button(QDialogButtonBox::Cancel)->setText(Tr("MDS.Cancel"));
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		root->addWidget(buttons);
		Dialogs::GuardEnterKey(this);
	}

	QString Name() const { return name->text().trimmed(); }

	std::vector<CueStep> Collect() const
	{
		std::vector<CueStep> steps;
		for (int r = 0; r < table->rowCount(); r++) {
			CueStep s;
			auto *scene = static_cast<QComboBox *>(table->cellWidget(r, COL_SCENE));
			auto *pre = static_cast<QDoubleSpinBox *>(table->cellWidget(r, COL_PRE));
			auto *tr = static_cast<QComboBox *>(table->cellWidget(r, COL_TRANSITION));
			auto *dur = static_cast<QSpinBox *>(table->cellWidget(r, COL_DURATION));
			auto *post = static_cast<QDoubleSpinBox *>(table->cellWidget(r, COL_POST));
			auto *media = table->cellWidget(r, COL_MEDIA)->findChild<QCheckBox *>();
			s.scene = scene->currentData().toString().toStdString();
			s.preWaitMs = int(pre->value() * 1000.0 + 0.5);
			s.transition = tr->currentData().toString().toStdString();
			s.durationMs = dur->isEnabled() ? dur->value() : 0;
			s.nextOnMediaEnd = media && media->isChecked();
			s.postWaitMs = int(post->value() * 1000.0 + 0.5);
			steps.push_back(s);
		}
		return steps;
	}

private:
	void Populate(const std::vector<CueStep> &steps)
	{
		table->setRowCount(0);
		table->setRowCount(int(steps.size()));
		for (size_t i = 0; i < steps.size(); i++) {
			const CueStep &s = steps[i];
			const int r = int(i);
			table->setCellWidget(r, COL_SCENE, SceneCombo(s.scene, table));
			table->setCellWidget(r, COL_PRE, SecondsSpin(s.preWaitMs, table));
			QComboBox *tr = TransitionCombo(s.transition, table);
			table->setCellWidget(r, COL_TRANSITION, tr);
			auto *dur = new QSpinBox(table);
			dur->setRange(0, 20000);
			dur->setSingleStep(50);
			dur->setSuffix(" ms");
			dur->setValue(s.durationMs);
			table->setCellWidget(r, COL_DURATION, dur);
			UpdateDurationField(tr, dur);
			connect(tr, &QComboBox::currentIndexChanged, dur,
				[tr, dur]() { UpdateDurationField(tr, dur); });
			/* Première scène : pas de transition propre. Elle suit ce qui
			 * lance la cue (Transition, transition rapide, lecture). */
			if (i == 0) {
				tr->clear();
				tr->addItem(Tr("MDS.CueFirstTransition"), QString());
				tr->setEnabled(false);
				tr->setToolTip(Tr("MDS.CueFirstTransitionTip"));
				dur->setEnabled(false);
				dur->setToolTip(Tr("MDS.CueFirstTransitionTip"));
			}

			/* Fin du média -> scène suivante (remplace le post-délai). */
			auto *mediaCell = new QWidget(table);
			auto *mediaLayout = new QHBoxLayout(mediaCell);
			mediaLayout->setContentsMargins(0, 0, 0, 0);
			auto *media = new QCheckBox(mediaCell);
			media->setChecked(s.nextOnMediaEnd);
			media->setToolTip(Tr("MDS.CueMediaEndTip"));
			mediaLayout->addWidget(media, 0, Qt::AlignCenter);
			table->setCellWidget(r, COL_MEDIA, mediaCell);
			QDoubleSpinBox *post = SecondsSpin(s.postWaitMs, table);
			post->setEnabled(!s.nextOnMediaEnd);
			connect(media, &QCheckBox::toggled, post, [post](bool on) { post->setEnabled(!on); });
			table->setCellWidget(r, COL_POST, post);
		}
		table->resizeColumnToContents(COL_PRE);
		table->resizeColumnToContents(COL_DURATION);
		table->resizeColumnToContents(COL_POST);
		table->resizeColumnToContents(COL_MEDIA);
	}

	QLineEdit *name = nullptr;
	QTableWidget *table = nullptr;
};

bool EditCueDialog(QWidget *parent, Cue &cue)
{
	CueEditor editor(parent, cue);
	if (editor.exec() != QDialog::Accepted)
		return false;
	const QString n = editor.Name();
	if (!n.isEmpty())
		cue.name = n.toStdString();
	cue.steps = editor.Collect();
	return true;
}

} // namespace

/* ------------------------------------------------------------------ */

CuesPanel::CuesPanel(QWidget *parent) : QWidget(parent)
{
	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);

	tree = new OrganizerTree(this);
	tree->setContextMenuPolicy(Qt::CustomContextMenu);
	DockStyle::StyleTree(tree);
	tree->onDelete = [this]() {
		RemoveSelected();
	};
	tree->onMove = [this](QTreeWidgetItem *target, int position) {
		MoveRows(target, position);
	};

	CueEngine &engine = CueEngine::Get();
	/* Cliquer sur une cue l'arme : sa première scène passe en Aperçu. */
	connect(tree, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (refreshing)
			return;
		QTreeWidgetItem *row = tree->currentItem();
		if (!row || !row->isSelected() || !Manager::Get().CuesEnabled())
			return;
		const std::string id = CurrentCueId();
		if (!id.empty() && id != CueEngine::Get().ArmedId())
			CueEngine::Get().Arm(id);
	});
	/* Clic sur la cue déjà sélectionnée (l'Aperçu a changé entre-temps) :
	 * elle repasse en Aperçu. */
	connect(tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *) {
		if (refreshing || !Manager::Get().CuesEnabled())
			return;
		const std::string id = CurrentCueId();
		if (!id.empty() && id != CueEngine::Get().ArmedId())
			CueEngine::Get().Arm(id);
	});
	connect(tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *) { EditCue(CurrentCueId()); });
	connect(tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		QMenu menu(this);
		connect(menu.addAction(Tr("MDS.CueNew")), &QAction::triggered, this, &CuesPanel::NewCue);
		const std::string id = CurrentCueId();
		if (!id.empty()) {
			connect(menu.addAction(Tr("MDS.CueEdit")), &QAction::triggered, this,
				[this, id]() { EditCue(id); });
			connect(menu.addAction(Tr("MDS.CueGo")), &QAction::triggered, this,
				[id]() { CueEngine::Get().Go(id, false); });
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.CueRemove")), &QAction::triggered, this,
				&CuesPanel::RemoveSelected);
		}
		if (CueEngine::Get().Running()) {
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.CueStop")), &QAction::triggered, this,
				[]() { CueEngine::Get().StopWithFade(); });
		}
		menu.exec(tree->viewport()->mapToGlobal(pos));
	});
	connect(&engine, &CueEngine::cuesChanged, this, &CuesPanel::Refresh);
	connect(&engine, &CueEngine::stateChanged, this, &CuesPanel::UpdateState);
	connect(&engine, &CueEngine::progressChanged, this, &CuesPanel::UpdateState);
	connect(&Manager::Get(), &Manager::scenesChanged, this, &CuesPanel::Refresh);

	QStyle *st = style();
	QToolBar *bar = DockStyle::Toolbar(this);
	DockStyle::AddAction(bar, "icon-plus", DockStyle::PlusIcon(), Tr("MDS.CueNew"), [this]() { NewCue(); });
	DockStyle::AddAction(bar, "icon-trash", st->standardIcon(QStyle::SP_TrashIcon), Tr("MDS.CueRemove"),
			     [this]() { RemoveSelected(); });
	DockStyle::AddAction(bar, "icon-gear", st->standardIcon(QStyle::SP_FileDialogDetailedView), Tr("MDS.CueEdit"),
			     [this]() { EditCue(CurrentCueId()); });
	bar->addSeparator();
	DockStyle::AddAction(bar, "icon-media-play", st->standardIcon(QStyle::SP_MediaPlay), Tr("MDS.CueGo"), [this]() {
		const std::string id = CurrentCueId();
		if (!id.empty())
			CueEngine::Get().Go(id, false);
	});
	DockStyle::AddAction(bar, "icon-media-stop", st->standardIcon(QStyle::SP_MediaStop), Tr("MDS.CueStop"),
			     []() { CueEngine::Get().StopWithFade(); });
	bar->addSeparator();
	DockStyle::AddAction(bar, "icon-up", st->standardIcon(QStyle::SP_ArrowUp), Tr("MDS.MoveUp"),
			     [this]() { MoveCue(-1); });
	DockStyle::AddAction(bar, "icon-down", st->standardIcon(QStyle::SP_ArrowDown), Tr("MDS.MoveDown"),
			     [this]() { MoveCue(1); });

	layout->addWidget(tree, 1);
	layout->addWidget(bar);
	Refresh();
}

std::string CuesPanel::CurrentCueId() const
{
	QTreeWidgetItem *row = tree->currentItem();
	if (!row)
		return {};
	if (row->parent())
		row = row->parent();
	return row->data(0, Qt::UserRole).toString().toStdString();
}

void CuesPanel::Refresh()
{
	const std::string current = CurrentCueId();
	std::set<std::string> expanded;
	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		if (tree->topLevelItem(i)->isExpanded())
			expanded.insert(tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString());
	}

	refreshing = true;
	const QSignalBlocker blocker(tree);
	tree->clear();
	for (const Cue &cue : CueEngine::Get().Cues()) {
		auto *row = new QTreeWidgetItem();
		row->setText(0, QString::fromStdString(cue.name));
		row->setData(0, Qt::UserRole, QString::fromStdString(cue.id));
		row->setData(0, Qt::UserRole + 1, ROW_CUE);
		row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
		row->setData(0, OrganizerTree::ContainerRole, true);
		for (size_t i = 0; i < cue.steps.size(); i++) {
			auto *child = new QTreeWidgetItem();
			child->setText(0, StepText(i, cue.steps[i]));
			child->setToolTip(0, child->text(0));
			child->setData(0, Qt::UserRole + 1, ROW_STEP);
			child->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
			row->addChild(child);
		}
		tree->addTopLevelItem(row);
		row->setExpanded(expanded.count(cue.id) > 0);
		if (cue.id == current)
			tree->setCurrentItem(row);
	}
	refreshing = false;
	UpdateState();
}

/* Cue armée (en Aperçu) en gras, cue et étape en cours marquées « ▶ ». */
void CuesPanel::UpdateState()
{
	const CueEngine &engine = CueEngine::Get();
	const QSignalBlocker blocker(tree);
	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		QTreeWidgetItem *row = tree->topLevelItem(i);
		const std::string id = row->data(0, Qt::UserRole).toString().toStdString();
		const Cue *cue = engine.Find(id);
		if (!cue)
			continue;
		const bool running = engine.RunningId() == id;
		if (running && !row->isExpanded())
			row->setExpanded(true);
		QFont font = row->font(0);
		font.setBold(engine.ArmedId() == id || running);
		row->setFont(0, font);
		row->setText(0, (running ? QString("▶ ") : QString()) + QString::fromStdString(cue->name));
		for (int k = 0; k < row->childCount() && size_t(k) < cue->steps.size(); k++) {
			const bool current = running && engine.RunningStep() == k;
			QFont f = row->child(k)->font(0);
			f.setBold(current);
			row->child(k)->setFont(0, f);
			QString text = StepText(size_t(k), cue->steps[size_t(k)]);
			/* Étape en cours : son état (attente, média 0:12 / 0:45). */
			if (current) {
				const QString status = engine.StatusText();
				text = "▶ " + text + (status.isEmpty() ? QString() : "   " + status);
			}
			if (row->child(k)->text(0) != text)
				row->child(k)->setText(0, text);
		}
	}
}

void CuesPanel::NewCue()
{
	Cue cue;
	cue.id = CueEngine::NewId();
	cue.name = QString("%1 %2").arg(Tr("MDS.Cue")).arg(CueEngine::Get().Cues().size() + 1).toStdString();
	/* Les scènes sélectionnées dans le dock Scènes servent de départ. */
	if (selectedScenes) {
		for (const std::string &uuid : selectedScenes()) {
			CueStep step;
			step.scene = uuid;
			cue.steps.push_back(step);
		}
	}
	if (!EditCueDialog(this, cue))
		return;
	auto cues = CueEngine::Get().Cues();
	cues.push_back(cue);
	CueEngine::Get().SetCues(cues);
}

void CuesPanel::EditCue(const std::string &id)
{
	const Cue *found = CueEngine::Get().Find(id);
	if (!found)
		return;
	Cue cue = *found;
	if (!EditCueDialog(this, cue))
		return;
	auto cues = CueEngine::Get().Cues();
	for (auto &c : cues) {
		if (c.id == id)
			c = cue;
	}
	CueEngine::Get().SetCues(cues);
}

void CuesPanel::RemoveSelected()
{
	std::set<std::string> ids;
	for (QTreeWidgetItem *row : tree->selectedItems()) {
		QTreeWidgetItem *top = row->parent() ? row->parent() : row;
		ids.insert(top->data(0, Qt::UserRole).toString().toStdString());
	}
	if (ids.empty())
		return;
	const QString question = ids.size() == 1 ? Tr("MDS.CueConfirmRemove")
						 : Tr("MDS.CueConfirmRemoveMany").arg(int(ids.size()));
	if (QMessageBox::question(this, Tr("MDS.CueRemove"), question) != QMessageBox::Yes)
		return;
	auto cues = CueEngine::Get().Cues();
	if (ids.count(CueEngine::Get().RunningId()))
		CueEngine::Get().Stop();
	cues.erase(std::remove_if(cues.begin(), cues.end(), [&](const Cue &c) { return ids.count(c.id) > 0; }),
		   cues.end());
	CueEngine::Get().SetCues(cues);
}

void CuesPanel::MoveCue(int delta)
{
	const std::string id = CurrentCueId();
	auto cues = CueEngine::Get().Cues();
	for (size_t i = 0; i < cues.size(); i++) {
		if (cues[i].id != id)
			continue;
		const long target = long(i) + delta;
		if (target < 0 || size_t(target) >= cues.size())
			return;
		std::swap(cues[i], cues[size_t(target)]);
		CueEngine::Get().SetCues(cues);
		return;
	}
}

/* Glisser-déposer : seules les cues se déplacent (pas leurs étapes). */
void CuesPanel::MoveRows(QTreeWidgetItem *target, int position)
{
	std::vector<std::string> dragged;
	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		if (tree->topLevelItem(i)->isSelected())
			dragged.push_back(tree->topLevelItem(i)->data(0, Qt::UserRole).toString().toStdString());
	}
	if (dragged.empty())
		return;
	auto cues = CueEngine::Get().Cues();
	std::vector<Cue> moving;
	std::vector<Cue> rest;
	for (const Cue &c : cues) {
		if (std::find(dragged.begin(), dragged.end(), c.id) != dragged.end())
			moving.push_back(c);
		else
			rest.push_back(c);
	}
	size_t insertAt = rest.size();
	if (target && position != OrganizerTree::DropViewport) {
		QTreeWidgetItem *top = target->parent() ? target->parent() : target;
		const std::string anchor = top->data(0, Qt::UserRole).toString().toStdString();
		const bool below = position == OrganizerTree::DropBelow || target->parent() != nullptr;
		for (size_t i = 0; i < rest.size(); i++) {
			if (rest[i].id == anchor)
				insertAt = i + (below ? 1 : 0);
		}
		if (std::find(dragged.begin(), dragged.end(), anchor) != dragged.end())
			return;
	}
	rest.insert(rest.begin() + long(insertAt), moving.begin(), moving.end());
	CueEngine::Get().SetCues(rest);
}
