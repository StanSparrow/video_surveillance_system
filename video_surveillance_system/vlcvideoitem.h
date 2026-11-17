#ifndef VLCVIDEOITEM_H
#define VLCVIDEOITEM_H


#include <QQuickItem>
#include <QMutex>
#include <QImage>
#include <vlc/vlc.h>

class VlcVideoItem : public QQuickItem
{
	Q_OBJECT
	Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)

public:
	explicit VlcVideoItem(QQuickItem *parent = nullptr);
	~VlcVideoItem();

	QString source() const { return m_source; }
	Q_INVOKABLE void setSource(const QString &url);
	Q_INVOKABLE void play();
	Q_INVOKABLE void stop();

signals:
	void sourceChanged();
	void errorOccurred(QString errorStr);

protected:
	// Переопределяем метод отрисовки компонента в QML
	QSGNode* updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
	// Коллбэки для LibVLC (открытие, захват кадра, закрытие)
	static void* lockCallback(void *opaque, void **planes);
	static void unlockCallback(void *opaque, void *picture, void *const *planes);
	static void displayCallback(void *opaque, void *picture);

	void initVlc();
	void stopVlc();

	QString m_source;

	// LibVLC структуры
	libvlc_instance_t *m_vlcInstance = nullptr;
	libvlc_media_player_t *m_vlcPlayer = nullptr;

	// Буферы для пикселей
	QByteArray m_pixelBuffer;
	QImage m_currentImage;
	QMutex m_mutex;
	bool m_frameReady = false;

	int m_videoWidth = 1280; // Дефолтное разрешение (подстроится под поток)
	int m_videoHeight = 720;
};

#endif // VLCVIDEOITEM_H
