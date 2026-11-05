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

#include "scene-editor.hpp"

#include <obs.hpp>

#include <QMainWindow>
#include <QPointer>

#include <string>
#include <vector>

class AudioMixer;
class CuesPanel;
class QDockWidget;
class MediaControls;
class Destination;
class ProjectorWindow;
class TileGrid;
class TileWidget;
class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QMenu;
class QPushButton;
class QSpinBox;
class QToolButton;

/* Multidestination Studio : alternative complète à la fenêtre d'OBS pour
 * travailler sur plusieurs destinations (Preview / Program par destination,
 * scènes, sources, mixeur, transitions). */
class StudioWindow : public QMainWindow {
	Q_OBJECT

public:
	explicit StudioWindow(QWidget *parent);
	~StudioWindow() override;

	void Toggle();
	void Open();
	/* Ouvre chaque destination en plein écran sur son écran de sortie. */
	void OpenAllOutputs();
	void Close();
	void Shutdown();

	/* Demande les destinations d'une scène créée dans OBS. */
	void AskCoverage(obs_source_t *scene);
	void RefreshTransitions();

	QByteArray SaveLayout() const;
	void RestoreLayout(const QByteArray &state);

protected:
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void dragEnterEvent(QDragEnterEvent *event) override;
	void dropEvent(QDropEvent *event) override;

private slots:
	void RebuildGrids() { UpdateGrids(false); }
	void UpdateGrids(bool force);
	void RefreshScenes();
	void RefreshSources();
	void RefreshTally();
	void OnReset();

private:
	void BuildCentral();
	void BuildDocks();
	void BuildMenus();
	void ConnectTile(TileWidget *tile);
	void SetActiveTile(TileWidget *tile);
	TileWidget *ActiveTile() const;
	obs_scene_t *ActiveScene() const;
	void SetModifyProgram(bool on);
	void ToggleFocus(Destination *dest);
	void UpdateMediaControls();
	/* Après une modification : si elle a été faite dans le Programme, la
	 * garder aussi dans la scène. */
	void AfterEdit();
	void SetHighlight(bool on);
	void UpdateShowing(const std::vector<OBSSource> &next);

	/* Scènes */
	void AddScene();
	void EditScene(obs_source_t *scene);
	void RemoveScene(obs_source_t *scene);
	OBSSource SelectedScene() const;

	/* Sources de la destination active */
	OBSSceneItem CurrentItem() const;
	void SelectItem(int64_t id);
	void FillAddSourceMenu(QMenu *menu);
	void CreateNewSource(const char *typeId);
	void AddExistingSource(obs_source_t *source, bool freshMedia = false);
	void AddFiles(const QStringList &paths, TileWidget *target);
	void ShowAddSourceMenu();
	void ShowItemMenu(const QPoint &globalPos);

	/* Édition (menus + raccourcis) */
	QAction *MakeEditAction(const QString &text, const QList<QKeySequence> &keys, void (StudioWindow::*fn)());
	void EditUndo();
	void EditCopy();
	void EditPaste();
	void EditRemove();
	void EditCopyTransform();
	void EditPasteTransform();
	void EditResetTransform();
	void EditFit();
	void EditStretch();
	void EditCenter();
	void EditMoveUp();
	void EditMoveDown();
	void EditMoveTop();
	void EditMoveBottom();
	void EditProperties();
	void EditFilters();

