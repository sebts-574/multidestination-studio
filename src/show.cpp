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

#include "show.hpp"
#include "manager.hpp"
#include "text.hpp"

#include <obs.hpp>
#include <plugin-support.h>
#include <util/config-file.h>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QInputDialog>
#include <QMainWindow>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QTimer>

#include <string>

namespace {

/* Le show tel qu'il a été chargé ou enregistré. */
QString showName;
QString showPath;
QByteArray savedFile;
std::string savedFingerprint;
bool haveBaseline = false;

/* Fermeture d'OBS. */
bool exitAnswered = false;
bool revertOnExit = false;

QWidget *MainWindow()
{
	return static_cast<QWidget *>(obs_frontend_get_main_window());
}

QWidget *PromptParent(QWidget *parent)
{
	if (parent && parent->isVisible())
		return parent;
	QWidget *active = QApplication::activeWindow();
	return active ? active : MainWindow();
}

/* Enregistrement immédiat de la collection par OBS (obs_frontend_save()
 * ne fait que le programmer). */
void SaveNow()
{
	obs_frontend_save();
	if (QObject *main = MainWindow())
		QMetaObject::invokeMethod(main, "SaveProjectDeferred", Qt::DirectConnection);
}

QString CollectionNameInFile(const QString &path)
{
	OBSDataAutoRelease data = obs_data_create_from_json_file(path.toUtf8().constData());
	return data ? QString::fromUtf8(obs_data_get_string(data, "name")) : QString();
}

/* Fichier .json de la collection, dans le dossier des collections d'OBS. */
QString CollectionFile(const QString &name)
{
	QStringList dirs;
	if (config_t *app = obs_frontend_get_app_config()) {
		for (const char *root : {config_get_string(app, "Locations", "SceneCollections"),
					 config_get_default_string(app, "Locations", "SceneCollections")}) {
			if (root && *root) {
				QString dir = QDir::cleanPath(QString::fromUtf8(root) + "/obs-studio/basic/scenes");
				if (!dirs.contains(dir))
					dirs << dir;
			}
		}
	}

	QString file;
	if (config_t *user = obs_frontend_get_user_config()) {
		const char *f = config_get_string(user, "Basic", "SceneCollectionFile");
		file = QString::fromUtf8(f ? f : "");
	}

	for (const QString &dir : dirs) {
		if (file.isEmpty())
			break;
		for (const QString &candidate : {dir + "/" + file, dir + "/" + file + ".json"}) {
			if (QFileInfo(candidate).isFile() && CollectionNameInFile(candidate) == name)
				return candidate;
		}
	}
	for (const QString &dir : dirs) {
		for (const QFileInfo &info : QDir(dir).entryInfoList({"*.json"}, QDir::Files)) {
			if (CollectionNameInFile(info.absoluteFilePath()) == name)
				return info.absoluteFilePath();
		}
	}
	return QString();
}

QByteArray ReadFile(const QString &path)
{
	QFile f(path);
	if (path.isEmpty() || !f.open(QIODevice::ReadOnly))
		return QByteArray();
	return f.readAll();
}

void WriteFile(const QString &path, const QByteArray &data)
{
	if (path.isEmpty() || data.isEmpty())
		return;
	QSaveFile f(path);
	if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit()) {
		obs_log(LOG_WARNING, "show: could not restore %s", path.toUtf8().constData());
		return;
	}
	obs_log(LOG_INFO, "show: unsaved changes discarded (%s)", path.toUtf8().constData());
}

/* Ce qui fait le contenu d'un show : les sources et scènes d'OBS, l'ordre
 * des scènes et les réglages du plugin. L'état d'antenne (scène en
 * Preview, scènes à l'antenne) n'en fait pas partie. */
