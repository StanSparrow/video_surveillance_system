#ifndef CAMERAMANAGER_H
#define CAMERAMANAGER_H

#include <QObject>
#include <QVector>
#include <QVariantList>

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
	Q_INVOKABLE void saveUiAngle(const QString &ip, int angle);
	Q_INVOKABLE int loadUiAngle(const QString &ip) const;
	Q_INVOKABLE void requestRecordings(int slotIndex, const QString &dateStr);

signals:
	void cameraStatusUpdated(int slotIndex, QString ip, bool connected, QString statusText, QString rtspUrl);
	void stopAllRequested();
	void recordingsReady(int slotIndex, QVariantList recordings);

private:
	QVector<Camera*> activeCameras;
};

#endif // CAMERAMANAGER_H
