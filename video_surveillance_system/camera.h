#ifndef CAMERA_H
#define CAMERA_H

#pragma once

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QVariantList>

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
};

#endif // CAMERA_H
