#ifndef ONVIFPTZCONTROLLER_H
#define ONVIFPTZCONTROLLER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>
#include <QTimer>

class OnvifPtzController : public QObject
{
	Q_OBJECT
public:
	explicit OnvifPtzController(QNetworkAccessManager *networkManager, QObject *parent = nullptr);

	void setTarget(const QString &ip, int port, const QString &profileToken);

	void continuousMove(double panSpeed, double tiltSpeed);
	void stop();

private:
	void sendMovePayload(double panSpeed, double tiltSpeed);
	void sendStopCommand();
	void sendSoapRequest(const QByteArray &xmlPayload);

	QNetworkAccessManager *m_networkManager = nullptr;
	QString m_ip;
	int m_port = 8899;
	QString m_profileToken;

	QTimer *m_minStepTimer = nullptr;
	bool m_stopPending = false;
};

#endif // ONVIFPTZCONTROLLER_H
