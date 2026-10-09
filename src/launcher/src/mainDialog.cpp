#include "mainDialog.h"

#include "configuration.h"
#include "configurationItem.h"
#include "configurationListWidget.h"
#include "controllerLightbar.h"
#include "emulatorArgs.h"
#include "gameContent.h"
#include "updateChecker.h"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QLabel>
#include <QLayout>
#include <QMessageBox>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QRadioButton>
#include <QRegularExpression>
#include <QScreen>
#include <QSettings>
#include <QStringList>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVariant>
#include <QtCore>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "ui_main_dialog.h"

#if defined(_WIN32)
#include <windows.h> // IWYU pragma: keep
#endif
#ifdef __linux__
#include <cerrno>
#include <signal.h>
#include <sys/types.h>
#endif

// IWYU pragma: no_include <minwindef.h>
// IWYU pragma: no_include <processthreadsapi.h>
// IWYU pragma: no_include <winbase.h>

class QWidget;

#if defined(_WIN32)
constexpr char CMD_EXE[] = "cmd.exe";
#elif defined(__linux__)
constexpr char KYTY_BASH_FILE[]   = "kyty_run.sh";
constexpr char KYTY_STATUS_FILE[] = "status";
// Some terminals hand the command to a server and exit before the script starts.
constexpr qint64 SCRIPT_START_TIMEOUT_MS = 15000;
#endif
constexpr int PROBE_TIMEOUT_MS        = 5000;
constexpr int STATUS_POLL_INTERVAL_MS = 500;
#if defined(_WIN32)
constexpr DWORD CMD_X_CHARS = 175;
constexpr DWORD CMD_Y_CHARS = 1000;
#endif
constexpr char SETTINGS_MAIN_DIALOG[]        = "MainDialog";
constexpr char SETTINGS_MAIN_LAST_GEOMETRY[] = "geometry";
constexpr char SETTINGS_CHECK_UPDATES[]       = "check_updates_on_startup";

class MainDialogPrivate: public QObject {
	Q_OBJECT

public:
	explicit MainDialogPrivate(QObject* parent = nullptr): QObject(parent) {}
	~MainDialogPrivate() override;

	void Setup(MainDialog* main_dialog);

	/*slots:*/

	void Update();
	void FitToGameList();
	void FindInterpreter();
	void Run();
	// Returns false when the launcher must stay open because a game is still running.
	bool ConfirmClose();

	[[nodiscard]] const QString& GetInterpreter() const { return m_interpreter; }
	[[nodiscard]] bool           IsGameRunning() const {
		return m_process.state() != QProcess::NotRunning || m_running_outside_process;
	}
#ifdef __linux__
	// Directory for this launch's script and status file; replaced on every launch.
	QString               CreateRunDirectory();
	[[nodiscard]] QString RunStatusFile() const;
#endif

	static void WriteSettings(QSettings& s);
	static void ReadSettings(QSettings& s);

private:
	void ProbeFinished(const QString& output, const QString& error);
	void ProcessFinished();
	void GameFinished();
#ifdef __linux__
	[[nodiscard]] bool ScriptRunning() const;
#endif

	static QByteArray g_last_geometry;
	static bool       g_check_updates_on_startup;

	Ui::MainDialog* m_ui             = {nullptr};
	MainDialog*     m_main_dialog    = nullptr;
	UpdateChecker*  m_update_checker = nullptr;
	ControllerLightbar m_lightbar;
	QString         m_interpreter;

	QProcess m_process;
	// The game outlives m_process when a terminal hands the script to a server process.
	bool   m_running_outside_process = false;
	bool   m_quit_when_finished      = false;
	QTimer m_status_timer;
#ifdef __linux__
	std::unique_ptr<QTemporaryDir> m_run_dir;
	qint64                         m_status_deadline = 0;
#endif

	QPointer<ConfigurationItem> m_running_item;
};

QByteArray MainDialogPrivate::g_last_geometry;
bool       MainDialogPrivate::g_check_updates_on_startup = true;

MainDialog::MainDialog(QWidget* parent): QDialog(parent), m_p(new MainDialogPrivate(this)) {
	m_p->Setup(this);
}

