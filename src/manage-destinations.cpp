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

#include "manage-destinations.hpp"
#include "destination.hpp"
#include "dialogs.hpp"
#include "manager.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>

#include <set>

ManageDestinationsDialog::ManageDestinationsDialog(QWidget *parent) : QDialog(parent)
{
	setWindowTitle(Tr("MDS.ManageDestinations"));
	resize(820, 560);
	auto *root = new QVBoxLayout(this);
	auto *splitter = new QSplitter(Qt::Horizontal, this);
	root->addWidget(splitter, 1);

	/* Liste des destinations. */
	auto *left = new QWidget(splitter);
	auto *leftLayout = new QVBoxLayout(left);
	leftLayout->setContentsMargins(0, 0, 0, 0);
	list = new QListWidget(left);
	leftLayout->addWidget(list, 1);
	auto *newButton = new QPushButton(Tr("MDS.NewDestinationMenu"), left);
	leftLayout->addWidget(newButton);
	connect(newButton, &QPushButton::clicked, this, [this]() {
		if (onNew)
			onNew();
		Refresh();
		list->setCurrentRow(list->count() - 1);
	});

	/* Détails de la destination sélectionnée. */
	details = new QWidget(splitter);
	auto *detailsLayout = new QVBoxLayout(details);
	detailsLayout->setContentsMargins(8, 0, 0, 0);
	auto *form = new QFormLayout();
	nameEdit = new QLineEdit(details);
	nameFixed = new QLabel(details);
	auto *nameRow = new QHBoxLayout();
	nameRow->addWidget(nameEdit);
	nameRow->addWidget(nameFixed);
	form->addRow(Tr("MDS.Name"), nameRow);

	widthSpin = new QSpinBox(details);
	widthSpin->setRange(16, 16384);
	heightSpin = new QSpinBox(details);
	heightSpin->setRange(16, 16384);
	auto *resRow = new QHBoxLayout();
	resRow->addWidget(widthSpin);
	resRow->addWidget(new QLabel("×", details));
	resRow->addWidget(heightSpin);
	resRow->addStretch(1);
	form->addRow(Tr("MDS.Resolution"), resRow);
	resolutionNote = new QLabel(details);
	resolutionNote->setWordWrap(true);
	form->addRow(resolutionNote);

	screenCombo = Dialogs::ScreenCombo("", details);
	form->addRow(Tr("MDS.OutputScreen"), screenCombo);
	lockBox = new QCheckBox(Tr("MDS.LockedOption"), details);
	lockBox->setToolTip(Tr("MDS.LockTip"));
	form->addRow(lockBox);
	detailsLayout->addLayout(form);

	auto *scenesGroup = new QGroupBox(Tr("MDS.ShownInScenes"), details);
	auto *scenesGroupLayout = new QVBoxLayout(scenesGroup);
	scenesSummary = new QLabel(scenesGroup);
	scenesGroupLayout->addWidget(scenesSummary);
	auto *scroll = new QScrollArea(scenesGroup);
	scroll->setWidgetResizable(true);
	scenesBox = new QWidget(scroll);
	scenesLayout = new QVBoxLayout(scenesBox);
	scenesLayout->setContentsMargins(4, 4, 4, 4);
	scroll->setWidget(scenesBox);
	scenesGroupLayout->addWidget(scroll, 1);
	detailsLayout->addWidget(scenesGroup, 1);

	auto *actions = new QHBoxLayout();
	blendingButton = new QPushButton(Tr("MDS.OutputBlendingMenu").replace("&", "&&"), details);
	projectButton = new QPushButton(Tr("MDS.OpenFullscreen"), details);
	deleteButton = new QPushButton(details);
	applyButton = new QPushButton(Tr("MDS.Apply"), details);
	actions->addWidget(blendingButton);
	actions->addWidget(projectButton);
	actions->addWidget(deleteButton);
	actions->addStretch(1);
	actions->addWidget(applyButton);
	detailsLayout->addLayout(actions);

	splitter->setStretchFactor(0, 0);
	splitter->setStretchFactor(1, 1);
	splitter->setSizes({240, 580});

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
	buttons->button(QDialogButtonBox::Close)->setText(Tr("MDS.Close"));
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
	root->addWidget(buttons);

	connect(list, &QListWidget::currentRowChanged, this, [this]() { LoadCurrent(); });
	for (QLineEdit *e : {nameEdit})
		connect(e, &QLineEdit::textEdited, this, &ManageDestinationsDialog::MarkDirty);
	connect(widthSpin, &QSpinBox::valueChanged, this, &ManageDestinationsDialog::MarkDirty);
	connect(heightSpin, &QSpinBox::valueChanged, this, &ManageDestinationsDialog::MarkDirty);
	connect(screenCombo, &QComboBox::currentIndexChanged, this, &ManageDestinationsDialog::MarkDirty);
	connect(lockBox, &QCheckBox::toggled, this, &ManageDestinationsDialog::MarkDirty);
	connect(applyButton, &QPushButton::clicked, this, &ManageDestinationsDialog::Apply);
	connect(blendingButton, &QPushButton::clicked, this, [this]() {
		if (Destination *d = Current(); d && onBlending)
			onBlending(d);
		Refresh();
	});
	connect(projectButton, &QPushButton::clicked, this, [this]() {
		if (Destination *d = Current(); d && onProject)
			onProject(d);
	});
	connect(deleteButton, &QPushButton::clicked, this, [this]() {
		if (Destination *d = Current(); d && onDelete)
			onDelete(d);
		Refresh();
	});

	Refresh();
	list->setCurrentRow(0);
	Dialogs::GuardEnterKey(this);
}

