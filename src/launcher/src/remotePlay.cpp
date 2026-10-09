#include "remotePlay.h"

#include "configuration.h"
#include "emulatorArgs.h"
#include "gameContent.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace RemotePlay {

constexpr char SETTINGS_GROUP[]       = "RemotePlay";
constexpr char SETTINGS_SHARE_GAMES[] = "share_games";
constexpr char SETTINGS_HOST[]        = "host";
constexpr char SETTINGS_MOONLIGHT[]   = "moonlight";
constexpr char APPS_FILE_NAME[]       = "sunshine_apps.json";
constexpr char ICONS_DIR_NAME[]       = "sunshine_icons";
constexpr char SUNSHINE_WEB_UI[]      = "https://localhost:47990";
// Moonlight gives up on an unreachable host by itself; this only guards against a hung process.
constexpr int LIST_TIMEOUT_MS = 60000;

static QString Tr(const char* text, int n = -1) {
	return QCoreApplication::translate("RemotePlay", text, nullptr, n);
}

void Settings::Read(QSettings& s) {
	s.beginGroup(SETTINGS_GROUP);
	share_games = s.value(SETTINGS_SHARE_GAMES, false).toBool();
	host        = s.value(SETTINGS_HOST).toString().trimmed();
	moonlight   = s.value(SETTINGS_MOONLIGHT).toString().trimmed();
	s.endGroup();
}

void Settings::Write(QSettings& s) const {
	s.beginGroup(SETTINGS_GROUP);
	s.setValue(SETTINGS_SHARE_GAMES, share_games);
	s.setValue(SETTINGS_HOST, host);
	s.setValue(SETTINGS_MOONLIGHT, moonlight);
	s.endGroup();
}

static QDir DataDirectory() {
	return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation));
}

QString AppsFile() {
	return DataDirectory().filePath(QString::fromLatin1(APPS_FILE_NAME));
}

QString SunshineSetting() {
	return QStringLiteral("file_apps = ") + QDir::toNativeSeparators(AppsFile());
}

QString DefaultMoonlight() {
#if defined(__APPLE__)
	return QStringLiteral("/Applications/Moonlight.app/Contents/MacOS/Moonlight");
#elif defined(_WIN32)
	return QStringLiteral("C:/Program Files/Moonlight Game Streaming/Moonlight.exe");
#else
	return QStringLiteral("moonlight");
#endif
}

QString MoonlightProgram(const QString& configured) {
	auto program = QDir::fromNativeSeparators(configured.trimmed());
	if (program.isEmpty()) {
		program = DefaultMoonlight();
	}
#if defined(__APPLE__)
	if (program.endsWith(QStringLiteral(".app")) && QFileInfo(program).isDir()) {
		program += QStringLiteral("/Contents/MacOS/Moonlight");
	}
#endif
	return program;
}

// The Moonlight executable to start, or an empty string when it cannot be found.
static QString FindMoonlight(const QString& configured) {
	const auto program = MoonlightProgram(configured);
	if (!program.contains(QLatin1Char('/'))) {
		// A bare name such as "moonlight" is looked up in PATH.
		return QStandardPaths::findExecutable(program);
	}
	return QFileInfo(program).isFile() ? program : QString();
}

static QString MoonlightMissingMessage(const QString& configured) {
	return Tr("Moonlight was not found at:\n%1\n\nInstall Moonlight, or set its location in "
	          "Settings > Remote play.")
	    .arg(QDir::toNativeSeparators(MoonlightProgram(configured)));
}

