#ifndef ARCHIVECONTROLLER_H
#define ARCHIVECONTROLLER_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QTcpServer>

class ArchiveController : public QObject
{
	Q_OBJECT
public:
	explicit ArchiveController(const QString &ip, QObject *parent = nullptr);

	void setCredentials(const QString &user, const QString &password);
	void fetchRecordings(const QString &dateStr);

signals:
	void recordingsReceived(QVariantList recordings);

private:
	void ensureReplayServer();

	QString m_ip;
	QTcpServer *m_replayServer = nullptr;
	QString m_dvripUser = "admin";
	QString m_dvripPass = "";
	QString m_dvripAuthPass = "";
	QString m_dvripAuthEnc = "MD5";
};

#endif // ARCHIVECONTROLLER_H