MainDialogPrivate::~MainDialogPrivate() {
	m_process.disconnect(this);
	delete m_ui;
}

void MainDialogPrivate::Setup(MainDialog* main_dialog) {
	m_ui = new Ui::MainDialog;
	m_ui->setupUi(main_dialog);

	m_main_dialog = main_dialog;
	m_update_checker = new UpdateChecker(main_dialog);
	m_ui->check_updates_on_startup->setChecked(g_check_updates_on_startup);
	m_ui->check_updates_link->setVisible(UpdateChecker::IsSupported());
	m_ui->check_updates_on_startup->setVisible(UpdateChecker::IsSupported());

	main_dialog->setWindowFlags(Qt::Dialog /*| Qt::MSWindowsFixedSizeDialogHint*/);

	connect(main_dialog, &MainDialog::Start, this, &MainDialogPrivate::FindInterpreter,
	        Qt::QueuedConnection);
	connect(m_ui->widget, &ConfigurationListWidget::Select, this, &MainDialogPrivate::Update);
	connect(m_ui->widget, &ConfigurationListWidget::PreviewControllerColor, this,
	        [this](const QString& color) {
		        if (!IsGameRunning()) {
			        m_lightbar.SetColor(color);
		        }
	        });
	connect(m_ui->widget, &ConfigurationListWidget::Run, this, &MainDialogPrivate::Run);
	connect(m_ui->check_updates_link, &QLabel::linkActivated, this,
	        [this](const QString&) { m_update_checker->Check(true); });
	connect(m_update_checker, &UpdateChecker::CheckingChanged, m_ui->check_updates_link,
	        &QLabel::setDisabled);
	connect(m_ui->check_updates_on_startup, &QCheckBox::toggled, this, [this](bool checked) {
		g_check_updates_on_startup = checked;
		m_ui->widget->WriteSettings();
	});
	connect(main_dialog, &MainDialog::Resize, [this]() {
		g_last_geometry = m_main_dialog->saveGeometry();
		m_ui->widget->WriteSettings();
	});

	connect(&m_process, &QProcess::stateChanged, this, [this](QProcess::ProcessState state) {
		if (state == QProcess::NotRunning) {
			ProcessFinished();
		}
	});
	m_status_timer.setInterval(STATUS_POLL_INTERVAL_MS);
#ifdef __linux__
	connect(&m_status_timer, &QTimer::timeout, this, [this]() {
		if (!ScriptRunning()) {
			GameFinished();
		}
	});
#endif

	m_ui->label_settings_file->setText(tr("Settings file: ") + m_ui->widget->GetSettingsFile());

	// The settings path, emulator path and build string are only useful for debugging; they stay
	// up to date on the labels but are not shown.
	m_ui->label_settings_file->setVisible(false);
	m_ui->label_Interpreter->setVisible(false);
	m_ui->label_Version->setVisible(false);

	m_main_dialog->restoreGeometry(g_last_geometry);
	FitToGameList();

	Update();
}

void MainDialogPrivate::FitToGameList() {
	// Open wide enough to show the game list columns up to Path, but never larger than the screen.
	const auto margins = m_main_dialog->layout()->contentsMargins();
	int        width   = m_ui->widget->PreferredWidth() + margins.left() + margins.right();
	int        height  = (std::max)(m_main_dialog->height(), 560);
	auto       pos     = m_main_dialog->pos();
	if (const auto* screen = m_main_dialog->screen(); screen != nullptr) {
		// Leave room for the window frame and title bar.
		const auto available = screen->availableGeometry().adjusted(8, 8, -8, -48);
		width                = (std::min)(width, available.width());
		height               = (std::min)(height, available.height());
		if (g_last_geometry.isEmpty()) {
			// First start: center instead of keeping the default top-left position.
			pos = available.center() - QPoint(width / 2, height / 2);
		}
		pos.setX((std::max)(available.left(),
		                    (std::min)(pos.x(), available.left() + available.width() - width)));
		pos.setY((std::max)(available.top(),
		                    (std::min)(pos.y(), available.top() + available.height() - height)));
	}
	if (width > m_main_dialog->width() || height > m_main_dialog->height()) {
		m_main_dialog->resize((std::max)(width, m_main_dialog->width()), height);
		m_main_dialog->move(pos);
	}
}

