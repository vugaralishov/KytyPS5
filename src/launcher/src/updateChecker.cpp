#include "updateChecker.h"

#include "kytyGitVersion.h"

#include <QByteArray>
#include <QDate>
#include <QDebug>
#include <QDesktopServices>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QWidget>

namespace {

constexpr char DEFAULT_FEED_URL[]  = "https://kytyps5.github.io/data/updates.json";
constexpr char FALLBACK_FEED_URL[] =
    "https://api.github.com/repos/KytyPS5/KytyPS5/releases/latest";
// Both feeds are a few KB; never buffer more than this.
constexpr qint64 MAX_FEED_SIZE = 1024 * 1024;
constexpr char   TOO_LARGE[]   = "kyty_too_large";

// Only open release pages on the project's own hosts.
bool IsTrustedPageUrl(const QUrl& url) {
	const auto host = url.host().toLower();
	return url.isValid() && url.scheme() == QStringLiteral("https") && url.userInfo().isEmpty() &&
	       (url.port() == -1 || url.port() == 443) &&
	       (host == QStringLiteral("github.com") || host == QStringLiteral("kytyps5.github.io"));
}

// Release tags are KytyPS5-YYYY-MM-DD-<sha7>; the build date orders them.
QDate TagDate(const QString& tag) {
	static const QRegularExpression pattern(
	    QStringLiteral("\\AKytyPS5-([0-9]{4}-[0-9]{2}-[0-9]{2})-[0-9a-f]{7,40}\\z"));
	const auto match = pattern.match(tag);
	return match.hasMatch() ? QDate::fromString(match.captured(1), Qt::ISODate) : QDate();
}

bool IsNewerThanCurrent(const QString& tag) {
	const auto latest  = TagDate(tag);
	const auto current = TagDate(QString::fromLatin1(KYTY_RELEASE_TAG));
	return latest.isValid() && current.isValid() && latest > current;
}

} // namespace

struct UpdateChecker::UpdateInfo {
	QString tag;
	QUrl    page_url;
	QString error;
};

UpdateChecker::UpdateChecker(QWidget* parent): QObject(parent), m_parent(parent), m_network(this) {}

bool UpdateChecker::IsSupported() {
#if defined(KYTY_OFFICIAL_BUILD) && defined(NDEBUG)
	return !QString::fromLatin1(KYTY_GIT_HASH).endsWith(QStringLiteral("-dirty"));
#else
	return false;
#endif
}

UpdateChecker::UpdateInfo UpdateChecker::ParseUpdateInfo(const QByteArray& data) {
	const auto document = QJsonDocument::fromJson(data);
	if (!document.isObject()) {
		return {{}, {}, tr("Invalid update feed")};
	}

	const auto root = document.object();
	UpdateInfo info {root.value(QStringLiteral("tag")).toString(),
	                 QUrl(root.value(QStringLiteral("html_url")).toString()), {}};
	if (info.tag.isEmpty()) {
		info.tag = root.value(QStringLiteral("tag_name")).toString();
	}
	if (info.tag.isEmpty() || !info.page_url.isValid() ||
	    info.page_url.scheme() != QStringLiteral("https")) {
		info.error = tr("Incomplete update feed");
	} else if (!TagDate(info.tag).isValid()) {
		info.error = tr("Unrecognized release tag: %1").arg(info.tag);
	} else if (!IsTrustedPageUrl(info.page_url)) {
		info.error = tr("Untrusted release page: %1").arg(info.page_url.toDisplayString());
	}
	return info;
}

void UpdateChecker::Check(bool manual) {
	if (!IsSupported() || m_checking_updates) {
		return;
	}
	m_checking_updates = true;
	emit CheckingChanged(true);
	FetchUpdateInfo(DEFAULT_FEED_URL, false, manual);
}

void UpdateChecker::FetchUpdateInfo(const char* url, bool fallback, bool manual) {
	QNetworkRequest request(QUrl(QString::fromLatin1(url)));
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
	                     QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setRawHeader("User-Agent", "Kyty-Launcher");
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setTransferTimeout(15000);

	auto* reply = m_network.get(request);
	connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
		if (received > MAX_FEED_SIZE) {
			reply->setProperty(TOO_LARGE, true);
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, fallback, manual]() {
		UpdateInfo info;
		if (reply->property(TOO_LARGE).toBool()) {
			info.error = tr("Update feed is too large");
		} else if (reply->error() == QNetworkReply::NoError) {
			info = ParseUpdateInfo(reply->readAll());
		} else {
			info.error = reply->errorString();
		}
		reply->deleteLater();

		if (!fallback) {
			const bool up_to_date = info.error.isEmpty() && !IsNewerThanCurrent(info.tag);
			if (!up_to_date) {
				if (info.error.isEmpty()) {
					// The primary feed is reachable but disagrees with the build tag
					// (e.g. a stale mirror); remember its answer in case the fallback
					// request also fails.
					m_primary_tag      = info.tag;
					m_primary_page_url = info.page_url;
				}
				FetchUpdateInfo(FALLBACK_FEED_URL, true, manual);
				return;
			}
		}

		if (fallback && !info.error.isEmpty() && !m_primary_tag.isEmpty()) {
			// The fallback feed failed, but the primary feed already returned a
			// usable answer; prefer it over reporting a network error.
			info = {m_primary_tag, m_primary_page_url, {}};
		}
		m_primary_tag.clear();
		m_primary_page_url.clear();

		m_checking_updates = false;
		emit CheckingChanged(false);
		ShowUpdateResult(info, manual);
	});
}

void UpdateChecker::ShowUpdateResult(const UpdateInfo& info, bool manual) {
	if (!info.error.isEmpty()) {
		qWarning() << "Could not check for updates:" << info.error;
		if (manual) {
			QMessageBox::warning(m_parent, tr("Update Check"),
			                     tr("Could not check for updates:\n%1").arg(info.error));
		}
		return;
	}

	// Only offer releases built after this one; a stale feed must not offer a downgrade.
	if (!IsNewerThanCurrent(info.tag)) {
		if (manual) {
			QMessageBox::information(m_parent, tr("Update Check"),
			                         tr("You are using the latest version (%1).")
			                             .arg(QString::fromLatin1(KYTY_RELEASE_TAG)));
		}
		return;
	}
	const auto message =
	    tr("An update is available.\n\nCurrent: %1\nLatest: %2\n\n"
	       "Open the release page?")
	        .arg(QString::fromLatin1(KYTY_RELEASE_TAG), info.tag);
	if (QMessageBox::question(m_parent, tr("KytyPS5 Update"), message,
	                          QMessageBox::Open | QMessageBox::Cancel,
	                          QMessageBox::Open) == QMessageBox::Open) {
		QDesktopServices::openUrl(info.page_url);
	}
}
