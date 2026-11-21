#include "camera.h"
#include <QNetworkRequest>
#include <QXmlStreamReader>
#include <QDebug>
#include <QVariantMap>

const QByteArray STREAM_URI_PAYLOAD =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<soap:Envelope xmlns:soap=\"http://w3.org\" xmlns:trt=\"http://onvif.org\">"
		"<soap:Body>"
			"<trt:GetStreamUri>"
				"<trt:StreamSetup>"
					"<trt:Stream xmlns=\"http://onvif.org\">RTP-Unicast</trt:Stream>"
					"<trt:Transport xmlns=\"http://onvif.org\">"
						"<trt:Protocol>RTSP</trt:Protocol>"
					"</trt:Transport>"
				"</trt:StreamSetup>"
				"<trt:ProfileToken>%1</trt:ProfileToken>"
			"</trt:GetStreamUri>"
		"</soap:Body>"
	"</soap:Envelope>";

Camera::Camera(const QString &ip, QObject *parent)
	: QObject(parent), m_ip(ip)
	{
	m_networkManager = new QNetworkAccessManager(this);
	m_ptzController = new OnvifPtzController(m_networkManager, this);
}

Camera::Camera(QObject *parent)
	: QObject(parent), m_networkManager(new QNetworkAccessManager(this))
{
}

void Camera::movePtz(double panSpeed, double tiltSpeed)
{
	if (m_ptzController) m_ptzController->continuousMove(panSpeed, tiltSpeed);
}

void Camera::stopPtz()
{
	if (m_ptzController) m_ptzController->stop();
}

void Camera::connectToOnvif()
{
	qDebug() << "[" << m_ip << "] Отправка запроса GetProfiles...";

	// Формируем URL. Попробуйте 8899, если глухо — меняйте на 80 или 8000
	QUrl url(QString("http://%1:8899/onvif/device_service").arg(m_ip));

	// Чистый SOAP-пакет для получения видеопрофилей
	QByteArray soapEnvelope =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" xmlns:trt=\"http://onvif.org\">"
			"<soap:Body>"
				"<trt:GetProfiles/>"
			"</soap:Body>"
		"</soap:Envelope>";

	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");

	QNetworkReply *reply = m_networkManager->post(request, soapEnvelope);
	connect(reply, &QNetworkReply::finished, this, &Camera::onProfilesReceived);
}

void Camera::onProfilesReceived()
{
	QNetworkReply *reply = qobject_cast<QNetworkReply*>(sender());
	if (!reply) return;

	if (reply->error() == QNetworkReply::NoError) {
		QByteArray response = reply->readAll();
		QString profileToken = "";

		// Простейший парсинг токена через QXmlStreamReader
		QXmlStreamReader xml(response);
		while (!xml.atEnd() && !xml.hasError()) {
			xml.readNext();
			if (xml.isStartElement() && (xml.name() == "Profile" || xml.name() == "Profiles")) {
				if (xml.attributes().hasAttribute("token")) {
					profileToken = xml.attributes().value("token").toString();
					break; // Берем первый попавшийся профиль (обычно основной поток)
				}
			}
		}

		if (!profileToken.isEmpty()) {
			qDebug() << "[" << m_ip << "] Найден токен профиля:" << profileToken;

			m_ptzController->setTarget(m_ip, 8899, profileToken);
			m_ptzController->restoreSavedPosition();

			// ШАГ 2: Запрашиваем GetStreamUri
			QUrl url(QString("http://%1:8899/onvif/device_service").arg(m_ip)); // проверьте ваш порт (8899/80)
			QNetworkRequest request(url);
			request.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");

			QByteArray soapEnvelope = QString(STREAM_URI_PAYLOAD).arg(profileToken).toUtf8();

			QNetworkReply *uriReply = m_networkManager->post(request, soapEnvelope);
			// Подключаем к новому слоту обработки ссылки
			connect(uriReply, &QNetworkReply::finished, this, [this, uriReply]() {
				if (uriReply->error() == QNetworkReply::NoError) {
					QByteArray uriResponse = uriReply->readAll();
					QString rtspUrl = "";

					// Ищем тег <Uri> в ответе камеры
					QXmlStreamReader uriXml(uriResponse);
					while (!uriXml.atEnd() && !uriXml.hasError()) {
						uriXml.readNext();
						if (uriXml.isStartElement() && uriXml.name() == "Uri") {
							rtspUrl = uriXml.readElementText();
							break;
						}
					}

					if (!rtspUrl.isEmpty()) {
						qDebug() << "[" << m_ip << "] RTSP URL получен:" << rtspUrl;
						m_rtspUrl = rtspUrl;
						// Сигнализируем об успехе И передаем RTSP ссылку
						emit connectionStatusChanged(m_ip, true, m_rtspUrl);
					} else {
						emit connectionStatusChanged(m_ip, false, "Тег <Uri> не найден в ответе");
					}
				} else {
					emit connectionStatusChanged(m_ip, false, "Ошибка GetStreamUri: " + uriReply->errorString());
				}
				uriReply->deleteLater();
			});

		} else {
			emit connectionStatusChanged(m_ip, false, "Не удалось найти токен профиля в XML");
		}
	} else {
		qDebug() << "[" << m_ip << "] Ошибка ONVIF GetProfiles:" << reply->errorString();
		emit connectionStatusChanged(m_ip, false, reply->errorString());
	}

	reply->deleteLater();
}