void MainDialogPrivate::FindInterpreter() {
	m_interpreter = EmulatorArgs::FindEmulator();

	m_ui->widget->SetRuntimeDirectory(QFileInfo(m_interpreter).absolutePath());

	if (!QFile::exists(m_interpreter)) {
		ProbeFinished({}, tr("Can't find emulator"));
		return;
	}
	m_ui->label_Interpreter->setText(tr("Emulator: ") + m_interpreter);

	// Probe the version without blocking the UI; a hung emulator is killed after a short timeout.
	auto* probe = new QProcess(this);
	probe->setProgram(m_interpreter);
	auto* timeout = new QTimer(probe);
	timeout->setSingleShot(true);
	connect(timeout, &QTimer::timeout, probe, [probe]() { probe->kill(); });
	connect(probe, &QProcess::finished, this, [this, probe, timeout](int, QProcess::ExitStatus) {
		const bool timed_out = !timeout->isActive();
		timeout->stop();
		probe->deleteLater();
		ProbeFinished(QString::fromLocal8Bit(probe->readAllStandardOutput()),
		              timed_out ? tr("The emulator did not respond:\n%1").arg(m_interpreter)
		                        : QString());
	});
	connect(probe, &QProcess::errorOccurred, this, [this, probe](QProcess::ProcessError error) {
		// finished() is not emitted when the process never started.
		if (error == QProcess::FailedToStart) {
			probe->deleteLater();
			ProbeFinished({}, tr("Can't start emulator:\n%1").arg(probe->errorString()));
		}
	});
	timeout->start(PROBE_TIMEOUT_MS);
	probe->start();
}

void MainDialogPrivate::ProbeFinished(const QString& output, const QString& error) {
	const auto lines = output.split(QRegularExpression("[\r\n]"), Qt::SkipEmptyParts);
	if (!error.isEmpty() || lines.count() < 2) {
		QMessageBox::critical(m_main_dialog, tr("Error"),
		                      error.isEmpty() ? tr("Can't find emulator") : error);
		QApplication::quit();
		return;
	}
	m_ui->label_Version->setText(tr("Version: ") +
	                             (lines.at(0).startsWith("exe_name") ? lines.at(1) : lines.at(0)));

	// Prompt for a game folder when none are configured, but keep the launcher
	// open if the user dismisses the dialog (quitting here can segfault during
	// nested modal shutdown / background compatibility load).
	m_ui->widget->EnsureGameDirectory();

	m_ui->label_settings_file->setText(tr("Settings file: ") + m_ui->widget->GetSettingsFile());

	Update();
	if (m_ui->check_updates_on_startup->isChecked()) {
		m_update_checker->Check(false);
	}
}

#ifdef __linux__
static QString BashQuote(QString value) {
	value.replace('\'', "'\\''");
	return QStringLiteral("'") + value + QStringLiteral("'");
}

// The script reports its PID in status_file while the emulator runs and "done" afterwards, so the
// launcher can track the game even when the terminal process exits immediately.
static bool CreateBashScript(const QString& interpreter, const QStringList& args,
                             const QString& file_name, const QString& status_file) {
	QFile file(file_name);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream s(&file);

		s << "#!/bin/bash\n";
		s << "kyty_status=" << BashQuote(status_file) << "\n";
		s << "trap 'echo done > \"$kyty_status\"' EXIT\n";
		s << "echo $$ > \"$kyty_status\"\n";
		s << BashQuote(interpreter);
		for (const auto& arg: args) {
			s << " " << BashQuote(arg);
		}
		s << "\n";
		s << "echo done > \"$kyty_status\"\n";
		s << "echo Press any key...\n";
		s << "read -n1\n";

		file.close();

		return file.error() == QFile::NoError &&
		       file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
	}
	return false;
}

