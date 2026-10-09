#ifndef REMOTE_PLAY_H
#define REMOTE_PLAY_H

#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <vector>

class Configuration;
class QByteArray;
class QObject;
class QSettings;
class QWidget;

// Remote play through Sunshine (on the PC that has the games) and Moonlight (on the device the
// user plays on).
namespace RemotePlay {

struct Settings {
	// Host: keep a Sunshine apps file with the detected games up to date.
	bool share_games = false;
	// Client: the PC running Sunshine, and the Moonlight executable (empty for the default).
	QString host;
	QString moonlight;

	void Read(QSettings& s);
	void Write(QSettings& s) const;
};

// Sunshine apps file the launcher writes, and the sunshine.conf line that makes Sunshine use it.
[[nodiscard]] QString AppsFile();
[[nodiscard]] QString SunshineSetting();
[[nodiscard]] QString DefaultMoonlight();
// The executable to run for a configured Moonlight path; a macOS app bundle resolves to the
// executable inside it.
[[nodiscard]] QString MoonlightProgram(const QString& configured);

// Quotes one argument for CommandLineToArgvW and the Microsoft C runtime.
[[nodiscard]] QString QuoteWindowsArgument(const QString& arg);

struct SyncResult {
	bool    ok        = false;
	int     app_count = 0;
	QString message;
};

// Writes a Sunshine apps file with one app per game. Every game starts full screen.
[[nodiscard]] SyncResult WriteSunshineApps(const QString&                              emulator,
                                           std::vector<std::unique_ptr<Configuration>> games,
                                           const QString& file = AppsFile());

enum class ListError { None, MoonlightMissing, NotPaired, Offline, Failed };

struct AppList {
	ListError   error = ListError::None;
	QStringList apps;
	QString     message;
};

// App names from the output of "moonlight list <host>", one per line.
[[nodiscard]] QStringList ParseAppList(const QByteArray& output);
// Runs "moonlight list <host>" in the background; done is called once, unless context is
// destroyed first.
void ListApps(QObject* context, const QString& moonlight, const QString& host,
              const std::function<void(const AppList&)>& done);
// Starts streaming an app; returns an error message, or an empty string on success.
[[nodiscard]] QString Stream(const QString& moonlight, const QString& host, const QString& app);
// Shows a pairing PIN, runs "moonlight pair" and checks the result; paired is called on success.
void Pair(QWidget* parent, const QString& moonlight, const QString& host,
          const std::function<void()>& paired = {});

} // namespace RemotePlay

#endif // REMOTE_PLAY_H