QString QuoteWindowsArgument(const QString& arg) {
	static const QRegularExpression needs_quotes(QStringLiteral("[ \\t\\n\\v\"]"));
	if (!arg.isEmpty() && !arg.contains(needs_quotes)) {
		return arg;
	}

	// Backslashes are literal unless they precede a double quote; there, each one must be doubled
	// and the quote itself escaped.
	QString result      = QStringLiteral("\"");
	int     backslashes = 0;
	for (const QChar c: arg) {
		if (c == QLatin1Char('\\')) {
			backslashes++;
			continue;
		}
		if (c == QLatin1Char('"')) {
			result += QString(backslashes * 2 + 1, QLatin1Char('\\'));
		} else {
			result += QString(backslashes, QLatin1Char('\\'));
		}
		result += c;
		backslashes = 0;
	}
	result += QString(backslashes * 2, QLatin1Char('\\'));
	result += QLatin1Char('"');
	return result;
}

#if !defined(_WIN32)
// Sunshine passes the command to Boost.Process, which splits it at spaces outside double quotes
// and cannot pass a double quote reliably.
static bool QuotePosixArgument(const QString& arg, QString* quoted) {
	if (arg.contains(QLatin1Char('"'))) {
		return false;
	}
	*quoted = arg;
	if (arg.isEmpty() || arg.contains(QLatin1Char(' '))) {
		*quoted = QStringLiteral("\"%1\"").arg(arg);
	}
	return true;
}
#endif

static bool BuildCommand(const QString& program, const QStringList& args, QString* command) {
#if defined(_WIN32)
	// The program name is parsed without escapes, and Windows paths cannot contain quotes.
	*command = QLatin1Char('"') + QDir::toNativeSeparators(program) + QLatin1Char('"');
	for (const auto& arg: args) {
		*command += QLatin1Char(' ') + QuoteWindowsArgument(arg);
	}
	return true;
#else
	QString token;
	if (!QuotePosixArgument(program, &token)) {
		return false;
	}
	*command = token;
	for (const auto& arg: args) {
		if (!QuotePosixArgument(arg, &token)) {
			return false;
		}
		*command += QLatin1Char(' ') + token;
	}
	return true;
#endif
}

// Sunshine expands $(NAME) in app fields; "$$" is a literal dollar sign.
static QString EscapeSunshine(QString value) {
	return value.replace(QLatin1Char('$'), QStringLiteral("$$"));
}

static QString IconKey(const Configuration& game) {
	static const QRegularExpression serial(QStringLiteral("\\A[A-Za-z0-9_-]{1,32}\\z"));
	const auto                      title_id = game.title_id.trimmed();
	if (serial.match(title_id).hasMatch()) {
		return title_id.toUpper();
	}
	const auto hash =
	    QCryptographicHash::hash(game.game_path.toUtf8(), QCryptographicHash::Sha1).toHex();
	return QStringLiteral("game-") + QString::fromLatin1(hash.left(16));
}

// Copies the game's icon0.png next to the apps file; returns its path, or an empty string.
static QString ExportIcon(const Configuration& game, const QDir& icons_dir) {
	static const QByteArray png_signature("\x89PNG\r\n\x1a\n", 8);
	const auto data = GameContent::ReadFile(game.basedir, QStringLiteral("sce_sys/icon0.png"),
	                                        GameContent::MaxImageSize);
	// Sunshine only shows PNG images and checks the signature.
	if (!data.startsWith(png_signature)) {
		return {};
	}

	const auto path = icons_dir.filePath(IconKey(game) + QStringLiteral(".png"));
	if (QFile existing(path); existing.open(QIODevice::ReadOnly) && existing.readAll() == data) {
		return path;
	}
	QSaveFile file(path);
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
		return {};
	}
	return path;
}

static QString UniqueName(const Configuration& game, QSet<QString>* used) {
	auto base = game.name.trimmed();
	if (base.isEmpty()) {
		base = !game.title_id.trimmed().isEmpty() ? game.title_id.trimmed()
		                                          : QFileInfo(game.basedir).fileName();
	}

	QStringList candidates({base});
	if (!game.title_id.trimmed().isEmpty() && game.title_id.trimmed() != base) {
		candidates.append(QStringLiteral("%1 (%2)").arg(base, game.title_id.trimmed()));
	}
	for (const auto& candidate: candidates) {
		// Names that differ only in case are hard to tell apart in Moonlight.
		if (!used->contains(candidate.toCaseFolded())) {
			used->insert(candidate.toCaseFolded());
			return candidate;
		}
	}
	for (int index = 2;; index++) {
		const auto candidate = QStringLiteral("%1 (%2)").arg(candidates.last()).arg(index);
		if (!used->contains(candidate.toCaseFolded())) {
			used->insert(candidate.toCaseFolded());
			return candidate;
		}
	}
}

