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
	void restoreSavedPosition();

private:
	void sendMovePayload(double panSpeed, double tiltSpeed);
	void sendStopCommand();
	void sendSoapRequest(const QByteArray &xmlPayload, std::function<void(const QByteArray&, bool)> callback = nullptr);
	void saveCurrentPosition();
	void absoluteMove(double pan, double tilt);
	void gotoPreset(const QString &presetToken);

	QNetworkAccessManager *m_networkManager = nullptr;
	QString m_ip;
	int m_port = 8899;
	QString m_profileToken;

	QTimer *m_minStepTimer = nullptr;
	QTimer *m_saveDelayTimer = nullptr;
	bool m_stopPending = false;
};

#endif // ONVIFPTZCONTROLLER_H
