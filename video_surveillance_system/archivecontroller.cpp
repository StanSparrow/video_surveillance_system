#include "archivecontroller.h"
#include "settingsmanager.h"
#include <QTcpSocket>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QUrlQuery>
#include <QtEndian>
#include <QDebug>
#include <memory>

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
		int headerLen = (type == 0xFC) ? 16 : 8;
		if (buf.size() < headerLen) break;

		uint32_t frameLen = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(buf.constData() + headerLen - 4));
		if (frameLen > 10 * 1024 * 1024) {
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

ArchiveController::ArchiveController(const QString &ip, QObject *parent)
	: QObject(parent), m_ip(ip)
{
}

void ArchiveController::setCredentials(const QString &user, const QString &password)
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

void ArchiveController::ensureReplayServer()
{
	if (m_replayServer) return;

	m_replayServer = new QTcpServer(this);
	m_replayServer->listen(QHostAddress::LocalHost, 0);

	connect(m_replayServer, &QTcpServer::newConnection, this, [this]() {
		QTcpSocket *vlcSock = m_replayServer->nextPendingConnection();
		connect(vlcSock, &QTcpSocket::readyRead, this, [this, vlcSock]() {
			QByteArray httpReq = vlcSock->readAll();
			int getIdx = httpReq.indexOf("GET /?");
			if (getIdx < 0) return;

			int endIdx = httpReq.indexOf(' ', getIdx + 4);
			QString pathAndQuery = QString::fromUtf8(httpReq.mid(getIdx + 4, endIdx - (getIdx + 4)));
			QUrlQuery query(pathAndQuery.mid(2));

			QStringList parts = query.queryItemValue("file", QUrl::FullyDecoded).split('|');
			if (parts.size() < 3) return;

			QString fileName = parts.value(0);
			QString startTime = parts.value(1);
			QString endTime = parts.value(2);
			int offsetSec = query.queryItemValue("offset").toInt();

			if (offsetSec > 0) {
				QDateTime dtStart = QDateTime::fromString(startTime, "yyyy-MM-dd HH:mm:ss");
				QDateTime dtEnd = QDateTime::fromString(endTime, "yyyy-MM-dd HH:mm:ss");
				if (dtStart.isValid()) {
					dtStart = dtStart.addSecs(offsetSec);
					if (!dtEnd.isValid() || dtStart < dtEnd) {
						startTime = dtStart.toString("yyyy-MM-dd HH:mm:ss");
					}
				}
			}

			QTcpSocket *camSock = new QTcpSocket(this);
			auto rxBuf = std::make_shared<QByteArray>();
			auto mediaBuf = std::make_shared<QByteArray>();
			auto sid = std::make_shared<uint32_t>(0);
			auto sidStr = std::make_shared<QString>();
			auto step = std::make_shared<int>(0);
			auto headerSent = std::make_shared<bool>(false);

			connect(camSock, &QTcpSocket::connected, this, [this, camSock]() {
				QJsonObject loginJson{
					{"EncryptType", m_dvripAuthEnc},
					{"LoginType", "DVRIP-Web"},
					{"UserName", m_dvripUser},
					{"PassWord", m_dvripAuthPass}
				};
				camSock->write(buildDvripPacket(1000, 0, 0, loginJson));
			});

			connect(camSock, &QTcpSocket::readyRead, this, [this, camSock, vlcSock, rxBuf, mediaBuf, sid, sidStr, step, headerSent, fileName, startTime, endTime]() {
				rxBuf->append(camSock->readAll());
				while (rxBuf->size() >= 20) {
					uint32_t pktSid = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 4));
					uint16_t msgId = qFromLittleEndian<uint16_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 14));
					uint32_t len = qFromLittleEndian<uint32_t>(reinterpret_cast<const uchar*>(rxBuf->constData() + 16));
					if (rxBuf->size() < 20 + static_cast<int>(len)) break;

					QByteArray payload = rxBuf->mid(20, len);
					rxBuf->remove(0, 20 + len);

					if (msgId != 1422) {
						int lastBrace = payload.lastIndexOf('}');
						if (lastBrace >= 0) payload = payload.left(lastBrace + 1);
					}

					auto makePlaybackObj = [&](const QString &action) {
						QJsonObject param{
							{"FileName", fileName},
							{"PlayMode", "ByName"},
							{"StreamType", 0},
							{"TransMode", "TCP"},
							{"Value", 0}
						};
						return QJsonObject{
							{"Name", "OPPlayBack"},
							{"SessionID", *sidStr},
							{"OPPlayBack", QJsonObject{
								{"Action", action},
								{"StartTime", startTime},
								{"EndTime", endTime},
								{"Parameter", param}
							}}
						};
					};

					if (msgId == 1001 && *step == 0) {
						QJsonObject resp = QJsonDocument::fromJson(payload).object();
						if (resp.value("Ret").toInt(-1) != 100) {
							vlcSock->disconnectFromHost();
							return;
						}
						*sid = pktSid;
						*sidStr = resp.value("SessionID").toString();
						if (sidStr->isEmpty()) {
							*sidStr = QString("0x%1").arg(*sid, 8, 16, QChar('0'));
						}
						*step = 1;
						camSock->write(buildDvripPacket(1424, *sid, 1, makePlaybackObj("Claim")));

					} else if (msgId == 1425 && *step == 1) {
						QJsonObject resp = QJsonDocument::fromJson(payload).object();
						if (resp.value("Ret").toInt(-1) != 100) {
							vlcSock->disconnectFromHost();
							return;
						}
						*step = 2;
						camSock->write(buildDvripPacket(1420, *sid, 2, makePlaybackObj("Start")));

					} else if (msgId == 1422) {
						mediaBuf->append(payload);
						QByteArray rawVideo = extractRawVideoFrames(*mediaBuf);
						if (!rawVideo.isEmpty() && vlcSock->state() == QAbstractSocket::ConnectedState) {
							if (!*headerSent) {
								*headerSent = true;
								bool isHevc = rawVideo.contains(QByteArray::fromHex("0000000140")) ||
											  rawVideo.contains(QByteArray::fromHex("00000140"));
								QByteArray mime = isHevc ? "video/hevc" : "video/h264";
								vlcSock->write("HTTP/1.1 200 OK\r\nContent-Type: " + mime + "\r\nConnection: close\r\n\r\n");
							}
							vlcSock->write(rawVideo);
						}
					} else if (msgId == 1423) {
						vlcSock->disconnectFromHost();
					}
				}
			});

			connect(vlcSock, &QTcpSocket::disconnected, this, [camSock, sid, sidStr, fileName, startTime, endTime]() {
			if (camSock->state() == QAbstractSocket::ConnectedState && *sid != 0) {
				QJsonObject stopObj{
					{"Name", "OPPlayBack"},
					{"SessionID", *sidStr},
					{"OPPlayBack", QJsonObject{
						{"Action", "Stop"},
						{"StartTime", startTime},
						{"EndTime", endTime},
						{"Parameter", QJsonObject{
							{"FileName", fileName},
							{"PlayMode", "ByName"},
							{"StreamType", 0},
							{"TransMode", "TCP"},
							{"Value", 0}
						}}
					}}
				};
				camSock->write(buildDvripPacket(1420, *sid, 3, stopObj));
				camSock->flush();
			}
			camSock->disconnectFromHost();
			camSock->deleteLater();
			});
			connect(vlcSock, &QTcpSocket::disconnected, vlcSock, &QTcpSocket::deleteLater);

			camSock->connectToHost(m_ip, 34567);
		});
	});
}

