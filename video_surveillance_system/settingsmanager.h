#ifndef SETTINGSMANAGER_H
#define SETTINGSMANAGER_H

#include <QString>
#include <QJsonObject>

class SettingsManager
{
public:
	static QJsonObject getCameraSettings(const QString &ip);
	static void updateCameraSettings(const QString &ip, const QJsonObject &updates);

private:
	static QString filePath();
	static QJsonObject readAll();
	static void writeAll(const QJsonObject &root);
};

#endif // SETTINGSMANAGER_H
