#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QVariant>

#include <QJSValue>
#include <QJSValueList>
#include <QDebug>

#include "camerascanner.h"
#include "cameramanager.h"
#include "vlcvideoitem.h"

int main(int argc, char *argv[]) {

qDebug() << "C++ standard:" << __cplusplus;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
	QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
#endif

	QGuiApplication app(argc, argv);

	qmlRegisterType<VlcVideoItem>("VlcComponents", 1, 0, "VlcVideoPlayer");

	QQmlApplicationEngine engine;

	CameraScanner scanner;
	engine.rootContext()->setContextProperty("cameraScanner", &scanner);

	CameraManager cameraManager;
	engine.rootContext()->setContextProperty("cameraManager", &cameraManager);

	QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
		&cameraManager, &CameraManager::stopAllRequested);

	scanner.startDiscovery();

	const QUrl url(QStringLiteral("qrc:/main.qml")); // или прямой путь к файлу, если без ресурсов

	QObject::connect(&engine, &QQmlApplicationEngine::objectCreated,
					 &app, [url](QObject *obj, const QUrl &objUrl) {
		if (!obj && url == objUrl)
			QCoreApplication::exit(-1);
	}, Qt::QueuedConnection);

	engine.load(url);

	return app.exec();
}