// Find a terminal and its command separator.
static bool FindTerminal(QString* program, QStringList* prefix) {
	struct TerminalSpec {
		const char* executable;
		const char* separator; // nullptr when the command follows immediately
	};

	static const TerminalSpec candidates[] = {
	    {"x-terminal-emulator", "-e"},
	    {"gnome-terminal", "--"},
	    {"konsole", "-e"},
	    {"xfce4-terminal", "-x"},
	    {"mate-terminal", "--"},
	    {"tilix", "-e"},
	    {"alacritty", "-e"},
	    {"kitty", nullptr},
	    {"foot", nullptr},
	    {"wezterm", "-e"},
	    {"urxvt", "-e"},
	    {"xterm", "-e"},
	};

	const auto try_candidate = [program, prefix](const QString& executable, const char* separator) {
		const auto resolved = QStandardPaths::findExecutable(executable);
		if (resolved.isEmpty()) {
			return false;
		}
		*program = resolved;
		prefix->clear();
		if (separator != nullptr) {
			*prefix << QString::fromLatin1(separator);
		}
		return true;
	};

	if (const auto from_env = qEnvironmentVariable("TERMINAL"); !from_env.isEmpty()) {
		// Reuse the known separator for an explicit terminal.
		const auto  env_name  = QFileInfo(from_env).fileName();
		const char* separator = "-e";
		for (const auto& candidate: candidates) {
			if (env_name == QLatin1String(candidate.executable)) {
				separator = candidate.separator;
				break;
			}
		}
		if (try_candidate(from_env, separator)) {
			return true;
		}
	}

	for (const auto& candidate: candidates) {
		if (try_candidate(QString::fromLatin1(candidate.executable), candidate.separator)) {
			return true;
		}
	}

	return false;
}
#endif

#if defined(_WIN32)
// Quote one token so it survives cmd.exe /K parsing and then the C runtime's argv splitting in
// kyty_emulator. cmd expands %VAR% (and !VAR! with delayed expansion) even inside quotes, so those
// characters are escaped with ^ outside the quotes. A double quote cannot be passed through a
// quoted cmd token at all, so such values are rejected.
static bool WinCmdQuote(const QString& value, QString* quoted) {
	if (value.contains(QLatin1Char('"'))) {
		return false;
	}

	QString result      = QStringLiteral("\"");
	int     backslashes = 0;
	for (const QChar c: value) {
		if (c == QLatin1Char('\\')) {
			backslashes++;
			result += c;
			continue;
		}
		if (c == QLatin1Char('%') || c == QLatin1Char('!')) {
			// Backslashes in front of a closing quote must be doubled for the argv parser.
			result += QString(backslashes, QLatin1Char('\\'));
			result += QStringLiteral("\"^");
			result += c;
			result += QLatin1Char('"');
		} else {
			result += c;
		}
		backslashes = 0;
	}
	result += QString(backslashes, QLatin1Char('\\'));
	result += QLatin1Char('"');

	*quoted = result;
	return true;
}

static bool BuildWinCmdKCommand(const QString& interpreter, const QStringList& args,
                                QString* command) {
	QString token;
	if (!WinCmdQuote(QDir::toNativeSeparators(interpreter), &token)) {
		return false;
	}
	*command = token;
	for (const auto& arg: args) {
		if (!WinCmdQuote(arg, &token)) {
			return false;
		}
		*command += QLatin1Char(' ');
		*command += token;
	}
	return true;
}
#endif