void Camera::fetchRecordings(const QString &dateStr)
{
	QUrl url(QString("http://%1:8899/onvif/device_service").arg(m_ip));
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");

	// 1. Запрашиваем список доступных треков/записей (GetRecordings)
	QByteArray soap =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
		"<soap:Envelope xmlns:soap=\"http://w3.org\" "
					   "xmlns:trc=\"http://onvif.org\">"
			"<soap:Body><trc:GetRecordings/></soap:Body>"
		"</soap:Envelope>";

	QNetworkReply *reply = m_networkManager->post(request, soap);
	connect(reply, &QNetworkReply::finished, this, [this, reply, dateStr, url]() {
		QString recordingToken;
		if (reply->error() == QNetworkReply::NoError) {
			QXmlStreamReader xml(reply->readAll());
			while (!xml.atEnd() && !xml.hasError()) {
				xml.readNext();
				if (xml.isStartElement() && xml.name() == "RecordingToken") {
					recordingToken = xml.readElementText();
					break;
				}
			}
		}
		reply->deleteLater();

		if (recordingToken.isEmpty()) {
			qDebug() << "[" << m_ip << "] Камера не вернула RecordingToken (ONVIF Profile G не поддерживается)";
			emit recordingsReceived(QVariantList());
			return;
		}

		// 2. Запрашиваем RTSP-ссылку воспроизведения (GetReplayUri)
		QByteArray replaySoap = QString(
			"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
			"<soap:Envelope xmlns:soap=\"http://w3.org\" "
						   "xmlns:trp=\"http://onvif.org\" "
						   "xmlns:tt=\"http://onvif.org\">"
				"<soap:Body>"
					"<trp:GetReplayUri>"
						"<trp:StreamSetup>"
							"<trp:Stream>RTP-Unicast</trp:Stream>"
							"<trp:Transport><trp:Protocol>RTSP</trp:Protocol></trp:Transport>"
						"</trp:StreamSetup>"
						"<trp:RecordingToken>%1</trp:RecordingToken>"
					"</trp:GetReplayUri>"
				"</soap:Body>"
			"</soap:Envelope>"
		).arg(recordingToken).toUtf8();

		QNetworkRequest reqReplay(url);
		reqReplay.setHeader(QNetworkRequest::ContentTypeHeader, "application/soap+xml; charset=utf-8");
		QNetworkReply *repReply = m_networkManager->post(reqReplay, replaySoap);

		connect(repReply, &QNetworkReply::finished, this, [this, repReply, dateStr]() {
			QVariantList result;
			if (repReply->error() == QNetworkReply::NoError) {
				QXmlStreamReader xml(repReply->readAll());
				while (!xml.atEnd() && !xml.hasError()) {
					xml.readNext();
					if (xml.isStartElement() && xml.name() == "Uri") {
						QString baseReplayUrl = xml.readElementText();
						// Формируем интервалы за выбранную дату (например, по часам)
						for (int h = 0; h < 24; ++h) {
							QString start = QString("%1T%2:00:00Z").arg(dateStr).arg(h, 2, 10, QChar('0'));
							QString end   = QString("%1T%2:59:59Z").arg(dateStr).arg(h, 2, 10, QChar('0'));
							QVariantMap item;
							item["title"] = QString("%1:00 - %1:59").arg(h, 2, 10, QChar('0'));
							item["replayUrl"] = QString("%1?starttime=%2&endtime=%3").arg(baseReplayUrl, start, end);
							result.append(item);
						}
						break;
					}
				}
			}
			repReply->deleteLater();
			emit recordingsReceived(result);
		});
	});
}