void ArchiveController::fetchRecordings(const QString &dateStr)
{
	ensureReplayServer();

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
			emit recordingsReceived(QVariantList());
			return;
		}

		AuthAttempt cur = attempts->at(*attemptIdx);
		(*attemptIdx)++;

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
				if (lastBrace >= 0) payload = payload.left(lastBrace + 1);

				if (msgId == 1001 && !*loggedIn) {
					QJsonObject resp = QJsonDocument::fromJson(payload).object();
					if (resp.value("Ret").toInt(-1) != 100) {
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
					if (sidStr.isEmpty()) sidStr = QString("0x%1").arg(sid, 8, 16, QChar('0'));

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
					QJsonObject root = QJsonDocument::fromJson(payload).object();
					QJsonArray files = root.value("OPFileQuery").toArray();
					QVariantList result;
					quint16 localPort = m_replayServer->serverPort();

					for (const QJsonValue &val : files) {
						QJsonObject f = val.toObject();
						QString begin = f.value("BeginTime").toString();
						QString end = f.value("EndTime").toString();
						QString fileName = f.value("FileName").toString();

						QDateTime dtBegin = QDateTime::fromString(begin, "yyyy-MM-dd HH:mm:ss");
						QDateTime dtEnd = QDateTime::fromString(end, "yyyy-MM-dd HH:mm:ss");
						qint64 durationMs = (dtBegin.isValid() && dtEnd.isValid()) ? dtBegin.msecsTo(dtEnd) : 0;

						QString rawParam = QString("%1|%2|%3").arg(fileName, begin, end);
						QString url = QString("http://127.0.0.1:%1/?file=%2")
										  .arg(localPort)
										  .arg(QString::fromUtf8(rawParam.toUtf8().toPercentEncoding()));

						QVariantMap item;
						item["title"] = begin.mid(11) + " - " + end.mid(11);
						item["replayUrl"] = url;
						item["durationMs"] = durationMs > 0 ? durationMs : qint64(0);
						result.append(item);
					}

					emit recordingsReceived(result);
					querySock->disconnectFromHost();
					querySock->deleteLater();
					return;
				}
			}
		});

        connect(querySock, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::errorOccurred),
				this, [querySock, loggedIn, tryNextLogin](QAbstractSocket::SocketError) {
			querySock->deleteLater();
			if (!*loggedIn) (*tryNextLogin)();
		});

		querySock->connectToHost(m_ip, 34567);
	};

	(*tryNextLogin)();
}

