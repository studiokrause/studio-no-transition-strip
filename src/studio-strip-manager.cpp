/*
Studio No Transition Strip
Copyright (C) 2026 studiokrause

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "studio-strip-manager.hpp"

#include <plugin-support.h>

#include <obs-module.h>
#include <obs-frontend-api.h>

#include <QCoreApplication>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSlider>
#include <QVBoxLayout>

#include <util/config-file.h>

#define CONFIG_SECTION "StudioNoTransitionStrip"
#define CONFIG_PANEL_IN_MAIN "PanelInMainWindow"
#define CONFIG_DOCK_SHOWN_ONCE "DockShownOnce"
#define DOCK_ID "StudioNoTransitionStripPanel"

StudioStripManager::StudioStripManager(QObject *parent) : QObject(parent) {}

StudioStripManager::~StudioStripManager()
{
	shutdown();
}

void StudioStripManager::init()
{
	if (callbacksRegistered)
		return;

	obs_frontend_add_event_callback(frontendEventCallback, this);
	callbacksRegistered = true;

	transitionHotkey = obs_hotkey_register_frontend("StudioNoTransitionStrip.DoTransition",
							obs_module_text("HotkeyDoTransition"), hotkeyTransitionCallback,
							this);
	toggleHotkey = obs_hotkey_register_frontend("StudioNoTransitionStrip.TogglePanel",
						    obs_module_text("HotkeyTogglePanel"), hotkeyToggleCallback, this);

	obs_log(LOG_INFO, "initialized");
}

void StudioStripManager::shutdown()
{
	if (callbacksRegistered) {
		obs_frontend_remove_event_callback(frontendEventCallback, this);
		callbacksRegistered = false;
	}

	if (transitionHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(transitionHotkey);
		transitionHotkey = OBS_INVALID_HOTKEY_ID;
	}
	if (toggleHotkey != OBS_INVALID_HOTKEY_ID) {
		obs_hotkey_unregister(toggleHotkey);
		toggleHotkey = OBS_INVALID_HOTKEY_ID;
	}

	const bool uiAlive = QCoreApplication::instance() && !QCoreApplication::closingDown();

	if (uiAlive) {
		/* Best effort: put the strip back where OBS created it. */
		QWidget *strip = findStrip();
		if (strip && dockContainer && strip->parentWidget() == dockContainer) {
			QHBoxLayout *main = mainPreviewLayout();
			if (main) {
				QWidget *program = findProgramWidget(strip);
				main->insertWidget(program ? main->indexOf(program) : main->count(), strip);
				main->setAlignment(strip, Qt::AlignCenter);
				strip->show();
			}
		}
		restoreMainSpacing();
	}

	delete toolsTransitionAction.data();
	delete toolsPanelToggleAction.data();

	/* Remove our dock (OBS destroys the container widget with it).
	 * The strip itself is moved back above, so OBS never loses it. */
	if (uiAlive && dockReady) {
		obs_frontend_remove_dock(DOCK_ID);
		dockReady = false;
		dockContainer = nullptr;
		placeholder = nullptr;
	}
}

void StudioStripManager::frontendEventCallback(enum obs_frontend_event event, void *data)
{
	auto *self = static_cast<StudioStripManager *>(data);
	if (!self)
		return;

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		QMetaObject::invokeMethod(self, "reapply", Qt::QueuedConnection);
		break;
	default:
		break;
	}
}

void StudioStripManager::hotkeyTransitionCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	auto *self = static_cast<StudioStripManager *>(data);
	if (!self || !pressed)
		return;
	QMetaObject::invokeMethod(self, "doTransition", Qt::QueuedConnection);
}

void StudioStripManager::hotkeyToggleCallback(void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed)
{
	auto *self = static_cast<StudioStripManager *>(data);
	if (!self || !pressed)
		return;
	QMetaObject::invokeMethod(self, "togglePanelPlacement", Qt::QueuedConnection);
}

