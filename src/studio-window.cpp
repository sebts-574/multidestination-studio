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

#include "studio-window.hpp"
#include "audio-mixer.hpp"
#include "destination.hpp"
#include "dialogs.hpp"
#include "manager.hpp"
#include "media-controls.hpp"
#include "projector-window.hpp"
#include "reorder-list.hpp"
#include "manage-destinations.hpp"
#include "blend-editor.hpp"
#include "text.hpp"
#include "tile-grid.hpp"
#include "undo.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QScreen>
#include <QShortcut>
#include <QSpinBox>
#include <QCursor>
#include <QStyle>
#include <QTimer>
#include <QTreeWidget>
#include <set>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cstring>

/* ------------------------------------------------------------------ */
/* Petits utilitaires d'interface                                       */
/* ------------------------------------------------------------------ */

static QToolButton *ToolButton(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setAutoRaise(true);
	b->setMinimumWidth(26);
	return b;
}

/* Contenu d'un dock : liste + barre de boutons en bas, comme dans OBS. */
/* Lignes de la liste des scènes (Qt::UserRole + 1). */
static constexpr int SCENE_ROW_SCENE = 1;
static constexpr int SCENE_ROW_FOLDER = 2;
/* Lignes de la liste des sources : Qt::UserRole = id de l'élément,
 * Qt::UserRole + 1 = id du groupe parent (0 = premier niveau). */

static QWidget *ListPanel(QWidget *list, QHBoxLayout *buttons, QWidget *parent)
{
	auto *panel = new QWidget(parent);
	auto *layout = new QVBoxLayout(panel);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(list, 1);
	buttons->setContentsMargins(2, 2, 2, 2);
	buttons->setSpacing(2);
	buttons->addStretch(1);
	layout->addLayout(buttons);
	return panel;
}

static QDockWidget *Dock(const QString &title, const char *objectName, QWidget *content, QWidget *parent)
{
	auto *dock = new QDockWidget(title, parent);
	dock->setObjectName(objectName);
	dock->setWidget(content);
	dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable |
			  QDockWidget::DockWidgetClosable);
	return dock;
}

/* Type de source OBS selon l'extension d'un fichier déposé. */
static const char *SourceTypeForFile(const QString &path, const char **settingKey)
{
	const QString ext = QFileInfo(path).suffix().toLower();
	static const QStringList images = {"png", "jpg", "jpeg", "bmp", "gif", "webp", "tga", "psd", "jxr"};
	static const QStringList media = {"mp4",  "mov", "mkv", "webm", "avi", "m4v", "ts",  "flv",  "mpg",
					  "mpeg", "mxf", "mp3", "wav",  "aac", "m4a", "ogg", "flac", "opus"};
	static const QStringList web = {"html", "htm"};
	if (images.contains(ext)) {
		*settingKey = "file";
		return "image_source";
	}
	if (media.contains(ext)) {
		*settingKey = "local_file";
		return "ffmpeg_source";
	}
	if (web.contains(ext)) {
		*settingKey = "local_file";
		return "browser_source";
	}
	return nullptr;
}

static QString UniqueSourceName(const QString &base)
{
	QString name = base;
	for (int i = 2;; i++) {
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
		if (!existing)
			return name;
		name = QString("%1 %2").arg(base).arg(i);
	}
}

/* ------------------------------------------------------------------ */
/* Construction                                                         */
/* ------------------------------------------------------------------ */

StudioWindow::StudioWindow(QWidget *parent) : QMainWindow(parent, Qt::Window)
{
	setWindowTitle("Multidestination Studio");
	setDockNestingEnabled(true);
	setAcceptDrops(true);
	qApp->installEventFilter(this);

	BuildCentral();
	BuildDocks();
	BuildMenus();
	BuildWindowControls();

	Manager &m = Manager::Get();
	connect(&m, &Manager::previewChanged, this, &StudioWindow::RebuildGrids);
	connect(&m, &Manager::settingsChanged, this, [this]() { UpdateGrids(true); });
	connect(&m, &Manager::scenesChanged, this, [this]() {
		RefreshScenes();
		RebuildGrids();
	});
	connect(&m, &Manager::destinationsChanged, this, [this]() {
		UpdateControls();
		UpdateGrids(true);
		for (TileGrid *grid : {pvwGrid, pgmGrid}) {
			for (TileWidget *tile : grid->Tiles())
				tile->RefreshHeader();
		}
	});
	connect(&m, &Manager::programChanged, this, &StudioWindow::RefreshTally);
	connect(&m, &Manager::aboutToReset, this, &StudioWindow::OnReset);
	connect(&m, &Manager::loaded, this, [this]() {
		RefreshScenes();
		RefreshTransitions();
		UpdateControls();
		UpdateGrids(true);
	});
	/* Menu Destinations rempli immédiatement : les menus natifs macOS
     * n'affichent pas de façon fiable un menu rempli via aboutToShow. */
	/* Reconstruction différée : un clic dans ce menu (Supprimer…) peut
	 * déclencher ces signaux, et le menu ne doit pas être vidé pendant
	 * que son action s'exécute. */
	for (auto signal : {&Manager::destinationsChanged, &Manager::scenesChanged, &Manager::loaded})
		connect(&m, signal, this, &StudioWindow::ScheduleDestinationsMenu);

	connect(&m, &Manager::aboutToRemove, this, [this](Destination *dest) {
		CloseProjectors(dest);
		if (activeDest == dest)
			activeDest = nullptr;
		if (focusDest == dest)
			focusDest = nullptr;
		pvwGrid->Clear();
		pgmGrid->Clear();
	});

	/* Changements faits ailleurs (OBS, transitions en cours, stream). */
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, [this]() {
		if (!isVisible())
			return;
		RefreshTally();
		UpdateControls();
		/* Taille des vidéos vues, pour pouvoir les éditer inactives. */
		for (TileGrid *grid : {pvwGrid, pgmGrid}) {
			for (TileWidget *tile : grid->Tiles()) {
				OBSSource scene = tile->EditScene();
				SceneEditor::RememberSizes(obs_scene_from_source(scene));
			}
		}
	});
	timer->start(500);
}

StudioWindow::~StudioWindow()
{
	Shutdown();
}

/* Titre « Aperçu » / « Programme » : très grand et gras, proportionnel à
 * la largeur de la colonne. Les destinations prennent la place restante. */
class ColumnTitle : public QLabel {
public:
	ColumnTitle(const QString &text, QWidget *parent) : QLabel(text, parent)
	{
		setAlignment(Qt::AlignCenter);
		Fit();
	}

protected:
	void resizeEvent(QResizeEvent *event) override
	{
		QLabel::resizeEvent(event);
		Fit();
	}

private:
	void Fit()
	{
		/* Maquette (1920×1080) : titre d'environ 36 px pour une colonne
		 * de 830 px, bien détaché du haut et des destinations. */
		const int px = std::clamp(width() * 36 / 830, 22, 48);
		if (font().pixelSize() != px || !font().bold()) {
			QFont f = font();
			f.setPixelSize(px);
			f.setBold(true);
			setFont(f);
		}
		const QMargins m(0, px * 2 / 3, 0, px);
		if (contentsMargins() != m)
			setContentsMargins(m);
	}
};

void StudioWindow::BuildCentral()
{
	monitors = new QWidget(this);
	auto *layout = new QHBoxLayout(monitors);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(6);

	auto makeColumn = [this](const QString &label, TileGrid *grid) {
		auto *col = new QWidget(monitors);
		auto *l = new QVBoxLayout(col);
		l->setContentsMargins(0, 0, 0, 0);
		l->setSpacing(2);
		auto *t = new ColumnTitle(label, col);
		l->addWidget(t);
		grid->setParent(col);
		l->addWidget(grid, 1);
		return col;
	};

	pvwGrid = new TileGrid(false, monitors);
	pgmGrid = new TileGrid(true, monitors);
	layout->addWidget(makeColumn(Tr("MDS.Preview"), pvwGrid), 1);

	/* Boutons entre Aperçu et Programme, comme le mode Studio d'OBS. */
	/* « Modifier le PGM » bien au-dessus de Transition / Cut, pour ne pas
	 * le confondre avec eux. */
	auto *controls = new QVBoxLayout();
	controls->addStretch(2);

	modifyPgmButton = new QPushButton(Tr("MDS.ModifyProgram"), monitors);
	modifyPgmButton->setCheckable(true);
	modifyPgmButton->setToolTip(Tr("MDS.ModifyProgramTip"));
	/* Rouge, texte blanc quand il est activé ; style normal sinon. */
	modifyPgmButton->setStyleSheet(
		"QPushButton:checked { background-color: #c62828; color: white; border-color: #c62828; }");
	connect(modifyPgmButton, &QPushButton::toggled, this, &StudioWindow::SetModifyProgram);
	controls->addWidget(modifyPgmButton);
	controls->addStretch(3);

	auto *transition = new QPushButton(Tr("MDS.Transition"), monitors);
	transition->setToolTip(Tr("MDS.TransitionTip"));
	connect(transition, &QPushButton::clicked, this, []() { Manager::Get().Take(false); });
	controls->addWidget(transition);
	auto *cut = new QPushButton(Tr("MDS.Cut"), monitors);
	cut->setToolTip(Tr("MDS.CutTip"));
	connect(cut, &QPushButton::clicked, this, []() { Manager::Get().Take(true); });
	controls->addWidget(cut);
	controls->addStretch(5);
	layout->addLayout(controls);

	layout->addWidget(makeColumn(Tr("MDS.Program"), pgmGrid), 1);
	setCentralWidget(monitors);
}

