#include "cameramanager.h"
#include "camera.h"

CameraManager::CameraManager(QObject *parent) : QObject(parent)
{

}

CameraManager::~CameraManager()
{
	// Гарантированно удаляем все объекты камер и закрываем их соединения
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