std::string Fingerprint()
{
	OBSDataAutoRelease all = obs_data_create();

	OBSDataArrayAutoRelease sources = obs_save_sources();
	obs_data_set_array(all, "sources", sources);

	OBSDataArrayAutoRelease order = obs_data_array_create();
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		OBSDataAutoRelease item = obs_data_create();
		obs_data_set_string(item, "name", obs_source_get_name(scenes.sources.array[i]));
		obs_data_array_push_back(order, item);
	}
	obs_frontend_source_list_free(&scenes);
	obs_data_set_array(all, "scene_order", order);

	OBSDataAutoRelease mds = obs_data_create();
	Manager::Get().Save(mds);
	obs_data_erase(mds, "preview_scene");
	OBSDataArrayAutoRelease list = obs_data_get_array(mds, "list");
	for (size_t i = 0, n = obs_data_array_count(list); i < n; i++) {
		OBSDataAutoRelease item = obs_data_array_item(list, i);
		obs_data_erase(item, "program_link");
	}
	obs_data_set_obj(all, "mds", mds);

	const char *json = obs_data_get_json(all);
	return json ? json : "";
}

/* Le show chargé devient la référence : enregistré tel quel. */
void TakeBaseline()
{
	showName = Show::CurrentName();
	SaveNow();
	showPath = CollectionFile(showName);
	savedFile = ReadFile(showPath);
	savedFingerprint = Fingerprint();
	haveBaseline = true;
	if (showPath.isEmpty())
		obs_log(LOG_WARNING, "show: file of scene collection '%s' not found", showName.toUtf8().constData());
}

void ScheduleBaseline()
{
	haveBaseline = false;
	/* Laisse le plugin et OBS finir d'appliquer la collection. */
	QTimer::singleShot(1500, qApp, []() {
		if (!haveBaseline)
			TakeBaseline();
	});
}

/* Fermeture de la fenêtre d'OBS : demande avant qu'OBS n'enregistre. */
class ExitFilter : public QObject {
public:
	using QObject::QObject;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override
	{
		if (event->type() != QEvent::Close || watched != MainWindow() || exitAnswered)
			return false;

		Show::Answer answer = Show::AskToSave(nullptr);
		if (answer == Show::Answer::Cancelled) {
			event->ignore();
			return true;
		}
		exitAnswered = true;
		revertOnExit = answer == Show::Answer::Discarded;
		/* Si OBS ne se ferme finalement pas (fermeture refusée ensuite),
		 * la question sera reposée à la prochaine fermeture. */
		QTimer::singleShot(5000, qApp, []() {
			exitAnswered = false;
			revertOnExit = false;
		});
		return false;
	}
};

QPointer<ExitFilter> exitFilter;

QAction *ObsAction(const char *objectName)
{
	QWidget *main = MainWindow();
	return main ? main->findChild<QAction *>(objectName) : nullptr;
}

} // namespace

namespace Show {

void OnFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
		if (!exitFilter && MainWindow()) {
			exitFilter = new ExitFilter(MainWindow());
			MainWindow()->installEventFilter(exitFilter);
		}
		ScheduleBaseline();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		ScheduleBaseline();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_RENAMED:
		showName = CurrentName();
		showPath = CollectionFile(showName);
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		/* OBS a déjà enregistré l'état courant et n'enregistre plus. */
		if (revertOnExit)
			WriteFile(showPath, savedFile);
		revertOnExit = false;
		break;
	default:
		break;
	}
}

QString CurrentName()
{
	char *name = obs_frontend_get_current_scene_collection();
	QString result = QString::fromUtf8(name ? name : "");
	bfree(name);
	return result;
}

QStringList Names()
{
	QStringList names;
	char **list = obs_frontend_get_scene_collections();
	for (char **p = list; p && *p; p++)
		names << QString::fromUtf8(*p);
	bfree(list);
	names.sort(Qt::CaseInsensitive);
	return names;
}

bool IsDirty()
{
	if (!haveBaseline || showName != CurrentName())
		return false;
	return Fingerprint() != savedFingerprint;
}

void Save()
{
	TakeBaseline();
}