SyncResult WriteSunshineApps(const QString&                              emulator,
                             std::vector<std::unique_ptr<Configuration>> games,
                             const QString&                              file) {
	SyncResult result;
	if (!QFileInfo(emulator).isFile()) {
		result.message = Tr("Can't find the emulator:\n%1").arg(QDir::toNativeSeparators(emulator));
		return result;
	}

	const QDir data_dir = QFileInfo(file).absoluteDir();
	const QDir icons_dir(data_dir.filePath(QString::fromLatin1(ICONS_DIR_NAME)));
	if (!data_dir.mkpath(QStringLiteral(".")) || !icons_dir.mkpath(QStringLiteral("."))) {
		result.message =
		    Tr("Can't create the folder:\n%1").arg(QDir::toNativeSeparators(data_dir.path()));
		return result;
	}

	// Keep the file stable between syncs so Sunshine's list does not reorder.
	std::stable_sort(games.begin(), games.end(), [](const auto& a, const auto& b) {
		return QString::localeAwareCompare(a->name, b->name) < 0;
	});

	const auto    working_dir = QDir::toNativeSeparators(QFileInfo(emulator).absolutePath());
	QJsonArray    apps;
	QSet<QString> used_names;
	QStringList   skipped;
	for (const auto& game: games) {
		game->fullscreen_enabled = true;
		const auto args          = EmulatorArgs::Create(*game);
		QString    command;
		if (args.isEmpty() || !BuildCommand(emulator, args, &command)) {
			skipped.append(game->name);
			continue;
		}

		QJsonObject app;
		app.insert(QStringLiteral("name"), EscapeSunshine(UniqueName(*game, &used_names)));
		app.insert(QStringLiteral("cmd"), EscapeSunshine(command));
		app.insert(QStringLiteral("working-dir"), EscapeSunshine(working_dir));
		if (const auto icon = ExportIcon(*game, icons_dir); !icon.isEmpty()) {
			app.insert(QStringLiteral("image-path"),
			           EscapeSunshine(QDir::toNativeSeparators(icon)));
		}
		// The stream lasts while the emulator and its child processes run; when the stream is
		// stopped, Sunshine gives the emulator 5 seconds to exit.
		app.insert(QStringLiteral("auto-detach"), QStringLiteral("true"));
		app.insert(QStringLiteral("wait-all"), QStringLiteral("true"));
		app.insert(QStringLiteral("exit-timeout"), QStringLiteral("5"));
		apps.append(app);
	}

	const QJsonObject root {{QStringLiteral("env"), QJsonObject()}, {QStringLiteral("apps"), apps}};
	const auto        data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	QSaveFile         save(file);
	if (!save.open(QIODevice::WriteOnly) || save.write(data) != data.size() || !save.commit()) {
		result.message =
		    Tr("Can't write %1:\n%2").arg(QDir::toNativeSeparators(file), save.errorString());
		return result;
	}

	result.ok        = true;
	result.app_count = static_cast<int>(apps.size());
	result.message   = Tr("Shared %n game(s) with Sunshine.", result.app_count);
	if (!skipped.isEmpty()) {
		result.message += QLatin1Char(' ') + Tr("Skipped (settings can't be passed to the "
		                                        "emulator): %1.")
		                                         .arg(skipped.join(QStringLiteral(", ")));
	}
	return result;
}

