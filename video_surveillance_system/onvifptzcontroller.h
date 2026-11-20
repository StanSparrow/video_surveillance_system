#ifndef ONVIFPTZCONTROLLER_H
#define ONVIFPTZCONTROLLER_H

#include <QObject>
#include <QString>
#include <QNetworkAccessManager>

class OnvifPtzController : public QObject
{
	Q_OBJECT
public:
	explicit OnvifPtzController(QNetworkAccessManager *networkManager, QObject *parent = nullptr);

	void setTarget(const QString &ip, int port, const QString &profileToken);

	// pan (X): от -1.0 (влево) до 1.0 (вправо)
	// tilt (Y): от -1.0 (вниз) до 1.0 (вверх)
	void continuousMove(double panSpeed, double tiltSpeed);
	void stop();

private:
	void sendSoapRequest(const QByteArray &xmlPayload);

	QNetworkAccessManager *m_networkManager = nullptr;
	QString m_ip;
	int m_port = 8899;
	QString m_profileToken;
};

#endif // ONVIFPTZCONTROLLER_H