	/* Destinations */
	void FillDestinationsMenu();
	void NewDestination();
	void ManageDestinations();
	void ScheduleDestinationsMenu();
	void RunScheduledDestinationsMenu();
	void EditPasteDuplicate();
	void EditDestination(Destination *dest);
	void DeleteDestination(Destination *dest);
	void OpenProjector(Destination *dest, bool fullscreen);
	void ApplySceneOrderFromList();
	void ApplySourceOrderFromList();
	void MoveSceneRows(QTreeWidgetItem *target, int position);
	void MoveSourceRows(QTreeWidgetItem *target, int position);
	void RenameRow(QTreeWidget *tree, QTreeWidgetItem *row);
	void MoveScene(int delta);
	void RemoveSelectedScenes();
	void SaveSceneTree();
	void NewSceneFolder();
	void RenameSceneFolder(QTreeWidgetItem *folder);
	OBSSceneItem ItemFromRow(QTreeWidgetItem *row) const;
	void EditGroup();
	void EditUngroup();
	void EditRenameGroup();
	void BuildWindowControls();
	void UpdateWindowControls();
	void PlaceFullscreenHint();
	void ResetDocks();
	void ApplyDefaultDockSizes();
	void NewBlendedDestination();
	void EditBlending(Destination *dest);
	void RefreshBlendProjectors(Destination *dest);
	std::string DefaultDestinationName() const;
	void CloseProjectors(Destination *dest);
	bool HasProjector(Destination *dest) const;

	void UpdateControls();
	void ShowMainDisabledHint(QWidget *anchor);

	TileGrid *pvwGrid = nullptr;
	TileGrid *pgmGrid = nullptr;
	QWidget *monitors = nullptr;
	/* Plein écran : Réduire / Plein écran / Quitter, en haut à droite au
	 * survol de la souris. */
	QWidget *windowControls = nullptr;
	QPushButton *fullscreenButton = nullptr;
	QWidget *fullscreenHint = nullptr;
	QLabel *hintText = nullptr;
	bool defaultDocksPending = true;
	bool destinationsMenuPending = false;
	QAction *settingsMenuAction = nullptr;
	QTreeWidget *sceneList = nullptr;
	QTreeWidget *sourceList = nullptr;
	QToolButton *addSourceButton = nullptr;
	QPushButton *transitionButton = nullptr;
	QPushButton *cutButton = nullptr;
	QLabel *folderTakeNote = nullptr;
	bool scenePreviewPending = false;
	void UpdateTakeButtons();
	void ApplySceneSelection();
	/* Transitions rapides (barre entre Aperçu et Programme). */
	QWidget *quickPanel = nullptr;
	std::vector<QPushButton *> quickButtons;
	void RefreshQuickTransitions();
	void ShowQuickTransitionMenu(int index, const QPoint &globalPos);
	void AddQuickTransition(QWidget *anchor);
	/* Ajoute une transition d'OBS d'un type donné (fenêtre d'OBS). */
	void CreateObsTransition(const QString &displayName);
	/* Fonction Cues : dock affiché ou non. */
	CuesPanel *cuesPanel = nullptr;
	QDockWidget *cuesDock = nullptr;
	void ApplyCuesFeature();
	void EditCuesSetting();
	void EditCompanionSetting();
	AudioMixer *mixer = nullptr;
	MediaControls *mediaControls = nullptr;
	QPushButton *modifyPgmButton = nullptr;
	QComboBox *transitionCombo = nullptr;
	QSpinBox *durationSpin = nullptr;
	QPushButton *streamButton = nullptr;
	QPushButton *recordButton = nullptr;
	QMenu *destinationsMenu = nullptr;
	QMenu *viewMenu = nullptr;
	std::vector<QAction *> editActions;

	Destination *activeDest = nullptr;
	bool activeIsProgram = false;
	Destination *focusDest = nullptr;
	bool modifyProgram = false;
	bool enabledObsStudioMode = false;
	std::vector<OBSSource> showing;
	std::vector<std::pair<Destination *, QPointer<ProjectorWindow>>> projectors;
	bool refreshingSources = false;
	bool refreshingScenes = false;
	bool sceneOrderPending = false;
	bool sourceOrderPending = false;
	bool refreshingTransitions = false;
	bool layoutRestored = false;

	/* Presse-papiers de l'édition. */
	OBSWeakSource clipboardSource;
	SceneEditor::Transform clipboardTransform;
	bool hasClipboardTransform = false;
	SceneEditor::Transform copiedTransform;
	bool hasCopiedTransform = false;
};
