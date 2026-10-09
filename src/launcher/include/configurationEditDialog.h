#ifndef CONFIGURATION_EDIT_DIALOG_H
#define CONFIGURATION_EDIT_DIALOG_H

#include "remotePlay.h"

#include <QDialog>
#include <QString>
#include <QStringList>
class QByteArray;
class QCheckBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QMoveEvent;
class QResizeEvent;
class QStackedWidget;
class QToolButton;
class QSettings;
class QWidget;

class Configuration;
namespace Ui {
class ConfigurationEditDialog;
} // namespace Ui

class ConfigurationEditDialog: public QDialog {
	Q_OBJECT

public:
	explicit ConfigurationEditDialog(Configuration& info, QWidget* parent = nullptr);
	~ConfigurationEditDialog() override;

	static void WriteSettings(QSettings& s);
	static void ReadSettings(QSettings& s);

	void SetGlobalSettings(const QStringList& dirs, const RemotePlay::Settings& remote_play);
	[[nodiscard]] QStringList          GetGameDirectories() const;
	[[nodiscard]] RemotePlay::Settings GetRemotePlaySettings() const;
	// Result of a SyncRemotePlay request.
	void SetRemotePlayStatus(const QString& text);
	void ShowRemotePlay();

signals:
	void PreviewControllerColor(const QString& color);
	void ImportGameSettings();
	void ExportGameSettings();
	void SyncRemotePlay();

private:
	Ui::ConfigurationEditDialog* m_ui = nullptr;
	Configuration&               m_info;
	QGroupBox*                   m_game_dirs_group        = nullptr;
	QListWidget*                 m_game_dirs_list         = nullptr;
	QToolButton*                 m_remove_game_dir_button = nullptr;
	QListWidget*                 m_section_list           = nullptr;
	QStackedWidget*              m_section_stack          = nullptr;
	QGroupBox*                   m_remote_host_group      = nullptr;
	QGroupBox*                   m_remote_client_group    = nullptr;
	QCheckBox*                   m_share_games            = nullptr;
	QLabel*                      m_sync_status            = nullptr;
	QLineEdit*                   m_remote_host            = nullptr;
	QLineEdit*                   m_moonlight_path         = nullptr;
	int                          m_remote_play_section    = -1;
	bool                         m_global_settings        = false;

protected:
	void Init(const Configuration& info);
	void InitGameDirectories();
	void InitRemotePlay();
	void SetRemotePlayFields(const RemotePlay::Settings& settings);
	void BuildSections();
	void ShowSectionOf(const QWidget* widget);
	void AddGameDirectoryItem(const QString& dir);

	void moveEvent(QMoveEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

	static QByteArray g_last_geometry;
	static int        g_last_section;

	/*slots:*/

	void save();
	void clear();
	void add_game_directory();
	void remove_selected_game_directories();
	void update_game_directory_buttons();
};

#endif // CONFIGURATION_EDIT_DIALOG_H
