#ifndef VLCVIDEOITEM_H
#define VLCVIDEOITEM_H


#include <QQuickItem>
#include <QMutex>
#include <QImage>
#include <QTimer>

#include <vlc/vlc.h>

class VlcVideoItem : public QQuickItem
{
	Q_OBJECT
	Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
	Q_PROPERTY(qint64 positionMs READ positionMs NOTIFY playbackStateChanged)
	Q_PROPERTY(qint64 durationMs READ durationMs NOTIFY playbackStateChanged)
	Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY playbackStateChanged)
	Q_PROPERTY(bool isSeekable READ isSeekable NOTIFY playbackStateChanged)

public:
	explicit VlcVideoItem(QQuickItem *parent = nullptr);
	~VlcVideoItem();

	QString source() const { return m_source; }
	Q_INVOKABLE void setSource(const QString &url);
	Q_INVOKABLE void play();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePause();
	Q_INVOKABLE void seekTo(qint64 ms);
	Q_INVOKABLE void seekRelative(qint64 deltaMs);

	qint64 positionMs() const { return m_positionMs; }
	qint64 durationMs() const { return m_durationMs; }
	bool isPlaying() const { return m_isPlaying; }
	bool isSeekable() const { return m_isSeekable; }

signals:
	void sourceChanged();
	void errorOccurred(QString errorStr);
	void playbackStateChanged();

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

	QTimer *m_stateTimer = nullptr;
	qint64 m_positionMs = 0;
	qint64 m_durationMs = 0;
	bool m_isPlaying = false;
	bool m_isSeekable = false;
	bool m_isPaused = false;
};

#endif // VLCVIDEOITEM_H
