#ifndef REMOTE_GAMES_DIALOG_H
#define REMOTE_GAMES_DIALOG_H

#include "remotePlay.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class QWidget;

// Lists the games a PC running Sunshine shares and streams them with Moonlight.
class RemoteGamesDialog: public QDialog {
	Q_OBJECT

public:
	explicit RemoteGamesDialog(const RemotePlay::Settings& settings, QWidget* parent = nullptr);
	~RemoteGamesDialog() override = default;

	// The user asked to set up remote play in the settings.
	[[nodiscard]] bool OpenSettingsRequested() const { return m_open_settings; }

private:
	void Refresh();
	void Play();
	void Pair();
	void UpdateButtons();
	void ShowStatus(const QString& text);

	RemotePlay::Settings m_settings;
	QLabel*              m_hint            = nullptr;
	QListWidget*         m_list            = nullptr;
	QLabel*              m_status          = nullptr;
	QPushButton*         m_pair_button     = nullptr;
	QPushButton*         m_settings_button = nullptr;
	QPushButton*         m_refresh_button  = nullptr;
	QPushButton*         m_play_button     = nullptr;
	// Results of an older list request are dropped.
	int  m_request       = 0;
	bool m_loading       = false;
	bool m_open_settings = false;
};

#endif // REMOTE_GAMES_DIALOG_H
