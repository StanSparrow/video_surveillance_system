#include "vlcvideoitem.h"
#include <QSGSimpleTextureNode>
#include <QQuickWindow>
#include <QDebug>

VlcVideoItem::VlcVideoItem(QQuickItem *parent) : QQuickItem(parent)
{
	// Говорим QML, что этот элемент сам себя рисует
	setFlag(ItemHasContents, true);
	initVlc();
}

VlcVideoItem::~VlcVideoItem()
{
	stopVlc();
	if (m_vlcInstance) libvlc_release(m_vlcInstance);
}

void VlcVideoItem::initVlc()
{
	const char* vlc_args[] = {
		"--network-caching=300", // Увеличим буфер до 300мс для стабильности 2K/H.265 потока
		"--rtsp-tcp",            // Принудительно поверх TCP, чтобы пакеты не терялись
		"--no-audio",
		"--skip-frames"
	};
	m_vlcInstance = libvlc_new(sizeof(vlc_args) / sizeof(vlc_args[0]), vlc_args);
}


void VlcVideoItem::setSource(const QString &url)
{
	if (m_source == url) return;
	m_source = url;
	emit sourceChanged();
	stopVlc();
}

void VlcVideoItem::play()
{
	if (m_source.isEmpty()) return;
	stopVlc();

	// 1. УБИРАЕМ жесткое выделение m_pixelBuffer отсюда!
	// Мы выделим память динамически, когда VLC определит реальное разрешение камеры.

	m_vlcPlayer = libvlc_media_player_new(m_vlcInstance);

	libvlc_video_set_callbacks(m_vlcPlayer, lockCallback, unlockCallback, displayCallback, this);

	// 2. Вместо жесткого set_format настраиваем автоматический коллбэк формата
	// Он сработает, когда камера пришлет первый кадр и сообщит свое реальное разрешение (например, 2304x1296)
	libvlc_video_set_format_callbacks(m_vlcPlayer,
		[](void **opaque, char *chroma, unsigned *width, unsigned *height, unsigned *pitches, unsigned *lines) -> unsigned {
			VlcVideoItem *item = static_cast<VlcVideoItem*>(*opaque);
			item->m_mutex.lock();

			// Запоминаем реальное разрешение камеры
			item->m_videoWidth = *width;
			item->m_videoHeight = *height;

			// Выделяем буфер под реальный размер кадра
			item->m_pixelBuffer.resize(item->m_videoWidth * item->m_videoHeight * 4);
			item->m_pixelBuffer.fill(0);

			item->m_mutex.unlock();

			// Возвращаем параметры обратно в VLC
			memcpy(chroma, "RGBA", 4);
			*pitches = item->m_videoWidth * 4;
			*lines = item->m_videoHeight;

			return 1; // 1 буфер для плоскости
		},
		[](void *) {} // cleanup callback (не нужен)
	);

	libvlc_media_t *media = libvlc_media_new_location(m_vlcInstance, m_source.toUtf8().constData());
	libvlc_media_add_option(media, ":rtsp-tcp");
	libvlc_media_player_set_media(m_vlcPlayer, media);
	libvlc_media_release(media);

	libvlc_media_player_play(m_vlcPlayer);
}


void VlcVideoItem::stop()
{
	m_source = "";
	emit sourceChanged();
	stopVlc();
	update();
}

void VlcVideoItem::stopVlc()
{
	/*m_mutex.lock();
	if (m_vlcPlayer) {
		// Останавливаем воспроизведение (отправляет TEARDOWN камере)
		libvlc_media_player_stop(m_vlcPlayer);
		libvlc_media_player_release(m_vlcPlayer);
		m_vlcPlayer = nullptr;
	}
	m_frameReady = false;
	m_currentImage = QImage();
	m_mutex.unlock();*/

	libvlc_media_player_t *playerToStop = nullptr;

	m_mutex.lock();
	if (m_vlcPlayer) {
		playerToStop = m_vlcPlayer;
		m_vlcPlayer = nullptr;
	}
	m_frameReady = false;
	m_currentImage = QImage();
	m_mutex.unlock();

	// Останавливаем плеер строго ПОСЛЕ освобождения m_mutex,
	// чтобы не заблокировать фоновый поток декодера VLC
	if (playerToStop) {
		libvlc_media_player_stop(playerToStop);
		libvlc_media_player_release(playerToStop);
	}
}

// VLC запрашивает буфер для записи нового кадра
void* VlcVideoItem::lockCallback(void *opaque, void **planes)
{
	VlcVideoItem *item = static_cast<VlcVideoItem*>(opaque);
	item->m_mutex.lock();

	// СТРОГО ТАК: записываем указатель в нулевой элемент массива плоскостей
	planes[0] = static_cast<void*>(item->m_pixelBuffer.data());

	return nullptr;
}

void VlcVideoItem::unlockCallback(void *opaque, void *picture, void *const *planes)
{
	VlcVideoItem *item = static_cast<VlcVideoItem*>(opaque);
	Q_UNUSED(picture);
	Q_UNUSED(planes);

	// Из сырого буфера конструируем QImage (без копирования памяти)
	item->m_currentImage = QImage(
		reinterpret_cast<const uchar*>(item->m_pixelBuffer.constData()),
		item->m_videoWidth,
		item->m_videoHeight,
		QImage::Format_RGBA8888
	);

	item->m_frameReady = true;
	item->m_mutex.unlock();
}

// VLC сигнализирует, что кадр можно отобразить
void VlcVideoItem::displayCallback(void *opaque, void *picture)
{
	Q_UNUSED(picture);
	VlcVideoItem *item = static_cast<VlcVideoItem*>(opaque);

	QMetaObject::invokeMethod(item, "update", Qt::QueuedConnection);
}

QSGNode* VlcVideoItem::updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *)
{
	QSGSimpleTextureNode *node = static_cast<QSGSimpleTextureNode *>(oldNode);

	m_mutex.lock();
	if (m_frameReady && !m_currentImage.isNull()) {
		if (!node) {
			node = new QSGSimpleTextureNode();
		}

		// Создаем текстуру. Теперь QImage имеет правильный размер (2304x1296)
		QSGTexture *texture = window()->createTextureFromImage(m_currentImage);

		if (node->texture()) {
			delete node->texture();
		}

		node->setTexture(texture);
		m_frameReady = false;
	}
	m_mutex.unlock();

	if (node) {
		node->setRect(boundingRect());
	}
	return node;
}