QStringList ParseAppList(const QByteArray& output) {
	// Moonlight log lines start with a timestamp, for example "00:00:01 - SDL Info (0): ...".
	static const QRegularExpression log_line(QStringLiteral("^\\d{2}:\\d{2}:\\d{2}(\\.\\d+)? - "));
	static const QRegularExpression line_break(QStringLiteral("[\\r\\n]"));
	QStringList                     apps;
	for (const auto& line: QString::fromLocal8Bit(output).split(line_break, Qt::SkipEmptyParts)) {
		const auto name = line.trimmed();
		if (name.isEmpty() || log_line.match(name).hasMatch() ||
		    name == QLatin1String("Establishing connection to PC...") ||
		    name == QLatin1String("Loading app list...")) {
			continue;
		}
		apps.append(name);
	}
	return apps;
}

// Starts a Moonlight command whose lifetime ends with context: when context is destroyed, the
// process is stopped and none of its signals are delivered any more.
static QProcess* StartMoonlight(QObject* context, const QString& program, const QStringList& args) {
	// No parent: a child would be destroyed while context's widgets are half torn down.
	auto* process = new QProcess;
	process->setProgram(program);
	process->setArguments(args);
	QObject::connect(context, &QObject::destroyed, process, [process]() {
		process->disconnect();
		process->kill();
		process->deleteLater();
	});
	return process;
}

void ListApps(QObject* context, const QString& moonlight, const QString& host,
              const std::function<void(const AppList&)>& done) {
	const auto program = FindMoonlight(moonlight);
	if (program.isEmpty()) {
		done({ListError::MoonlightMissing, {}, MoonlightMissingMessage(moonlight)});
		return;
	}

	auto* process = StartMoonlight(context, program, {QStringLiteral("list"), host});
	auto* timeout = new QTimer(process);
	timeout->setSingleShot(true);
	QObject::connect(timeout, &QTimer::timeout, process, [process]() { process->kill(); });
	QObject::connect(
	    process, &QProcess::errorOccurred, process,
	    [process, moonlight, done](QProcess::ProcessError error) {
		    // finished() is not emitted when the process never started.
		    if (error == QProcess::FailedToStart) {
			    process->disconnect();
			    process->deleteLater();
			    done({ListError::MoonlightMissing, {}, MoonlightMissingMessage(moonlight)});
		    }
	    });
	QObject::connect(
	    process, &QProcess::finished, process,
	    [process, timeout, host, done](int exit_code, QProcess::ExitStatus status) {
		    const bool timed_out = !timeout->isActive();
		    timeout->stop();
		    process->disconnect();
		    process->deleteLater();

		    const auto output = process->readAllStandardOutput();
		    const auto errors = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
		    AppList    result;
		    if (status == QProcess::NormalExit && exit_code == 0) {
			    result.apps = ParseAppList(output);
			    if (result.apps.isEmpty()) {
				    result.message = Tr("%1 has no games to stream. On that PC, turn on \"Share "
				                        "games with Sunshine\" in Settings > Remote play.")
				                         .arg(host);
			    }
		    } else if (errors.contains(QLatin1String("has not been paired"))) {
			    result.error = ListError::NotPaired;
			    result.message =
			        Tr("This device is not paired with %1 yet. Click Pair... to pair it.")
			            .arg(host);
		    } else if (timed_out || errors.contains(QLatin1String("Failed to connect"))) {
			    result.error   = ListError::Offline;
			    result.message = Tr("Could not reach %1. Check that the PC is on, Sunshine is "
			                        "running and the address is right. Over the internet, both "
			                        "devices must be connected to Tailscale.")
			                         .arg(host);
		    } else {
			    result.error   = ListError::Failed;
			    result.message = Tr("Moonlight could not list the games on %1 (exit code %2).")
			                         .arg(host)
			                         .arg(exit_code);
			    if (!errors.isEmpty()) {
				    result.message += QLatin1Char('\n') + errors.right(500);
			    }
		    }
		    done(result);
	    });
	timeout->start(LIST_TIMEOUT_MS);
	process->start();
}