bool MainDialog::RunInterpreter(QProcess* process, const Configuration& info) {
	const auto& interpreter = m_p->GetInterpreter();

	QFileInfo f(interpreter);
	auto      dir = f.absoluteDir();

	auto args = EmulatorArgs::Create(info);
	if (args.isEmpty()) {
		QMessageBox::critical(this, tr("Error"), tr("Invalid emulator configuration"));
		return false;
	}

#ifdef __linux__
	// Write the script to a private per-user directory: the install directory may be read-only.
	const auto run_dir = m_p->CreateRunDirectory();
	if (run_dir.isEmpty()) {
		QMessageBox::critical(this, tr("Error"), tr("Can't create a temporary directory."));
		return false;
	}
	const auto bash_file_name = QDir(run_dir).filePath(KYTY_BASH_FILE);
	if (!CreateBashScript(interpreter, args, bash_file_name, m_p->RunStatusFile())) {
		QMessageBox::critical(this, tr("Error"), tr("Can't create file:\n") + bash_file_name);
		return false;
	}

	{
		QString     terminal;
		QStringList terminal_prefix;
		// Pass the script as a file argument (not bash -c) so paths with spaces work.
		if (FindTerminal(&terminal, &terminal_prefix)) {
			process->setProgram(terminal);
			process->setArguments(terminal_prefix + QStringList {"bash", bash_file_name});
		} else {
			// Run without a terminal as a fallback.
			process->setProgram(QStringLiteral("bash"));
			process->setArguments({bash_file_name});
		}
	}
#elif defined(_WIN32)
	{
		QString command;
		if (!BuildWinCmdKCommand(interpreter, args, &command)) {
			QMessageBox::critical(this, tr("Error"),
			                      tr("Emulator settings must not contain double quotes (\")."));
			return false;
		}
		// Use nativeArguments so Qt does not re-quote the /K command string.
		process->setProgram(CMD_EXE);
		process->setArguments({});
		process->setNativeArguments(QStringLiteral("/K \"") + command + QLatin1Char('"'));
	}
#else
	process->setProgram(interpreter);
	process->setArguments(args);
#endif
	process->setWorkingDirectory(dir.path());
#if defined(_WIN32)
	process->setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
		args->flags |= static_cast<uint32_t>(CREATE_NEW_CONSOLE);
		args->startupInfo->dwFlags &= ~static_cast<DWORD>(STARTF_USESTDHANDLES);
		args->startupInfo->dwFlags |= static_cast<DWORD>(STARTF_USECOUNTCHARS);
		args->startupInfo->dwXCountChars = CMD_X_CHARS;
		args->startupInfo->dwYCountChars = CMD_Y_CHARS;
		// args->startupInfo->dwFlags |= static_cast<DWORD>(STARTF_USEFILLATTRIBUTE);
		// args->startupInfo->dwFillAttribute =
		//     static_cast<DWORD>(BACKGROUND_BLUE) | static_cast<DWORD>(FOREGROUND_RED) |
		//     static_cast<DWORD>(FOREGROUND_INTENSITY);
	});
#endif
	process->start();
#if !defined(_WIN32)
	// Report immediate launch failures.
	if (!process->waitForStarted(5000)) {
		QMessageBox::critical(
		    this, tr("Error"),
		    tr("Failed to start:\n%1\n\n%2").arg(process->program(), process->errorString()));
		return false;
	}
#endif
	process->waitForFinished(100);
	return true;
}

void MainDialog::reject() {
	if (m_p->ConfirmClose()) {
		QDialog::reject();
	}
}

void MainDialog::WriteSettings(QSettings& s) {
	MainDialogPrivate::WriteSettings(s);
}

void MainDialog::ReadSettings(QSettings& s) {
	MainDialogPrivate::ReadSettings(s);
}

void MainDialog::resizeEvent(QResizeEvent* event) {
	emit Resize();
	QDialog::resizeEvent(event);
}

void MainDialogPrivate::WriteSettings(QSettings& s) {
	s.beginGroup(SETTINGS_MAIN_DIALOG);

	if (!g_last_geometry.isEmpty()) {
		s.setValue(SETTINGS_MAIN_LAST_GEOMETRY, g_last_geometry);
	}
	s.setValue(SETTINGS_CHECK_UPDATES, g_check_updates_on_startup);

	s.endGroup();
}

void MainDialogPrivate::ReadSettings(QSettings& s) {
	s.beginGroup(SETTINGS_MAIN_DIALOG);

	g_last_geometry = s.value(SETTINGS_MAIN_LAST_GEOMETRY, g_last_geometry).toByteArray();
	g_check_updates_on_startup = s.value(SETTINGS_CHECK_UPDATES, true).toBool();

	s.endGroup();
}

