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
#include "dock-style.hpp"
#include "manager.hpp"
#include "media-controls.hpp"
#include "projector-window.hpp"
#include "reorder-list.hpp"
#include "manage-destinations.hpp"
#include "show.hpp"
#include "blend-editor.hpp"
#include "cues.hpp"
#include "cues-dock.hpp"
#include "crop-preview.hpp"
#include "text.hpp"
#include "tile-grid.hpp"
#include "undo.hpp"

#include <obs-frontend-api.h>
#include <plugin-support.h>

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFormLayout>
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
#include <QMouseEvent>
#include <QSlider>
#include <QStyleOptionSlider>
#include <QToolBar>
#include <QWidgetAction>
#include <QWindow>
#include <QTreeWidget>
#include <set>
#include <QToolButton>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSet>

#include <algorithm>
#include <cstring>
#include <functional>
#include <map>
#include <memory>

/* ------------------------------------------------------------------ */
/* Petits utilitaires d'interface                                       */
/* ------------------------------------------------------------------ */

static QToolButton *ToolButton(const QString &text, const QString &tip, QWidget *parent)
{
	auto *b = new QToolButton(parent);
	b->setText(text);
	b->setToolTip(tip);
	b->setAutoRaise(true);
	b->setFixedSize(28, 24);
	return b;
}

/* Icône du thème d'OBS (classe icon-…) sur un bouton seul. */
static void SetIconClass(QAbstractButton *button, const char *iconClass)
{
	button->setProperty("class", iconClass);
	button->style()->unpolish(button);
	button->style()->polish(button);
}

static std::vector<QTreeWidgetItem *> SelectedRowsInOrder(QTreeWidget *tree);

/* Rôles des icônes à droite des sources. */
static constexpr int SOURCE_ICON_VISIBLE = 0;
static constexpr int SOURCE_ICON_LOCK = 1;

