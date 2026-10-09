#include "compatibilityDatabase.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>

#include <utility>

namespace {

constexpr char FILE_NAME[]    = "compatibility_db.json";
constexpr char URL[]          = "https://kytyps5.github.io/data/compatibility.json";
constexpr int  RETRY_COUNT    = 3;
constexpr int  RETRY_DELAY_MS = 750;
constexpr int  TIMEOUT_MS     = 15000;
// The database is a few hundred KB; refuse anything far larger instead of buffering it.
constexpr qint64 MAX_DOWNLOAD_SIZE = 16LL * 1024 * 1024;
constexpr char   TOO_LARGE[]       = "kyty_too_large";

struct LoadResult {
	QMap<QString, CompatibilityEntry> entries;
	QString                           error;
};

QString TitleKey(const QString& title_id) {
	return title_id.trimmed().toUpper();
}

Configuration::GameStatus StatusFromText(const QString& text) {
	const auto value = text.trimmed();
	if (value == QStringLiteral("InGame") || value == QStringLiteral("In game")) {
		return Configuration::GameStatus::InGame;
	}
	if (value == QStringLiteral("MainMenu") || value == QStringLiteral("Main menu")) {
		return Configuration::GameStatus::MainMenu;
	}
	if (value == QStringLiteral("Logo")) {
		return Configuration::GameStatus::Logo;
	}
	if (value == QStringLiteral("DoesntBoot") || value == QStringLiteral("Doesn't boot")) {
		return Configuration::GameStatus::DoesntBoot;
	}
	return Configuration::GameStatus::Unknown;
}

QString StatusToText(Configuration::GameStatus status) {
	switch (status) {
		case Configuration::GameStatus::InGame: return QStringLiteral("InGame");
		case Configuration::GameStatus::MainMenu: return QStringLiteral("MainMenu");
		case Configuration::GameStatus::Logo: return QStringLiteral("Logo");
		case Configuration::GameStatus::DoesntBoot: return QStringLiteral("DoesntBoot");
		case Configuration::GameStatus::Unknown: return QStringLiteral("Unknown");
	}
	return QStringLiteral("Unknown");
}

LoadResult Parse(const QByteArray& data) {
	LoadResult      ret;
	QJsonParseError parse_error;
	const auto      doc = QJsonDocument::fromJson(data, &parse_error);
	if (parse_error.error != QJsonParseError::NoError || !doc.isObject()) {
		ret.error = QStringLiteral("Invalid compatibility JSON: %1").arg(parse_error.errorString());
		return ret;
	}

	const auto root = doc.object();
	for (auto it = root.constBegin(); it != root.constEnd(); ++it) {
		const auto title_id = TitleKey(it.key());
		const auto object   = it.value().toObject();
		if (title_id.isEmpty() || object.isEmpty()) {
			continue;
		}
		ret.entries.insert(title_id,
		                   {StatusFromText(object.value(QStringLiteral("status")).toString()),
		                    object.value(QStringLiteral("comment")).toString()});
	}
	return ret;
}

QString FilePath() {
	// Use the same base directory as a portable Kyty.ini: next to the launcher, or the current
	// directory for existing setups that keep the database there.
	const auto file_name = QString::fromLatin1(FILE_NAME);
	for (const auto& dir: {QCoreApplication::applicationDirPath(), QDir::currentPath()}) {
		const auto path = QDir(dir).absoluteFilePath(file_name);
		if (QFileInfo::exists(path)) {
			return path;
		}
	}
	return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(file_name);
}

} // namespace

CompatibilityDatabase::CompatibilityDatabase(bool local, QObject* parent)
    : QObject(parent), m_local(local) {}

const CompatibilityEntry* CompatibilityDatabase::Find(const QString& title_id) const {
	const auto entry = m_entries.constFind(TitleKey(title_id));
	return entry != m_entries.constEnd() ? &entry.value() : nullptr;
}

void CompatibilityDatabase::Load() {
	if (m_local) {
		QFile file(FilePath());
		if (!file.exists()) {
			emit Updated();
			return;
		}
		if (!file.open(QIODevice::ReadOnly)) {
			qWarning() << "Could not open compatibility database:" << file.errorString();
			return;
		}
		auto result = Parse(file.readAll());
		if (!result.error.isEmpty()) {
			qWarning() << result.error;
			return;
		}
		m_entries = std::move(result.entries);
		emit Updated();
		return;
	}

	// Download asynchronously on this thread, so nothing outlives the launcher on shutdown.
	m_attempt = 0;
	StartDownload();
}

void CompatibilityDatabase::StartDownload() {
	if (m_network == nullptr) {
		m_network = new QNetworkAccessManager(this);
	}

	QNetworkRequest request(QUrl(QString::fromLatin1(URL)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
	                     QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Kyty-Launcher");
	request.setTransferTimeout(TIMEOUT_MS);

	auto*      reply              = m_network->get(request);
	const auto abort_if_too_large = [reply](qint64 size) {
		if (size > MAX_DOWNLOAD_SIZE) {
			reply->setProperty(TOO_LARGE, true);
			reply->abort();
		}
	};
	connect(reply, &QNetworkReply::metaDataChanged, reply, [reply, abort_if_too_large]() {
		abort_if_too_large(reply->header(QNetworkRequest::ContentLengthHeader).toLongLong());
	});
	connect(
	    reply, &QNetworkReply::downloadProgress, reply,
	    [abort_if_too_large](qint64 received, qint64 /*total*/) { abort_if_too_large(received); });
	connect(reply, &QNetworkReply::finished, this, [this, reply]() { DownloadFinished(reply); });
}

void CompatibilityDatabase::DownloadFinished(QNetworkReply* reply) {
	reply->deleteLater();

	const bool too_large = reply->property(TOO_LARGE).toBool();
	QString    error;
	if (too_large) {
		error = QStringLiteral("Compatibility database exceeds %1 bytes").arg(MAX_DOWNLOAD_SIZE);
	} else if (reply->error() != QNetworkReply::NoError) {
		error = reply->error() == QNetworkReply::OperationCanceledError
		            ? QStringLiteral("Compatibility download timed out")
		            : reply->errorString();
	} else {
		auto result = Parse(reply->readAll());
		if (result.error.isEmpty()) {
			m_entries = std::move(result.entries);
			emit Updated();
			return;
		}
		error = result.error;
	}

	if (!too_large && m_attempt < RETRY_COUNT) {
		m_attempt++;
		QTimer::singleShot(RETRY_DELAY_MS * m_attempt, this, &CompatibilityDatabase::StartDownload);
		return;
	}
	qWarning() << "Could not refresh compatibility database:" << error;
}

void CompatibilityDatabase::SetStatus(const QString& title_id, Configuration::GameStatus status) {
	const auto key = TitleKey(title_id);
	if (!m_local || key.isEmpty()) {
		return;
	}
	m_entries[key].status = status;
	Save();
}

void CompatibilityDatabase::SetComment(const QString& title_id, const QString& comment) {
	const auto key = TitleKey(title_id);
	if (!m_local || key.isEmpty()) {
		return;
	}
	m_entries[key].comment = comment;
	Save();
}

void CompatibilityDatabase::Save() const {
	QJsonObject root;
	for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
		root.insert(it.key(),
		            QJsonObject {{QStringLiteral("status"), StatusToText(it.value().status)},
		                         {QStringLiteral("comment"), it.value().comment}});
	}

	QSaveFile  file(FilePath());
	const auto data = QJsonDocument(root).toJson(QJsonDocument::Indented);
	if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
		qWarning() << "Could not save compatibility database:" << file.errorString();
	}
}