void StudioWindow::BuildDocks()
{
	/* --- Scènes --- */
	/* Liste des scènes avec dossiers repliables (organisation seulement :
	 * on prend toujours une scène, jamais un dossier). */
	auto *scenes = new OrganizerTree(this);
	scenes->onDelete = [this]() {
		RemoveSelectedScenes();
	};
	scenes->onDropped = [this]() {
		ApplySceneOrderFromList();
	};
	sceneList = scenes;
	connect(sceneList, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (refreshingScenes)
			return;
		OBSSource scene = SelectedScene();
		if (scene)
			Manager::Get().SetPreviewScene(scene);
	});
	connect(sceneList, &QTreeWidget::itemExpanded, this, [this]() {
		if (!refreshingScenes)
			SaveSceneTree();
	});
	connect(sceneList, &QTreeWidget::itemCollapsed, this, [this]() {
		if (!refreshingScenes)
			SaveSceneTree();
	});
	sceneList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sceneList, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
		OBSSource scene = SelectedScene();
		QTreeWidgetItem *current = sceneList->currentItem();
		const bool onFolder = current && current->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER;
		QMenu menu(this);
		connect(menu.addAction(Tr("MDS.AddScene")), &QAction::triggered, this, &StudioWindow::AddScene);
		connect(menu.addAction(Tr("MDS.NewSceneFolder")), &QAction::triggered, this,
			&StudioWindow::NewSceneFolder);
		if (onFolder) {
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.RenameFolder")), &QAction::triggered, this,
				[this, current]() { RenameSceneFolder(current); });
			connect(menu.addAction(Tr("MDS.RemoveFolder")), &QAction::triggered, this,
				[this]() { RemoveSelectedScenes(); });
		}
		if (scene || onFolder) {
			menu.addSeparator();
			if (scene)
				connect(menu.addAction(Tr("MDS.SceneSettingsMenu")), &QAction::triggered, this,
					[this, scene]() { EditScene(scene); });
			connect(menu.addAction(Tr("MDS.MoveUp")), &QAction::triggered, this,
				[this]() { MoveScene(-1); });
			connect(menu.addAction(Tr("MDS.MoveDown")), &QAction::triggered, this,
				[this]() { MoveScene(1); });
			connect(menu.addAction(Tr("MDS.MoveTop")), &QAction::triggered, this,
				[this]() { MoveScene(-100000); });
			connect(menu.addAction(Tr("MDS.MoveBottom")), &QAction::triggered, this,
				[this]() { MoveScene(100000); });
		}
		if (scene) {
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.RemoveScene")), &QAction::triggered, this,
				[this]() { RemoveSelectedScenes(); });
		}
		menu.exec(sceneList->viewport()->mapToGlobal(pos));
	});
	connect(sceneList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
		if (item && item->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER)
			item->setExpanded(!item->isExpanded());
		else
			EditScene(SelectedScene());
	});
	auto *sceneButtons = new QHBoxLayout();
	auto *addScene = ToolButton("+", Tr("MDS.AddScene"), this);
	auto *removeScene = ToolButton("−", Tr("MDS.RemoveScene"), this);
	auto *editScene = ToolButton("⚙", Tr("MDS.SceneSettingsMenu"), this);
	auto *folderButton = new QToolButton(this);
	folderButton->setIcon(style()->standardIcon(QStyle::SP_FileDialogNewFolder));
	folderButton->setToolTip(Tr("MDS.NewSceneFolder"));
	folderButton->setAutoRaise(true);
	connect(addScene, &QToolButton::clicked, this, &StudioWindow::AddScene);
	connect(removeScene, &QToolButton::clicked, this, [this]() { RemoveSelectedScenes(); });
	connect(editScene, &QToolButton::clicked, this, [this]() { EditScene(SelectedScene()); });
	connect(folderButton, &QToolButton::clicked, this, &StudioWindow::NewSceneFolder);
	sceneButtons->addWidget(addScene);
	sceneButtons->addWidget(removeScene);
	sceneButtons->addWidget(editScene);
	sceneButtons->addWidget(folderButton);
	auto *upScene = ToolButton("↑", Tr("MDS.MoveUp"), this);
	auto *downScene = ToolButton("↓", Tr("MDS.MoveDown"), this);
	connect(upScene, &QToolButton::clicked, this, [this]() { MoveScene(-1); });
	connect(downScene, &QToolButton::clicked, this, [this]() { MoveScene(1); });
	sceneButtons->addWidget(upScene);
	sceneButtons->addWidget(downScene);
	Dock(Tr("MDS.Scenes"), "mdsScenesDock", ListPanel(sceneList, sceneButtons, this), this);

	/* --- Sources --- */
	/* Glisser-déposer pour changer l'ordre (haut de la liste = devant),
	 * cliquer-glisser pour sélectionner plusieurs sources. */
	auto *sources = new OrganizerTree(this);
	sources->onDelete = [this]() {
		EditRemove();
	};
	sources->onDropped = [this]() {
		ApplySourceOrderFromList();
	};
	sourceList = sources;
	connect(sourceList, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (refreshingSources)
			return;
		TileWidget *tile = ActiveTile();
		QTreeWidgetItem *row = sourceList->currentItem();
		if (tile) {
			/* Un élément d'un groupe : c'est le groupe qu'on sélectionne
			 * dans l'aperçu. */
			int64_t id = -1;
			if (row) {
				const qlonglong parent = row->data(0, Qt::UserRole + 1).toLongLong();
				id = parent ? parent : row->data(0, Qt::UserRole).toLongLong();
			}
			tile->SetSelectedItem(id);
		}
		UpdateMediaControls();
	});
	connect(sourceList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *row) {
		if (refreshingSources)
			return;
		OBSSceneItem item = ItemFromRow(row);
		if (item) {
			Undo::PushSceneState(ActiveScene());
			obs_sceneitem_set_visible(item, row->checkState(0) == Qt::Checked);
			AfterEdit();
		}
	});
	auto groupCollapsed = [this](QTreeWidgetItem *row, bool collapsed) {
		if (refreshingSources)
			return;
		if (OBSSceneItem item = ItemFromRow(row)) {
			OBSDataAutoRelease priv = obs_sceneitem_get_private_settings(item);
			obs_data_set_bool(priv, "collapsed", collapsed);
		}
	};
	connect(sourceList, &QTreeWidget::itemExpanded, this,
		[groupCollapsed](QTreeWidgetItem *row) { groupCollapsed(row, false); });
	connect(sourceList, &QTreeWidget::itemCollapsed, this,
		[groupCollapsed](QTreeWidgetItem *row) { groupCollapsed(row, true); });
	connect(sourceList, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *row) {
		OBSSceneItem item = ItemFromRow(row);
		if (item && obs_sceneitem_is_group(item))
			row->setExpanded(!row->isExpanded());
		else
			EditProperties();
	});
	sourceList->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(sourceList, &QTreeWidget::customContextMenuRequested, this,
		[this](const QPoint &pos) { ShowItemMenu(sourceList->viewport()->mapToGlobal(pos)); });

	auto *sourceButtons = new QHBoxLayout();
	addSourceButton = ToolButton("+", Tr("MDS.AddSource"), this);
	connect(addSourceButton, &QToolButton::clicked, this, &StudioWindow::ShowAddSourceMenu);
	auto *removeSource = ToolButton("−", Tr("MDS.RemoveSource"), this);
	auto *propSource = ToolButton("⚙", Tr("MDS.Properties"), this);
	auto *upSource = ToolButton("↑", Tr("MDS.MoveUp"), this);
	auto *downSource = ToolButton("↓", Tr("MDS.MoveDown"), this);
	connect(removeSource, &QToolButton::clicked, this, &StudioWindow::EditRemove);
	connect(propSource, &QToolButton::clicked, this, &StudioWindow::EditProperties);
	connect(upSource, &QToolButton::clicked, this, &StudioWindow::EditMoveUp);
	connect(downSource, &QToolButton::clicked, this, &StudioWindow::EditMoveDown);
	sourceButtons->addWidget(addSourceButton);
	sourceButtons->addWidget(removeSource);
	sourceButtons->addWidget(propSource);
	sourceButtons->addWidget(upSource);
	sourceButtons->addWidget(downSource);
	Dock(Tr("MDS.Sources"), "mdsSourcesDock", ListPanel(sourceList, sourceButtons, this), this);

	/* --- Mixeur audio --- */
	mixer = new AudioMixer(this, false);
	Dock(Tr("MDS.AudioMixer"), "mdsMixerDock", mixer, this);

	/* --- Contrôles média --- */
	mediaControls = new MediaControls(this);
	Dock(Tr("MDS.MediaControls"), "mdsMediaDock", mediaControls, this);

	/* --- Transitions de scène --- */
	auto *transitions = new QWidget(this);
	auto *tl = new QVBoxLayout(transitions);
	tl->setContentsMargins(4, 4, 4, 4);
	auto *row = new QHBoxLayout();
	transitionCombo = new QComboBox(transitions);
	connect(transitionCombo, &QComboBox::currentIndexChanged, this, [this](int index) {
		if (refreshingTransitions || index < 0)
			return;
		const QString wanted = transitionCombo->itemData(index).toString();
		struct obs_frontend_source_list list = {};
		obs_frontend_get_transitions(&list);
		for (size_t i = 0; i < list.sources.num; i++) {
			if (wanted == QString::fromUtf8(obs_source_get_uuid(list.sources.array[i])))
				obs_frontend_set_current_transition(list.sources.array[i]);
		}
		obs_frontend_source_list_free(&list);
		RefreshTransitions();
	});
	row->addWidget(transitionCombo, 1);
	auto *transitionProps = ToolButton("⚙", Tr("MDS.TransitionProperties"), transitions);
	connect(transitionProps, &QToolButton::clicked, this, []() {
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		if (current)
			obs_frontend_open_source_properties(current);
	});
	row->addWidget(transitionProps);
	tl->addLayout(row);
	auto *durationRow = new QHBoxLayout();
	durationRow->addWidget(new QLabel(Tr("MDS.Duration"), transitions));
	durationSpin = new QSpinBox(transitions);
	durationSpin->setRange(50, 20000);
	durationSpin->setSingleStep(50);
	durationSpin->setSuffix(" ms");
	connect(durationSpin, &QSpinBox::valueChanged, this, [this](int value) {
		if (!refreshingTransitions)
			obs_frontend_set_transition_duration(value);
	});
	durationRow->addWidget(durationSpin, 1);
	tl->addLayout(durationRow);
	auto *transitionNote = new QLabel(Tr("MDS.TransitionNote"), transitions);
	transitionNote->setWordWrap(true);
	tl->addWidget(transitionNote);
	tl->addStretch(1);
	Dock(Tr("MDS.SceneTransitions"), "mdsTransitionsDock", transitions, this);

	/* --- Commandes --- */
	auto *controls = new QWidget(this);
	auto *cl = new QVBoxLayout(controls);
	cl->setContentsMargins(4, 4, 4, 4);
	streamButton = new QPushButton(controls);
	connect(streamButton, &QPushButton::clicked, this, [this]() {
		if (Manager::Get().Main()->Disabled()) {
			ShowMainDisabledHint(streamButton);
			return;
		}
		if (obs_frontend_streaming_active())
			obs_frontend_streaming_stop();
		else
			obs_frontend_streaming_start();
		QTimer::singleShot(300, this, &StudioWindow::UpdateControls);
	});
	cl->addWidget(streamButton);
	recordButton = new QPushButton(controls);
	connect(recordButton, &QPushButton::clicked, this, [this]() {
		if (Manager::Get().Main()->Disabled()) {
			ShowMainDisabledHint(recordButton);
			return;
		}
		if (obs_frontend_recording_active())
			obs_frontend_recording_stop();
		else
			obs_frontend_recording_start();
		QTimer::singleShot(300, this, &StudioWindow::UpdateControls);
	});
	cl->addWidget(recordButton);
	auto *exit = new QPushButton(Tr("MDS.ExitStudio"), controls);
	connect(exit, &QPushButton::clicked, this, &StudioWindow::Close);
	cl->addWidget(exit);
	cl->addStretch(1);
	Dock(Tr("MDS.Controls"), "mdsControlsDock", controls, this);
	UpdateControls();

	ResetDocks();
}

/* Disposition par défaut : six docks côte à côte en bas, dans cet ordre,
 * avec ces proportions (d'après la maquette de Sébastien, en 1920×1080). */
void StudioWindow::ResetDocks()
{
	const char *names[] = {"mdsScenesDock", "mdsSourcesDock",     "mdsMediaDock",
			       "mdsMixerDock",  "mdsTransitionsDock", "mdsControlsDock"};
	QList<QDockWidget *> docks;
	for (const char *name : names) {
		if (auto *dock = findChild<QDockWidget *>(name))
			docks << dock;
	}
	if (docks.isEmpty())
		return;

	for (QDockWidget *dock : docks) {
		dock->setFloating(false);
		if (dockWidgetArea(dock) != Qt::NoDockWidgetArea)
			removeDockWidget(dock);
	}
	addDockWidget(Qt::BottomDockWidgetArea, docks.front());
	docks.front()->show();
	for (int i = 1; i < docks.size(); i++) {
		splitDockWidget(docks[i - 1], docks[i], Qt::Horizontal);
		docks[i]->show();
	}
	defaultDocksPending = true;
	ApplyDefaultDockSizes();
}

void StudioWindow::ApplyDefaultDockSizes()
{
	QList<QDockWidget *> docks;
	for (const char *name : {"mdsScenesDock", "mdsSourcesDock", "mdsMediaDock", "mdsMixerDock",
				 "mdsTransitionsDock", "mdsControlsDock"}) {
		if (auto *dock = findChild<QDockWidget *>(name))
			docks << dock;
	}
	if (docks.size() != 6)
		return;
	const int W = std::max(width(), 1200);
	const int H = std::max(height(), 700);
	const int parts[6] = {298, 327, 420, 285, 295, 268}; /* total 1893 */
	QList<int> widths;
	for (int p : parts)
		widths << W * p / 1893;
	resizeDocks(docks, widths, Qt::Horizontal);
	resizeDocks({docks.front()}, {H * 242 / 1080}, Qt::Vertical);
}

QAction *StudioWindow::MakeEditAction(const QString &text, const QList<QKeySequence> &keys, void (StudioWindow::*fn)())
{
	auto *action = new QAction(text, this);
	action->setShortcuts(keys);
	/* Actifs quand le focus est dans les aperçus ou la liste des sources,
	 * pour ne pas gêner la saisie dans les autres champs. */
	action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
	connect(action, &QAction::triggered, this, fn);
	monitors->addAction(action);
	sourceList->addAction(action);
	editActions.push_back(action);
	return action;
}