/* Contenu d'un dock : liste + barre de boutons en bas, comme dans OBS. */
/* Lignes de la liste des scènes (Qt::UserRole + 1). */
static constexpr int SCENE_ROW_SCENE = 1;
static constexpr int SCENE_ROW_FOLDER = 2;
/* Lignes de la liste des sources : Qt::UserRole = id de l'élément,
 * Qt::UserRole + 1 = id du groupe parent (0 = premier niveau). */

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
	TileWidget::openProperties = [this](obs_source_t *source) {
		OBSSource src = source;
		OpenOnStudio([src]() { obs_frontend_open_source_properties(src); });
	};

	BuildCentral();
	BuildDocks();
	BuildMenus();
	BuildWindowControls();
	ApplyCuesFeature();

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
	connect(&m, &Manager::quickTransitionsChanged, this, &StudioWindow::RefreshQuickTransitions);
	connect(&m, &Manager::settingsChanged, this, &StudioWindow::ApplyCuesFeature);
	connect(&m, &Manager::aboutToReset, this, &StudioWindow::OnReset);
	connect(&m, &Manager::loaded, this, [this]() {
		RefreshScenes();
		RefreshTransitions();
		RefreshQuickTransitions();
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

/* Barre T d'OBS : on ne la déplace qu'en tirant sa poignée (un clic à côté
 * ne la fait pas sauter). */
class TBarSlider : public QSlider {
public:
	explicit TBarSlider(QWidget *parent) : QSlider(Qt::Horizontal, parent) {}

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		QStyleOptionSlider opt;
		initStyleOption(&opt);
		const QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
		if (!handle.contains(event->position().toPoint())) {
			event->ignore();
			return;
		}
		QSlider::mousePressEvent(event);
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
	QWidget *pvwColumn = makeColumn(Tr("MDS.Preview"), pvwGrid);
	layout->addWidget(pvwColumn, 1);
	/* Cue en Aperçu : le titre le dit (Transition lancera la cue, pas
	 * seulement sa première scène). */
	if (auto *title = pvwColumn->findChild<QLabel *>()) {
		auto update = [title]() {
			const CueEngine &engine = CueEngine::Get();
			const Cue *armed = engine.Find(engine.ArmedId());
			title->setText(armed && Manager::Get().CuesEnabled()
					       ? Tr("MDS.PreviewCue").arg(QString::fromStdString(armed->name))
					       : Tr("MDS.Preview"));
		};
		connect(&CueEngine::Get(), &CueEngine::stateChanged, title, update);
		connect(&Manager::Get(), &Manager::settingsChanged, title, update);
	}

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

	/* Comme le mode Studio d'OBS : Transition et son menu « ⋮ », puis les
	 * transitions rapides. */
	auto *transitionRow = new QHBoxLayout();
	transitionRow->setSpacing(4);
	auto *transition = new QPushButton(Tr("MDS.Transition"), monitors);
	transition->setToolTip(Tr("MDS.TransitionTip"));
	connect(transition, &QPushButton::clicked, this, []() { Manager::Get().Take(false); });
	transitionRow->addWidget(transition, 1);
	auto *options = new QPushButton(monitors);
	options->setToolTip(Tr("MDS.TransitionOptions"));
	options->setIcon(DockStyle::DotsIcon());
	options->setFixedWidth(30);
	SetIconClass(options, "icon-dots-vert");
	connect(options, &QPushButton::clicked, this, [this, options]() {
		Manager &m = Manager::Get();
		QMenu menu(this);
		QAction *swap = menu.addAction(Tr("MDS.TransitionOptionSwap"));
		swap->setCheckable(true);
		swap->setChecked(m.SwapAfterTake());
		connect(swap, &QAction::toggled, this, [](bool on) { Manager::Get().SetSwapAfterTake(on); });
		QAction *all = menu.addAction(Tr("MDS.TransitionOptionShowAll"));
		all->setCheckable(true);
		all->setChecked(m.ShowAllDestinations());
		connect(all, &QAction::toggled, this, [](bool on) { Manager::Get().SetShowAllDestinations(on); });
		menu.exec(options->mapToGlobal(QPoint(0, options->height())));
	});
	transitionRow->addWidget(options);
	controls->addLayout(transitionRow);
	transitionButton = transition;
	cutButton = nullptr;
	/* Dossier de scènes sélectionné : rien à prendre, on l'explique. */
	folderTakeNote = new QLabel(Tr("MDS.FolderTakeNote"), monitors);
	folderTakeNote->setWordWrap(true);
	folderTakeNote->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
	folderTakeNote->setMaximumWidth(std::max(transition->sizeHint().width(), 110) + 30);
	folderTakeNote->setStyleSheet("color: #ffb74d; font-size: 11px;");
	folderTakeNote->hide();
	controls->addWidget(folderTakeNote);
	/* Transitions rapides, comme le mode Studio d'OBS. */
	auto *quickRow = new QHBoxLayout();
	quickRow->setContentsMargins(0, 8, 0, 0);
	quickRow->addWidget(new QLabel(Tr("MDS.QuickTransitions"), monitors), 1);
	auto *quickAdd = new QPushButton(monitors);
	quickAdd->setIcon(DockStyle::PlusIcon());
	quickAdd->setToolTip(Tr("MDS.QuickTransitionAdd"));
	quickAdd->setFixedWidth(30);
	SetIconClass(quickAdd, "icon-plus");
	connect(quickAdd, &QPushButton::clicked, this, [this, quickAdd]() { AddQuickTransition(quickAdd); });
	quickRow->addWidget(quickAdd);
	controls->addLayout(quickRow);
	quickPanel = new QWidget(monitors);
	auto *quickLayout = new QVBoxLayout(quickPanel);
	quickLayout->setContentsMargins(0, 2, 0, 0);
	quickLayout->setSpacing(4);
	controls->addWidget(quickPanel);
	/* Barre T : glisser jusqu'au bout pour finir la transition, revenir au
	 * début pour l'annuler (comme OBS). */
	tbar = new TBarSlider(monitors);
	tbar->setRange(0, Manager::TBarMax);
	tbar->setProperty("class", "slider-tbar");
	tbar->setToolTip(Tr("MDS.TBarTip"));
	connect(tbar, &QSlider::valueChanged, this, [](int value) { Manager::Get().TBarMove(value); });
	connect(tbar, &QSlider::sliderReleased, this, [this]() {
		if (Manager::Get().TBarRelease(tbar->value())) {
			const QSignalBlocker blocker(tbar);
			tbar->setValue(0);
		}
	});
	controls->addWidget(tbar);
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
	scenes->onMove = [this](QTreeWidgetItem *target, int position) {
		MoveSceneRows(target, position);
	};
	sceneList = scenes;
	connect(sceneList, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (refreshingScenes)
			return;
		UpdateTakeButtons();
		/* Bouton encore enfoncé : l'Aperçu change au relâchement (sinon la
		 * liste est reconstruite sous la souris et le glisser échoue). */
		if (static_cast<OrganizerTree *>(sceneList)->MousePressed()) {
			scenePreviewPending = true;
			return;
		}
		ApplySceneSelection();
	});
	scenes->onReleased = [this]() {
		if (!scenePreviewPending)
			return;
		scenePreviewPending = false;
		ApplySceneSelection();
	};
	connect(sceneList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *row, int) {
		if (refreshingScenes)
			return;
		const QString name = row->text(0).trimmed();
		if (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER) {
			if (name == row->data(0, Qt::UserRole).toString())
				return;
			if (!name.isEmpty())
				row->setData(0, Qt::UserRole, name);
			SaveSceneTree();
		} else {
			OBSSourceAutoRelease scene =
				obs_get_source_by_uuid(row->data(0, Qt::UserRole).toString().toUtf8().constData());
			if (!scene || name == QString::fromUtf8(obs_source_get_name(scene)))
				return;
			if (!name.isEmpty()) {
				OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
				if (existing)
					QMessageBox::warning(this, Tr("MDS.Rename"), Tr("MDS.SceneExists"));
				else
					Manager::Get().RenameScene(scene, name.toStdString());
			}
		}
		QTimer::singleShot(0, this, &StudioWindow::RefreshScenes);
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
		connect(menu.addAction(sceneList->selectedItems().isEmpty() ? Tr("MDS.NewSceneFolder")
									    : Tr("MDS.GroupInFolder")),
			&QAction::triggered, this, &StudioWindow::NewSceneFolder);
		if (current && current->isSelected()) {
			connect(menu.addAction(Tr("MDS.Rename")), &QAction::triggered, this,
				[this, current]() { RenameRow(sceneList, current); });
		}
		if (onFolder) {
			menu.addSeparator();
			connect(menu.addAction(Tr("MDS.RemoveFolder")), &QAction::triggered, this,
				[this]() { RemoveSelectedScenes(); });
		}
		if (scene || onFolder) {
			menu.addSeparator();
			if (scene) {
				connect(menu.addAction(Tr("MDS.SceneSettingsMenu")), &QAction::triggered, this,
					[this, scene]() { EditScene(scene); });
				connect(menu.addAction(Tr("MDS.DuplicateScene")), &QAction::triggered, this,
					[this, scene]() { DuplicateScene(scene); });
			}
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
	/* Barre du bas comme celle d'OBS (icônes du thème). */
	QStyle *st = style();
	QToolBar *sceneBar = DockStyle::Toolbar(this);
	DockStyle::AddAction(sceneBar, "icon-plus", DockStyle::PlusIcon(), Tr("MDS.AddScene"),
			     [this]() { AddScene(); });
	DockStyle::AddAction(sceneBar, "icon-trash", st->standardIcon(QStyle::SP_TrashIcon), Tr("MDS.RemoveScene"),
			     [this]() { RemoveSelectedScenes(); });
	sceneBar->addSeparator();
	DockStyle::AddAction(sceneBar, "icon-gear", st->standardIcon(QStyle::SP_FileDialogDetailedView),
			     Tr("MDS.SceneSettingsMenu"), [this]() { EditScene(SelectedScene()); });
	DockStyle::AddAction(sceneBar, "icon-filter", st->standardIcon(QStyle::SP_FileDialogContentsView),
			     Tr("MDS.SceneFilters"), [this]() {
				     if (OBSSource scene = SelectedScene())
					     OpenOnStudio([scene]() { obs_frontend_open_source_filters(scene); });
			     });
	DockStyle::AddAction(sceneBar, nullptr, DockStyle::FolderPlusIcon(), Tr("MDS.GroupInFolderTip"),
			     [this]() { NewSceneFolder(); });
	sceneBar->addSeparator();
	DockStyle::AddAction(sceneBar, "icon-up", st->standardIcon(QStyle::SP_ArrowUp), Tr("MDS.MoveUp"),
			     [this]() { MoveScene(-1); });
	DockStyle::AddAction(sceneBar, "icon-down", st->standardIcon(QStyle::SP_ArrowDown), Tr("MDS.MoveDown"),
			     [this]() { MoveScene(1); });
	DockStyle::StyleTree(sceneList);
	Dock(Tr("MDS.Scenes"), "mdsScenesDock", DockStyle::Panel(sceneList, sceneBar, this), this);

	/* --- Sources --- */
	/* Glisser-déposer pour changer l'ordre (haut de la liste = devant),
	 * cliquer-glisser pour sélectionner plusieurs sources. */
	auto *sources = new OrganizerTree(this);
	sources->SetTrailingIcons(2);
	sources->onIconClicked = [this](QTreeWidgetItem *row, int icon) {
		OBSSceneItem item = ItemFromRow(row);
		if (!item)
			return;
		const QColor color = sourceList->palette().color(QPalette::Text);
		const QSignalBlocker blocker(sourceList);
		if (icon == SOURCE_ICON_VISIBLE) {
			Undo::PushSceneState(ActiveScene());
			const bool visible = !obs_sceneitem_visible(item);
			obs_sceneitem_set_visible(item, visible);
			row->setData(0, TrailingIconDelegate::IconRole + SOURCE_ICON_VISIBLE,
				     DockStyle::VisibilityIcon(visible, color));
			AfterEdit();
		} else {
			const bool locked = !obs_sceneitem_locked(item);
			obs_sceneitem_set_locked(item, locked);
			row->setData(0, TrailingIconDelegate::IconRole + SOURCE_ICON_LOCK,
				     DockStyle::LockIcon(locked, color));
		}
	};
	sources->onDelete = [this]() {
		EditRemove();
	};
	sources->onMove = [this](QTreeWidgetItem *target, int position) {
		MoveSourceRows(target, position);
	};
	sourceList = sources;
	connect(sourceList, &QTreeWidget::itemSelectionChanged, this, [this]() {
		if (refreshingSources)
			return;
		TileWidget *tile = ActiveTile();
		QTreeWidgetItem *row = sourceList->currentItem();
		/* Ctrl + clic qui retire la ligne courante : la première ligne
		 * encore sélectionnée devient l'élément de l'aperçu. */
		if (row && !row->isSelected()) {
			const auto selected = sourceList->selectedItems();
			row = selected.isEmpty() ? nullptr : selected.front();
		}
		if (tile) {
			/* Un élément d'un groupe se sélectionne lui-même dans l'aperçu :
			 * c'est lui qu'on déplace, pas tout le groupe. */
			tile->SetSelectedItem(row ? row->data(0, Qt::UserRole).toLongLong() : -1);
		}
		UpdateMediaControls();
	});
	/* Renommer (F2, double-clic lent ou menu). */
	connect(sourceList, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem *row, int column) {
		if (refreshingSources || column != 0)
			return;
		OBSSceneItem item = ItemFromRow(row);
		if (!item)
			return;
		obs_source_t *source = obs_sceneitem_get_source(item);
		const QString name = row->text(0).trimmed();
		if (name == QString::fromUtf8(obs_source_get_name(source)))
			return;
		if (!name.isEmpty()) {
			OBSSourceAutoRelease existing = obs_get_source_by_name(name.toUtf8().constData());
			if (existing)
				QMessageBox::warning(this, Tr("MDS.Rename"), Tr("MDS.SourceExists"));
			else
				obs_source_set_name(source, name.toUtf8().constData());
		}
		QTimer::singleShot(0, this, &StudioWindow::RefreshSources);
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

	QToolBar *sourceBar = DockStyle::Toolbar(this);
	QAction *addSource = DockStyle::AddAction(sourceBar, "icon-plus", DockStyle::PlusIcon(), Tr("MDS.AddSource"),
						  [this]() { ShowAddSourceMenu(); });
	addSourceButton = qobject_cast<QToolButton *>(sourceBar->widgetForAction(addSource));
	DockStyle::AddAction(sourceBar, "icon-trash", st->standardIcon(QStyle::SP_TrashIcon), Tr("MDS.RemoveSource"),
			     [this]() { EditRemove(); });
	DockStyle::AddAction(sourceBar, "icon-gear", st->standardIcon(QStyle::SP_FileDialogDetailedView),
			     Tr("MDS.Properties"), [this]() { EditProperties(); });
	sourceBar->addSeparator();
	DockStyle::AddAction(sourceBar, nullptr, DockStyle::FolderPlusIcon(), Tr("MDS.NewSourceFolderTip"),
			     [this]() { EditGroup(); });
	sourceBar->addSeparator();
	DockStyle::AddAction(sourceBar, "icon-up", st->standardIcon(QStyle::SP_ArrowUp), Tr("MDS.MoveUp"),
			     [this]() { EditMoveUp(); });
	DockStyle::AddAction(sourceBar, "icon-down", st->standardIcon(QStyle::SP_ArrowDown), Tr("MDS.MoveDown"),
			     [this]() { EditMoveDown(); });
	DockStyle::StyleTree(sourceList, true);
	Dock(Tr("MDS.Sources"), "mdsSourcesDock", DockStyle::Panel(sourceList, sourceBar, this), this);

	/* --- Cues (fonction optionnelle, voir Réglages) --- */
	cuesPanel = new CuesPanel(this);
	cuesPanel->selectedScenes = [this]() {
		std::vector<std::string> uuids;
		for (QTreeWidgetItem *row : SelectedRowsInOrder(sceneList)) {
			if (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_SCENE)
				uuids.push_back(row->data(0, Qt::UserRole).toString().toStdString());
		}
		return uuids;
	};
	cuesDock = Dock(Tr("MDS.Cues"), "mdsCuesDock", cuesPanel, this);

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
	auto *transitionProps = ToolButton(QString(), Tr("MDS.TransitionProperties"), transitions);
	transitionProps->setIcon(style()->standardIcon(QStyle::SP_FileDialogDetailedView));
	SetIconClass(transitionProps, "icon-gear");
	connect(transitionProps, &QToolButton::clicked, this, [this]() {
		OBSSourceAutoRelease current = obs_frontend_get_current_transition();
		OBSSource tr = current.Get();
		if (tr)
			OpenOnStudio([tr]() { obs_frontend_open_source_properties(tr); });
	});
	row->addWidget(transitionProps);
	/* Supprimer : bouton d'OBS (avec sa confirmation). */
	auto *transitionRemove = ToolButton(QString(), Tr("MDS.TransitionRemove"), transitions);
	transitionRemove->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
	SetIconClass(transitionRemove, "icon-trash");
	connect(transitionRemove, &QToolButton::clicked, this, []() {
		auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (auto *button = main ? main->findChild<QPushButton *>("transitionRemove") : nullptr)
			button->click();
	});
	row->addWidget(transitionRemove);
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
	/* Cues (si la fonction est activée) entre Sources et Contrôles média. */
	const bool cues = Manager::Get().CuesEnabled();
	std::vector<const char *> names = {"mdsScenesDock", "mdsSourcesDock"};
	if (cues)
		names.push_back("mdsCuesDock");
	for (const char *n : {"mdsMediaDock", "mdsMixerDock", "mdsTransitionsDock", "mdsControlsDock"})
		names.push_back(n);
	QList<QDockWidget *> docks;
	for (const char *name : names) {
		if (auto *dock = findChild<QDockWidget *>(name))
			docks << dock;
	}
	if (cuesDock && !cues) {
		cuesDock->setFloating(false);
		if (dockWidgetArea(cuesDock) != Qt::NoDockWidgetArea)
			removeDockWidget(cuesDock);
		cuesDock->hide();
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
	const bool cues = Manager::Get().CuesEnabled() && cuesDock && !cuesDock->isHidden();
	std::vector<std::pair<const char *, int>> parts = {{"mdsScenesDock", 298}, {"mdsSourcesDock", 327}};
	if (cues)
		parts.push_back({"mdsCuesDock", 330});
	for (auto p : std::vector<std::pair<const char *, int>>{{"mdsMediaDock", 420},
								{"mdsMixerDock", 285},
								{"mdsTransitionsDock", 295},
								{"mdsControlsDock", 268}})
		parts.push_back(p);
	QList<QDockWidget *> docks;
	int total = 0;
	for (auto &p : parts) {
		if (auto *dock = findChild<QDockWidget *>(p.first))
			docks << dock;
		total += p.second;
	}
	if (docks.size() != int(parts.size()))
		return;
	const int W = std::max(width(), 1200);
	const int H = std::max(height(), 700);
	QList<int> widths;
	for (auto &p : parts)
		widths << W * p.second / total;
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
	connect(file->addAction(Tr("MDS.ExitStudio")), &QAction::triggered, this, &StudioWindow::RequestClose);

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
	transform->addAction(MakeEditAction(Tr("MDS.CropSource"), {}, &StudioWindow::EditCrop));
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

	/* --- Show : la collection de scènes d'OBS et ses réglages --- */
	showMenu = bar->addMenu(Tr("MDS.MenuShow"));
	connect(showMenu->addAction(Tr("MDS.ShowNew")), &QAction::triggered, this, [this]() { Show::New(this); });
	showMenu->addSeparator();
	connect(showMenu->addAction(Tr("MDS.ShowSave")), &QAction::triggered, this, []() { Show::Save(); });
	connect(showMenu->addAction(Tr("MDS.ShowSaveAs")), &QAction::triggered, this,
		[this]() { OpenOnStudio([this]() { Show::SaveAs(this); }); });
	loadShowMenu = showMenu->addMenu(Tr("MDS.ShowLoad"));
	showMenu->addSeparator();
	connect(showMenu->addAction(Tr("MDS.ShowImport")), &QAction::triggered, this,
		[this]() { OpenOnStudio([this]() { Show::Import(this); }); });
	connect(showMenu->addAction(Tr("MDS.ShowExport")), &QAction::triggered, this,
		[this]() { OpenOnStudio([this]() { Show::Export(this); }); });
#ifndef Q_OS_MACOS
	connect(showMenu, &QMenu::aboutToShow, this, &StudioWindow::FillShowMenu);
#endif
	FillShowMenu();

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
	settingsMenu->addSeparator();
	connect(settingsMenu->addAction(Tr("MDS.SettingCuesMenu")), &QAction::triggered, this,
		&StudioWindow::EditCuesSetting);
	settingsMenu->addSeparator();
	connect(settingsMenu->addAction(Tr("MDS.SettingStartupMenu")), &QAction::triggered, this,
		[this]() { Dialogs::EditStartupSetting(this); });
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
		RequestClose();
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

bool StudioWindow::RequestClose()
{
	if (Show::AskToSave(this) == Show::Answer::Cancelled)
		return false;
	Close();
	return true;
}

void StudioWindow::closeEvent(QCloseEvent *event)
{
	event->ignore();
	RequestClose();
}

void StudioWindow::FillShowMenu()
{
	if (!loadShowMenu)
		return;
	loadShowMenu->clear();
	const QString current = Show::CurrentName();
	for (const QString &name : Show::Names()) {
		QAction *action = loadShowMenu->addAction(name);
		action->setCheckable(true);
		action->setChecked(name == current);
		connect(action, &QAction::triggered, this, [this, name]() {
			Show::Load(this, name);
			FillShowMenu();
		});
	}
	loadShowMenu->setEnabled(!loadShowMenu->isEmpty());
}

void StudioWindow::OnShowChanged()
{
	const QString name = Show::CurrentName();
	setWindowTitle(name.isEmpty() ? QStringLiteral("Multidestination Studio")
				      : QStringLiteral("Multidestination Studio — %1").arg(name));
	/* Hors de l'ouverture du menu (macOS : menus natifs). */
	QTimer::singleShot(0, this, &StudioWindow::FillShowMenu);
}

void StudioWindow::Shutdown()
{
	TileWidget::openProperties = nullptr;
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
	ApplyCuesFeature();
}

/* Dock Cues affiché seulement si la fonction est activée ; à l'activation,
 * il prend sa place entre Sources et Contrôles média. */
void StudioWindow::ApplyCuesFeature()
{
	if (!cuesDock)
		return;
	const bool enabled = Manager::Get().CuesEnabled();
	cuesDock->toggleViewAction()->setVisible(enabled);
	if (!enabled) {
		cuesDock->hide();
		CueEngine::Get().Disarm();
		return;
	}
	if (!cuesDock->isHidden() && dockWidgetArea(cuesDock) != Qt::NoDockWidgetArea)
		return;
	auto *sources = findChild<QDockWidget *>("mdsSourcesDock");
	cuesDock->setFloating(false);
	if (sources && dockWidgetArea(sources) != Qt::NoDockWidgetArea && !sources->isFloating())
		splitDockWidget(sources, cuesDock, Qt::Horizontal);
	else
		addDockWidget(Qt::BottomDockWidgetArea, cuesDock);
	cuesDock->show();
	if (auto *s = findChild<QDockWidget *>("mdsSourcesDock"))
		resizeDocks({s, cuesDock}, {s->width() / 2 + 1, s->width() / 2 + 1}, Qt::Horizontal);
}

void StudioWindow::EditCuesSetting()
{
	Dialogs::EditSwitchSetting(this, Tr("MDS.SettingCues"), Tr("MDS.SettingCuesNote"),
				   {{Tr("MDS.SettingCuesActivate"), Manager::Get().CuesEnabled(), [](bool on) {
					     Manager::Get().SetCuesEnabled(on);
				     }}});
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
		QTreeWidgetItem *found = nullptr;
		for (int i = 0; i < sourceList->topLevelItemCount() && !found; i++) {
			QTreeWidgetItem *top = sourceList->topLevelItem(i);
			if (top->data(0, Qt::UserRole).toLongLong() == id)
				found = top;
			for (int k = 0; k < top->childCount() && !found; k++) {
				if (top->child(k)->data(0, Qt::UserRole).toLongLong() == id)
					found = top->child(k);
			}
		}
		/* Clic droit sur une source déjà sélectionnée : la sélection
		 * reste (le menu agit sur toutes). */
		if (t->ContextClick() && found && found->isSelected()) {
			sourceList->setCurrentItem(found, 0, QItemSelectionModel::NoUpdate);
			UpdateMediaControls();
			return;
		}
		/* Un clic dans l'aperçu sélectionne cette source seule. */
		sourceList->clearSelection();
		if (found) {
			if (found->parent())
				found->parent()->setExpanded(true);
			sourceList->setCurrentItem(found);
			sourceList->scrollToItem(found);
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

	/* Ctrl+Z (Cmd+Z) dans le studio : OBS déclare son Annuler pour toute
	 * l'application ; avec celui du studio, Qt voyait deux raccourcis pour
	 * la même touche et n'en lançait aucun. Le studio le prend pour lui, sauf
	 * dans un champ de texte. */
	if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) && isVisible()) {
		auto *w = qobject_cast<QWidget *>(watched);
		auto *key = static_cast<QKeyEvent *>(event);
		const bool textInput = qobject_cast<QLineEdit *>(w) || qobject_cast<QAbstractSpinBox *>(w) ||
				       (w && (w->inherits("QTextEdit") || w->inherits("QPlainTextEdit")));
		if (w && w->window() == this && !textInput && key->matches(QKeySequence::Undo)) {
			event->accept();
			if (event->type() == QEvent::KeyPress)
				EditUndo();
			return true;
		}
	}

	/* Fenêtre d'OBS sur le point de s'afficher, ouverte depuis le studio :
	 * elle passe devant le studio au lieu de faire venir la fenêtre d'OBS. */
	if (adoptingWindows && event->type() == QEvent::Show) {
		auto *w = qobject_cast<QWidget *>(watched);
		QWidget *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		if (w && w != this && w->isWindow() && w->parentWidget() == main && w->windowHandle() &&
		    windowHandle() && !w->inherits("QMenu"))
			w->windowHandle()->setTransientParent(windowHandle());
	}

	/* La fenêtre d'OBS revient (clic dans la barre des tâches / le Dock,
	 * fenêtre restaurée) : le studio ouvert passe devant. */
	if (event->type() == QEvent::WindowStateChange && isVisible() &&
	    watched == static_cast<QObject *>(obs_frontend_get_main_window())) {
		auto *change = static_cast<QWindowStateChangeEvent *>(event);
		auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
		if ((change->oldState() & Qt::WindowMinimized) && !(main->windowState() & Qt::WindowMinimized))
			QTimer::singleShot(0, this, &StudioWindow::BringToFront);
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

void StudioWindow::BringToFront()
{
	if (!isVisible())
		return;
	if (isMinimized())
		showNormal();
	raise();
	activateWindow();
}

/* Fenêtres d'OBS ouvertes depuis le studio (propriétés, filtres) : OBS les
 * rattache à sa fenêtre principale, que le système faisait alors passer
 * devant le studio. Juste avant leur affichage (voir eventFilter), elles
 * sont rattachées à la fenêtre du studio : elles s'ouvrent devant lui, sans
 * que la fenêtre d'OBS apparaisse. Elles restent des fenêtres d'OBS (même
 * propriétaire Qt), qui les ferme et les détruit comme d'habitude. */
void StudioWindow::OpenOnStudio(const std::function<void()> &open)
{
	adoptingWindows = true;
	open();
	adoptingWindows = false;
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

/* ---- Déplacements à la souris (scènes et sources) ---- */

static QTreeWidgetItem *NextSibling(QTreeWidget *tree, QTreeWidgetItem *item)
{
	if (!item)
		return nullptr;
	if (QTreeWidgetItem *p = item->parent()) {
		const int i = p->indexOfChild(item);
		return i + 1 < p->childCount() ? p->child(i + 1) : nullptr;
	}
	const int i = tree->indexOfTopLevelItem(item);
	return i + 1 < tree->topLevelItemCount() ? tree->topLevelItem(i + 1) : nullptr;
}

/* Lignes sélectionnées dans l'ordre de la liste. Un élément d'un dossier
 * (ou d'un groupe) sélectionné en même temps que son dossier suit le
 * dossier : il n'est pas compté à part. */
static std::vector<QTreeWidgetItem *> SelectedRowsInOrder(QTreeWidget *tree)
{
	std::vector<QTreeWidgetItem *> rows;
	for (int i = 0; i < tree->topLevelItemCount(); i++) {
		QTreeWidgetItem *top = tree->topLevelItem(i);
		if (top->isSelected()) {
			rows.push_back(top);
			continue;
		}
		for (int k = 0; k < top->childCount(); k++) {
			if (top->child(k)->isSelected())
				rows.push_back(top->child(k));
		}
	}
	return rows;
}

/* Où insérer : dans quel conteneur (nullptr = premier niveau), devant
 * quelle ligne (nullptr = à la fin). */
struct DropPlace {
	QTreeWidgetItem *parent = nullptr;
	QTreeWidgetItem *before = nullptr;
};

using RowTest = std::function<bool(QTreeWidgetItem *)>;

static DropPlace ResolveDrop(QTreeWidget *tree, QTreeWidgetItem *target, int position, const RowTest &isContainer,
			     bool intoTop)
{
	DropPlace place;
	if (!target || position == OrganizerTree::DropViewport)
		return place;
	/* Sur un dossier : dedans (à la fin pour les scènes, en haut pour les
	 * sources, comme OBS). */
	if (position == OrganizerTree::DropOn && isContainer(target)) {
		place.parent = target;
		place.before = intoTop && target->childCount() > 0 ? target->child(0) : nullptr;
		return place;
	}
	if (position == OrganizerTree::DropBelow) {
		/* Juste sous un dossier ouvert : c'est son premier élément. */
		if (isContainer(target) && target->isExpanded() && target->childCount() > 0) {
			place.parent = target;
			place.before = target->child(0);
			return place;
		}
		place.parent = target->parent();
		place.before = NextSibling(tree, target);
		return place;
	}
	place.parent = target->parent();
	place.before = target;
	return place;
}

/* Déplace des lignes dans la liste (sans rien appliquer ailleurs). Un
 * dossier ne va jamais dans un dossier : il se place juste après. */
static bool MoveRows(QTreeWidget *tree, const std::vector<QTreeWidgetItem *> &rows, DropPlace place,
		     const RowTest &isContainer)
{
	if (rows.empty())
		return false;
	const std::set<QTreeWidgetItem *> moving(rows.begin(), rows.end());
	if (place.parent && moving.count(place.parent))
		return false;
	bool hasContainer = false;
	for (QTreeWidgetItem *r : rows)
		hasContainer = hasContainer || isContainer(r);
	if (hasContainer && place.parent) {
		place.before = NextSibling(tree, place.parent);
		place.parent = nullptr;
	}
	while (place.before && moving.count(place.before))
		place.before = NextSibling(tree, place.before);

	std::map<QTreeWidgetItem *, bool> expanded;
	for (QTreeWidgetItem *r : rows)
		expanded[r] = r->isExpanded();
	for (QTreeWidgetItem *r : rows) {
		if (QTreeWidgetItem *p = r->parent())
			p->takeChild(p->indexOfChild(r));
		else
			tree->takeTopLevelItem(tree->indexOfTopLevelItem(r));
	}
	int index = 0;
	if (place.before)
		index = place.parent ? place.parent->indexOfChild(place.before)
				     : tree->indexOfTopLevelItem(place.before);
	else
		index = place.parent ? place.parent->childCount() : tree->topLevelItemCount();
	for (QTreeWidgetItem *r : rows) {
		if (place.parent)
			place.parent->insertChild(index++, r);
		else
			tree->insertTopLevelItem(index++, r);
		r->setExpanded(expanded[r]);
	}
	if (place.parent)
		place.parent->setExpanded(true);
	return true;
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
	const QSignalBlocker blocker(sceneList);
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
	row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsEditable);
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
	row->setData(0, OrganizerTree::ContainerRole, true);
	row->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled |
		      Qt::ItemIsEditable);
	QIcon icon = DockStyle::MainIcon("groupIcon");
	row->setIcon(0, icon.isNull() ? QApplication::style()->standardIcon(QStyle::SP_DirIcon) : icon);
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

	/* Sélection (Ctrl / Maj) et défilement conservés. */
	auto rowKey = [](QTreeWidgetItem *row) {
		return (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER ? QString("folder:")
										   : QString("scene:")) +
		       row->data(0, Qt::UserRole).toString();
	};
	QSet<QString> selectedKeys;
	for (QTreeWidgetItem *row : sceneList->selectedItems())
		selectedKeys.insert(rowKey(row));
	const int scroll = sceneList->verticalScrollBar()->value();

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

	QTreeWidgetItem *previewRow = nullptr;
	for (QTreeWidgetItem *row : SceneRows(sceneList)) {
		if (preview && row->data(0, Qt::UserRole).toString() == QString::fromUtf8(obs_source_get_uuid(preview)))
			previewRow = row;
	}
	const bool keepSelection = !previewRow || selectedKeys.contains(rowKey(previewRow));
	if (keepSelection) {
		for (int i = 0; i < sceneList->topLevelItemCount(); i++) {
			QTreeWidgetItem *top = sceneList->topLevelItem(i);
			top->setSelected(selectedKeys.contains(rowKey(top)));
			for (int k = 0; k < top->childCount(); k++)
				top->child(k)->setSelected(selectedKeys.contains(rowKey(top->child(k))));
		}
		if (previewRow)
			sceneList->setCurrentItem(previewRow, 0, QItemSelectionModel::NoUpdate);
	} else {
		sceneList->setCurrentItem(previewRow);
	}
	sceneList->verticalScrollBar()->setValue(scroll);
	refreshingScenes = false;
	UpdateTakeButtons();

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
			obs_data_set_string(entry, "folder",
					    top->data(0, Qt::UserRole).toString().toUtf8().constData());
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

void StudioWindow::MoveSceneRows(QTreeWidgetItem *target, int position)
{
	const RowTest isFolder = [](QTreeWidgetItem *r) {
		return r->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER;
	};
	const auto rows = SelectedRowsInOrder(sceneList);
	const DropPlace place = ResolveDrop(sceneList, target, position, isFolder, false);
	bool moved = false;
	{
		const QSignalBlocker blocker(sceneList);
		refreshingScenes = true;
		moved = MoveRows(sceneList, rows, place, isFolder);
		refreshingScenes = false;
	}
	if (!moved)
		return;
	SaveSceneTree();
	RefreshScenes();
}

void StudioWindow::ApplySceneSelection()
{
	/* Ctrl + clic qui retire une scène de la sélection, ou dossier : l'Aperçu
	 * ne change pas. */
	QTreeWidgetItem *row = sceneList->currentItem();
	if (!row || !row->isSelected())
		return;
	OBSSource scene = SelectedScene();
	if (scene)
		Manager::Get().SetPreviewScene(scene);
}

/* Transition / Cut indisponibles quand un dossier est sélectionné : un
 * dossier n'est qu'un rangement, il ne passe pas à l'antenne. */
void StudioWindow::UpdateTakeButtons()
{
	if (!transitionButton || !sceneList)
		return;
	QTreeWidgetItem *row = sceneList->currentItem();
	const bool folder = row && row->isSelected() && row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_FOLDER;
	transitionButton->setEnabled(!folder);
	if (cutButton)
		cutButton->setEnabled(!folder);
	for (QPushButton *b : quickButtons)
		b->setEnabled(!folder);
	if (tbar)
		tbar->setEnabled(!folder || Manager::Get().TBarActive());
	folderTakeNote->setVisible(folder);
}

void StudioWindow::RenameRow(QTreeWidget *tree, QTreeWidgetItem *row)
{
	if (!tree || !row)
		return;
	tree->scrollToItem(row);
	tree->editItem(row, 0);
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

	/* Les scènes sélectionnées entrent dans le nouveau dossier, ainsi
	 * que celles des dossiers sélectionnés (ces dossiers disparaissent). */
	std::vector<QTreeWidgetItem *> selected;
	std::vector<QTreeWidgetItem *> mergedFolders;
	for (QTreeWidgetItem *row : SelectedRowsInOrder(sceneList)) {
		if (row->data(0, Qt::UserRole + 1).toInt() == SCENE_ROW_SCENE) {
			selected.push_back(row);
		} else {
			mergedFolders.push_back(row);
			for (int k = 0; k < row->childCount(); k++)
				selected.push_back(row->child(k));
		}
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
		for (QTreeWidgetItem *f : mergedFolders)
			delete sceneList->takeTopLevelItem(sceneList->indexOfTopLevelItem(f));
		folder->setExpanded(true);
		sceneList->clearSelection();
		folder->setSelected(true);
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

void StudioWindow::MoveSourceRows(QTreeWidgetItem *target, int position)
{
	obs_scene_t *s = ActiveScene();
	if (!s)
		return;
	const RowTest isGroup = [](QTreeWidgetItem *r) {
		return r->data(0, Qt::UserRole + 2).toBool();
	};

	/* Éléments OBS de toutes les lignes, résolus avant tout changement
	 * (d'après leur place d'origine). */
	std::map<QTreeWidgetItem *, OBSSceneItem> items;
	for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
		QTreeWidgetItem *top = sourceList->topLevelItem(i);
		items[top] = ItemFromRow(top);
		for (int k = 0; k < top->childCount(); k++)
			items[top->child(k)] = ItemFromRow(top->child(k));
	}

	const auto rows = SelectedRowsInOrder(sourceList);
	const DropPlace place = ResolveDrop(sourceList, target, position, isGroup, true);
	bool moved = false;
	{
		const QSignalBlocker blocker(sourceList);
		refreshingSources = true;
		moved = MoveRows(sourceList, rows, place, isGroup);
		refreshingSources = false;
	}
	if (!moved) {
		RefreshSources();
		return;
	}

	Undo::PushSceneState(s);
	/* Nouvel ordre appliqué d'un coup, groupes compris, comme le fait OBS
	 * (obs_scene_reorder_items2) : les éléments qui entrent dans un groupe ou
	 * en sortent gardent leur place à l'écran. Liste du bas vers le haut :
	 * chaque groupe, puis ses éléments du bas vers le haut. */
	std::vector<struct obs_sceneitem_order_info> order;
	for (int i = sourceList->topLevelItemCount() - 1; i >= 0; i--) {
		QTreeWidgetItem *top = sourceList->topLevelItem(i);
		OBSSceneItem &item = items[top];
		if (!item)
			continue;
		order.push_back({nullptr, item});
		if (!obs_sceneitem_is_group(item))
			continue;
		for (int k = top->childCount() - 1; k >= 0; k--) {
			if (OBSSceneItem &child = items[top->child(k)])
				order.push_back({item, child});
		}
	}
	obs_scene_reorder_items2(s, order.data(), order.size());

	/* Après un glisser : seules les lignes déplacées restent sélectionnées
	 * (l'ancienne sélection de l'aperçu ne s'y ajoute plus). */
	QSet<qlonglong> movedIds;
	for (QTreeWidgetItem *row : rows)
		movedIds.insert(row->data(0, Qt::UserRole).toLongLong());
	if (TileWidget *tile = ActiveTile(); tile && !rows.empty())
		tile->SetSelectedItem(rows.front()->data(0, Qt::UserRole).toLongLong());
	RefreshSources();
	{
		const QSignalBlocker blocker(sourceList);
		sourceList->clearSelection();
		for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
			QTreeWidgetItem *top = sourceList->topLevelItem(i);
			top->setSelected(movedIds.contains(top->data(0, Qt::UserRole).toLongLong()));
			for (int k = 0; k < top->childCount(); k++)
				top->child(k)->setSelected(
					movedIds.contains(top->child(k)->data(0, Qt::UserRole).toLongLong()));
		}
	}
	SyncMultiSelection();
	AfterEdit();
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

/* Dupliquer une scène : nom et destinations, puis copie de son contenu dans
 * chaque destination. */
void StudioWindow::DuplicateScene(obs_source_t *scene)
{
	if (!scene)
		return;
	const QString base = QString::fromUtf8(obs_source_get_name(scene));
	QString candidate;
	for (int n = 2;; n++) {
		candidate = QString("%1 %2").arg(base).arg(n);
		OBSSourceAutoRelease existing = obs_get_source_by_name(candidate.toUtf8().constData());
		if (!existing)
			break;
	}
	std::string name = candidate.toStdString();
	std::set<Destination *> coverage;
	while (true) {
		if (!Dialogs::DuplicateScene(this, scene, name, coverage))
			return;
		OBSSource copy = Manager::Get().DuplicateScene(scene, name, coverage);
		if (copy) {
			Manager::Get().SetPreviewScene(copy);
			return;
		}
		QMessageBox::warning(this, Tr("MDS.DuplicateSceneTitle"), Tr("MDS.SceneExists"));
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

static QTreeWidgetItem *MakeSourceRow(obs_sceneitem_t *item, int64_t parentId, const QColor &color)
{
	obs_source_t *src = obs_sceneitem_get_source(item);
	auto *row = new QTreeWidgetItem();
	const bool group = obs_sceneitem_is_group(item);
	row->setText(0, QString::fromUtf8(obs_source_get_name(src)));
	/* Tout se glisse ; on dépose dans un groupe (pas dans une source). */
	Qt::ItemFlags flags = Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsDragEnabled | Qt::ItemIsEditable;
	if (group)
		flags |= Qt::ItemIsDropEnabled;
	row->setFlags(flags);
	row->setData(0, Qt::UserRole, qlonglong(obs_sceneitem_get_id(item)));
	row->setData(0, Qt::UserRole + 1, qlonglong(parentId));
	row->setData(0, Qt::UserRole + 2, group);
	row->setData(0, OrganizerTree::ContainerRole, group);
	/* Icône du type de source, comme OBS. */
	QIcon icon = DockStyle::SourceIcon(src);
	if (icon.isNull() && group)
		icon = QApplication::style()->standardIcon(QStyle::SP_DirIcon);
	row->setIcon(0, icon);
	row->setData(0, TrailingIconDelegate::IconRole + SOURCE_ICON_VISIBLE,
		     DockStyle::VisibilityIcon(obs_sceneitem_visible(item), color));
	row->setData(0, TrailingIconDelegate::IconRole + SOURCE_ICON_LOCK,
		     DockStyle::LockIcon(obs_sceneitem_locked(item), color));
	return row;
}

void StudioWindow::RefreshSources()
{
	refreshingSources = true;
	/* Sélection (Ctrl / Maj) et défilement conservés. */
	QSet<qlonglong> selectedIds;
	for (QTreeWidgetItem *row : sourceList->selectedItems())
		selectedIds.insert(row->data(0, Qt::UserRole).toLongLong());
	const int scroll = sourceList->verticalScrollBar()->value();
	const QColor color = sourceList->palette().color(QPalette::Text);
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
		QTreeWidgetItem *currentRow = nullptr;
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
		/* À l'antenne, OBS peut afficher une copie de la scène dont les
		 * groupes n'ont pas de vrai nom : on reprend ceux de la scène
		 * d'origine, dans le même ordre. */
		std::vector<std::string> originalGroups;
		if (activeIsProgram && activeDest) {
			OBSSource original = activeDest->ProgramOriginal();
			if (obs_scene_t *os = obs_scene_from_source(original); os && os != s) {
				obs_scene_enum_items(
					os,
					[](obs_scene_t *, obs_sceneitem_t *item, void *param) {
						if (obs_sceneitem_is_group(item)) {
							const char *n =
								obs_source_get_name(obs_sceneitem_get_source(item));
							static_cast<std::vector<std::string> *>(param)->insert(
								static_cast<std::vector<std::string> *>(param)->begin(),
								n ? n : "");
						}
						return true;
					},
					&originalGroups);
			}
		}
		size_t groupIndex = 0;
		for (obs_sceneitem_t *item : items) {
			QTreeWidgetItem *row = MakeSourceRow(item, 0, color);
			if (obs_sceneitem_is_group(item) && groupIndex < originalGroups.size()) {
				const std::string &n = originalGroups[groupIndex++];
				if (!n.empty())
					row->setText(0, QString::fromStdString(n));
			}
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
					QTreeWidgetItem *c = MakeSourceRow(child, id, color);
					row->addChild(c);
					if (obs_sceneitem_get_id(child) == selected)
						currentRow = c;
				}
				OBSDataAutoRelease priv = obs_sceneitem_get_private_settings(item);
				row->setExpanded(!obs_data_get_bool(priv, "collapsed"));
			}
			if (id == selected && !currentRow)
				currentRow = row;
		}
		/* La ligne de l'aperçu fait partie de la sélection : on garde
		 * toute la sélection ; sinon elle seule est sélectionnée. */
		if (currentRow && selectedIds.contains(currentRow->data(0, Qt::UserRole).toLongLong())) {
			for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
				QTreeWidgetItem *top = sourceList->topLevelItem(i);
				top->setSelected(selectedIds.contains(top->data(0, Qt::UserRole).toLongLong()));
				for (int k = 0; k < top->childCount(); k++)
					top->child(k)->setSelected(selectedIds.contains(
						top->child(k)->data(0, Qt::UserRole).toLongLong()));
			}
			sourceList->setCurrentItem(currentRow, 0, QItemSelectionModel::NoUpdate);
		} else if (currentRow) {
			sourceList->setCurrentItem(currentRow);
		}
	}
	sourceList->verticalScrollBar()->setValue(scroll);
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
	if (id < 0) {
		if (QTreeWidgetItem *row = sourceList->currentItem())
			return ItemFromRow(row);
	}
	/* Premier niveau ou élément d'un groupe. */
	return id >= 0 ? OBSSceneItem(SceneEditor::FindItem(s, id)) : OBSSceneItem();
}

void StudioWindow::SelectItem(int64_t id)
{
	if (TileWidget *t = ActiveTile())
		t->SetSelectedItem(id);
	RefreshSources();
}

/* Sélection multiple de la liste transmise à la tuile active (déplacement
 * et redimensionnement de plusieurs sources dans l'aperçu). */
void StudioWindow::SyncMultiSelection()
{
	std::vector<int64_t> ids;
	for (QTreeWidgetItem *row : sourceList->selectedItems())
		ids.push_back(row->data(0, Qt::UserRole).toLongLong());
	TileWidget *active = ActiveTile();
	for (TileGrid *grid : {pvwGrid, pgmGrid}) {
		for (TileWidget *tile : grid->Tiles())
			tile->SetMultiSelection(tile == active ? ids : std::vector<int64_t>());
	}
}

void StudioWindow::UpdateMediaControls()
{
	SyncMultiSelection();
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
	OBSSource created = source.Get();
	OpenOnStudio([created]() { obs_frontend_open_source_properties(created); });
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
		QAction *lock = menu.addAction(Tr("MDS.LockSource"));
		lock->setCheckable(true);
		lock->setChecked(obs_sceneitem_locked(item));
		connect(lock, &QAction::toggled, this, [this, it](bool on) {
			obs_sceneitem_set_locked(it, on);
			RefreshSources();
		});
		connect(menu.addAction(Tr("MDS.Rename")), &QAction::triggered, this, [this]() {
			QTreeWidgetItem *row = sourceList->currentItem();
			if (row)
				RenameRow(sourceList, row);
			else
				EditRenameGroup();
		});
		/* Mettre la source dans un groupe ou l'en sortir sans la déplacer à
		 * l'écran (comme un glisser dans la liste). */
		QTreeWidgetItem *row = nullptr;
		for (int i = 0; i < sourceList->topLevelItemCount() && !row; i++) {
			QTreeWidgetItem *top = sourceList->topLevelItem(i);
			if (ItemFromRow(top).Get() == item.Get())
				row = top;
			for (int k = 0; k < top->childCount() && !row; k++) {
				if (ItemFromRow(top->child(k)).Get() == item.Get())
					row = top->child(k);
			}
		}
		if (row && !obs_sceneitem_is_group(item)) {
			QMenu *into = nullptr;
			for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
				QTreeWidgetItem *top = sourceList->topLevelItem(i);
				if (!top->data(0, Qt::UserRole + 2).toBool() || top == row->parent())
					continue;
				if (!into)
					into = menu.addMenu(Tr("MDS.MoveToGroup"));
				connect(into->addAction(top->text(0)), &QAction::triggered, this, [this, row, top]() {
					sourceList->clearSelection();
					sourceList->setCurrentItem(row);
					row->setSelected(true);
					MoveSourceRows(top, OrganizerTree::DropOn);
				});
			}
			if (QTreeWidgetItem *parent = row->parent()) {
				connect(menu.addAction(Tr("MDS.RemoveFromGroup")), &QAction::triggered, this,
					[this, row, parent]() {
						sourceList->clearSelection();
						sourceList->setCurrentItem(row);
						row->setSelected(true);
						MoveSourceRows(parent, OrganizerTree::DropAbove);
					});
			}
		}
		if (obs_sceneitem_is_group(item))
			connect(menu.addAction(Tr("MDS.UngroupSources")), &QAction::triggered, this,
				&StudioWindow::EditUngroup);
		else if (sourceList->selectedItems().size() > 1)
			connect(menu.addAction(Tr("MDS.GroupSources")), &QAction::triggered, this,
				&StudioWindow::EditGroup);
		menu.addSeparator();
	}
	/* Coller : raccourci écrit entre parenthèses (macOS n'affiche pas les
	 * raccourcis dans les menus contextuels), en Cmd / Option sur macOS et
	 * Ctrl / Alt ailleurs. */
#ifdef Q_OS_MACOS
	const QString pasteKeys = "Cmd+V";
	const QString pasteDuplicateKeys = "Cmd+Option+V";
#else
	const QString pasteKeys = "Ctrl+V";
	const QString pasteDuplicateKeys = "Ctrl+Alt+V";
#endif
	std::vector<std::pair<QAction *, QString>> renamed;
	for (QAction *a : editActions) {
		const bool paste = a->text() == Tr("MDS.Paste");
		const bool pasteDuplicate = a->text() == Tr("MDS.PasteDuplicate");
		const bool always = paste || pasteDuplicate || a->text() == Tr("MDS.Undo");
		a->setEnabled(item || always);
		if (paste || pasteDuplicate) {
			renamed.emplace_back(a, a->text());
			a->setShortcutVisibleInContextMenu(false);
			a->setText(QString("%1 (%2)").arg(a->text(), paste ? pasteKeys : pasteDuplicateKeys));
		}
		menu.addAction(a);
	}
	menu.exec(globalPos);
	for (auto &[a, text] : renamed)
		a->setText(text);
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
	/* Source d'un groupe : sa transformation est relative au groupe. On
	 * garde sa place réelle à l'écran pour la coller au même endroit. */
	hasClipboardBox = false;
	if (obs_scene_t *s = ActiveScene(); s && obs_sceneitem_get_group(s, item))
		hasClipboardBox = SceneEditor::GetWorldBox(s, item, clipboardBox);
}

void StudioWindow::ApplyClipboardTransform(obs_scene_t *scene, obs_sceneitem_t *item)
{
	if (!hasClipboardTransform)
		return;
	SceneEditor::SetTransform(item, clipboardTransform);
	if (hasClipboardBox)
		SceneEditor::SetWorldBox(scene, item, clipboardBox);
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
	ApplyClipboardTransform(s, item);
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
	/* OBS attend les éléments du fond vers l'avant : dans l'ordre de la
	 * sélection, les sources changeaient de plan en entrant dans le groupe. */
	std::sort(items.begin(), items.end(), [](obs_sceneitem_t *a, obs_sceneitem_t *b) {
		return obs_sceneitem_get_order_position(a) < obs_sceneitem_get_order_position(b);
	});
	if (items.empty()) {
		OBSSceneItem current = CurrentItem();
		if (current && obs_sceneitem_get_scene(current) == s && sourceList->currentItem() &&
		    sourceList->currentItem()->isSelected())
			items.push_back(current);
	}

	std::string name;
	for (int n = 1;; n++) {
		name = QString("%1 %2").arg(Tr("MDS.GroupName")).arg(n).toStdString();
		OBSSourceAutoRelease existing = obs_get_source_by_name(name.c_str());
		if (!existing)
			break;
	}
	/* Rien de sélectionné : nouveau dossier vide (groupe d'OBS). */
	obs_sceneitem_t *group = items.empty() ? obs_scene_add_group2(s, name.c_str(), true)
					       : obs_scene_insert_group(s, name.c_str(), items.data(), items.size());
	if (!group)
		return;
	const int64_t id = obs_sceneitem_get_id(group);
	SelectItem(id);
	AfterEdit();
	/* Nom modifiable tout de suite, comme dans OBS. */
	for (int i = 0; i < sourceList->topLevelItemCount(); i++) {
		if (sourceList->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == id)
			RenameRow(sourceList, sourceList->topLevelItem(i));
	}
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
	ApplyClipboardTransform(s, item);
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
	obs_scene_t *s = ActiveScene();
	hasCopiedBox = s && obs_sceneitem_get_group(s, item) && SceneEditor::GetWorldBox(s, item, copiedBox);
}

void StudioWindow::EditPasteTransform()
{
	OBSSceneItem item = CurrentItem();
	if (!item || !hasCopiedTransform)
		return;
	Undo::PushSceneState(ActiveScene());
	SceneEditor::SetTransform(item, copiedTransform);
	/* Même place à l'écran, que les sources soient dans un groupe ou non. */
	obs_scene_t *s = ActiveScene();
	if (hasCopiedBox || (s && obs_sceneitem_get_group(s, item))) {
		SceneEditor::Box box;
		if (hasCopiedBox)
			box = copiedBox;
		else if (!SceneEditor::GetBox(item, box))
			box = {};
		if (box.w > 0.0f && box.h > 0.0f)
			SceneEditor::SetWorldBox(s, item, box);
	}
	AfterEdit();
}

/* Ctrl+Z (Cmd+Z) dans une fenêtre du studio, le temps de son existence. */
class UndoKeyFilter : public QObject {
public:
	UndoKeyFilter(QWidget *window_, std::function<void()> fn_) : window(window_), fn(std::move(fn_))
	{
		qApp->installEventFilter(this);
	}

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() != QEvent::ShortcutOverride && event->type() != QEvent::KeyPress)
			return false;
		auto *w = qobject_cast<QWidget *>(watched);
		auto *key = static_cast<QKeyEvent *>(event);
		if (!w || w->window() != window || !key->matches(QKeySequence::Undo))
			return false;
		event->accept();
		if (event->type() == QEvent::KeyPress)
			fn();
		return true;
	}

private:
	QWidget *window;
	std::function<void()> fn;
};

/* Rognage de la source, comme la partie « Rogner » de la fenêtre
 * « Modifier la transformation » d'OBS : gauche, haut, droite, bas en
 * pixels de la source, appliqué en direct ; Annuler remet le rognage
 * d'avant. */
void StudioWindow::EditCrop()
{
	OBSSceneItem item = CurrentItem();
	if (!item)
		return;
	struct obs_sceneitem_crop before = {};
	obs_sceneitem_get_crop(item, &before);
	Undo::PushSceneState(ActiveScene());

	obs_source_t *source = obs_sceneitem_get_source(item);
	QDialog dialog(this);
	dialog.setWindowTitle(Tr("MDS.CropTitle").arg(QString::fromUtf8(obs_source_get_name(source))));
	auto *root = new QVBoxLayout(&dialog);
	auto *form = new QFormLayout();
	const uint32_t w = obs_source_get_width(source);
	const uint32_t h = obs_source_get_height(source);
	if (w && h)
		form->addRow(Tr("MDS.CropSourceSize"), new QLabel(QString("%1 × %2 px").arg(w).arg(h), &dialog));
	auto spin = [&dialog](int value) {
		auto *box = new QSpinBox(&dialog);
		box->setRange(0, 100000);
		box->setSuffix(" px");
		box->setValue(value);
		return box;
	};
	QSpinBox *left = spin(before.left);
	QSpinBox *top = spin(before.top);
	QSpinBox *right = spin(before.right);
	QSpinBox *bottom = spin(before.bottom);
	form->addRow(Tr("MDS.CropLeft"), left);
	form->addRow(Tr("MDS.CropRight"), right);
	form->addRow(Tr("MDS.CropTop"), top);
	form->addRow(Tr("MDS.CropBottom"), bottom);

	/* Aperçu de la source entière : poignées et cadre réglables à la
	 * souris, comme les champs. */
	auto *preview = new CropPreview(source, &dialog);
	preview->SetCrop(before);
	auto *hint = new QLabel(Tr("MDS.CropPreviewHint"), &dialog);
	hint->setWordWrap(true);
	root->addWidget(preview, 1);
	root->addWidget(hint);
	root->addLayout(form);
	dialog.resize(720, 640);

	/* Historique pour Ctrl+Z (Cmd+Z) dans la fenêtre : un glisser ou une
	 * saisie compte pour une étape. */
	struct CropHistory {
		std::vector<struct obs_sceneitem_crop> steps;
		struct obs_sceneitem_crop last = {};
		QElapsedTimer sinceChange;
		bool restoring = false;
		bool newStep = true;
	};
	auto history = std::make_shared<CropHistory>();
	history->last = before;

	obs_sceneitem_t *it = item;
	auto apply = [it, left, top, right, bottom, preview, history]() {
		struct obs_sceneitem_crop crop = {left->value(), top->value(), right->value(), bottom->value()};
		if (!history->restoring) {
			if (history->newStep || !history->sinceChange.isValid() || history->sinceChange.elapsed() > 600)
				history->steps.push_back(history->last);
			history->newStep = false;
			history->sinceChange.restart();
		}
		history->last = crop;
		obs_sceneitem_set_crop(it, &crop);
		preview->SetCrop(crop);
	};
	auto undo = [left, top, right, bottom, history]() {
		if (history->steps.empty())
			return;
		const struct obs_sceneitem_crop c = history->steps.back();
		history->steps.pop_back();
		history->restoring = true;
		left->setValue(c.left);
		top->setValue(c.top);
		right->setValue(c.right);
		bottom->setValue(c.bottom);
		history->restoring = false;
		history->newStep = true;
	};
	/* OBS déclare son Annuler pour toute l'application : la fenêtre le
	 * prend pour elle, même dans un champ. */
	UndoKeyFilter undoKeys(&dialog, undo);
	preview->onChanged = [left, top, right, bottom](const struct obs_sceneitem_crop &crop) {
		left->setValue(crop.left);
		top->setValue(crop.top);
		right->setValue(crop.right);
		bottom->setValue(crop.bottom);
	};
	for (QSpinBox *box : {left, top, right, bottom})
		connect(box, &QSpinBox::valueChanged, &dialog, apply);

	auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset,
					     &dialog);
	buttons->button(QDialogButtonBox::Cancel)->setText(Tr("MDS.Cancel"));
	buttons->button(QDialogButtonBox::Reset)->setText(Tr("MDS.CropReset"));
	connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
	connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
	connect(buttons->button(QDialogButtonBox::Reset), &QPushButton::clicked, &dialog, [=]() {
		for (QSpinBox *box : {left, top, right, bottom})
			box->setValue(0);
	});
	root->addWidget(buttons);
	Dialogs::GuardEnterKey(&dialog);

	if (dialog.exec() != QDialog::Accepted)
		obs_sceneitem_set_crop(item, &before);
	AfterEdit();
}

/* Sources visées par les commandes de transformation : toutes celles
 * sélectionnées dans la liste, sinon celle de l'aperçu. */
std::vector<OBSSceneItem> StudioWindow::TransformTargets() const
{
	std::vector<OBSSceneItem> items;
	if (sourceList->selectedItems().size() > 1) {
		for (QTreeWidgetItem *row : sourceList->selectedItems()) {
			if (OBSSceneItem it = ItemFromRow(row))
				items.push_back(it);
		}
	}
	if (items.empty()) {
		if (OBSSceneItem item = CurrentItem())
			items.push_back(item);
	}
	/* Un groupe et une de ses sources sélectionnés ensemble : la commande
	 * agit sur le groupe seulement (OBS ne recalcule la place du groupe
	 * qu'à l'image suivante, la source serait placée de travers). */
	obs_scene_t *s = ActiveScene();
	std::vector<OBSSceneItem> kept;
	for (auto &item : items) {
		obs_sceneitem_t *group = s ? obs_sceneitem_get_group(s, item) : nullptr;
		const bool groupToo = group && std::any_of(items.begin(), items.end(),
							   [group](const OBSSceneItem &o) { return o.Get() == group; });
		if (!groupToo)
			kept.push_back(item);
	}
	return kept;
}

void StudioWindow::EditResetTransform()
{
	const auto items = TransformTargets();
	if (!items.empty())
		Undo::PushSceneState(ActiveScene());
	for (auto &item : items)
		SceneEditor::ResetTransform(item);
	AfterEdit();
}

void StudioWindow::EditFit()
{
	const auto items = TransformTargets();
	if (!items.empty())
		Undo::PushSceneState(ActiveScene());
	for (auto &item : items)
		SceneEditor::FitToCanvas(ActiveScene(), item, activeDest->Width(), activeDest->Height());
	AfterEdit();
}

void StudioWindow::EditStretch()
{
	const auto items = TransformTargets();
	if (!items.empty())
		Undo::PushSceneState(ActiveScene());
	for (auto &item : items)
		SceneEditor::StretchToCanvas(ActiveScene(), item, activeDest->Width(), activeDest->Height());
	AfterEdit();
}

void StudioWindow::EditCenter()
{
	const auto items = TransformTargets();
	if (!items.empty())
		Undo::PushSceneState(ActiveScene());
	for (auto &item : items)
		SceneEditor::CenterInCanvas(ActiveScene(), item, activeDest->Width(), activeDest->Height());
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
	if (OBSSceneItem item = CurrentItem()) {
		OBSSource source = obs_sceneitem_get_source(item);
		OpenOnStudio([source]() { obs_frontend_open_source_properties(source); });
	}
}

void StudioWindow::EditFilters()
{
	if (OBSSceneItem item = CurrentItem()) {
		OBSSource source = obs_sceneitem_get_source(item);
		OpenOnStudio([source]() { obs_frontend_open_source_filters(source); });
	}
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

	/* Transition rapide en cours de création (fenêtre d'OBS) : la nouvelle
	 * transition de ce type apparaît dans la liste. */
	if (!pendingQuickType.empty()) {
		struct obs_frontend_source_list fresh = {};
		obs_frontend_get_transitions(&fresh);
		for (size_t i = 0; i < fresh.sources.num; i++) {
			obs_source_t *tr = fresh.sources.array[i];
			if (pendingQuickType == obs_source_get_id(tr) &&
			    !pendingQuickBefore.count(obs_source_get_uuid(tr))) {
				auto quick = Manager::Get().QuickTransitions();
				Manager::QuickTransition q;
				q.transition = obs_source_get_uuid(tr);
				q.name = obs_source_get_name(tr);
				q.durationMs = pendingQuickDuration;
				quick.push_back(q);
				pendingQuickType.clear();
				Manager::Get().SetQuickTransitions(quick);
				break;
			}
		}
		obs_frontend_source_list_free(&fresh);
	}

	durationSpin->setValue(obs_frontend_get_transition_duration());
	durationSpin->setEnabled(current && !obs_transition_fixed(current));
	refreshingTransitions = false;
	RefreshQuickTransitions();
}

/* Nouvelle transition d'OBS d'un type : on passe par le bouton « + »
 * d'OBS (son menu, sa fenêtre de nom et de propriétés), pour qu'elle soit
 * enregistrée et utilisable partout dans OBS. */
void StudioWindow::CreateObsTransition(const QString &displayName)
{
	auto *main = static_cast<QWidget *>(obs_frontend_get_main_window());
	auto *add = main ? main->findChild<QPushButton *>("transitionAdd") : nullptr;
	if (!add) {
		QMessageBox::information(this, Tr("MDS.SceneTransitions"), Tr("MDS.TransitionAddUnavailable"));
		return;
	}
	QTimer::singleShot(0, this, [displayName]() {
		auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
		if (!menu)
			return;
		QPointer<QAction> chosen;
		for (QAction *a : menu->actions()) {
			if (a->text() == displayName)
				chosen = a;
		}
		menu->close();
		if (chosen)
			QTimer::singleShot(0, chosen.data(), [chosen]() {
				if (chosen)
					chosen->trigger();
			});
	});
	add->click();
}

/* Bouton avec menu, comme celui d'OBS : clic sur la flèche = menu, clic
 * ailleurs = action. */
class MenuButton : public QPushButton {
public:
	using QPushButton::QPushButton;

protected:
	void mousePressEvent(QMouseEvent *event) override
	{
		if (menu() && width() - event->position().x() <= 30)
			showMenu();
		else if (menu())
			setDown(true);
		else
			QPushButton::mousePressEvent(event);
	}
};

static QString QuickTransitionText(const Manager::QuickTransition &q, obs_source_t *tr)
{
	QString text = QString::fromUtf8(obs_source_get_name(tr));
	if (!obs_transition_fixed(tr))
		text += QString(" (%1ms)").arg(q.durationMs);
	return text;
}

void StudioWindow::RefreshQuickTransitions()
{
	if (!quickPanel)
		return;
	quickButtons.clear();
	QLayout *layout = quickPanel->layout();
	while (QLayoutItem *item = layout->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}

	/* Une transition supprimée dans OBS : son bouton n'est pas affiché. */
	const auto &list = Manager::Get().QuickTransitions();
	for (size_t i = 0; i < list.size(); i++) {
		OBSSource tr = Manager::ResolveQuickTransition(list[i]);
		if (!tr)
			continue;
		auto *button = new MenuButton(QuickTransitionText(list[i], tr), quickPanel);
		const int index = int(i);
		auto *menu = new QMenu(button);
		connect(menu, &QMenu::aboutToShow, this, [this, menu, index]() {
			menu->clear();
			FillQuickTransitionMenu(menu, index);
		});
		button->setMenu(menu);
		button->setProperty("quickIndex", index);
		connect(button, &QPushButton::clicked, this, [index]() {
			const auto &l = Manager::Get().QuickTransitions();
			if (size_t(index) >= l.size())
				return;
			OBSSource t = Manager::ResolveQuickTransition(l[size_t(index)]);
			if (t)
				Manager::Get().Take(obs_transition_fixed(t), t, l[size_t(index)].durationMs);
		});
		layout->addWidget(button);
		quickButtons.push_back(button);
	}
	UpdateTakeButtons();
}

static QSpinBox *DurationSpin(int value, QWidget *parent)
{
	auto *spin = new QSpinBox(parent);
	spin->setRange(50, 20000);
	spin->setSingleStep(50);
	spin->setSuffix(" ms");
	spin->setValue(value);
	return spin;
}

/* « + » : uniquement la création d'une nouvelle transition rapide, à partir
 * d'un type de transition d'OBS et de la durée choisie en bas du menu. Cut et
 * Fondu utilisent les transitions de base d'OBS ; les autres types créent
 * une transition d'OBS (fenêtres de nom et de propriétés d'OBS). */
void StudioWindow::AddQuickTransition(QWidget *anchor)
{
	QMenu menu(this);
	QSpinBox *duration = DurationSpin(300, &menu);
	auto addBuiltIn = [duration](const char *id) {
		struct obs_frontend_source_list list = {};
		obs_frontend_get_transitions(&list);
		for (size_t i = 0; i < list.sources.num; i++) {
			obs_source_t *tr = list.sources.array[i];
			if (strcmp(obs_source_get_id(tr), id) != 0)
				continue;
			auto quick = Manager::Get().QuickTransitions();
			Manager::QuickTransition q;
			q.transition = obs_source_get_uuid(tr);
			q.name = obs_source_get_name(tr);
			q.durationMs = duration->value();
			quick.push_back(q);
			Manager::Get().SetQuickTransitions(quick);
			break;
		}
		obs_frontend_source_list_free(&list);
	};
	for (const char *builtIn : {"cut_transition", "fade_transition"}) {
		connect(menu.addAction(QString::fromUtf8(obs_source_get_display_name(builtIn))), &QAction::triggered,
			this, [addBuiltIn, builtIn]() { addBuiltIn(builtIn); });
	}
	size_t idx = 0;
	const char *id = nullptr;
	while (obs_enum_transition_types(idx++, &id)) {
		if (!obs_is_source_configurable(id))
			continue;
		const std::string type = id;
		const QString display = QString::fromUtf8(obs_source_get_display_name(id));
		connect(menu.addAction(display), &QAction::triggered, this, [this, type, display, duration]() {
			pendingQuickType = type;
			pendingQuickDuration = duration->value();
			pendingQuickBefore.clear();
			struct obs_frontend_source_list list = {};
			obs_frontend_get_transitions(&list);
			for (size_t i = 0; i < list.sources.num; i++)
				pendingQuickBefore.insert(obs_source_get_uuid(list.sources.array[i]));
			obs_frontend_source_list_free(&list);
			QTimer::singleShot(0, this, [this, display]() { CreateObsTransition(display); });
		});
	}
	menu.addSeparator();
	auto *durationAction = new QWidgetAction(&menu);
	durationAction->setDefaultWidget(duration);
	menu.addAction(durationAction);
	menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
}

/* Flèche d'une transition rapide : la modifier (propriétés, durée) ou la
 * supprimer ; pas de création ici. */
void StudioWindow::FillQuickTransitionMenu(QMenu *menu, int index)
{
	const auto &list = Manager::Get().QuickTransitions();
	if (index < 0 || size_t(index) >= list.size())
		return;
	OBSSource tr = Manager::ResolveQuickTransition(list[size_t(index)]);
	connect(menu->addAction(Tr("MDS.QuickTransitionRemove")), &QAction::triggered, this, [index]() {
		auto l = Manager::Get().QuickTransitions();
		if (size_t(index) < l.size())
			l.erase(l.begin() + index);
		Manager::Get().SetQuickTransitions(l);
	});
	if (tr && obs_source_configurable(tr)) {
		connect(menu->addAction(Tr("MDS.QuickTransitionProperties")), &QAction::triggered, this,
			[this, tr]() { OpenOnStudio([tr]() { obs_frontend_open_source_properties(tr); }); });
	}
	if (!tr || obs_transition_fixed(tr))
		return;
	menu->addSeparator();
	QSpinBox *duration = DurationSpin(list[size_t(index)].durationMs, menu);
	connect(duration, &QSpinBox::valueChanged, this, [this, index](int value) {
		auto l = Manager::Get().QuickTransitions();
		if (size_t(index) >= l.size())
			return;
		l[size_t(index)].durationMs = value;
		/* Texte du bouton mis à jour sans reconstruire (le menu est ouvert). */
		Manager::Get().SetQuickTransitionsQuietly(l);
		OBSSource t = Manager::ResolveQuickTransition(l[size_t(index)]);
		for (QPushButton *b : quickButtons) {
			if (t && b->property("quickIndex").toInt() == index)
				b->setText(QuickTransitionText(l[size_t(index)], t));
		}
	});
	auto *durationAction = new QWidgetAction(menu);
	durationAction->setDefaultWidget(duration);
	menu->addAction(durationAction);
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