void StudioStripManager::reapply()
{
	ensureToolsActions();
	ensureDock();
	applyStripPlacement();
}

void StudioStripManager::doTransition()
{
	if (!obs_frontend_preview_program_mode_active()) {
		obs_log(LOG_INFO, "transition requested outside Studio Mode, ignored");
		return;
	}
	/* Same call the hidden Transition button makes. */
	obs_frontend_preview_program_trigger_transition();
}

void StudioStripManager::togglePanelPlacement()
{
	setPanelInMainWindow(!panelInMainWindow());
}

void StudioStripManager::ensureToolsActions()
{
	if (!toolsTransitionAction) {
		toolsTransitionAction = static_cast<QAction *>(
			obs_frontend_add_tools_menu_qaction(obs_module_text("ToolsDoTransition")));
		if (toolsTransitionAction)
			connect(toolsTransitionAction, &QAction::triggered, this,
				&StudioStripManager::doTransition);
	}

	if (!toolsPanelToggleAction) {
		toolsPanelToggleAction = static_cast<QAction *>(
			obs_frontend_add_tools_menu_qaction(obs_module_text("ToolsPanelInMain")));
		if (toolsPanelToggleAction) {
			toolsPanelToggleAction->setCheckable(true);
			toolsPanelToggleAction->setChecked(panelInMainWindow());
			connect(toolsPanelToggleAction, &QAction::toggled, this,
				[this](bool checked) { setPanelInMainWindow(checked); });
		}
	}
}

void StudioStripManager::ensureDock()
{
	if (dockReady)
		return;

	QWidget *container = new QWidget();
	QVBoxLayout *layout = new QVBoxLayout(container);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(4);

	QLabel *info = new QLabel(obs_module_text("StudioModeRequired"), container);
	info->setWordWrap(true);
	info->setAlignment(Qt::AlignCenter);
	layout->addWidget(info);

	container->setMinimumWidth(170);

	if (!obs_frontend_add_dock_by_id(DOCK_ID, obs_module_text("DockTitle"), container)) {
		obs_log(LOG_WARNING, "failed to create transition dock");
		delete container;
		return;
	}

	dockContainer = container;
	placeholder = info;
	dockReady = true;

	/* OBS registers new API docks hidden and floating. Show ours once so
	 * the user can see where the strip went; afterwards its state is
	 * remembered by OBS itself. */
	config_t *cfg = obs_frontend_get_profile_config();
	const bool shownOnce = cfg ? config_get_bool(cfg, CONFIG_SECTION, CONFIG_DOCK_SHOWN_ONCE) : true;
	if (!shownOnce) {
		if (QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window())) {
			if (QDockWidget *dock = mainWindow->findChild<QDockWidget *>(DOCK_ID))
				dock->setVisible(true);
		}
		if (cfg) {
			config_set_bool(cfg, CONFIG_SECTION, CONFIG_DOCK_SHOWN_ONCE, true);
			obs_frontend_save();
		}
	}
}

void StudioStripManager::applyStripPlacement()
{
	QWidget *strip = findStrip();
	if (strip)
		connect(strip, &QObject::destroyed, this, &StudioStripManager::reapply, Qt::UniqueConnection);

	if (!strip) {
		restoreMainSpacing();
	} else if (panelInMainWindow()) {
		QHBoxLayout *main = mainPreviewLayout();
		if (main && main->indexOf(strip) == -1) {
			QWidget *program = findProgramWidget(strip);
			main->insertWidget(program ? main->indexOf(program) : main->count(), strip);
			main->setAlignment(strip, Qt::AlignCenter);
		}
		restoreMainSpacing();
		strip->show();
	} else if (dockContainer) {
		QVBoxLayout *dockLayout = qobject_cast<QVBoxLayout *>(dockContainer->layout());
		if (dockLayout && dockLayout->indexOf(strip) == -1)
			dockLayout->addWidget(strip);
		tightenMainSpacing();
		strip->show();
	}

	const bool stripInDock = strip && dockContainer && strip->parentWidget() == dockContainer;
	if (placeholder)
		placeholder->setVisible(!stripInDock);

	if (toolsPanelToggleAction) {
		const QSignalBlocker blocker(toolsPanelToggleAction);
		toolsPanelToggleAction->setChecked(panelInMainWindow());
	}
}