void StudioWindow::BuildMenus()
{
	QMenuBar *bar = menuBar();

	/* --- Fichier --- */
	QMenu *file = bar->addMenu(Tr("MDS.MenuFile"));
	connect(file->addAction(Tr("MDS.ExitStudio")), &QAction::triggered, this, &StudioWindow::Close);

	/* --- Édition --- */
	QMenu *edit = bar->addMenu(Tr("MDS.MenuEdit"));
	edit->addAction(MakeEditAction(Tr("MDS.Undo"), {QKeySequence::Undo}, &StudioWindow::EditUndo));
	edit->addSeparator();
	edit->addAction(MakeEditAction(Tr("MDS.Copy"), {QKeySequence::Copy}, &StudioWindow::EditCopy));
	edit->addAction(MakeEditAction(Tr("MDS.Paste"), {QKeySequence::Paste}, &StudioWindow::EditPaste));
	edit->addAction(MakeEditAction(Tr("MDS.PasteDuplicate"), {QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_V)},
				       &StudioWindow::EditPasteDuplicate));
	edit->addSeparator();
	edit->addAction(
		MakeEditAction(Tr("MDS.GroupSources"), {QKeySequence(Qt::CTRL | Qt::Key_G)}, &StudioWindow::EditGroup));
	edit->addAction(MakeEditAction(Tr("MDS.UngroupSources"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)},
				       &StudioWindow::EditUngroup));
	edit->addAction(MakeEditAction(Tr("MDS.RemoveSource"),
				       {QKeySequence(Qt::Key_Delete), QKeySequence(Qt::Key_Backspace)},
				       &StudioWindow::EditRemove));
	edit->addSeparator();
	QMenu *transform = edit->addMenu(Tr("MDS.Transform"));
	transform->addAction(MakeEditAction(Tr("MDS.CopyTransform"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C)},
					    &StudioWindow::EditCopyTransform));
	transform->addAction(MakeEditAction(Tr("MDS.PasteTransform"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_V)},
					    &StudioWindow::EditPasteTransform));
	transform->addSeparator();
	transform->addAction(MakeEditAction(Tr("MDS.ResetTransform"), {QKeySequence(Qt::CTRL | Qt::Key_R)},
					    &StudioWindow::EditResetTransform));
	transform->addAction(
		MakeEditAction(Tr("MDS.FitToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_F)}, &StudioWindow::EditFit));
	transform->addAction(MakeEditAction(Tr("MDS.StretchToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_S)},
					    &StudioWindow::EditStretch));
	transform->addAction(MakeEditAction(Tr("MDS.CenterToScreen"), {QKeySequence(Qt::CTRL | Qt::Key_D)},
					    &StudioWindow::EditCenter));
	QMenu *order = edit->addMenu(Tr("MDS.Order"));
	order->addAction(
		MakeEditAction(Tr("MDS.MoveUp"), {QKeySequence(Qt::CTRL | Qt::Key_Up)}, &StudioWindow::EditMoveUp));
	order->addAction(MakeEditAction(Tr("MDS.MoveDown"), {QKeySequence(Qt::CTRL | Qt::Key_Down)},
					&StudioWindow::EditMoveDown));
	order->addAction(
		MakeEditAction(Tr("MDS.MoveTop"), {QKeySequence(Qt::CTRL | Qt::Key_Home)}, &StudioWindow::EditMoveTop));
	order->addAction(MakeEditAction(Tr("MDS.MoveBottom"), {QKeySequence(Qt::CTRL | Qt::Key_End)},
					&StudioWindow::EditMoveBottom));
	edit->addSeparator();
	edit->addAction(MakeEditAction(Tr("MDS.Properties"), {}, &StudioWindow::EditProperties));
	edit->addAction(MakeEditAction(Tr("MDS.Filters"), {}, &StudioWindow::EditFilters));

	/* --- Destinations --- */
	destinationsMenu = bar->addMenu(Tr("MDS.MenuDestinations"));
#ifndef Q_OS_MACOS
	/* macOS : les menus natifs n'affichent pas de façon fiable un menu
	 * rempli à l'ouverture ; il est rempli à l'avance (voir
	 * ScheduleDestinationsMenu). */
	connect(destinationsMenu, &QMenu::aboutToShow, this, &StudioWindow::FillDestinationsMenu);
#endif

	/* --- Docks : afficher / masquer --- */
	viewMenu = bar->addMenu(Tr("MDS.MenuView"));
	for (QDockWidget *dock : findChildren<QDockWidget *>())
		viewMenu->addAction(dock->toggleViewAction());
	viewMenu->addSeparator();
	connect(viewMenu->addAction(Tr("MDS.ResetDocks")), &QAction::triggered, this, [this]() {
		ResetDocks();
		defaultDocksPending = false;
		QTimer::singleShot(0, this, &StudioWindow::ApplyDefaultDockSizes);
	});
	auto *fullscreen = viewMenu->addAction(Tr("MDS.WindowFullscreen"));
	fullscreen->setShortcut(QKeySequence(Qt::Key_F11));
	fullscreen->setShortcutContext(Qt::WindowShortcut);
	addAction(fullscreen);
	connect(fullscreen, &QAction::triggered, this, [this]() {
		if (isFullScreen())
			showMaximized();
		else
			showFullScreen();
	});

	/* --- Réglages --- */
	QMenu *settingsMenu = bar->addMenu(Tr("MDS.MenuSettings"));
	connect(settingsMenu->addAction(Tr("MDS.SettingAfterTakeMenu")), &QAction::triggered, this, [this]() {
		bool swap = Manager::Get().SwapAfterTake();
		if (Dialogs::EditAfterTakeSetting(this, swap))
			Manager::Get().SetSwapAfterTake(swap);
	});
	connect(settingsMenu->addAction(Tr("MDS.SettingDisplayMenu")), &QAction::triggered, this, [this]() {
		bool showAll = Manager::Get().ShowAllDestinations();
		if (Dialogs::EditDisplaySetting(this, showAll))
			Manager::Get().SetShowAllDestinations(showAll);
	});
	connect(settingsMenu->addAction(Tr("MDS.SettingStartupMenu")), &QAction::triggered, this,
		[this]() { Dialogs::EditStartupSetting(this); });
	settingsMenu->addSeparator();
	connect(settingsMenu->addAction(Tr("MDS.SettingLanguageMenu")), &QAction::triggered, this, [this]() {
		std::string locale = Manager::Get().Language();
		if (!Dialogs::EditLanguageSetting(this, locale) || locale == Manager::Get().Language())
			return;
		Manager::Get().SetLanguage(locale);
		QMessageBox::information(this, Tr("MDS.SettingLanguage"), Tr("MDS.SettingLanguageRestart"));
	});
	settingsMenuAction = settingsMenu->menuAction();

	/* Info « F11 … » juste après le dernier menu. */
	fullscreenHint = new QWidget(bar);
	auto *hintLayout = new QHBoxLayout(fullscreenHint);
	hintLayout->setContentsMargins(0, 0, 0, 0);
	hintLayout->setSpacing(5);
	auto *hintIcon = new QLabel(fullscreenHint);
	hintIcon->setPixmap(style()->standardIcon(QStyle::SP_MessageBoxInformation).pixmap(14, 14));
	hintText = new QLabel(Tr("MDS.FullscreenHint"), fullscreenHint);
	hintText->setStyleSheet("QLabel { color: #8ab4f8; font-style: italic; }");
	hintLayout->addWidget(hintIcon);
	hintLayout->addWidget(hintText);
	fullscreenHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
	PlaceFullscreenHint();

	auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
	connect(escape, &QShortcut::activated, this, &StudioWindow::Close);

	FillDestinationsMenu();
}

/* ------------------------------------------------------------------ */
/* Commandes de fenêtre en plein écran                                  */
/* ------------------------------------------------------------------ */

void StudioWindow::BuildWindowControls()
{
	windowControls = new QWidget(this);
	windowControls->setObjectName("mdsWindowControls");
	windowControls->setAttribute(Qt::WA_StyledBackground, true);
	windowControls->setStyleSheet("#mdsWindowControls { background-color: rgba(20, 20, 20, 225); "
				      "border-bottom-left-radius: 8px; }");
	auto *layout = new QHBoxLayout(windowControls);
	layout->setContentsMargins(8, 6, 8, 6);
	layout->setSpacing(6);

	auto *minimize = new QPushButton(Tr("MDS.WindowMinimize"), windowControls);
	fullscreenButton = new QPushButton(Tr("MDS.WindowExitFullscreen"), windowControls);
	auto *quit = new QPushButton(Tr("MDS.WindowQuit"), windowControls);
	quit->setToolTip(Tr("MDS.WindowQuitTip"));
	connect(minimize, &QPushButton::clicked, this, [this]() { showMinimized(); });
	connect(fullscreenButton, &QPushButton::clicked, this, [this]() {
		if (isFullScreen())
			showMaximized();
		else
			showFullScreen();
		UpdateWindowControls();
	});
	connect(quit, &QPushButton::clicked, this, &StudioWindow::Close);
	layout->addWidget(minimize);
	layout->addWidget(fullscreenButton);
	layout->addWidget(quit);
	windowControls->hide();

	/* La souris dans le coin haut droit fait apparaître les commandes ;
	 * elles disparaissent quand elle s'en éloigne. */
	auto *timer = new QTimer(this);
	connect(timer, &QTimer::timeout, this, &StudioWindow::UpdateWindowControls);
	timer->start(120);
}

void StudioWindow::UpdateWindowControls()
{
	if (!windowControls)
		return;

	/* Texte d'info selon l'état de la fenêtre. */
	if (hintText) {
		const QString hint = isFullScreen() ? Tr("MDS.FullscreenHintExit") : Tr("MDS.FullscreenHint");
		if (hintText->text() != hint) {
			hintText->setText(hint);
			PlaceFullscreenHint();
		}
	}

	if (!isVisible() || !isFullScreen() || isMinimized()) {
		windowControls->hide();
		return;
	}
	fullscreenButton->setText(Tr("MDS.WindowExitFullscreen"));
	windowControls->adjustSize();
	const QSize size = windowControls->sizeHint();
	windowControls->setGeometry(width() - size.width(), 0, size.width(), size.height());

	const QPoint p = mapFromGlobal(QCursor::pos());
	const bool shown = windowControls->isVisible();
	const int zoneW = std::max(size.width() + 60, 320);
	const int zoneH = shown ? size.height() + 30 : 36;
	const bool inZone = rect().contains(p) && p.x() >= width() - zoneW && p.y() <= zoneH;
	if (inZone && !shown) {
		windowControls->show();
		windowControls->raise();
	} else if (!inZone && shown) {
		windowControls->hide();
	}
}

/* ------------------------------------------------------------------ */
/* Ouverture / fermeture                                                */
/* ------------------------------------------------------------------ */

void StudioWindow::Toggle()
{
	if (isVisible())
		Close();
	else
		Open();
}

void StudioWindow::Open()
{
	/* La destination par défaut est protégée comme les autres par le mode
	 * Studio d'OBS (« Dupliquer la scène ») : on l'active si besoin. */
	if (!obs_frontend_preview_program_mode_active()) {
		obs_frontend_set_preview_program_mode(true);
		enabledObsStudioMode = true;
	}

	Manager::Get().Sync();
	RefreshScenes();
	RefreshTransitions();
	mixer->Refresh();

	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
#ifdef Q_OS_MACOS
	/* macOS : grande fenêtre (pas de plein écran natif, qui ouvre un
	 * Space séparé et fait basculer vers OBS à la fermeture des
	 * fenêtres). F11 reste disponible. */
	if (main && main->screen())
		setGeometry(main->screen()->availableGeometry());
	showMaximized();
#else
	if (main && main->screen())
		setGeometry(main->screen()->geometry());
	showFullScreen();
#endif
	raise();
	activateWindow();
	/* Tailles par défaut des docks, une fois la taille réelle connue. */
	if (defaultDocksPending)
		QTimer::singleShot(150, this, [this]() {
			if (!defaultDocksPending)
				return;
			defaultDocksPending = false;
			ApplyDefaultDockSizes();
		});

	UpdateGrids(true);
}

void StudioWindow::Close()
{
	pvwGrid->Clear();
	pgmGrid->Clear();
	UpdateShowing({});
	mixer->Clear();
	mediaControls->SetSource(nullptr);
	hide();

	if (enabledObsStudioMode) {
		obs_frontend_set_preview_program_mode(false);
		enabledObsStudioMode = false;
	}
}

void StudioWindow::closeEvent(QCloseEvent *event)
{
	event->ignore();
	Close();
}

void StudioWindow::Shutdown()
{
	for (auto &p : projectors) {
		if (p.second)
			p.second->close();
	}
	projectors.clear();
	if (pvwGrid)
		pvwGrid->Clear();
	if (pgmGrid)
		pgmGrid->Clear();
	if (mixer)
		mixer->Clear();
	UpdateShowing({});
	Undo::Clear();
}

void StudioWindow::OnReset()
{
	for (auto &p : projectors) {
		if (p.second)
			p.second->close();
	}
	projectors.clear();
	pvwGrid->Clear();
	pgmGrid->Clear();
	mixer->Clear();
	mediaControls->SetSource(nullptr);
	UpdateShowing({});
	Undo::Clear();
	activeDest = nullptr;
	focusDest = nullptr;
	const QSignalBlocker b1(sceneList);
	const QSignalBlocker b2(sourceList);
	sceneList->clear();
	sourceList->clear();
}

QByteArray StudioWindow::SaveLayout() const
{
	return saveState();
}

void StudioWindow::RestoreLayout(const QByteArray &state)
{
	if (!state.isEmpty() && restoreState(state))
		defaultDocksPending = false;
}

void StudioWindow::UpdateControls()
{
	const bool mainDisabled = Manager::Get().Main() && Manager::Get().Main()->Disabled();
	streamButton->setText(obs_frontend_streaming_active() ? Tr("MDS.StopStreaming") : Tr("MDS.StartStreaming"));
	recordButton->setText(obs_frontend_recording_active() ? Tr("MDS.StopRecording") : Tr("MDS.StartRecording"));

	/* Destination par défaut désactivée : boutons visibles mais grisés ;
	 * survol et clic rappellent comment la réactiver. */
	for (QPushButton *b : {streamButton, recordButton}) {
		b->setStyleSheet(mainDisabled ? "QPushButton { color: gray; }" : "");
		b->setToolTip(mainDisabled ? Tr("MDS.ActivateDefaultDestination") : QString());
	}
}

void StudioWindow::ShowMainDisabledHint(QWidget *anchor)
{
	QToolTip::showText(anchor->mapToGlobal(QPoint(0, anchor->height())), Tr("MDS.ActivateDefaultDestination"),
			   anchor);
}

/* ------------------------------------------------------------------ */
/* Aperçus                                                              */
/* ------------------------------------------------------------------ */

void StudioWindow::UpdateShowing(const std::vector<OBSSource> &next)
{
	/* Les scènes en Aperçu doivent être « showing » pour que leurs
	 * sources se rendent même hors antenne. */
	for (auto &s : next) {
		if (s)
			obs_source_inc_showing(s);
	}
	for (auto &s : showing) {
		if (s)
			obs_source_dec_showing(s);
	}
	showing = next;
}

void StudioWindow::UpdateGrids(bool force)
{
	if (!isVisible()) {
		RefreshScenes();
		return;
	}

	Manager &m = Manager::Get();
	OBSSource preview = m.PreviewScene();

	std::vector<Destination *> covered;
	std::vector<OBSSource> linked;
	if (preview) {
		for (auto *d : m.ActiveDestinations()) {
			OBSSource l = d->LinkedScene(preview);
			/* Réglage : toutes les destinations, ou seulement celles de
			 * la scène sélectionnée. */
			if (!l && !m.ShowAllDestinations())
				continue;
			/* Vue agrandie : seulement la destination choisie. */
			if (focusDest && d != focusDest)
				continue;
			covered.push_back(d);
			linked.push_back(l);
		}
	}
	if (focusDest && covered.empty())
		focusDest = nullptr;

	bool same = !force && pvwGrid->Tiles().size() == covered.size();
	for (size_t i = 0; same && i < covered.size(); i++) {
		TileWidget *t = pvwGrid->Tiles()[i];
		same = t->Dest() == covered[i] && t->LinkedScene().Get() == linked[i].Get();
	}
	if (same) {
		RefreshScenes();
		return;
	}

	for (auto *t : pvwGrid->SetDestinations(covered))
		ConnectTile(t);
	for (auto *t : pgmGrid->SetDestinations(covered))
		ConnectTile(t);
	for (size_t i = 0; i < covered.size(); i++) {
		pvwGrid->Tiles()[i]->SetLinkedScene(linked[i]);
		pvwGrid->Tiles()[i]->SetFocused(focusDest != nullptr);
		pgmGrid->Tiles()[i]->SetFocused(focusDest != nullptr);
		pgmGrid->Tiles()[i]->SetEditable(modifyProgram);
	}
	UpdateShowing(linked);

	TileWidget *active = ActiveTile();
	if (!active || (active->IsProgram() && !modifyProgram))
		active = pvwGrid->TileFor(activeDest);
	if (!active && !pvwGrid->Tiles().empty())
		active = pvwGrid->Tiles().front();
	SetActiveTile(active);
	RefreshSources();
	RefreshScenes();
}

void StudioWindow::ConnectTile(TileWidget *tile)
{
	connect(tile, &TileWidget::activated, this, [this](TileWidget *t) { SetActiveTile(t); });
	connect(tile, &TileWidget::selectionChanged, this, [this](TileWidget *t, qint64 id) {
		if (t != ActiveTile())
			return;
		const QSignalBlocker blocker(sourceList);
		sourceList->clearSelection();
		for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
			if (sourceList->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == id)
				sourceList->setCurrentItem(sourceList->topLevelItem(i));
		}
		UpdateMediaControls();
	});
	connect(tile, &TileWidget::itemsChanged, this, [this](TileWidget *t) {
		if (t->IsProgram())
			t->Dest()->KeepLiveEdits();
		if (t == ActiveTile())
			RefreshSources();
	});
	connect(tile, &TileWidget::focusRequested, this, [this](TileWidget *t) { ToggleFocus(t->Dest()); });
	connect(tile, &TileWidget::contextMenuRequested, this,
		[this](TileWidget *, const QPoint &pos) { ShowItemMenu(pos); });
}

void StudioWindow::SetActiveTile(TileWidget *tile)
{
	Destination *dest = tile ? tile->Dest() : nullptr;
	const bool isProgram = tile && tile->IsProgram();
	const bool changed = dest != activeDest || isProgram != activeIsProgram;
	activeDest = dest;
	activeIsProgram = isProgram;

	for (auto *t : pvwGrid->Tiles())
		t->SetActive(t == tile);
	for (auto *t : pgmGrid->Tiles())
		t->SetActive(t == tile);
	if (changed) {
		RefreshSources();
		UpdateMediaControls();
	}
}

TileWidget *StudioWindow::ActiveTile() const
{
	if (!activeDest)
		return nullptr;
	return (activeIsProgram ? pgmGrid : pvwGrid)->TileFor(activeDest);
}

obs_scene_t *StudioWindow::ActiveScene() const
{
	TileWidget *tile = ActiveTile();
	OBSSource scene = tile ? tile->EditScene() : OBSSource();
	/* La scène reste vivante tant qu'elle est affichée dans la tuile. */
	return scene ? obs_scene_from_source(scene) : nullptr;
}

void StudioWindow::SetModifyProgram(bool on)
{
	modifyProgram = on;
	for (auto *t : pgmGrid->Tiles())
		t->SetEditable(on);
	if (!on && activeIsProgram)
		SetActiveTile(pvwGrid->TileFor(activeDest));
	RefreshSources();
}

/* Un clic ailleurs que dans un aperçu ou la liste des sources efface les
 * contours et la sélection affichés (la sélection est gardée). */
void StudioWindow::PlaceFullscreenHint()
{
	if (!fullscreenHint || !settingsMenuAction)
		return;
	QMenuBar *bar = menuBar();
	const QRect last = bar->actionGeometry(settingsMenuAction);
	fullscreenHint->adjustSize();
	const QSize size = fullscreenHint->sizeHint();
	fullscreenHint->setGeometry(last.right() + 18, (bar->height() - size.height()) / 2, size.width(),
				    size.height());
	fullscreenHint->setVisible(last.isValid() && last.right() + 18 + size.width() < bar->width());
}

bool StudioWindow::eventFilter(QObject *watched, QEvent *event)
{
	if (fullscreenHint && watched == menuBar() &&
	    (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
	     event->type() == QEvent::LayoutRequest))
		QTimer::singleShot(0, this, &StudioWindow::PlaceFullscreenHint);

	/* Une fenêtre du studio se ferme (dialogue, dock flottant, sortie) :
	 * si plus aucune fenêtre n'est active, le studio reprend le focus au
	 * lieu de laisser le système passer à une autre application (macOS). */
	if ((event->type() == QEvent::Hide || event->type() == QEvent::Close) && isVisible() && !isMinimized()) {
		auto *w = qobject_cast<QWidget *>(watched);
		if (w && w != this && w->isWindow() && w->windowType() != Qt::Popup && w->windowType() != Qt::ToolTip &&
		    w->windowType() != Qt::SplashScreen && !w->inherits("QMenu") &&
		    QGuiApplication::applicationState() == Qt::ApplicationActive) {
			QTimer::singleShot(0, this, [this]() {
				if (isVisible() && !isMinimized() && !QApplication::activeWindow() &&
				    !QApplication::activeModalWidget()) {
					raise();
					activateWindow();
				}
			});
		}
	}

	if (event->type() == QEvent::MouseButtonPress && isVisible()) {
		auto *w = qobject_cast<QWidget *>(watched);
		if (w && w->window() == this) {
			bool inside = false;
			for (QWidget *p = w; p; p = p->parentWidget()) {
				if (qobject_cast<TileWidget *>(p) || p == sourceList) {
					inside = true;
					break;
				}
			}
			SetHighlight(inside);
		}
	}
	return QMainWindow::eventFilter(watched, event);
}

void StudioWindow::SetHighlight(bool on)
{
	for (auto *t : pvwGrid->Tiles())
		t->SetHighlighted(on);
	for (auto *t : pgmGrid->Tiles())
		t->SetHighlighted(on);
}

void StudioWindow::AfterEdit()
{
	if (activeIsProgram && activeDest)
		activeDest->KeepLiveEdits();
}

void StudioWindow::ToggleFocus(Destination *dest)
{
	focusDest = (focusDest == dest) ? nullptr : dest;
	UpdateGrids(true);
}

/* Toutes les lignes « scène » de la liste, dossiers compris. */
static std::vector<QTreeWidgetItem *> SceneRows(QTreeWidget *tree)
{
	std::vector<QTreeWidgetItem *> rows;
	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		QTreeWidgetItem *top = tree->topLevelItem(i);
		if (top->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_SCENE)
			rows.push_back(top);
		for (int k = 0; k < top->childCount(); k++)
			rows.push_back(top->child(k));
	}
	return rows;
}

void StudioWindow::RefreshTally()
{
	for (auto *t : pgmGrid->Tiles())
		t->RefreshHeader();

	OBSSource program = Manager::Get().ProgramScene();
	const char *pgmUuid = program ? obs_source_get_uuid(program) : nullptr;
	for (QTreeWidgetItem *row : SceneRows(sceneList)) {
		QFont font = row->font(0);
		font.setBold(pgmUuid && row->data(0, Qt::UserRole).toString() == QString::fromUtf8(pgmUuid));
		row->setFont(0, font);
	}
}

/* ------------------------------------------------------------------ */
/* Scènes                                                               */
/* ------------------------------------------------------------------ */

static QTreeWidgetItem *MakeSceneRow(obs_source_t *scene, const std::vector<Destination *> &dests)
{
	auto *row = new QTreeWidgetItem();
	row->setText(0, QString::fromUtf8(obs_source_get_name(scene)));
	row->setData(0, Qt::UserRole, QString::fromUtf8(obs_source_get_uuid(scene)));
	row->setData(0, Qt::UserRole + 1, SCENE_ROW_SCENE);
	row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled);
	QStringList names;
	for (auto *d : dests) {
		if (d->Covers(scene))
			names << QString::fromStdString(d->Name());
	}
	row->setToolTip(0, Tr("MDS.ShownOn").arg(names.isEmpty() ? Tr("MDS.NoDestination") : names.join(", ")));
	return row;
}

