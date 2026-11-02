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

#include "dialogs.hpp"
#include "destination.hpp"
#include "manager.hpp"
#include "text.hpp"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QGuiApplication>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QAbstractButton>
#include <QApplication>
#include <QKeyEvent>
#include <QPainter>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

#include <functional>
#include <vector>

namespace Dialogs {

namespace {
class EnterKeyGuard : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() != QEvent::KeyPress)
			return QObject::eventFilter(watched, event);
		auto *key = static_cast<QKeyEvent *>(event);
		if (key->key() != Qt::Key_Return && key->key() != Qt::Key_Enter)
			return QObject::eventFilter(watched, event);
		/* Bouton qui a le focus : on le clique. Sinon (champ, valeur) :
		 * la valeur est déjà validée par le champ, rien d'autre. */
		if (auto *button = qobject_cast<QAbstractButton *>(QApplication::focusWidget())) {
			if (button->window() == watched && button->isEnabled())
				button->click();
		}
		return true;
	}
};
} // namespace

void GuardEnterKey(QDialog *dialog)
{
	dialog->installEventFilter(new EnterKeyGuard(dialog));
	for (auto *button : dialog->findChildren<QPushButton *>()) {
		button->setAutoDefault(false);
		button->setDefault(false);
	}
}

QScreen *FindScreen(const std::string &name)
{
	if (name.empty())
		return nullptr;
	for (QScreen *screen : QGuiApplication::screens()) {
		if (screen->name().toStdString() == name)
			return screen;
	}
	return nullptr;
}

QString ScreenLabel(QScreen *screen, int index)
{
	const QRect g = screen->geometry();
	const qreal ratio = screen->devicePixelRatio();
	return QString("%1 %2 : %3 (%4×%5)")
		.arg(Tr("MDS.Screen"))
		.arg(index + 1)
		.arg(screen->name())
		.arg(int(g.width() * ratio))
		.arg(int(g.height() * ratio));
}

bool EditScene(QWidget *parent, obs_source_t *scene, std::string &name, std::set<Destination *> &coverage, bool isNew)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(isNew ? Tr("MDS.NewScene") : Tr("MDS.SceneSettings"));
	dialog.setMinimumWidth(380);
	auto *layout = new QVBoxLayout(&dialog);

	auto *form = new QFormLayout();
	auto *nameEdit = new QLineEdit(QString::fromStdString(name), &dialog);
	nameEdit->selectAll();
	form->addRow(Tr("MDS.Name"), nameEdit);
	layout->addLayout(form);

	auto *group = new QGroupBox(Tr("MDS.ShowSceneOn"), &dialog);
	auto *groupLayout = new QVBoxLayout(group);
	std::vector<std::pair<Destination *, QCheckBox *>> boxes;
	for (auto *d : Manager::Get().ActiveDestinations()) {
		auto *box = new QCheckBox(
			QString("%1  (%2×%3)").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height()),
			group);
		box->setChecked(isNew || !scene ? true : d->Covers(scene));
		groupLayout->addWidget(box);
		boxes.emplace_back(d, box);
	}
	layout->addWidget(group);

	if (!isNew) {
		auto *note = new QLabel(Tr("MDS.UncheckWarning"), &dialog);
		note->setWordWrap(true);
		layout->addWidget(note);
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;

	name = nameEdit->text().trimmed().toStdString();
	coverage.clear();
	for (auto &[d, box] : boxes) {
		if (box->isChecked())
			coverage.insert(d);
	}
	return !name.empty();
}

QComboBox *ScreenCombo(const std::string &current, QWidget *parent)
{
	auto *combo = new QComboBox(parent);
	combo->addItem(Tr("MDS.NoScreen"), QString());
	const QList<QScreen *> screens = QGuiApplication::screens();
	for (int i = 0; i < screens.size(); i++) {
		combo->addItem(ScreenLabel(screens[i], i), screens[i]->name());
		if (screens[i]->name().toStdString() == current)
			combo->setCurrentIndex(combo->count() - 1);
	}
	/* Écran configuré mais débranché : on le garde dans la liste. */
	if (!current.empty() && !FindScreen(current)) {
		combo->addItem(Tr("MDS.ScreenMissing").arg(QString::fromStdString(current)),
			       QString::fromStdString(current));
		combo->setCurrentIndex(combo->count() - 1);
	}
	return combo;
}