QWidget *StudioStripManager::findStrip() const
{
	QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return nullptr;

	QWidget *canvas = mainWindow->findChild<QWidget *>("canvasEditor");
	if (!canvas)
		return nullptr;

	/* The transition strip is the only container under the canvas that
	 * owns a QSlider (the manual transition T-bar). */
	QSlider *tbar = canvas->findChild<QSlider *>();
	if (!tbar)
		return nullptr;

	QWidget *strip = tbar->parentWidget();
	while (strip && strip->parentWidget() != canvas)
		strip = strip->parentWidget();
	return strip;
}

QWidget *StudioStripManager::findProgramWidget(QWidget *strip) const
{
	QHBoxLayout *main = mainPreviewLayout();
	if (!main)
		return nullptr;

	for (int i = 0; i < main->count(); i++) {
		QWidget *w = main->itemAt(i)->widget();
		if (!w || w == strip || !w->objectName().isEmpty())
			continue;
		/* The Program view hosts the OBS display widget. */
		const QList<QWidget *> children = w->findChildren<QWidget *>();
		for (QWidget *child : children) {
			if (QLatin1String(child->metaObject()->className()) == QLatin1String("OBSQTDisplay"))
				return w;
		}
	}
	return nullptr;
}

QHBoxLayout *StudioStripManager::mainPreviewLayout() const
{
	QWidget *mainWindow = static_cast<QWidget *>(obs_frontend_get_main_window());
	if (!mainWindow)
		return nullptr;

	QWidget *canvas = mainWindow->findChild<QWidget *>("canvasEditor");
	if (!canvas)
		return nullptr;

	if (QHBoxLayout *named = canvas->findChild<QHBoxLayout *>("previewLayout"))
		return named;
	return qobject_cast<QHBoxLayout *>(canvas->layout());
}

void StudioStripManager::tightenMainSpacing()
{
	QHBoxLayout *main = mainPreviewLayout();
	if (!main)
		return;

	if (savedSpacing < 0) {
		savedSpacing = main->spacing();
		int left = 0, top = 0, right = 0, bottom = 0;
		main->getContentsMargins(&left, &top, &right, &bottom);
		savedMargins = QMargins(left, top, right, bottom);
	}
	main->setSpacing(0);
	main->setContentsMargins(0, 0, 0, 0);
}

void StudioStripManager::restoreMainSpacing()
{
	if (savedSpacing < 0)
		return;

	if (QHBoxLayout *main = mainPreviewLayout()) {
		main->setSpacing(savedSpacing);
		main->setContentsMargins(savedMargins);
	}
	savedSpacing = -1;
}

void StudioStripManager::setPanelInMainWindow(bool inMain)
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (cfg) {
		config_set_default_bool(cfg, CONFIG_SECTION, CONFIG_PANEL_IN_MAIN, false);
		if (config_get_bool(cfg, CONFIG_SECTION, CONFIG_PANEL_IN_MAIN) != inMain) {
			config_set_bool(cfg, CONFIG_SECTION, CONFIG_PANEL_IN_MAIN, inMain);
			obs_frontend_save();
		}
	}

	obs_log(LOG_INFO, "transition panel in main window: %s", inMain ? "yes" : "no");
	reapply();
}

bool StudioStripManager::panelInMainWindow() const
{
	config_t *cfg = obs_frontend_get_profile_config();
	if (!cfg)
		return false;
	config_set_default_bool(cfg, CONFIG_SECTION, CONFIG_PANEL_IN_MAIN, false);
	return config_get_bool(cfg, CONFIG_SECTION, CONFIG_PANEL_IN_MAIN);
}
