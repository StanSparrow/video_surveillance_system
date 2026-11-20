#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QVector>

class Camera;
class CameraManager : public QObject
{
	Q_OBJECT
public:
	explicit CameraManager(QObject *parent = nullptr);
	~CameraManager();
	Q_INVOKABLE void connectToSelectedCameras(const QStringList &ips);
	Q_INVOKABLE void stopAllCameras();
	Q_INVOKABLE void ptzMove(int slotIndex, double panSpeed, double tiltSpeed);
	Q_INVOKABLE void ptzStop(int slotIndex);

signals:
	void cameraStatusUpdated(int slotIndex, QString ip, bool connected, QString statusText, QString rtspUrl);
	void stopAllRequested();

private:
	QVector<Camera*> activeCameras;
};

#endif // CAMERAMANAGER_H
