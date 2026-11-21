#include "settingsmanager.h"
#include "camera.h"
#include <QNetworkRequest>
#include <QXmlStreamReader>
#include <QDebug>
#include <QVariantMap>
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QtEndian>
#include <memory>

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

// Хеш пароля по стандарту XiongMai / Sofia (8 символов из MD5)
static QString sofiaPasswordHash(const QString &password)
{
	QByteArray md5 = QCryptographicHash::hash(password.toUtf8(), QCryptographicHash::Md5);
	const char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	QString result;
	for (int i = 0; i < 8; ++i) {
		int sum = static_cast<unsigned char>(md5[2 * i]) + static_cast<unsigned char>(md5[2 * i + 1]);
		result.append(chars[sum % 62]);
	}
	return result;
}

static QByteArray buildDvripPacket(uint16_t msgId, uint32_t sessionId, uint32_t seq, const QJsonObject &json)
{
	QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact) + "\n";
	QByteArray packet(20, '\0');
	uchar *hdr = reinterpret_cast<uchar*>(packet.data());
	*hdr = 0xFF;
	*(hdr + 1) = 0x00;
	qToLittleEndian<uint32_t>(sessionId, hdr + 4);
	qToLittleEndian<uint32_t>(seq, hdr + 8);
	qToLittleEndian<uint16_t>(msgId, hdr + 14);
	qToLittleEndian<uint32_t>(static_cast<uint32_t>(payload.size()), hdr + 16);
	return packet + payload;
}

// Вырезаем служебные заголовки кадров Sofia (00 00 01 FC/FD/FA), оставляя чистый H.264/H.265
static QByteArray extractRawVideoFrames(QByteArray &buf)
{
	QByteArray out;
	while (buf.size() >= 8) {
		int idx = buf.indexOf(QByteArray::fromHex("000001"));
		if (idx < 0) {
			buf.clear();
			break;
		}
		if (idx > 0) buf.remove(0, idx);
		if (buf.size() < 8) break;

		uint8_t type = static_cast<uint8_t>(buf.at(3));
		int headerLen = (type == 0xFC) ? 16 : 8; // 0xFC = I-frame (16B), 0xFD = P-frame (8B), 0xFA = Audio (8B)
		if (buf.size() < headerLen) break;

		uint32_t frameLen = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(buf.constData() + headerLen - 4));
		if (frameLen > 10 * 1024 * 1024) { // защита от рассинхрона
			buf.remove(0, 3);
			continue;
		}
		if (buf.size() < headerLen + static_cast<int>(frameLen)) break;

		if (type == 0xFC || type == 0xFD || type == 0xFE) {
			out.append(buf.constData() + headerLen, frameLen);
		}
		buf.remove(0, headerLen + frameLen);
	}
	return out;
}