static QTreeWidgetItem *MakeFolderRow(const QString &name)
{
	auto *row = new QTreeWidgetItem();
	row->setText(0, name);
	row->setData(0, Qt::UserRole, name);
	row->setData(0, Qt::UserRole + 1, SCENE_ROW_FOLDER);
	row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
	row->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
	QFont font = row->font(0);
	font.setItalic(true);
	row->setFont(0, font);
	return row;
}

/* Ordre des scènes : c'est la liste de scènes d'OBS qui fait foi (elle est
 * enregistrée avec la collection). L'API d'OBS ne permet pas de la
 * réordonner : on déplace directement ses lignes, sans changer la scène
 * sélectionnée dans OBS. */
static bool ReorderObsScenes(const QStringList &order)
{
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	QListWidget *list = main ? main->findChild<QListWidget *>("scenes") : nullptr;
	if (!list)
		return false;

	QListWidgetItem *current = list->currentItem();
	const QSignalBlocker blockList(list);
	const QSignalBlocker blockSelection(list->selectionModel());
	for (int i = 0; i < order.size() && i < list->count(); i++) {
		int found = -1;
		for (int k = i; k < list->count(); k++) {
			if (list->item(k)->text() == order[i]) {
				found = k;
				break;
			}
		}
		if (found < 0 || found == i)
			continue;
		QListWidgetItem *item = list->takeItem(found);
		list->insertItem(i, item);
	}
	if (current)
		list->setCurrentItem(current);
	return true;
}