Answer AskToSave(QWidget *parent)
{
	if (!IsDirty())
		return Answer::Clean;

	QMessageBox box(PromptParent(parent));
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(Tr("MDS.ShowSaveTitle"));
	box.setText(Tr("MDS.ShowSaveQuestion").arg(showName));
	QPushButton *save = box.addButton(Tr("MDS.ShowSaveButton"), QMessageBox::AcceptRole);
	QPushButton *discard = box.addButton(Tr("MDS.ShowDontSave"), QMessageBox::DestructiveRole);
	QPushButton *cancel = box.addButton(Tr("MDS.Cancel"), QMessageBox::RejectRole);
	box.setDefaultButton(save);
	box.setEscapeButton(cancel);
	box.exec();

	if (box.clickedButton() == save) {
		Save();
		return Answer::Saved;
	}
	if (box.clickedButton() == discard)
		return Answer::Discarded;
	return Answer::Cancelled;
}

/* Changement de show : OBS enregistre l'ancien avant de passer au nouveau ;
 * si l'utilisateur a renoncé à ses modifications, l'ancien fichier est
 * remis comme il était. */
template<typename Fn> static bool SwitchShow(QWidget *parent, Fn &&doSwitch)
{
	Answer answer = AskToSave(parent);
	if (answer == Answer::Cancelled)
		return false;

	const QString oldName = showName;
	const QString oldPath = showPath;
	const QByteArray oldFile = savedFile;

	doSwitch();

	if (answer == Answer::Discarded && CurrentName() != oldName)
		WriteFile(oldPath, oldFile);
	return true;
}

void New(QWidget *parent)
{
	QWidget *p = PromptParent(parent);
	QString name;
	for (;;) {
		bool ok = false;
		name = QInputDialog::getText(p, Tr("MDS.ShowNewTitle"), Tr("MDS.ShowName"), QLineEdit::Normal, name,
					     &ok)
			       .trimmed();
		if (!ok || name.isEmpty())
			return;
		if (!Names().contains(name, Qt::CaseInsensitive))
			break;
		QMessageBox::warning(p, Tr("MDS.ShowNewTitle"), Tr("MDS.ShowNameExists").arg(name));
	}

	SwitchShow(parent, [&]() {
		if (!obs_frontend_add_scene_collection(name.toUtf8().constData()))
			QMessageBox::warning(p, Tr("MDS.ShowNewTitle"), Tr("MDS.ShowCreateFailed").arg(name));
	});
}

void SaveAs(QWidget *)
{
	QAction *duplicate = ObsAction("actionDupSceneCollection");
	if (!duplicate)
		return;

	const QString oldName = showName;
	const QString oldPath = showPath;
	const QByteArray oldFile = savedFile;

	/* Boîte de nom d'OBS ; la copie reçoit l'état actuel et devient le
	 * show chargé. Le show d'origine reste comme il était enregistré. */
	duplicate->trigger();

	if (CurrentName() != oldName)
		WriteFile(oldPath, oldFile);
}

void Load(QWidget *parent, const QString &name)
{
	if (name.isEmpty() || name == CurrentName())
		return;
	SwitchShow(parent, [&]() { obs_frontend_set_current_scene_collection(name.toUtf8().constData()); });
}

void Import(QWidget *parent)
{
	QAction *import = ObsAction("actionImportSceneCollection");
	if (!import)
		return;

	const QStringList before = Names();
	import->trigger();

	QStringList added;
	for (const QString &name : Names()) {
		if (!before.contains(name))
			added << name;
	}
	if (added.size() != 1)
		return;

	QMessageBox box(PromptParent(parent));
	box.setIcon(QMessageBox::Question);
	box.setWindowTitle(Tr("MDS.ShowImportTitle"));
	box.setText(Tr("MDS.ShowLoadImported").arg(added.first()));
	QPushButton *load = box.addButton(Tr("MDS.ShowLoadNow"), QMessageBox::AcceptRole);
	QPushButton *later = box.addButton(Tr("MDS.ShowNotNow"), QMessageBox::RejectRole);
	box.setDefaultButton(load);
	box.setEscapeButton(later);
	box.exec();
	if (box.clickedButton() == load)
		Load(parent, added.first());
}

void Export(QWidget *)
{
	if (QAction *exportAction = ObsAction("actionExportSceneCollection"))
		exportAction->trigger();
}

} // namespace Show