bool EditDestination(QWidget *parent, Destination *dest, DestinationSettings &settings)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(dest ? Tr("MDS.DestinationSettingsTitle").arg(QString::fromStdString(dest->Name()))
				   : Tr("MDS.NewDestination"));
	dialog.setMinimumWidth(420);
	auto *form = new QFormLayout(&dialog);

	const bool isMain = dest && dest->IsMain();
	QLineEdit *nameEdit = nullptr;
	QSpinBox *widthSpin = nullptr;
	QSpinBox *heightSpin = nullptr;

	if (isMain) {
		form->addRow(Tr("MDS.Name"), new QLabel(QString::fromStdString(dest->Name()), &dialog));
		auto *res = new QLabel(QString("%1×%2").arg(dest->Width()).arg(dest->Height()), &dialog);
		form->addRow(Tr("MDS.Resolution"), res);
		auto *info = new QLabel(Tr("MDS.MainInfo"), &dialog);
		info->setWordWrap(true);
		form->addRow(info);
	} else {
		nameEdit = new QLineEdit(QString::fromStdString(settings.name), &dialog);
		form->addRow(Tr("MDS.Name"), nameEdit);

		widthSpin = new QSpinBox(&dialog);
		widthSpin->setRange(16, 16384);
		widthSpin->setValue(int(settings.width));
		heightSpin = new QSpinBox(&dialog);
		heightSpin->setRange(16, 16384);
		heightSpin->setValue(int(settings.height));
		auto *resRow = new QHBoxLayout();
		resRow->addWidget(widthSpin);
		resRow->addWidget(new QLabel("×", &dialog));
		resRow->addWidget(heightSpin);
		resRow->addStretch(1);
		form->addRow(Tr("MDS.Resolution"), resRow);

		/* Destination blendée : la résolution est celle du layout. */
		if (dest && dest->IsBlended()) {
			widthSpin->setEnabled(false);
			heightSpin->setEnabled(false);
			auto *note = new QLabel(Tr("MDS.BlendResolutionNote"), &dialog);
			note->setWordWrap(true);
			form->addRow(note);
		}
	}

	QComboBox *screenCombo = ScreenCombo(settings.outputScreen, &dialog);
	form->addRow(Tr("MDS.OutputScreen"), screenCombo);

	/* Préremplir la résolution avec celle de l'écran choisi. */
	if (widthSpin && heightSpin) {
		QObject::connect(screenCombo, &QComboBox::currentIndexChanged, &dialog, [=](int) {
			QScreen *screen = FindScreen(screenCombo->currentData().toString().toStdString());
			if (!screen)
				return;
			const qreal ratio = screen->devicePixelRatio();
			widthSpin->setValue(int(screen->geometry().width() * ratio));
			heightSpin->setValue(int(screen->geometry().height() * ratio));
		});
	}

	QCheckBox *addToExisting = nullptr;
	if (!dest) {
		addToExisting = new QCheckBox(Tr("MDS.AddToExistingScenes"), &dialog);
		addToExisting->setChecked(settings.addToExisting);
		addToExisting->setToolTip(Tr("MDS.AddToExistingScenesTip"));
		form->addRow(addToExisting);
		/* Avec « Toutes les destinations, en permanence », la nouvelle
		 * destination est visible sous chaque scène même si elle n'y est
		 * pas : on le précise pour éviter la confusion. */
		if (Manager::Get().ShowAllDestinations()) {
			auto *note = new QLabel(Tr("MDS.AddToExistingShowAllNote"), &dialog);
			note->setWordWrap(true);
			note->setStyleSheet("QLabel { color: #8ab4f8; font-style: italic; }");
			form->addRow(note);
		}
	}

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	if (!dest)
		buttons->button(QDialogButtonBox::Ok)->setText(Tr("MDS.Create"));
	settings.openBlending = false;
	if (dest && !isMain) {
		QPushButton *blending = buttons->addButton(Tr("MDS.OutputBlendingMenu"), QDialogButtonBox::ActionRole);
		blending->setToolTip(Tr("MDS.OutputBlendingTip"));
		QObject::connect(blending, &QPushButton::clicked, &dialog, [&]() {
			settings.openBlending = true;
			dialog.accept();
		});
	}
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	form->addRow(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;

	if (nameEdit)
		settings.name = nameEdit->text().trimmed().toStdString();
	if (widthSpin)
		settings.width = uint32_t(widthSpin->value());
	if (heightSpin)
		settings.height = uint32_t(heightSpin->value());
	settings.outputScreen = screenCombo->currentData().toString().toStdString();
	if (addToExisting)
		settings.addToExisting = addToExisting->isChecked();
	return isMain || !settings.name.empty();
}

