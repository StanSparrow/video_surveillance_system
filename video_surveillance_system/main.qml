import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import QtMultimedia 5.12
import VlcComponents 1.0

Window {
    id: root
    width: 800
    height: 600
    visible: true
    title: "Система видеонаблюдения"

    onClosing: {
        console.log("Закрытие приложения. Освобождаем камеры...")
        cameraManager.connectToSelectedCameras([]) // Передаем пустой список, чтобы сработал qDeleteAll
    }

    RowLayout {
        // Явно привязываем и заставляем контейнер растягиваться
        anchors.fill: parent
        anchors.margins: 10
        spacing: 15
        Layout.fillWidth: true
        Layout.fillHeight: true

        // 1. ЛЕВАЯ ЧАСТЬ: Сетка 2х2 для видео
        GridLayout {
            id: videoGrid
            Layout.fillWidth: true
            Layout.fillHeight: true
            columns: 2
            rows: 2
            columnSpacing: 8
            rowSpacing: 8

            Repeater {
                id: videoRepeater
                model: 4

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#f5f5f5"
                    border.color: "black"
                    border.width: 2

                    property alias statusTextLabel: statusTextLabel
                    property alias mediaPlayer: vlcPlayer // Псевдоним сохраняем для совместимости с вашим Connections

                    // Наш кастомный LibVLC плеер вместо старого медиаплеера
                    VlcVideoPlayer {
                        id: vlcPlayer
                        anchors.fill: parent
                    }

                    Text {
                        id: statusTextLabel
                        anchors.centerIn: parent
                        text: "Окно видео " + (index + 1)
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        color: "black"
                        // Скрываем текст, когда идет видео, чтобы он не перекрывал картинку
                        visible: vlcPlayer.source === ""
                    }
                }
            }
        }

        Connections {
            target: cameraManager

            onCameraStatusUpdated: {
                // Из C++ приходят: slotIndex, ip, connected, statusText, rtspUrl
                var rect = videoRepeater.itemAt(slotIndex)
                if (rect) {
                    rect.color = connected ? "#e2f0d9" : "#fce4d6"

                    if (connected && rtspUrl !== "") {
                        rect.statusTextLabel.text = "ЗАГРУЗКА ПОТОКА...\n" + ip
                        // Явно останавливаем, меняем URL и запускаем плеер
                        rect.mediaPlayer.stop()
                        rect.mediaPlayer.source = rtspUrl
                        rect.mediaPlayer.play()
                    } else {
                        rect.mediaPlayer.stop()
                        rect.mediaPlayer.source = ""
                        rect.statusTextLabel.text = connected ? "ПОДКЛЮЧЕНО\n" + ip : "ОШИБКА: " + statusText + "\n" + ip
                    }
                }
            }

            onStopAllRequested: {
                for (var i = 0; i < videoRepeater.count; ++i) {
                    var rect = videoRepeater.itemAt(i)
                    if (rect && rect.mediaPlayer) {
                        rect.mediaPlayer.stop()
                    }
                }
            }
        }

        // 2. ПРАВАЯ ЧАСТЬ: Боковая панель
        ColumnLayout {
            // Жестко запрещаем панели съедать лишнее место
            Layout.minimumWidth: 220
            Layout.maximumWidth: 220
            Layout.preferredWidth: 220
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                border.color: "black"
                border.width: 1

                ListView {
                    id: cameraListView
                    anchors.fill: parent
                    anchors.margins: 5
                    clip: true
                    property var selectedCameras: []

                    // ПОДКЛЮЧАЕМ C++ СВОЙСТВО: теперь список берет данные из нашего CameraScanner
                    model: cameraScanner.devices

                    delegate: CheckDelegate {
                        width: parent.width
                        // Показываем IP-адрес камеры
                        text: "Камера IP: " + modelData
                        font.italic: true
                        font.pixelSize: 15

                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            verticalAlignment: Text.AlignVCenter
                            leftPadding: 10
                        }

                        //onClicked: cameraListView.currentIndex = index
                        onCheckedChanged: {
                            if (checked) {
                                // Добавляем IP в массив, создавая новую копию через concat
                                cameraListView.selectedCameras = cameraListView.selectedCameras.concat([modelData])
                            } else {
                                // Удаляем IP из массива через фильтр
                                cameraListView.selectedCameras = cameraListView.selectedCameras.filter(function(item) {
                                    return item !== modelData
                                })
                            }
                            console.log("Выбрано камер:", cameraListView.selectedCameras.length)
                        }
                    }
                    highlight: Rectangle { color: "lightgray" }
                }
            }

            Button {
                id: connectButton
                Layout.fillWidth: true
                Layout.preferredHeight: 45

                contentItem: Text {
                    text: "подключить"
                    font.italic: true
                    font.pixelSize: 16
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    border.color: "black"
                    border.width: 1
                    color: connectButton.down ? "#eee" : "white"
                }

                onClicked: {
                    cameraManager.connectToSelectedCameras(cameraListView.selectedCameras)
                }
            }
        }
    }
}