void StudioWindow::RefreshScenes()
{
	Manager &m = Manager::Get();
	OBSSource preview = m.PreviewScene();
	auto dests = m.ActiveDestinations();
	const auto scenes = Manager::MainScenes();

	auto findScene = [&](const char *uuid) -> obs_source_t * {
		for (auto &sc : scenes) {
			if (uuid && strcmp(obs_source_get_uuid(sc), uuid) == 0)
				return sc;
		}
		return nullptr;
	};

	const QSignalBlocker blocker(sceneList);
	refreshingScenes = true;
	sceneList->clear();

	/* Dossiers et ordre enregistrés, puis les scènes qui n'y figurent pas
	 * encore (créées entre-temps), à la fin. */
	std::set<obs_source_t *> placed;
	std::vector<std::pair<QTreeWidgetItem *, bool>> folders;
	obs_data_array_t *tree = m.SceneTree();
	const size_t count = tree ? obs_data_array_count(tree) : 0;
	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease entry = obs_data_array_item(tree, i);
		const char *folder = obs_data_get_string(entry, "folder");
		if (folder && *folder) {
			QTreeWidgetItem *f = MakeFolderRow(QString::fromUtf8(folder));
			sceneList->addTopLevelItem(f);
			OBSDataArrayAutoRelease children = obs_data_get_array(entry, "scenes");
			for (size_t k = 0; k < obs_data_array_count(children); k++) {
				OBSDataAutoRelease c = obs_data_array_item(children, k);
				obs_source_t *sc = findScene(obs_data_get_string(c, "uuid"));
				if (sc && !placed.count(sc)) {
					f->addChild(MakeSceneRow(sc, dests));
					placed.insert(sc);
				}
			}
			folders.emplace_back(f, obs_data_get_bool(entry, "expanded"));
		} else {
			obs_source_t *sc = findScene(obs_data_get_string(entry, "scene"));
			if (sc && !placed.count(sc)) {
				sceneList->addTopLevelItem(MakeSceneRow(sc, dests));
				placed.insert(sc);
			}
		}
	}
	for (auto &sc : scenes) {
		if (!placed.count(sc))
			sceneList->addTopLevelItem(MakeSceneRow(sc, dests));
	}
	for (auto &[f, expanded] : folders)
		f->setExpanded(expanded);

	for (QTreeWidgetItem *row : SceneRows(sceneList)) {
		if (preview && row->data(0, Qt::UserRole).toString() == QString::fromUtf8(obs_source_get_uuid(preview)))
			sceneList->setCurrentItem(row);
	}
	refreshingScenes = false;

	/* L'ordre d'OBS suit la liste (dossiers dépliés). */
	QStringList order;
	for (QTreeWidgetItem *row : SceneRows(sceneList))
		order << row->text(0);
	QStringList obsOrder;
	for (auto &sc : scenes)
		obsOrder << QString::fromUtf8(obs_source_get_name(sc));
	if (order != obsOrder)
		ReorderObsScenes(order);

	RefreshTally();
}

void StudioWindow::SaveSceneTree()
{
	OBSDataArrayAutoRelease tree = obs_data_array_create();
	for (int i = 0; i < sceneList->topLevelItemCount(); i++) {
		QTreeWidgetItem *top = sceneList->topLevelItem(i);
		OBSDataAutoRelease entry = obs_data_create();
		if (top->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER) {
			obs_data_set_string(entry, "folder", top->text(0).toUtf8().constData());
			obs_data_set_bool(entry, "expanded", top->isExpanded());
			OBSDataArrayAutoRelease children = obs_data_array_create();
			for (int k = 0; k < top->childCount(); k++) {
				OBSDataAutoRelease c = obs_data_create();
				obs_data_set_string(c, "uuid",
						    top->child(k)->data(0, Qt::UserRole).toString().toUtf8());
				obs_data_array_push_back(children, c);
			}
			obs_data_set_array(entry, "scenes", children);
		} else {
			obs_data_set_string(entry, "scene", top->data(0, Qt::UserRole).toString().toUtf8().constData());
		}
		obs_data_array_push_back(tree, entry);
	}
	Manager::Get().SetSceneTree(tree);
}

void StudioWindow::ApplySceneOrderFromList()
{
	sceneOrderPending = false;
	/* Pas de dossier dans un dossier, ni de scène dans une scène : on
	 * remet à plat ce que le glisser-déposer aurait imbriqué. */
	{
		const QSignalBlocker blocker(sceneList);
		refreshingScenes = true;
		for (int i = 0; i < sceneList->topLevelItemCount(); i++) {
			QTreeWidgetItem *top = sceneList->topLevelItem(i);
			const bool isFolder = top->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER;
			for (int k = top->childCount() - 1; k >= 0; k--) {
				QTreeWidgetItem *child = top->child(k);
				const bool childFolder = child->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER;
				if (!isFolder || childFolder) {
					top->takeChild(k);
					sceneList->insertTopLevelItem(i + 1, child);
				}
			}
		}
		refreshingScenes = false;
	}
	SaveSceneTree();
	RefreshScenes();
}

void StudioWindow::MoveScene(int delta)
{
	QTreeWidgetItem *item = sceneList->currentItem();
	if (!item)
		return;
	QTreeWidgetItem *parent = item->parent();
	const int count = parent ? parent->childCount() : sceneList->topLevelItemCount();
	const int row = parent ? parent->indexOfChild(item) : sceneList->indexOfTopLevelItem(item);
	const int target = std::clamp(row + delta, 0, count - 1);
	if (target == row)
		return;
	{
		const QSignalBlocker blocker(sceneList);
		refreshingScenes = true;
		const bool expanded = item->isExpanded();
		if (parent) {
			parent->takeChild(row);
			parent->insertChild(target, item);
		} else {
			sceneList->takeTopLevelItem(row);
			sceneList->insertTopLevelItem(target, item);
		}
		item->setExpanded(expanded);
		sceneList->setCurrentItem(item);
		refreshingScenes = false;
	}
	ApplySceneOrderFromList();
}

void StudioWindow::NewSceneFolder()
{
	bool ok = false;
	int n = 1;
	for (int i = 0; i < sceneList->topLevelItemCount(); i++) {
		if (sceneList->topLevelItem(i)->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER)
			n++;
	}
	const QString name = QInputDialog::getText(this, Tr("MDS.NewSceneFolder"), Tr("MDS.Name"), QLineEdit::Normal,
						   QString("%1 %2").arg(Tr("MDS.FolderName")).arg(n), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;

	/* Les scènes sélectionnées entrent dans le nouveau dossier. */
	std::vector<QTreeWidgetItem *> selected;
	for (QTreeWidgetItem *row : sceneList->selectedItems()) {
		if (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_SCENE)
			selected.push_back(row);
	}
	{
		const QSignalBlocker blocker(sceneList);
		refreshingScenes = true;
		QTreeWidgetItem *folder = MakeFolderRow(name);
		int at = sceneList->topLevelItemCount();
		if (!selected.empty()) {
			QTreeWidgetItem *first = selected.front();
			at = sceneList->indexOfTopLevelItem(first->parent() ? first->parent() : first);
		}
		sceneList->insertTopLevelItem(std::max(0, at), folder);
		for (QTreeWidgetItem *row : selected) {
			if (QTreeWidgetItem *p = row->parent())
				p->removeChild(row);
			else
				sceneList->takeTopLevelItem(sceneList->indexOfTopLevelItem(row));
			folder->addChild(row);
		}
		folder->setExpanded(true);
		refreshingScenes = false;
	}
	ApplySceneOrderFromList();
}

void StudioWindow::RenameSceneFolder(QTreeWidgetItem *folder)
{
	if (!folder)
		return;
	bool ok = false;
	const QString name = QInputDialog::getText(this, Tr("MDS.RenameFolder"), Tr("MDS.Name"), QLineEdit::Normal,
						   folder->text(0), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	folder->setText(0, name);
	folder->setData(0, Qt::UserRole, name);
	SaveSceneTree();
}

void StudioWindow::ApplySourceOrderFromList()
{
	sourceOrderPending = false;
	obs_scene_t *s = ActiveScene();
	if (!s)
		return;
	if (TileWidget *tile = ActiveTile()) {
		if (QTreeWidgetItem *row = sourceList->currentItem(); row && !row->parent())
			tile->SetSelectedItem(row->data(0, Qt::UserRole).toLongLong());
	}
	Undo::PushSceneState(s);
	/* Un élément lâché dans un autre est remis au premier niveau. */
	{
		const QSignalBlocker blocker(sourceList);
		for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
			QTreeWidgetItem *top = sourceList->topLevelItem(i);
			for (int k = top->childCount() - 1; k >= 0; k--) {
				QTreeWidgetItem *child = top->child(k);
				if (child->data(0, Qt::UserRole + 1).toLongLong() !=
				    top->data(0, Qt::UserRole).toLongLong()) {
					top->takeChild(k);
					sourceList->insertTopLevelItem(i + 1, child);
				}
			}
		}
	}
	/* Haut de la liste = devant : on place du fond vers l'avant. */
	const int n = sourceList->topLevelItemCount();
	for (int pos = 0; pos < n; pos++) {
		QTreeWidgetItem *row = sourceList->topLevelItem(n - 1 - pos);
		obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(s, row->data(0, Qt::UserRole).toLongLong());
		if (item)
			obs_sceneitem_set_order_position(item, pos);
	}
	RefreshSources();
	AfterEdit();
}

OBSSource StudioWindow::SelectedScene() const
{
	QTreeWidgetItem *row = sceneList->currentItem();
	if (!row || row->data(0, Qt::UserRole + 1).toInt() != SCENE_ROW_SCENE)
		return nullptr;
	OBSSourceAutoRelease s = obs_get_source_by_uuid(row->data(0, Qt::UserRole).toString().toUtf8().constData());
	return OBSSource(s.Get());
}

void StudioWindow::AddScene()
{
	std::string name = QString("%1 %2").arg(Tr("MDS.Scene")).arg(Manager::MainScenes().size() + 1).toStdString();
	std::set<Destination *> coverage;
	while (true) {
		if (!Dialogs::EditScene(this, nullptr, name, coverage, true))
			return;
		OBSSource scene = Manager::Get().CreateScene(name, coverage);
		if (scene) {
			Manager::Get().SetPreviewScene(scene);
			return;
		}
		QMessageBox::warning(this, Tr("MDS.NewScene"), Tr("MDS.SceneExists"));
	}
}

void StudioWindow::AskCoverage(obs_source_t *scene)
{
	if (!scene)
		return;
	const char *n = obs_source_get_name(scene);
	std::string name = n ? n : "";
	std::set<Destination *> coverage;
	QWidget *parent = isVisible() ? this : static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!Dialogs::EditScene(parent, scene, name, coverage, true))
		return;
	Manager::Get().SetCoverage(scene, coverage);
	if (n && name != n)
		Manager::Get().RenameScene(scene, name);
}