void ManageDestinationsDialog::Refresh()
{
	const int row = std::max(0, list->currentRow());
	loading = true;
	list->clear();
	for (Destination *d : Manager::Get().Destinations()) {
		QString text =
			QString("%1\n%2×%3").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height());
		if (d->Disabled())
			text += "  — " + Tr("MDS.DisabledSuffix");
		auto *item = new QListWidgetItem(text, list);
		item->setData(Qt::UserRole, QVariant::fromValue(static_cast<void *>(d)));
	}
	loading = false;
	list->setCurrentRow(std::min(row, list->count() - 1));
	LoadCurrent();
}

Destination *ManageDestinationsDialog::Current() const
{
	QListWidgetItem *item = list->currentItem();
	if (!item)
		return nullptr;
	auto *d = static_cast<Destination *>(item->data(Qt::UserRole).value<void *>());
	for (Destination *x : Manager::Get().Destinations()) {
		if (x == d)
			return d;
	}
	return nullptr;
}

void ManageDestinationsDialog::LoadCurrent()
{
	if (loading)
		return;
	Destination *d = Current();
	details->setEnabled(d != nullptr);
	if (!d)
		return;

	loading = true;
	const bool isMain = d->IsMain();
	const bool blended = d->IsBlended();
	nameEdit->setVisible(!isMain);
	nameFixed->setVisible(isMain);
	nameEdit->setText(QString::fromStdString(d->Name()));
	nameFixed->setText(QString::fromStdString(d->Name()));
	widthSpin->setValue(int(d->Width()));
	heightSpin->setValue(int(d->Height()));
	widthSpin->setEnabled(!isMain && !blended);
	heightSpin->setEnabled(!isMain && !blended);
	resolutionNote->setText(isMain ? Tr("MDS.MainInfo") : blended ? Tr("MDS.BlendResolutionNote") : QString());
	resolutionNote->setVisible(isMain || blended);
	int screen = screenCombo->findData(QString::fromStdString(d->OutputScreen()));
	screenCombo->setCurrentIndex(screen >= 0 ? screen : 0);
	lockBox->setChecked(d->Locked());
	blendingButton->setVisible(!isMain);
	deleteButton->setText(isMain ? (d->Disabled() ? Tr("MDS.ActivateDestination") : Tr("MDS.DeactivateAndHide"))
				     : Tr("MDS.Delete"));
	deleteButton->setText(deleteButton->text().replace("&", "&&"));

	/* Scènes : une case par scène, cochée si la destination y apparaît. */
	while (QLayoutItem *item = scenesLayout->takeAt(0)) {
		delete item->widget();
		delete item;
	}
	int covered = 0;
	const auto scenes = Manager::MainScenes();
	for (auto &scene : scenes) {
		auto *box = new QCheckBox(QString::fromUtf8(obs_source_get_name(scene)), scenesBox);
		const bool on = d->Covers(scene);
		covered += on ? 1 : 0;
		box->setChecked(on);
		box->setProperty("sceneUuid", QString::fromUtf8(obs_source_get_uuid(scene)));
		box->setEnabled(!d->Disabled());
		connect(box, &QCheckBox::toggled, this, &ManageDestinationsDialog::MarkDirty);
		scenesLayout->addWidget(box);
	}
	scenesLayout->addStretch(1);
	scenesSummary->setText(Tr("MDS.ShownInScenesCount").arg(covered).arg(int(scenes.size())));
	applyButton->setEnabled(false);
	loading = false;
}

void ManageDestinationsDialog::MarkDirty()
{
	if (!loading)
		applyButton->setEnabled(true);
}

void ManageDestinationsDialog::Apply()
{
	Destination *d = Current();
	if (!d)
		return;
	Manager &m = Manager::Get();

	/* Scènes retirées : leur placement sur cette destination est perdu. */
	std::vector<std::pair<OBSSource, bool>> changes;
	bool removing = false;
	for (int i = 0; i < scenesLayout->count(); i++) {
		auto *box = qobject_cast<QCheckBox *>(scenesLayout->itemAt(i)->widget());
		if (!box)
			continue;
		OBSSourceAutoRelease scene =
			obs_get_source_by_uuid(box->property("sceneUuid").toString().toUtf8().constData());
		if (!scene || box->isChecked() == d->Covers(scene))
			continue;
		removing = removing || !box->isChecked();
		changes.emplace_back(OBSSource(scene.Get()), box->isChecked());
	}
	if (removing && !d->IsMain() &&
	    QMessageBox::question(this, Tr("MDS.ManageDestinations"), Tr("MDS.ConfirmUncover")) != QMessageBox::Yes)
		return;

	if (!d->IsMain()) {
		const std::string name = nameEdit->text().trimmed().toStdString();
		if (!name.empty() && name != d->Name())
			d->SetName(name);
		if (!d->IsBlended() &&
		    (uint32_t(widthSpin->value()) != d->Width() || uint32_t(heightSpin->value()) != d->Height()))
			d->Attach(uint32_t(widthSpin->value()), uint32_t(heightSpin->value()));
	}
	d->SetOutputScreen(screenCombo->currentData().toString().toStdString());
	d->SetLocked(lockBox->isChecked());

	for (auto &[scene, on] : changes) {
		std::set<Destination *> coverage;
		for (Destination *x : m.ActiveDestinations()) {
			if (x != d && x->Covers(scene))
				coverage.insert(x);
		}
		if (on)
			coverage.insert(d);
		m.SetCoverage(scene, coverage);
	}
	m.NotifyDestinationsChanged();
	Refresh();
}
