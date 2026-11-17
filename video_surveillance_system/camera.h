#ifndef CAMERA_H
#define CAMERA_H

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class Camera : public QObject
{
	Q_OBJECT
public:
	explicit Camera(QObject *parent = nullptr);
	explicit Camera(const QString &ip, QObject *parent = nullptr);
	~Camera() = default;

	// Запуск процесса подключения к камере
	void connectToOnvif();

private slots:
	void onProfilesReceived();

signals:
	void connectionStatusChanged(QString ip, bool success, QString info);


private:
	QString m_ip;
	QString m_rtspUrl;
	QNetworkAccessManager *m_networkManager;
};

#endif // CAMERA_H