void Camera::fetchRecordings(const QString &dateStr)
{
	// 1. Запускаем локальный мост для проигрывания выбранного файла в LibVLC (один раз на камеру)
	if (!m_replayServer) {
		m_replayServer = new QTcpServer(this);
		m_replayServer->listen(QHostAddress::LocalHost, 0);

		connect(m_replayServer, &QTcpServer::newConnection, this, [this]() {
			QTcpSocket *vlcSock = m_replayServer->nextPendingConnection();
			connect(vlcSock, &QTcpSocket::readyRead, this, [this, vlcSock]() {
				QByteArray httpReq = vlcSock->readAll();
				int getIdx = httpReq.indexOf("GET /?file=");
				if (getIdx < 0) return;

				int endIdx = httpReq.indexOf(' ', getIdx + 11);
				QByteArray encoded = httpReq.mid(getIdx + 11, endIdx - (getIdx + 11));
				QStringList parts = QString::fromUtf8(QByteArray::fromPercentEncoding(encoded)).split('|');
				if (parts.size() < 3) return;

				QString fileName = parts.value(0);
				QString startTime = parts.value(1);
				QString endTime = parts.value(2);

				vlcSock->write("HTTP/1.1 200 OK\r\nContent-Type: video/h264\r\nConnection: close\r\n\r\n");

				// Открываем отдельный поток воспроизведения к камере (порт 34567)
				QTcpSocket *camSock = new QTcpSocket(vlcSock);
				auto rxBuf = std::make_shared<QByteArray>();
				auto mediaBuf = std::make_shared<QByteArray>();
				auto sid = std::make_shared<uint32_t>(0);
				auto step = std::make_shared<int>(0);

				connect(camSock, &QTcpSocket::connected, this, [this, camSock]() {
					QJsonObject loginJson{
						{"EncryptType", m_dvripAuthEnc},
						{"LoginType", "DVRIP-Web"},
						{"UserName", m_dvripUser},
						{"PassWord", m_dvripAuthPass}
					};
					camSock->write(buildDvripPacket(1000, 0, 0, loginJson));
				});

				connect(camSock, &QTcpSocket::readyRead, this, [camSock, vlcSock, rxBuf, mediaBuf, sid, step, fileName, startTime, endTime]() {
					rxBuf->append(camSock->readAll());
					while (rxBuf->size() >= 20) {
						uint32_t pktSid = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 4));
						uint16_t msgId = qFromLittleEndian<uint16_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 14));
						uint32_t len = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 16));
						if (rxBuf->size() < 20 + static_cast<int>(len)) break;

						QByteArray payload = rxBuf->mid(20, len);
						rxBuf->remove(0, 20 + len);

						if (msgId == 1001 && *step == 0) { // Login OK -> Claim + Start
							*sid = pktSid;
							*step = 1;
							QString sidStr = QString("0x%1").arg(*sid, 8, 16, QChar('0'));
							QJsonObject param{
								{"FileName", fileName},
								{"PlayMode", "ByName"},
								{"StreamType", 0},
								{"TransMode", "TCP"},
								{"Value", 0}
							};
							QJsonObject claim{
								{"Name", "OPPlayBack"},
								{"SessionID", sidStr},
								{"OPPlayBack", QJsonObject{{"Action", "Claim"}, {"StartTime", startTime}, {"EndTime", endTime}, {"Parameter", param}}}
							};
							QJsonObject start = claim;
							QJsonObject pb = start["OPPlayBack"].toObject();
							pb["Action"] = "Start";
							start["OPPlayBack"] = pb;

							camSock->write(buildDvripPacket(1424, *sid, 1, claim));
							camSock->write(buildDvripPacket(1420, *sid, 2, start));
						} else if (msgId == 1422) { // PLAY_DATA
							mediaBuf->append(payload);
							QByteArray rawVideo = extractRawVideoFrames(*mediaBuf);
							if (!rawVideo.isEmpty() && vlcSock->state() == QAbstractSocket::ConnectedState) {
								vlcSock->write(rawVideo);
							}
						} else if (msgId == 1423) { // PLAY_EOF
							vlcSock->disconnectFromHost();
						}
					}
				});

				connect(vlcSock, &QTcpSocket::disconnected, camSock, &QTcpSocket::deleteLater);
				connect(vlcSock, &QTcpSocket::disconnected, vlcSock, &QTcpSocket::deleteLater);
				camSock->connectToHost(m_ip, 34567);
			});
		});
	}

	// 2. Перебор вариантов входа и запрос списка записей на флешке за дату dateStr
	struct AuthAttempt { QString user; QString pass; QString enc; };
	auto attempts = std::make_shared<QList<AuthAttempt>>(QList<AuthAttempt>{
		{m_dvripUser, sofiaPasswordHash(m_dvripPass), "MD5"},
		{m_dvripUser, m_dvripPass, "NONE"},
		{"admin", sofiaPasswordHash(m_dvripPass), "MD5"},
		{"admin", m_dvripPass, "NONE"}
	});
	auto attemptIdx = std::make_shared<int>(0);
	auto tryNextLogin = std::make_shared<std::function<void()>>();

	*tryNextLogin = [this, attempts, attemptIdx, tryNextLogin, dateStr]() {
		if (*attemptIdx >= attempts->size()) {
			qDebug() << "[" << m_ip << "] Ни один из 4 способов входа на порт 34567 не подошёл";
			emit recordingsReceived(QVariantList());
			return;
		}

		AuthAttempt cur = attempts->at(*attemptIdx);
		(*attemptIdx)++;
		qDebug() << "[" << m_ip << "] Старт попытки" << *attemptIdx << cur.user << cur.enc;

		QTcpSocket *querySock = new QTcpSocket(this);
		auto rxBuf = std::make_shared<QByteArray>();
		auto loggedIn = std::make_shared<bool>(false);

		connect(querySock, &QTcpSocket::connected, this, [querySock, cur]() {
			QJsonObject loginJson{
				{"EncryptType", cur.enc},
				{"LoginType", "DVRIP-Web"},
				{"UserName", cur.user},
				{"PassWord", cur.pass}
			};
			querySock->write(buildDvripPacket(1000, 0, 0, loginJson));
		});

		connect(querySock, &QTcpSocket::readyRead, this, [this, querySock, rxBuf, loggedIn, dateStr, cur, tryNextLogin]() {
			rxBuf->append(querySock->readAll());
			while (rxBuf->size() >= 20) {
				uint32_t sid = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 4));
				uint16_t msgId = qFromLittleEndian<uint16_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 14));
				uint32_t len = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 16));
				if (rxBuf->size() < 20 + static_cast<int>(len)) break;

				QByteArray payload = rxBuf->mid(20, len);
				rxBuf->remove(0, 20 + len);

				int lastBrace = payload.lastIndexOf('}');
				if (lastBrace >= 0) {
					payload = payload.left(lastBrace + 1);
				}

				if (msgId == 1001 && !*loggedIn) {
					QJsonObject resp = QJsonDocument::fromJson(payload).object();
					int ret = resp.value("Ret").toInt(-1);
					qDebug() << "[" << m_ip << "] Попытка" << cur.user << cur.enc << "-> Ret:" << ret;

					if (ret != 100) {
						querySock->abort();
						querySock->deleteLater();
						(*tryNextLogin)();
						return;
					}

					m_dvripUser = cur.user;
					m_dvripAuthPass = cur.pass;
					m_dvripAuthEnc = cur.enc;

					*loggedIn = true;
					QString sidStr = resp.value("SessionID").toString();
					if (sidStr.isEmpty()) {
						sidStr = QString("0x%1").arg(sid, 8, 16, QChar('0'));
					}

					QJsonObject queryJson{
						{"Name", "OPFileQuery"},
						{"SessionID", sidStr},
						{"OPFileQuery", QJsonObject{
							{"BeginTime", dateStr + " 00:00:00"},
							{"EndTime", dateStr + " 23:59:59"},
							{"Channel", 0},
							{"DriverTypeMask", "0x0000FFFF"},
							{"Event", "*"},
							{"StreamType", "0x00000000"},
							{"Type", "h264"}
						}}
					};
					querySock->write(buildDvripPacket(1440, sid, 1, queryJson));
				} else if (msgId == 1441) {
					qDebug() << "[" << m_ip << "] Ответ OPFileQuery (1441):" << payload;
					QJsonObject root = QJsonDocument::fromJson(payload).object();
					QJsonArray files = root.value("OPFileQuery").toArray();
					QVariantList result;
					quint16 localPort = m_replayServer->serverPort();

					for (const QJsonValue &val : files) {
						QJsonObject f = val.toObject();
						QString begin = f.value("BeginTime").toString();
						QString end = f.value("EndTime").toString();
						QString fileName = f.value("FileName").toString();

						QString rawParam = QString("%1|%2|%3").arg(fileName, begin, end);
						QString url = QString("http://127.0.0.1:%1/?file=%2")
										  .arg(localPort)
										  .arg(QString::fromUtf8(rawParam.toUtf8().toPercentEncoding()));

						QVariantMap item;
						item["title"] = begin.mid(11) + " - " + end.mid(11);
						item["replayUrl"] = url;
						result.append(item);
					}

					qDebug() << "[" << m_ip << "] Найдено файлов на SD-карте:" << result.size();
					emit recordingsReceived(result);
					querySock->disconnectFromHost();
					querySock->deleteLater();
					return;
				}
			}
		});

		connect(querySock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, [this, querySock, loggedIn, tryNextLogin](QAbstractSocket::SocketError) {
			qDebug() << "[" << m_ip << "] Ошибка сокета 34567:" << querySock->errorString();
			querySock->deleteLater();
			if (!*loggedIn) {
				(*tryNextLogin)();
			}
		});

		querySock->connectToHost(m_ip, 34567);
	};

	(*tryNextLogin)();
}

void Camera::setCredentials(const QString &user, const QString &password)
{
	m_dvripUser = user;
	m_dvripPass = password;

	if (!m_ip.isEmpty()) {
		QJsonObject updates;
		updates["user"] = user;
		updates["password"] = password;
		SettingsManager::updateCameraSettings(m_ip, updates);
	}
}