void StudioWindow::EditScene(obs_source_t *scene)
{
	if (!scene)
		return;
	const std::string oldName = obs_source_get_name(scene);
	std::string name = oldName;
	std::set<Destination *> coverage;
	if (!Dialogs::EditScene(this, scene, name, coverage, false))
		return;

	bool dropping = false;
	for (auto *d : Manager::Get().ActiveDestinations()) {
		if (!d->IsMain() && d->Covers(scene) && !coverage.count(d))
			dropping = true;
	}
	if (dropping &&
	    QMessageBox::question(this, Tr("MDS.SceneSettings"), Tr("MDS.ConfirmUncover")) != QMessageBox::Yes)
		return;

	Manager::Get().SetCoverage(scene, coverage);
	if (name != oldName)
		Manager::Get().RenameScene(scene, name);
}

void StudioWindow::RemoveScene(obs_source_t *scene)
{
	if (!scene)
		return;
	const QString name = QString::fromUtf8(obs_source_get_name(scene));
	if (QMessageBox::question(this, Tr("MDS.RemoveScene"), Tr("MDS.ConfirmRemoveScene").arg(name)) !=
	    QMessageBox::Yes)
		return;
	Manager::Get().RemoveScene(scene);
}

void StudioWindow::RemoveSelectedScenes()
{
	std::vector<OBSSource> scenes;
	std::vector<QTreeWidgetItem *> folders;
	for (QTreeWidgetItem *row : sceneList->selectedItems()) {
		if (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER) {
			folders.push_back(row);
			continue;
		}
		OBSSourceAutoRelease s =
			obs_get_source_by_uuid(row->data(0, Qt::UserRole).toString().toUtf8().constData());
		if (s)
			scenes.emplace_back(s.Get());
	}

	/* Dossiers : seul le dossier est retiré, ses scènes restent. */
	if (!folders.empty()) {
		const QString question = folders.size() == 1
						 ? Tr("MDS.ConfirmRemoveFolder").arg(folders.front()->text(0))
						 : Tr("MDS.ConfirmRemoveFolders").arg(int(folders.size()));
		if (QMessageBox::question(this, Tr("MDS.RemoveFolder"), question) == QMessageBox::Yes) {
			const QSignalBlocker blocker(sceneList);
			refreshingScenes = true;
			for (QTreeWidgetItem *f : folders) {
				int at = sceneList->indexOfTopLevelItem(f);
				while (f->childCount() > 0)
					sceneList->insertTopLevelItem(++at, f->takeChild(0));
				delete sceneList->takeTopLevelItem(sceneList->indexOfTopLevelItem(f));
			}
			refreshingScenes = false;
			ApplySceneOrderFromList();
		}
		if (scenes.empty())
			return;
	}

	if (scenes.empty())
		return;
	if (scenes.size() == 1) {
		RemoveScene(scenes.front());
		return;
	}
	if (QMessageBox::question(this, Tr("MDS.RemoveScene"), Tr("MDS.ConfirmRemoveScenes").arg(int(scenes.size()))) !=
	    QMessageBox::Yes)
		return;
	for (auto &scene : scenes)
		Manager::Get().RemoveScene(scene);
}

/* ------------------------------------------------------------------ */
/* Sources                                                              */
/* ------------------------------------------------------------------ */

static QTreeWidgetItem *MakeSourceRow(obs_sceneitem_t *item, int64_t parentId)
{
	obs_source_t *src = obs_sceneitem_get_source(item);
	auto *row = new QTreeWidgetItem();
	row->setText(0, QString::fromUtf8(obs_source_get_name(src)));
	Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsUserCheckable;
	/* Seuls les éléments de premier niveau se réordonnent à la souris ;
	 * rien ne se dépose dans un groupe (Grouper / Dissocier pour cela). */
	if (!parentId)
		flags |= Qt::ItemIsDragEnabled;
	row->setFlags(flags);
	row->setCheckState(0, obs_sceneitem_visible(item) ? Qt::Checked : Qt::Unchecked);
	row->setData(0, Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
	row->setData(0, Qt::UserRole + 1, qlonglong(parentId));
	if (obs_sceneitem_is_group(item))
		row->setIcon(0, QApplication::style()->standardIcon(QStyle::SP_DirIcon));
	return row;
}

void StudioWindow::RefreshSources()
{
	refreshingSources = true;
	const int64_t childSelected = sourceList->currentItem() &&
						      sourceList->currentItem()->data(0, Qt::UserRole + 1).toLongLong()
					      ? sourceList->currentItem()->data(0, Qt::UserRole).toLongLong()
					      : -1;
	sourceList->clear();

	TileWidget *tile = ActiveTile();
	obs_scene_t *s = ActiveScene();

	if (auto *dock = qobject_cast<QDockWidget *>(sourceList->parentWidget()->parentWidget())) {
		QString title = Tr("MDS.Sources");
		if (activeDest) {
			title = Tr("MDS.SourcesOf").arg(QString::fromStdString(activeDest->Name()));
			if (activeIsProgram)
				title += "  " + Tr("MDS.LiveSuffix");
		}
		dock->setWindowTitle(title);
	}
	addSourceButton->setEnabled(s != nullptr);

	if (s && tile) {
		const int64_t selected = tile->SelectedItem();
		/* Haut de la pile en premier, comme OBS. */
		std::vector<obs_sceneitem_t *> items;
		obs_scene_enum_items(
			s,
			[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
				static_cast<std::vector<obs_sceneitem_t *> *>(param)->insert(
					static_cast<std::vector<obs_sceneitem_t *> *>(param)->begin(), item);
				return true;
			},
			&items);
		for (obs_sceneitem_t *item : items) {
			QTreeWidgetItem *row = MakeSourceRow(item, 0);
			sourceList->addTopLevelItem(row);
			const int64_t id = obs_sceneitem_get_id(item);
			if (obs_sceneitem_is_group(item)) {
				std::vector<obs_sceneitem_t *> children;
				obs_scene_enum_items(
					obs_sceneitem_group_get_scene(item),
					[](obs_scene_t *, obs_sceneitem_t *child, void *param) {
						static_cast<std::vector<obs_sceneitem_t *> *>(param)->insert(
							static_cast<std::vector<obs_sceneitem_t *> *>(param)->begin(),
							child);
						return true;
					},
					&children);
				for (obs_sceneitem_t *child : children) {
					QTreeWidgetItem *c = MakeSourceRow(child, id);
					row->addChild(c);
					if (id == selected && obs_sceneitem_get_id(child) == childSelected)
						sourceList->setCurrentItem(c);
				}
				OBSDataAutoRelease priv = obs_sceneitem_get_private_settings(item);
				row->setExpanded(!obs_data_get_bool(priv, "collapsed"));
			}
			if (id == selected && !sourceList->currentItem())
				sourceList->setCurrentItem(row);
		}
	}
	refreshingSources = false;
	UpdateMediaControls();
}

OBSSceneItem StudioWindow::ItemFromRow(QTreeWidgetItem *row) const
{
	obs_scene_t *s = ActiveScene();
	if (!row || !s)
		return nullptr;
	const int64_t id = row->data(0, Qt::UserRole).toLongLong();
	const int64_t parent = row->data(0, Qt::UserRole + 1).toLongLong();
	if (!parent)
		return OBSSceneItem(obs_scene_find_sceneitem_by_id(s, id));
	obs_sceneitem_t *group = obs_scene_find_sceneitem_by_id(s, parent);
	if (!group || !obs_sceneitem_is_group(group))
		return nullptr;
	return OBSSceneItem(obs_scene_find_sceneitem_by_id(obs_sceneitem_group_get_scene(group), id));
}

OBSSceneItem StudioWindow::CurrentItem() const
{
	TileWidget *tile = ActiveTile();
	obs_scene_t *s = ActiveScene();
	if (!s || !tile)
		return nullptr;
	int64_t id = tile->SelectedItem();
	/* Élément d'un groupe choisi dans la liste : c'est lui qu'on édite. */
	if (QTreeWidgetItem *row = sourceList->currentItem()) {
		const int64_t parent = row->data(0, Qt::UserRole + 1).toLongLong();
		if (parent && (id < 0 || id == parent))
			return ItemFromRow(row);
		if (id < 0)
			id = row->data(0, Qt::UserRole).toLongLong();
	}
	return id >= 0 ? OBSSceneItem(obs_scene_find_sceneitem_by_id(s, id)) : OBSSceneItem();
}

void StudioWindow::SelectItem(int64_t id)
{
	if (TileWidget *t = ActiveTile())
		t->SetSelectedItem(id);
	RefreshSources();
}

void StudioWindow::UpdateMediaControls()
{
	OBSSceneItem item = CurrentItem();
	mediaControls->SetSource(item ? obs_sceneitem_get_source(item) : nullptr);
}

void StudioWindow::AddExistingSource(obs_source_t *source, bool freshMedia)
{
	obs_scene_t *s = ActiveScene();
	if (!source || !s || !activeDest)
		return;

	obs_sceneitem_t *item = obs_scene_add(s, source);
	if (!item)
		return;
	/* Les sources gardent leur taille d'origine, centrées dans la
	 * destination (alignement au centre : même si la taille n'est connue
	 * qu'après la première image, la source reste centrée). Pas
	 * d'adaptation automatique au cadre. */
	obs_sceneitem_set_alignment(item, OBS_ALIGN_CENTER);
	struct vec2 pos;
	vec2_set(&pos, float(activeDest->Width()) * 0.5f, float(activeDest->Height()) * 0.5f);
	obs_sceneitem_set_pos(item, &pos);

	/* Média tout juste créé : on le met en pause au début pour afficher
	 * sa première image sans le lire. Il repart du début en passant à
	 * l'antenne (« Redémarrer quand la source devient active »). */
	if (freshMedia && (obs_source_get_output_flags(source) & OBS_SOURCE_CONTROLLABLE_MEDIA)) {
		OBSWeakSource weak = OBSGetWeakRef(source);
		QTimer::singleShot(400, this, [weak]() {
			OBSSource src = OBSGetStrongRef(weak);
			if (src && !obs_source_active(src)) {
				obs_source_media_play_pause(src, true);
				obs_source_media_set_time(src, 0);
			}
		});
	}

	const int64_t id = obs_sceneitem_get_id(item);
	Undo::PushAdded(s, id);
	SelectItem(id);
	AfterEdit();
}

void StudioWindow::CreateNewSource(const char *typeId)
{
	if (!ActiveScene())
		return;

	const QString base = QString::fromUtf8(obs_source_get_display_name(typeId));
	bool ok = false;
	QString name = QInputDialog::getText(this, Tr("MDS.NewSourceTitle").arg(base), Tr("MDS.Name"),
					     QLineEdit::Normal, UniqueSourceName(base), &ok)
			       .trimmed();
	if (!ok || name.isEmpty())
		return;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
	if (existing) {
		QMessageBox::warning(this, Tr("MDS.AddSource"), Tr("MDS.SourceExists"));
		return;
	}

	OBSSourceAutoRelease source = obs_source_create(typeId, name.toUtf8().constData(), nullptr, nullptr);
	if (!source)
		return;
	AddExistingSource(source);
	obs_frontend_open_source_properties(source);
}

void StudioWindow::AddFiles(const QStringList &paths, TileWidget *target)
{
	if (target)
		SetActiveTile(target);
	if (!ActiveScene()) {
		QMessageBox::information(this, Tr("MDS.AddSource"), Tr("MDS.SelectSceneFirst"));
		return;
	}

	for (const QString &path : paths) {
		const char *key = nullptr;
		const char *type = SourceTypeForFile(path, &key);
		if (!type)
			continue;

		OBSDataAutoRelease settings = obs_data_create();
		obs_data_set_string(settings, key, path.toUtf8().constData());
		if (strcmp(type, "ffmpeg_source") == 0) {
			obs_data_set_bool(settings, "is_local_file", true);
			obs_data_set_bool(settings, "restart_on_activate", true);
		} else if (strcmp(type, "browser_source") == 0) {
			obs_data_set_bool(settings, "is_local_file", true);
		}

		const QString name = UniqueSourceName(QFileInfo(path).completeBaseName());
		OBSSourceAutoRelease source = obs_source_create(type, name.toUtf8().constData(), settings, nullptr);
		if (source)
			AddExistingSource(source, true);
	}
}

void StudioWindow::dragEnterEvent(QDragEnterEvent *event)
{
	if (event->mimeData()->hasUrls())
		event->acceptProposedAction();
}

void StudioWindow::dropEvent(QDropEvent *event)
{
	QStringList paths;
	for (const QUrl &url : event->mimeData()->urls()) {
		if (url.isLocalFile())
			paths << url.toLocalFile();
	}
	if (paths.isEmpty())
		return;

	/* Déposé sur un aperçu : dans cette destination. Sinon : dans la
	 * destination active. */
	const QPoint global = mapToGlobal(event->position().toPoint());
	TileWidget *target = pvwGrid->TileAt(global);
	if (!target && modifyProgram)
		target = pgmGrid->TileAt(global);
	AddFiles(paths, target);
	event->acceptProposedAction();
}

void StudioWindow::ShowAddSourceMenu()
{
	QMenu menu(this);
	FillAddSourceMenu(&menu);
	menu.exec(addSourceButton->mapToGlobal(QPoint(0, addSourceButton->height())));
}

void StudioWindow::FillAddSourceMenu(QMenu *menu)
{
	menu->clear();
	OBSSource sceneSource = ActiveScene() ? OBSSource(obs_scene_get_source(ActiveScene())) : OBSSource();
	if (!sceneSource) {
		menu->addAction(Tr("MDS.SelectSceneFirst"))->setEnabled(false);
		return;
	}

	/* Nouveaux types de sources, comme le « + » d'OBS. */
	std::vector<std::pair<QString, std::string>> types;
	const char *id = nullptr;
	for (size_t i = 0; obs_enum_input_types(i, &id); i++) {
		const uint32_t flags = obs_get_source_output_flags(id);
		if (flags & (OBS_SOURCE_CAP_DISABLED | OBS_SOURCE_DEPRECATED))
			continue;
		const char *display = obs_source_get_display_name(id);
		if (display)
			types.emplace_back(QString::fromUtf8(display), id);
	}
	std::sort(types.begin(), types.end(),
		  [](const auto &a, const auto &b) { return a.first.localeAwareCompare(b.first) < 0; });
	for (auto &[display, typeId] : types) {
		std::string tid = typeId;
		connect(menu->addAction(display), &QAction::triggered, this,
			[this, tid]() { CreateNewSource(tid.c_str()); });
	}
	menu->addSeparator();

	auto addEntry = [this](QMenu *target, obs_source_t *source) {
		OBSWeakSource weak = OBSGetWeakRef(source);
		connect(target->addAction(QString::fromUtf8(obs_source_get_name(source))), &QAction::triggered, this,
			[this, weak]() {
				OBSSource src = OBSGetStrongRef(weak);
				AddExistingSource(src);
			});
	};

	QMenu *existing = menu->addMenu(Tr("MDS.ExistingSource"));
	std::vector<OBSSource> inputs;
	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *l = static_cast<std::vector<OBSSource> *>(param);
			if (obs_source_get_type(source) == OBS_SOURCE_TYPE_INPUT &&
			    (obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO))
				l->emplace_back(source);
			return true;
		},
		&inputs);
	for (auto &s : inputs)
		addEntry(existing, s);
	if (inputs.empty())
		existing->addAction(Tr("MDS.None"))->setEnabled(false);

	QMenu *scenes = menu->addMenu(Tr("MDS.SceneAsSource"));
	OBSSource preview = Manager::Get().PreviewScene();
	int added = 0;
	for (auto &s : Manager::MainScenes()) {
		/* Une scène ne peut pas s'inclure elle-même. */
		if (s.Get() == sceneSource.Get() || s.Get() == preview.Get())
			continue;
		addEntry(scenes, s);
		added++;
	}
	if (added == 0)
		scenes->addAction(Tr("MDS.None"))->setEnabled(false);
}

