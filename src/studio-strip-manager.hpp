#pragma once

#include <obs-frontend-api.h>
#include <obs-hotkey.h>

#include <QAction>
#include <QLabel>
#include <QMargins>
#include <QObject>
#include <QPointer>
#include <QWidget>

/*
 * StudioStripManager
 *
 * Finds the transition strip OBS creates between the Preview and Program
 * views in Studio Mode (a plain QWidget without object name, holding the
 * Transition button, T-bar and quick transitions) and moves it into a
 * movable dock, so Preview and Program sit directly side by side.
 *
 * The strip widget is re-created by OBS every time Studio Mode is enabled
 * and deleted when it is disabled, so placement is re-applied on the
 * matching frontend events. The strip itself is never deleted by us.
 */
class StudioStripManager : public QObject {
	Q_OBJECT

public:
	explicit StudioStripManager(QObject *parent = nullptr);
	~StudioStripManager() override;

	void init();
	void shutdown();

public slots:
	void reapply();
	void doTransition();
	void togglePanelPlacement();

private:
	static void frontendEventCallback(enum obs_frontend_event event, void *data);
	static void hotkeyTransitionCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);
	static void hotkeyToggleCallback(void *data, obs_hotkey_id id, obs_hotkey_t *hotkey, bool pressed);

	void ensureToolsActions();
	void ensureDock();
	void applyStripPlacement();
	QWidget *findStrip() const;
	QWidget *findProgramWidget(QWidget *strip) const;
	class QHBoxLayout *mainPreviewLayout() const;
	void tightenMainSpacing();
	void restoreMainSpacing();
	void setPanelInMainWindow(bool inMain);
	bool panelInMainWindow() const;

	QPointer<QWidget> dockContainer = nullptr;
	QPointer<QLabel> placeholder = nullptr;
	QPointer<QAction> toolsTransitionAction = nullptr;
	QPointer<QAction> toolsPanelToggleAction = nullptr;
	obs_hotkey_id transitionHotkey = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id toggleHotkey = OBS_INVALID_HOTKEY_ID;
	int savedSpacing = -1;
	QMargins savedMargins;
	bool dockReady = false;
	bool callbacksRegistered = false;
};
