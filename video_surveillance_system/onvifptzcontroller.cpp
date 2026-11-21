#include "onvifptzcontroller.h"
#include "settingsmanager.h"
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDebug>
#include <QXmlStreamReader>
#include <QJsonObject>
#include <QtMath>

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

	m_saveDelayTimer = new QTimer(this);
	m_saveDelayTimer->setSingleShot(true);
	m_saveDelayTimer->setInterval(350);
	connect(m_saveDelayTimer, &QTimer::timeout, this, &OnvifPtzController::saveCurrentPosition);

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
	m_saveDelayTimer->start();
}

void OnvifPtzController::sendSoapRequest(const QByteArray &xmlPayload, std::function<void(const QByteArray&, bool)> callback)
{
	if (!m_networkManager) return;

	QUrl url(QString("http://%1:%2/onvif/device_service").arg(m_ip).arg(m_port));
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");

	QNetworkReply *reply = m_networkManager->post(request, xmlPayload);
	connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
		bool ok = (reply->error() == QNetworkReply::NoError);
		QByteArray responseData = reply->readAll();
		if (!ok) {
			qDebug() << "[" << m_ip << "] Ошибка PTZ:" << reply->errorString();
		}
		if (callback) {
			callback(responseData, ok);
		}
		reply->deleteLater();
	});
}


void OnvifPtzController::saveCurrentPosition()
{
	if (m_ip.isEmpty() || m_profileToken.isEmpty()) return;

	QJsonObject camSettings = SettingsManager::getCameraSettings(m_ip);
	QString oldToken = camSettings.value("presetToken").toString();

	auto createNewPreset = [this]() {
		QString presetXml = QString(
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			"<soap:Envelope xmlns:soap=\"http://w3.org\" "
						   "xmlns:tptz=\"http://onvif.org\">"
				"<soap:Body>"
					"<tptz:SetPreset>"
						"<tptz:ProfileToken>%1</tptz:ProfileToken>"
						"<tptz:PresetName>AutoSave</tptz:PresetName>"
					"</tptz:SetPreset>"
				"</soap:Body>"
			"</soap:Envelope>"
		).arg(m_profileToken);

		sendSoapRequest(presetXml.toUtf8(), [this](const QByteArray &data, bool ok) {
			if (!ok) return;

			QXmlStreamReader xml(data);
			while (!xml.atEnd() && !xml.hasError()) {
				xml.readNext();
				if (xml.isStartElement() && xml.name() == "PresetToken") {
					QString realToken = xml.readElementText();
					if (!realToken.isEmpty()) {
						QJsonObject updates;
						updates["presetToken"] = realToken;
						SettingsManager::updateCameraSettings(m_ip, updates);
						qDebug() << "[" << m_ip << "] Сохранён реальный токен пресета:" << realToken;
					}
					break;
				}
			}
		});
	};

	// Если был старый пресет — сначала удаляем именно его, чтобы не забивать память камеры
	if (!oldToken.isEmpty()) {
		QString removeXml = QString(
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			"<soap:Envelope xmlns:soap=\"http://w3.org\" "
						   "xmlns:tptz=\"http://onvif.org\">"
				"<soap:Body>"
					"<tptz:RemovePreset>"
						"<tptz:ProfileToken>%1</tptz:ProfileToken>"
						"<tptz:PresetToken>%2</tptz:PresetToken>"
					"</tptz:RemovePreset>"
				"</soap:Body>"
			"</soap:Envelope>"
		).arg(m_profileToken, oldToken);

		sendSoapRequest(removeXml.toUtf8(), [createNewPreset](const QByteArray &, bool) {
			createNewPreset();
		});
	} else {
		createNewPreset();
	}
}

void OnvifPtzController::restoreSavedPosition()
{
	if (m_ip.isEmpty() || m_profileToken.isEmpty()) return;

	QJsonObject camSettings = SettingsManager::getCameraSettings(m_ip);
	QString presetToken = camSettings.value("presetToken").toString();
	if (presetToken.isEmpty()) return;

	qDebug() << "[" << m_ip << "] Калибровка по концевикам перед возвратом в пресет:" << presetToken;

	// Гоним камеру в крайний нижний-левый упор, чтобы сбросить счётчик шагов даже после поворота рукой
	//sendMovePayload(-1.0, -1.0);

	// Через 4 секунды останавливаем и отправляем в сохранённый пресет
	QTimer::singleShot(4000, this, [this, presetToken]() {
		sendMovePayload(0.0, 0.0);
		QTimer::singleShot(200, this, [this, presetToken]() {
			gotoPreset(presetToken);
		});
	});
}


void OnvifPtzController::absoluteMove(double pan, double tilt)
{
	QString xml = QString(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" "
					   "xmlns:tptz=\"http://onvif.org\" "
					   "xmlns:tt=\"http://onvif.org\">"
			"<soap:Body>"
				"<tptz:AbsoluteMove>"
					"<tptz:ProfileToken>%1</tptz:ProfileToken>"
					"<tptz:Position>"
						"<tt:PanTilt x=\"%2\" y=\"%3\"/>"
					"</tptz:Position>"
				"</tptz:AbsoluteMove>"
			"</soap:Body>"
		"</soap:Envelope>"
	).arg(m_profileToken)
	 .arg(pan, 0, 'f', 4)
	 .arg(tilt, 0, 'f', 4);

	sendSoapRequest(xml.toUtf8());
}

void OnvifPtzController::gotoPreset(const QString &presetToken)
{
	QString xml = QString(
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" "
					   "xmlns:tptz=\"http://onvif.org\" "
					   "xmlns:tt=\"http://onvif.org\">"
			"<soap:Body>"
				"<tptz:GotoPreset>"
					"<tptz:ProfileToken>%1</tptz:ProfileToken>"
					"<tptz:PresetToken>%2</tptz:PresetToken>"
					"<tptz:Speed>"
						"<tt:PanTilt x=\"0.5\" y=\"0.5\"/>"
					"</tptz:Speed>"
				"</tptz:GotoPreset>"
			"</soap:Body>"
		"</soap:Envelope>"
	).arg(m_profileToken, presetToken);

	sendSoapRequest(xml.toUtf8(), [this](const QByteArray &data, bool ok) {
		qDebug() << "[" << m_ip << "] Ответ GotoPreset:" << ok << data;
	});
}