void StudioWindow::ShowItemMenu(const QPoint &globalPos)
{
	OBSSceneItem item = CurrentItem();
	QMenu menu(this);

	if (item) {
		QAction *visible = menu.addAction(Tr("MDS.Visible"));
		visible->setCheckable(true);
		visible->setChecked(obs_sceneitem_visible(item));
		obs_sceneitem_t *it = item;
		connect(visible, &QAction::toggled, this, [this, it](bool on) {
			Undo::PushSceneState(ActiveScene());
			obs_sceneitem_set_visible(it, on);
			AfterEdit();
			RefreshSources();
		});
		if (obs_sceneitem_is_group(item))
			connect(menu.addAction(Tr("MDS.RenameGroup")), &QAction::triggered, this,
				&StudioWindow::EditRenameGroup);
		menu.addSeparator();
	}
	for (QAction *a : editActions) {
		const bool always = a->text() == Tr("MDS.Paste") || a->text() == Tr("MDS.PasteDuplicate") ||
				    a->text() == Tr("MDS.Undo");
		a->setEnabled(item || always);
		menu.addAction(a);
	}
	menu.exec(globalPos);
	for (QAction *a : editActions)
		a->setEnabled(true);
}

/* ------------------------------------------------------------------ */
/* Édition                                                              */
/* ------------------------------------------------------------------ */

void StudioWindow::EditUndo()
{
	if (Undo::UndoLast())
		RefreshSources();
	AfterEdit();
}

void StudioWindow::EditCopy()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	clipboardSource = OBSGetWeakRef(obs_sceneitem_get_source(item));
	clipboardTransform = SceneEditor::GetTransform(item);
	hasClipboardTransform = true;
}

void StudioWindow::EditPaste()
{
	OBSSource source = OBSGetStrongRef(clipboardSource);
	obs_scene_t *s = ActiveScene();
	if (!source || !s || source.Get() == obs_scene_get_source(s))
		return;

	obs_sceneitem_t *item = obs_scene_add(s, source);
	if (!item)
		return;
	if (hasClipboardTransform)
		SceneEditor::SetTransform(item, clipboardTransform);
	Undo::PushAdded(s, obs_sceneitem_get_id(item));
	SelectItem(obs_sceneitem_get_id(item));
	AfterEdit();
}

/* Grouper les sources sélectionnées (premier niveau) dans un groupe OBS. */
void StudioWindow::EditGroup()
{
	obs_scene_t *s = ActiveScene();
	if (!s)
		return;
	std::vector<obs_sceneitem_t *> items;
	for (QTreeWidgetItem *row : sourceList->selectedItems()) {
		if (row->parent())
			continue;
		if (obs_sceneitem_t *it = obs_scene_find_sceneitem_by_id(s, row->data(0, Qt::UserRole).toLongLong()))
			items.push_back(it);
	}
	if (items.empty()) {
		OBSSceneItem current = CurrentItem();
		if (current && obs_sceneitem_get_scene(current) == s)
			items.push_back(current);
	}
	if (items.empty())
		return;

	std::string name;
	for (int n = 1;; n++) {
		name = QString("%1 %2").arg(Tr("MDS.GroupName")).arg(n).toStdString();
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			break;
	}
	obs_sceneitem_t *group = obs_scene_insert_group(s, name.c_str(), items.data(), items.size());
	if (!group)
		return;
	SelectItem(obs_sceneitem_get_id(group));
	AfterEdit();
}

void StudioWindow::EditUngroup()
{
	OBSSceneItem item = CurrentItem();
	if (!item || !obs_sceneitem_is_group(item))
		return;
	obs_sceneitem_group_ungroup(item);
	SelectItem(-1);
	AfterEdit();
}

void StudioWindow::EditRenameGroup()
{
	OBSSceneItem item = CurrentItem();
	if (!item || !obs_sceneitem_is_group(item))
		return;
	obs_source_t *source = obs_sceneitem_get_source(item);
	bool ok = false;
	const QString name = QInputDialog::getText(this, Tr("MDS.RenameGroup"), Tr("MDS.Name"), QLineEdit::Normal,
						   QString::fromUtf8(obs_source_get_name(source)), &ok)
				     .trimmed();
	if (!ok || name.isEmpty())
		return;
	OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
	if (existing && existing.Get() != source) {
		QMessageBox::warning(this, Tr("MDS.RenameGroup"), Tr("MDS.SourceExists"));
		return;
	}
	obs_source_set_name(source, name.toUtf8().constData());
	RefreshSources();
}

/* Coller une copie indépendante : nouvelle source (avec ses filtres), qui
 * n'est plus liée à l'originale. Une scène collée reste une référence (la
 * dupliquer créerait une nouvelle scène dans OBS). */
void StudioWindow::EditPasteDuplicate()
{
	OBSSource source = OBSGetStrongRef(clipboardSource);
	obs_scene_t *s = ActiveScene();
	if (!source || !s || source.Get() == obs_scene_get_source(s))
		return;
	if (obs_source_is_scene(source) || obs_source_is_group(source)) {
		EditPaste();
		return;
	}

	const std::string base = obs_source_get_name(source);
	std::string name;
	for (int n = 2;; n++) {
		name = base + " " + std::to_string(n);
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			break;
	}
	OBSSourceAutoRelease copy = obs_source_duplicate(source, name.c_str(), false);
	if (!copy)
		return;

	obs_sceneitem_t *item = obs_scene_add(s, copy);
	if (!item)
		return;
	if (hasClipboardTransform)
		SceneEditor::SetTransform(item, clipboardTransform);
	Undo::PushAdded(s, obs_sceneitem_get_id(item));
	SelectItem(obs_sceneitem_get_id(item));
	AfterEdit();
}

void StudioWindow::EditRemove()
{
	obs_scene_t *s = ActiveScene();
	if (!s)
		return;

	/* Sources sélectionnées dans la liste (plusieurs possibles), sinon
	 * celle sélectionnée dans l'aperçu. */
	std::vector<OBSSceneItem> items;
	if (sourceList->hasFocus() || sourceList->selectedItems().size() > 1) {
		for (QTreeWidgetItem *row : sourceList->selectedItems()) {
			if (OBSSceneItem it = ItemFromRow(row))
				items.push_back(it);
		}
	}
	if (items.empty()) {
		if (OBSSceneItem item = CurrentItem())
			items.push_back(item);
	}
	if (items.empty())
		return;

	const QString question =
		items.size() == 1
			? Tr("MDS.ConfirmRemoveSource")
				  .arg(QString::fromUtf8(obs_source_get_name(obs_sceneitem_get_source(items.front()))))
			: Tr("MDS.ConfirmRemoveSources").arg(int(items.size()));
	if (QMessageBox::question(this, Tr("MDS.RemoveSource"), question) != QMessageBox::Yes)
		return;

	for (auto &item : items) {
		Undo::PushRemoved(obs_sceneitem_get_scene(item), item);
		obs_sceneitem_remove(item);
	}
	SelectItem(-1);
	AfterEdit();
}

void StudioWindow::EditCopyTransform()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	copiedTransform = SceneEditor::GetTransform(item);
	hasCopiedTransform = true;
}

void StudioWindow::EditPasteTransform()
{
	OBSSceneItem item = CurrentItem();
	if (!item || !hasCopiedTransform)
		return;
	Undo::PushSceneState(ActiveScene());
	SceneEditor::SetTransform(item, copiedTransform);
	AfterEdit();
}

void StudioWindow::EditResetTransform()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		SceneEditor::ResetTransform(item);
	}
	AfterEdit();
}

void StudioWindow::EditFit()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		SceneEditor::FitToCanvas(item, activeDest->Width(), activeDest->Height());
	}
	AfterEdit();
}

void StudioWindow::EditStretch()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		SceneEditor::StretchToCanvas(item, activeDest->Width(), activeDest->Height());
	}
	AfterEdit();
}

void StudioWindow::EditCenter()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		SceneEditor::CenterInCanvas(item, activeDest->Width(), activeDest->Height());
	}
	AfterEdit();
}

void StudioWindow::EditMoveUp()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_UP);
		RefreshSources();
	}
	AfterEdit();
}

void StudioWindow::EditMoveDown()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_DOWN);
		RefreshSources();
	}
	AfterEdit();
}

