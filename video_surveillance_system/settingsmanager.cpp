#include "settingsmanager.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QDebug>

QString SettingsManager::filePath()
{
	return QCoreApplication::applicationDirPath() + "/camera_settings.json";
}

QJsonObject SettingsManager::readAll()
{
	QFile file(filePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return QJsonObject();
	}

	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	file.close();
	return doc.isObject() ? doc.object() : QJsonObject();
}

void SettingsManager::writeAll(const QJsonObject &root)
{
	QFile file(filePath());
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
		qWarning() << "Не удалось записать файл настроек:" << filePath();
		return;
	}

	QJsonDocument doc(root);
	file.write(doc.toJson(QJsonDocument::Indented));
	file.close();
}

QJsonObject SettingsManager::getCameraSettings(const QString &ip)
{
	if (ip.isEmpty()) return QJsonObject();
	QJsonObject root = readAll();
	return root.value(ip).toObject();
}

void SettingsManager::updateCameraSettings(const QString &ip, const QJsonObject &updates)
{
	if (ip.isEmpty()) return;

	QJsonObject root = readAll();
	QJsonObject camObj = root.value(ip).toObject();

	// Объединяем существующие поля камеры с новыми
	for (auto it = updates.begin(); it != updates.end(); ++it) {
		camObj.insert(it.key(), it.value());
	}

	root.insert(ip, camObj);
	writeAll(root);
}
