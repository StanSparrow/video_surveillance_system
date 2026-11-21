#ifndef CAMERA_H
#define CAMERA_H

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>
#include <QTcpServer>

#include "onvifptzcontroller.h"

class Camera : public QObject
{
	Q_OBJECT
public:
	explicit Camera(QObject *parent = nullptr);
	explicit Camera(const QString &ip, QObject *parent = nullptr);
	~Camera() = default;

	// Запуск процесса подключения к камере
	void connectToOnvif();
	void movePtz(double panSpeed, double tiltSpeed);
	void stopPtz();
	void fetchRecordings(const QString &dateStr);
	void setCredentials(const QString &user, const QString &password);

private slots:
	void onProfilesReceived();

signals:
	void connectionStatusChanged(QString ip, bool success, QString info);
	void recordingsReceived(QVariantList recordings);


private:
	QString m_ip;
	QString m_rtspUrl;
	QNetworkAccessManager *m_networkManager;
	OnvifPtzController *m_ptzController = nullptr;
	QString m_profileToken;
	QTcpServer *m_replayServer = nullptr;
	QString m_dvripUser = "admin";
	QString m_dvripPass = "";
	QString m_dvripAuthPass = "";
	QString m_dvripAuthEnc = "MD5";
};

#endif // CAMERA_H