void StudioWindow::EditMoveTop()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_TOP);
		RefreshSources();
	}
	AfterEdit();
}

void StudioWindow::EditMoveBottom()
{
	if (OBSSceneItem item = CurrentItem()) {
		Undo::PushSceneState(ActiveScene());
		obs_sceneitem_set_order(item, OBS_ORDER_MOVE_BOTTOM);
		RefreshSources();
	}
	AfterEdit();
}

void StudioWindow::EditProperties()
{
	if (OBSSceneItem item = CurrentItem())
		obs_frontend_open_source_properties(obs_sceneitem_get_source(item));
}

void StudioWindow::EditFilters()
{
	if (OBSSceneItem item = CurrentItem())
		obs_frontend_open_source_filters(obs_sceneitem_get_source(item));
}

/* ------------------------------------------------------------------ */
/* Transitions                                                          */
/* ------------------------------------------------------------------ */

void StudioWindow::RefreshTransitions()
{
	refreshingTransitions = true;
	transitionCombo->clear();

	OBSSourceAutoRelease current = obs_frontend_get_current_transition();
	struct obs_frontend_source_list list = {};
	obs_frontend_get_transitions(&list);
	for (size_t i = 0; i < list.sources.num; i++) {
		obs_source_t *tr = list.sources.array[i];
		transitionCombo->addItem(QString::fromUtf8(obs_source_get_name(tr)),
					 QString::fromUtf8(obs_source_get_uuid(tr)));
		if (tr == current.Get())
			transitionCombo->setCurrentIndex(transitionCombo->count() - 1);
	}
	obs_frontend_source_list_free(&list);

	durationSpin->setValue(obs_frontend_get_transition_duration());
	durationSpin->setEnabled(current && !obs_transition_fixed(current));
	refreshingTransitions = false;
}

/* ------------------------------------------------------------------ */
/* Destinations                                                         */
/* ------------------------------------------------------------------ */

void StudioWindow::ScheduleDestinationsMenu()
{
	if (destinationsMenuPending)
		return;
	destinationsMenuPending = true;
	QTimer::singleShot(0, this, &StudioWindow::RunScheduledDestinationsMenu);
}

void StudioWindow::RunScheduledDestinationsMenu()
{
	/* Menu ouvert : on attend qu'il se ferme. */
	if (destinationsMenu->isVisible()) {
		QTimer::singleShot(300, this, &StudioWindow::RunScheduledDestinationsMenu);
		return;
	}
	destinationsMenuPending = false;
	FillDestinationsMenu();
}

void StudioWindow::FillDestinationsMenu()
{
	destinationsMenu->clear();

	for (auto *d : Manager::Get().Destinations()) {
		QString title =
			QString("%1  (%2×%3)").arg(QString::fromStdString(d->Name())).arg(d->Width()).arg(d->Height());
		if (d->Disabled())
			title += "  — " + Tr("MDS.DisabledSuffix");
		QMenu *sub = destinationsMenu->addMenu(title);

		connect(sub->addAction(Tr("MDS.DestinationSettings")), &QAction::triggered, this,
			[this, d]() { EditDestination(d); });

		if (!d->IsMain()) {
			connect(sub->addAction(Tr("MDS.OutputBlendingMenu")), &QAction::triggered, this,
				[this, d]() { EditBlending(d); });
		}
		sub->addSeparator();

		QAction *full = nullptr;
		if (d->IsBlended()) {
			bool anyScreen = false;
			for (const auto &o : d->Blend().outputs)
				anyScreen = anyScreen || (o.placed && Dialogs::FindScreen(o.screen));
			full = sub->addAction(Tr("MDS.OpenFullscreenOutputs"));
			full->setEnabled(anyScreen && !d->Disabled());
		} else {
			QScreen *screen = Dialogs::FindScreen(d->OutputScreen());
			full = sub->addAction(screen ? Tr("MDS.OpenFullscreenOn").arg(screen->name())
						     : Tr("MDS.OpenFullscreen"));
			full->setEnabled(screen != nullptr && !d->Disabled());
			if (!screen)
				full->setToolTip(Tr("MDS.NoScreenHint"));
		}
		connect(full, &QAction::triggered, this, [this, d]() { OpenProjector(d, true); });

		QAction *windowed = sub->addAction(Tr("MDS.OpenWindowed"));
		windowed->setEnabled(!d->Disabled());
		connect(windowed, &QAction::triggered, this, [this, d]() { OpenProjector(d, false); });

		QAction *close = sub->addAction(Tr("MDS.CloseProjection"));
		close->setEnabled(HasProjector(d));
		connect(close, &QAction::triggered, this, [this, d]() { CloseProjectors(d); });

		sub->addSeparator();
		if (d->IsMain()) {
			QAction *disable = sub->addAction(Tr("MDS.DeactivateAndHide").replace("&", "&&"));
			disable->setCheckable(true);
			disable->setChecked(d->Disabled());
			disable->setToolTip(Tr("MDS.DeactivateAndHideTip"));
			connect(disable, &QAction::toggled, this, [this, d](bool on) {
				if (on)
					CloseProjectors(d);
				Manager::Get().SetMainDisabled(on);
			});
		} else {
			connect(sub->addAction(Tr("MDS.Delete")), &QAction::triggered, this,
				[this, d]() { DeleteDestination(d); });
		}
	}

	destinationsMenu->addSeparator();
	connect(destinationsMenu->addAction(Tr("MDS.ManageDestinationsMenu")), &QAction::triggered, this,
		&StudioWindow::ManageDestinations);
	destinationsMenu->addSeparator();
	connect(destinationsMenu->addAction(Tr("MDS.OutputBlendingMenu")), &QAction::triggered, this,
		&StudioWindow::NewBlendedDestination);
	destinationsMenu->addSeparator();
	connect(destinationsMenu->addAction(Tr("MDS.NewDestinationMenu")), &QAction::triggered, this,
		&StudioWindow::NewDestination);
}

std::string StudioWindow::DefaultDestinationName() const
{
	return QString("%1 %2").arg(Tr("MDS.Destination")).arg(Manager::Get().Destinations().size()).toStdString();
}

void StudioWindow::NewBlendedDestination()
{
	BlendEditor editor(this, nullptr, DefaultDestinationName());
	editor.onCreate = [](BlendEditor *e) {
		const BlendConfig &config = e->Config();
		Destination *dest =
			Manager::Get().Add(e->DestinationName(), config.width, config.height, e->AddToExisting());
		if (!dest) {
			QMessageBox::warning(e, Tr("MDS.OutputBlending"), Tr("MDS.AddFailed"));
			return false;
		}
		dest->SetBlend(config);
		Manager::Get().NotifyDestinationsChanged();
		return true;
	};
	editor.exec();
}

void StudioWindow::EditBlending(Destination *dest)
{
	if (!dest || dest->IsMain())
		return;
	BlendEditor editor(this, dest, DefaultDestinationName());
	editor.onSave = [this, dest](BlendEditor *e) {
		Destination *d = dest;
		const BlendConfig &config = e->Config();
		d->SetName(e->DestinationName());
		if (config.HasLayout() && (config.width != d->Width() || config.height != d->Height()))
			d->Attach(config.width, config.height);
		d->SetBlend(config);
		Manager::Get().NotifyDestinationsChanged();
		RefreshBlendProjectors(d);
	};
	editor.onOpenOutputs = [this](Destination *d) {
		OpenProjector(d, true);
	};
	editor.exec();
}

void StudioWindow::RefreshBlendProjectors(Destination *dest)
{
	/* Les sorties ouvertes suivent la nouvelle configuration. */
	if (!HasProjector(dest))
		return;
	bool fullscreen = false;
	for (auto &p : projectors) {
		if (p.second && p.first == dest && p.second->TargetScreen())
			fullscreen = true;
	}
	CloseProjectors(dest);
	OpenProjector(dest, fullscreen);
}

void StudioWindow::OpenAllOutputs()
{
	for (Destination *d : Manager::Get().ActiveDestinations()) {
		bool hasScreen = false;
		if (d->IsBlended()) {
			for (const auto &o : d->Blend().outputs)
				hasScreen = hasScreen || (o.placed && Dialogs::FindScreen(o.screen));
		} else {
			hasScreen = Dialogs::FindScreen(d->OutputScreen()) != nullptr;
		}
		if (hasScreen && !HasProjector(d))
			OpenProjector(d, true);
	}
}

void StudioWindow::ManageDestinations()
{
	ManageDestinationsDialog dialog(this);
	dialog.onNew = [this]() {
		NewDestination();
	};
	dialog.onBlending = [this](Destination *d) {
		EditBlending(d);
	};
	dialog.onProject = [this](Destination *d) {
		OpenProjector(d, true);
	};
	dialog.onDelete = [this](Destination *d) {
		if (!d->IsMain()) {
			DeleteDestination(d);
			return;
		}
		if (!d->Disabled())
			CloseProjectors(d);
		Manager::Get().SetMainDisabled(!d->Disabled());
	};
	dialog.exec();
}

void StudioWindow::NewDestination()
{
	Dialogs::DestinationSettings settings;
	settings.name = DefaultDestinationName();
	if (!Dialogs::EditDestination(this, nullptr, settings))
		return;

	Destination *dest = Manager::Get().Add(settings.name, settings.width, settings.height, settings.addToExisting);
	if (!dest) {
		QMessageBox::warning(this, Tr("MDS.NewDestination"), Tr("MDS.AddFailed"));
		return;
	}
	dest->SetOutputScreen(settings.outputScreen);
}

void StudioWindow::EditDestination(Destination *dest)
{
	if (!dest)
		return;
	Dialogs::DestinationSettings settings;
	settings.name = dest->Name();
	settings.width = dest->Width();
	settings.height = dest->Height();
	settings.outputScreen = dest->OutputScreen();
	if (!Dialogs::EditDestination(this, dest, settings))
		return;

	if (!dest->IsMain()) {
		dest->SetName(settings.name);
		if (!dest->IsBlended())
			dest->Attach(settings.width, settings.height);
	}
	dest->SetOutputScreen(settings.outputScreen);
	Manager::Get().NotifyDestinationsChanged();
	if (settings.openBlending)
		EditBlending(dest);
}

void StudioWindow::DeleteDestination(Destination *dest)
{
	if (!dest || dest->IsMain())
		return;
	if (Dialogs::ConfirmDeleteDestination(this, dest))
		Manager::Get().Remove(dest);
}

bool StudioWindow::HasProjector(Destination *dest) const
{
	for (auto &p : projectors) {
		if (p.first == dest && p.second)
			return true;
	}
	return false;
}

void StudioWindow::OpenProjector(Destination *dest, bool fullscreen)
{
	/* Destination blendée : une fenêtre par sortie placée. */
	if (dest->IsBlended()) {
		const BlendConfig config = dest->Blend();
		for (size_t i = 0; i < config.outputs.size(); i++) {
			const BlendOutput &o = config.outputs[i];
			if (!o.placed)
				continue;
			QScreen *screen = fullscreen ? Dialogs::FindScreen(o.screen) : nullptr;
			if (fullscreen && !screen)
				continue;
			for (auto &p : projectors) {
				if (p.second && ((screen && p.second->TargetScreen() == screen) ||
						 (p.first == dest && p.second->OutputIndex() == int(i))))
					p.second->close();
			}
			auto *projector = new ProjectorWindow(dest, screen, int(i));
			projectors.emplace_back(dest, projector);
			projector->Start();
		}
		return;
	}

	QScreen *screen = fullscreen ? Dialogs::FindScreen(dest->OutputScreen()) : nullptr;
	if (fullscreen && !screen)
		return;

	/* Un seul projecteur par écran, et un seul plein écran par
	 * destination. */
	for (auto &p : projectors) {
		if (!p.second)
			continue;
		if ((screen && p.second->TargetScreen() == screen) ||
		    (fullscreen && p.first == dest && p.second->TargetScreen()))
			p.second->close();
	}

	auto *projector = new ProjectorWindow(dest, screen);
	projectors.emplace_back(dest, projector);
	projector->Start();
}

void StudioWindow::CloseProjectors(Destination *dest)
{
	for (auto &p : projectors) {
		if (p.second && p.first == dest)
			p.second->close();
	}
}
