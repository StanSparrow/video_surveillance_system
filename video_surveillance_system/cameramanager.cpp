#include "cameramanager.h"
#include "settingsmanager.h"
#include "camera.h"

CameraManager::CameraManager(QObject *parent) : QObject(parent)
{

}

CameraManager::~CameraManager()
{

}

void CameraManager::ptzMove(int slotIndex, double panSpeed, double tiltSpeed)
{
	if (slotIndex >= 0 && slotIndex < activeCameras.size() && activeCameras[slotIndex]) {
		activeCameras[slotIndex]->movePtz(panSpeed, tiltSpeed);
	}
}

void CameraManager::ptzStop(int slotIndex)
{
	if (slotIndex >= 0 && slotIndex < activeCameras.size() && activeCameras[slotIndex]) {
		activeCameras[slotIndex]->stopPtz();
	}
}

void CameraManager::stopAllCameras()
{
	// 1. Сначала мгновенно гасим все активные потоки VLC (отправляем TEARDOWN)
	emit stopAllRequested();

	// 2. Затем удаляем ONVIF-объекты камер
	qDeleteAll(activeCameras);
	activeCameras.clear();
}

void CameraManager::connectToSelectedCameras(const QStringList &ips) {
	qDeleteAll(activeCameras);
	activeCameras.clear();

	for (int i = 0; i < ips.size() && i < 4; ++i) { // Ограничимся 4 слотами сетки
		QString ip = ips.at(i);
		Camera *cam = new Camera(ip, this);
		activeCameras.append(cam);

		// Передаем индекс слота `i` через лямбду-захват
		connect(cam, &Camera::connectionStatusChanged, this, [this, i](QString cameraIp, bool success, QString info) {
			if (success) {
				// Если success = true, то в info лежит rtspUrl
				emit cameraStatusUpdated(i, cameraIp, true, "Воспроизведение", info);
			} else {
				// Если ошибка, то info — это текст ошибки
				emit cameraStatusUpdated(i, cameraIp, false, info, "");
			}
		});

		cam->connectToOnvif();
	}
}

void CameraManager::saveUiAngle(const QString &ip, int angle)
{
	if (ip.isEmpty()) return;
	QJsonObject updates;
	updates["uiAngle"] = angle;
	SettingsManager::updateCameraSettings(ip, updates);
}

int CameraManager::loadUiAngle(const QString &ip) const
{
	if (ip.isEmpty()) return 0;
	QJsonObject camSettings = SettingsManager::getCameraSettings(ip);
	return camSettings.value("uiAngle").toInt(0);
}

