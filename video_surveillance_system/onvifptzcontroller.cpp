#include "onvifptzcontroller.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>

OnvifPtzController::OnvifPtzController(QNetworkAccessManager *networkManager, QObject *parent)
	: QObject(parent), m_networkManager(networkManager)
{
	m_minStepTimer = new QTimer(this);
	m_minStepTimer->setSingleShot(true);
	m_minStepTimer->setInterval(100); // Длительность минимального шага (100 мс)

	connect(m_minStepTimer, &QTimer::timeout, this, [this]() {
		if (m_stopPending) {
			m_stopPending = false;
			sendStopCommand();
		}
	});
}

void OnvifPtzController::setTarget(const QString &ip, int port, const QString &profileToken)
{
	m_ip = ip;
	m_port = port;
	m_profileToken = profileToken;
}

void OnvifPtzController::continuousMove(double panSpeed, double tiltSpeed)
{
	if (m_ip.isEmpty() || m_profileToken.isEmpty()) return;

	m_stopPending = false;
	m_minStepTimer->start();

	sendMovePayload(panSpeed, tiltSpeed);
}

void OnvifPtzController::stop()
{
	if (m_ip.isEmpty() || m_profileToken.isEmpty()) return;

	if (m_minStepTimer->isActive()) {
		m_stopPending = true;
		return;
	}

	sendStopCommand();
}

void OnvifPtzController::sendMovePayload(double panSpeed, double tiltSpeed)
{
	QString xml = QString(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" "
					   "xmlns:tptz=\"http://onvif.org\" "
					   "xmlns:tt=\"http://onvif.org\">"
			"<soap:Body>"
				"<tptz:ContinuousMove>"
					"<tptz:ProfileToken>%1</tptz:ProfileToken>"
					"<tptz:Velocity>"
						"<tt:PanTilt x=\"%2\" y=\"%3\"/>"
					"</tptz:Velocity>"
					"<tptz:Timeout>PT1S</tptz:Timeout>"
				"</tptz:ContinuousMove>"
			"</soap:Body>"
		"</soap:Envelope>"
	).arg(m_profileToken)
	 .arg(panSpeed, 0, 'f', 2)
	 .arg(tiltSpeed, 0, 'f', 2);

	sendSoapRequest(xml.toUtf8());
}

void OnvifPtzController::sendStopCommand()
{
	// 1. Остановка через нулевую скорость (работает на камерах XMEye / iCSee)
	sendMovePayload(0.0, 0.0);

	// 2. Классический ONVIF Stop (для совместимости с другими брендами)
	QString stopXml = QString(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" "
					   "xmlns:tptz=\"http://onvif.org\">"
			"<soap:Body>"
				"<tptz:Stop>"
					"<tptz:ProfileToken>%1</tptz:ProfileToken>"
					"<tptz:PanTilt>true</tptz:PanTilt>"
					"<tptz:Zoom>false</tptz:Zoom>"
				"</tptz:Stop>"
			"</soap:Body>"
		"</soap:Envelope>"
	).arg(m_profileToken);

	sendSoapRequest(stopXml.toUtf8());
}

void OnvifPtzController::sendSoapRequest(const QByteArray &xmlPayload)
{
	if (!m_networkManager) return;

	QUrl url(QString("http://%1:%2/onvif/device_service").arg(m_ip).arg(m_port));
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");

	QNetworkReply *reply = m_networkManager->post(request, xmlPayload);
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		if (reply->error() != QNetworkReply::NoError) {
			qDebug() << "[" << m_ip << "] Ошибка PTZ:" << reply->errorString();
		}
		reply->deleteLater();
	});
}