/* Choix entre deux options, avec une explication sous chacune. */
static bool ChooseOption(QWidget *parent, const QString &title, const QString &intro, const QString &optionA,
			 const QString &detailA, const QString &optionB, const QString &detailB, bool &chooseA)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(title);
	dialog.setMinimumWidth(460);
	auto *layout = new QVBoxLayout(&dialog);

	auto *introLabel = new QLabel(intro, &dialog);
	introLabel->setWordWrap(true);
	layout->addWidget(introLabel);
	layout->addSpacing(6);

	auto addOption = [&](const QString &text, const QString &detail) {
		auto *radio = new QRadioButton(text, &dialog);
		layout->addWidget(radio);
		auto *label = new QLabel(detail, &dialog);
		label->setWordWrap(true);
		label->setContentsMargins(24, 0, 0, 8);
		label->setStyleSheet("color: palette(window-text);");
		layout->addWidget(label);
		return radio;
	};
	QRadioButton *a = addOption(optionA, detailA);
	QRadioButton *b = addOption(optionB, detailB);
	(chooseA ? a : b)->setChecked(true);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);

	if (dialog.exec() != QDialog::Accepted)
		return false;
	chooseA = a->isChecked();
	return true;
}

bool EditAfterTakeSetting(QWidget *parent, bool &swap)
{
	return ChooseOption(parent, Tr("MDS.SettingAfterTake"), Tr("MDS.SettingAfterTakeIntro"),
			    Tr("MDS.SettingAfterTakeSwap"), Tr("MDS.SettingAfterTakeSwapTip"),
			    Tr("MDS.SettingAfterTakeKeep"), Tr("MDS.SettingAfterTakeKeepTip"), swap);
}

bool EditDisplaySetting(QWidget *parent, bool &showAll)
{
	return ChooseOption(parent, Tr("MDS.SettingDisplay"), Tr("MDS.SettingDisplayIntro"),
			    Tr("MDS.SettingDisplayAll"), Tr("MDS.SettingDisplayAllTip"), Tr("MDS.SettingDisplayScene"),
			    Tr("MDS.SettingDisplaySceneTip"), showAll);
}

/* Interrupteur activé / désactivé (pilule, bleu quand activé). */
class ToggleSwitch : public QAbstractButton {
public:
	explicit ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
	{
		setCheckable(true);
		setCursor(Qt::PointingHandCursor);
		setFixedSize(56, 30);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter p(this);
		p.setRenderHint(QPainter::Antialiasing, true);
		const QRectF track = QRectF(rect()).adjusted(1, 1, -1, -1);
		p.setPen(Qt::NoPen);
		/* Comme la maquette : bleu profond activé, gris désactivé. */
		p.setBrush(isChecked() ? QColor(0x2B, 0x3F, 0xD6) : QColor(0x4A, 0x4D, 0x57));
		p.drawRoundedRect(track, track.height() / 2, track.height() / 2);
		const qreal m = 4;
		const qreal d = track.height() - 2 * m;
		const qreal x = isChecked() ? track.right() - m - d : track.left() + m;
		p.setBrush(isChecked() ? QColor(Qt::white) : QColor(0xC8, 0xCA, 0xD0));
		p.drawEllipse(QRectF(x, track.top() + m, d, d));
	}
};

