#ifndef CAMERASCANNER_H
#define CAMERASCANNER_H

#include <QObject>
#include <QUdpSocket>
#include <QStringList>

class CameraScanner : public QObject
{
	Q_OBJECT
	// Это свойство свяжет C++ список со списком (model) в QML
	Q_PROPERTY(QStringList devices READ devices NOTIFY devicesChanged)

public:
	explicit CameraScanner(QObject *parent = nullptr);

	QStringList devices() const { return m_devices; }

	// Этот метод мы будем вызывать из QML при нажатии на кнопку
	Q_INVOKABLE void startDiscovery();

signals:
	void devicesChanged();

private slots:
	void processPendingDatagrams();

private:
	QUdpSocket *m_udpSocket;
	QStringList m_devices; // Здесь будут храниться найденные IP
};

#endif // CAMERASCANNER_H
