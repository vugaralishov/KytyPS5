#ifndef COMPATIBILITY_DATABASE_H
#define COMPATIBILITY_DATABASE_H

#include "configuration.h"

#include <QMap>
#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

struct CompatibilityEntry {
	Configuration::GameStatus status = Configuration::GameStatus::Unknown;
	QString                   comment;
};

class CompatibilityDatabase: public QObject {
	Q_OBJECT

public:
	explicit CompatibilityDatabase(bool local, QObject* parent = nullptr);

	[[nodiscard]] bool                      IsLocal() const { return m_local; }
	[[nodiscard]] const CompatibilityEntry* Find(const QString& title_id) const;

	void Load();
	void SetStatus(const QString& title_id, Configuration::GameStatus status);
	void SetComment(const QString& title_id, const QString& comment);

signals:
	void Updated();

private:
	void Save() const;
	void StartDownload();
	void DownloadFinished(QNetworkReply* reply);

	QMap<QString, CompatibilityEntry> m_entries;
	QNetworkAccessManager*            m_network = nullptr;
	int                               m_attempt = 0;
	bool                              m_local   = false;
};

#endif // COMPATIBILITY_DATABASE_H