QString Stream(const QString& moonlight, const QString& host, const QString& app) {
	const auto program = FindMoonlight(moonlight);
	if (program.isEmpty()) {
		return MoonlightMissingMessage(moonlight);
	}
	if (!QProcess::startDetached(program, {QStringLiteral("stream"), host, app})) {
		return Tr("Could not start Moonlight:\n%1").arg(QDir::toNativeSeparators(program));
	}
	return {};
}

void Pair(QWidget* parent, const QString& moonlight, const QString& host,
          const std::function<void()>& paired) {
	if (host.trimmed().isEmpty()) {
		QMessageBox::information(
		    parent, Tr("Pair with home PC"),
		    Tr("Enter your home PC's address in Settings > Remote play first."));
		return;
	}
	const auto program = FindMoonlight(moonlight);
	if (program.isEmpty()) {
		QMessageBox::warning(parent, Tr("Pair with home PC"), MoonlightMissingMessage(moonlight));
		return;
	}

	const auto pin = QStringLiteral("%1").arg(QRandomGenerator::system()->bounded(10000), 4, 10,
	                                          QLatin1Char('0'));

	auto* dialog = new QDialog(parent, Qt::WindowCloseButtonHint);
	dialog->setAttribute(Qt::WA_DeleteOnClose);
	dialog->setWindowTitle(Tr("Pair with home PC"));
	dialog->setMinimumWidth(460);

	auto* layout = new QVBoxLayout(dialog);
	layout->setContentsMargins(20, 20, 20, 16);
	layout->setSpacing(12);

	auto* instructions = new QLabel(Tr("Open Sunshine on your PC (%1), go to PIN, and enter:")
	                                    .arg(QString::fromLatin1(SUNSHINE_WEB_UI)),
	                                dialog);
	instructions->setWordWrap(true);
	instructions->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(instructions);

	auto* pin_label = new QLabel(pin, dialog);
	pin_label->setObjectName(QStringLiteral("pair_pin"));
	pin_label->setAlignment(Qt::AlignCenter);
	pin_label->setTextInteractionFlags(Qt::TextSelectableByMouse);
	layout->addWidget(pin_label);

	auto* status = new QLabel(Tr("Moonlight is pairing with %1...").arg(host), dialog);
	status->setObjectName(QStringLiteral("remote_play_status"));
	status->setWordWrap(true);
	layout->addWidget(status);

	auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, dialog);
	layout->addWidget(buttons);
	QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);

	auto* process = StartMoonlight(dialog, program,
	                               {QStringLiteral("pair"), host, QStringLiteral("--pin"), pin});
	const auto finish = [buttons, status](const QString& text) {
		status->setText(text);
		buttons->setStandardButtons(QDialogButtonBox::Close);
	};
	QObject::connect(process, &QProcess::errorOccurred, process,
	                 [process, finish](QProcess::ProcessError error) {
		                 if (error == QProcess::FailedToStart) {
			                 process->disconnect();
			                 process->deleteLater();
			                 finish(
			                     Tr("Could not start Moonlight:\n%1").arg(process->errorString()));
		                 }
	                 });
	// "moonlight pair" exits with code 0 whether or not pairing worked, so ask the host instead.
	QObject::connect(
	    process, &QProcess::finished, process,
	    [process, dialog, status, finish, moonlight, host, paired]() {
		    process->disconnect();
		    process->deleteLater();
		    status->setText(Tr("Checking the pairing..."));
		    ListApps(dialog, moonlight, host, [finish, host, paired](const AppList& list) {
			    if (list.error == ListError::None) {
				    finish(Tr("Paired with %1.").arg(host));
				    if (paired) {
					    paired();
				    }
			    } else if (list.error == ListError::NotPaired) {
				    finish(Tr("Pairing was not completed. Click Pair... to try again."));
			    } else {
				    finish(list.message);
			    }
		    });
	    });
	process->start();
	dialog->show();
}

} // namespace RemotePlay