void EditStartupSetting(QWidget *parent)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(Tr("MDS.SettingStartup"));
	dialog.setMinimumWidth(480);
	auto *layout = new QVBoxLayout(&dialog);

	/* Une ligne par réglage : libellé, état, interrupteur ; enregistré dès
	 * qu'on clique. */
	auto addSwitch = [&](const QString &text, bool value, std::function<void(bool)> apply) {
		auto *row = new QHBoxLayout();
		auto *label = new QLabel(text, &dialog);
		label->setWordWrap(true);
		auto *toggle = new ToggleSwitch(&dialog);
		toggle->setChecked(value);
		auto *state = new QLabel(&dialog);
		auto refresh = [=]() {
			state->setText(toggle->isChecked() ? Tr("MDS.Enabled") : Tr("MDS.Disabled"));
		};
		refresh();
		QObject::connect(toggle, &QAbstractButton::toggled, &dialog, [=](bool on) {
			apply(on);
			refresh();
		});
		row->addWidget(label, 1);
		row->addWidget(state);
		row->addWidget(toggle);
		layout->addLayout(row);
	};
	addSwitch(Tr("MDS.SettingStartupLaunch"), Manager::Get().LaunchOnStartup(),
		  [](bool on) { Manager::Get().SetLaunchOnStartup(on); });
	addSwitch(Tr("MDS.SettingStartupOutputs"), Manager::Get().OpenOutputsOnStartup(),
		  [](bool on) { Manager::Get().SetOpenOutputsOnStartup(on); });

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
	buttons->button(QDialogButtonBox::Close)->setText(Tr("MDS.Close"));
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addSpacing(8);
	layout->addWidget(buttons);
	dialog.exec();
}

bool EditLanguageSetting(QWidget *parent, std::string &locale)
{
	QDialog dialog(parent);
	dialog.setWindowTitle(Tr("MDS.SettingLanguage"));
	dialog.setMinimumWidth(420);
	auto *layout = new QVBoxLayout(&dialog);
	auto *intro = new QLabel(Tr("MDS.SettingLanguageIntro"), &dialog);
	intro->setWordWrap(true);
	layout->addWidget(intro);
	layout->addSpacing(6);

	/* Noms des langues dans leur propre langue. */
	const std::pair<std::string, QString> choices[] = {
		{"", Tr("MDS.SettingLanguageObs")},
		{"en-US", "English"},
		{"fr-FR", "Français"},
		{"nb-NO", "Norsk (bokmål)"},
	};
	std::vector<std::pair<QRadioButton *, std::string>> radios;
	for (const auto &c : choices) {
		auto *radio = new QRadioButton(c.second, &dialog);
		radio->setChecked(c.first == locale);
		layout->addWidget(radio);
		radios.emplace_back(radio, c.first);
	}
	auto *note = new QLabel(Tr("MDS.SettingLanguageRestart"), &dialog);
	note->setWordWrap(true);
	note->setContentsMargins(0, 8, 0, 0);
	note->setStyleSheet("color: palette(window-text);");
	layout->addWidget(note);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
	QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	layout->addWidget(buttons);
	if (dialog.exec() != QDialog::Accepted)
		return false;
	for (auto &r : radios) {
		if (r.first->isChecked())
			locale = r.second;
	}
	return true;
}

bool ConfirmDeleteDestination(QWidget *parent, Destination *dest)
{
	QMessageBox box(parent);
	box.setIcon(QMessageBox::Warning);
	box.setWindowTitle(Tr("MDS.DeleteDestinationTitle"));
	box.setText(Tr("MDS.DeleteDestinationText").arg(QString::fromStdString(dest->Name())));
	box.setInformativeText(Tr("MDS.DeleteDestinationInfo"));
	QPushButton *yes = box.addButton(Tr("MDS.Delete"), QMessageBox::DestructiveRole);
	box.addButton(QMessageBox::Cancel);
	box.setDefaultButton(QMessageBox::Cancel);
	box.exec();
	return box.clickedButton() == yes;
}

} // namespace Dialogs