void MainDialogPrivate::Run() {
	m_running_item = m_ui->widget->GetSelectedItem();
	if (m_running_item == nullptr) {
		return;
	}

	m_running_item->SetRunning(true);
	m_lightbar.Stop();

	auto info = m_ui->widget->CreateConfiguration(*m_running_item);
	if (!m_main_dialog->RunInterpreter(&m_process, *info)) {
		// Nothing was started, so clear the running state set above.
		if (m_running_item != nullptr) {
			m_running_item->SetRunning(false);
		}
		m_running_item = nullptr;
	}

	Update();
}

void MainDialogPrivate::ProcessFinished() {
#ifdef __linux__
	if (ScriptRunning()) {
		// The terminal handed the script to another process; follow the script's status file.
		m_running_outside_process = true;
		m_status_timer.start();
		return;
	}
#endif
	GameFinished();
}

void MainDialogPrivate::GameFinished() {
	m_status_timer.stop();
	m_running_outside_process = false;
	if (m_running_item != nullptr) {
		m_running_item->SetRunning(false);
	}
	Update();
	if (m_quit_when_finished) {
		QApplication::quit();
	}
}

bool MainDialogPrivate::ConfirmClose() {
	if (!IsGameRunning()) {
		return true;
	}
	// Destroying m_process would kill the game (or its terminal), so keep the launcher process
	// alive, hidden, until the game ends. This works the same on every OS: on Windows the game's
	// console window stays open, on Linux the terminal stays open, and on macOS the emulator keeps
	// its output pipes.
	const auto answer = QMessageBox::question(
	    m_main_dialog, tr("Game running"),
	    tr("A game is still running.\n\nClose the launcher window and keep the game running? The "
	       "launcher exits in the background when the game ends."),
	    QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel);
	if (answer == QMessageBox::Yes && IsGameRunning()) {
		m_quit_when_finished = true;
		m_main_dialog->hide();
	} else if (answer == QMessageBox::Yes) {
		// The game ended while the question was open.
		return true;
	}
	return false;
}

#ifdef __linux__
QString MainDialogPrivate::CreateRunDirectory() {
	m_run_dir.reset();
	auto base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
	if (base.isEmpty()) {
		base = QDir::tempPath();
	}
	auto run_dir =
	    std::make_unique<QTemporaryDir>(QDir(base).filePath(QStringLiteral("kyty-XXXXXX")));
	if (!run_dir->isValid()) {
		return {};
	}
	m_run_dir         = std::move(run_dir);
	m_status_deadline = QDateTime::currentMSecsSinceEpoch() + SCRIPT_START_TIMEOUT_MS;
	return m_run_dir->path();
}

QString MainDialogPrivate::RunStatusFile() const {
	return m_run_dir != nullptr ? m_run_dir->filePath(KYTY_STATUS_FILE) : QString();
}

bool MainDialogPrivate::ScriptRunning() const {
	if (m_run_dir == nullptr) {
		return false;
	}
	QFile file(RunStatusFile());
	if (!file.open(QIODevice::ReadOnly)) {
		// The script has not started yet; give terminals that start it asynchronously some time.
		return QDateTime::currentMSecsSinceEpoch() < m_status_deadline;
	}
	const auto text = QString::fromLatin1(file.readAll()).trimmed();
	if (text.isEmpty()) {
		// Read between the script truncating and writing the file.
		return true;
	}
	bool       ok  = false;
	const auto pid = text.toLongLong(&ok);
	// "done" (not a number) means the emulator exited; a dead PID means the script was killed.
	return ok && pid > 0 && (::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM);
}
#endif

void MainDialogPrivate::Update() {
	const auto* item = m_ui->widget->GetSelectedItem();

	bool run_enabled = (!IsGameRunning() && item != nullptr);

	if (run_enabled) {
		const auto& info = item->GetInfo();
		auto        dir  = info.basedir;
		run_enabled      = !dir.isEmpty() && (QDir(dir).exists() || GameContent::IsArchive(dir));
	}

	m_ui->widget->SetRunEnabled(run_enabled);
	if (IsGameRunning()) {
		m_lightbar.Stop();
		return;
	}
	m_lightbar.SetColor(m_ui->widget->GetGlobalControllerColor());
}

#include "mainDialog.moc"
