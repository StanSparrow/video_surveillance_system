#include "camerascanner.h"
#include <QNetworkDatagram>

// XML-шаблон WS-Discovery
const QByteArray PROBE_PAYLOAD =
	"<?xml version=\"1.0\" encoding=\"utf-8\"?>"
	"<Envelope xmlns:tds=\"http://onvif.org\" xmlns:dn=\"http://onvif.org\" xmlns:wsdis=\"http://xmlsoap.org\" xmlns:soap=\"http://w3.org\" xmlns:wsa=\"http://xmlsoap.org\">"
		"<Header>"
			"<wsa:MessageID>urn:uuid:11111111-2222-3333-4444-555555555555</wsa:MessageID>"
			"<wsa:To>urn:schemas-xmlsoap.org:convention:discovery</wsa:To>"
			"<wsa:Action>http://xmlsoap.org/Probe</wsa:Action>"
		"</Header>"
		"<Body>"
			"<wsdis:Probe>"
				"<wsdis:Types>tds:Device dn:NetworkVideoTransmitter</wsdis:Types>"
			"</wsdis:Probe>"
		"</Body>"
	"</Envelope>";

CameraScanner::CameraScanner(QObject *parent) : QObject(parent)
{
	m_udpSocket = new QUdpSocket(this);

	// Связываем сигнал получения данных со слотом обработки
	connect(m_udpSocket, &QUdpSocket::readyRead, this, &CameraScanner::processPendingDatagrams);
}

void CameraScanner::startDiscovery()
{
	m_devices.clear();
	emit devicesChanged(); // Очищаем список на экране перед новым поиском

	// Привязываемся к любому свободному порту для отправки
	m_udpSocket->abort();
	m_udpSocket->bind();

	// ONVIF мультикаст адрес и порт
	QHostAddress multicastAddress("239.255.255.250");
	quint16 port = 3702;

	// Отправляем поисковый XML-пакет в сеть
	m_udpSocket->writeDatagram(PROBE_PAYLOAD, multicastAddress, port);
}

void CameraScanner::processPendingDatagrams()
{
	while (m_udpSocket->hasPendingDatagrams()) {
		QNetworkDatagram datagram = m_udpSocket->receiveDatagram();
		QString ip = datagram.senderAddress().toString();

		// Преобразуем IPv6-mapped IPv4 (если прилетит в таком формате) к обычному виду
		if (ip.startsWith("::ffff:")) {
			ip = ip.mid(7);
		}

		// Если такой IP еще не находили и это не пустой адрес
		if (!m_devices.contains(ip) && !ip.isEmpty()) {
			m_devices.append(ip);
			emit devicesChanged(); // Уведомляем QML, чтобы он обновил интерфейс
		}
	}
}
