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
        cameraManager.stopAllCameras()
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
                    id: cameraCell
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#f5f5f5"
                    border.color: "black"
                    border.width: 2
                    clip: true // Обрезаем всё, что выходит за рамки слота

                    property string currentIp: ""
                    property alias statusTextLabel: statusTextLabel
                    property alias mediaPlayer: vlcPlayer

                    VlcVideoPlayer {
                        id: vlcPlayer
                        anchors.centerIn: parent

                        // Угол поворота (0, 90, 180, 270)
                        property int angle: 0
                        rotation: angle

                        // При повороте на 90° и 270° меняем ширину и высоту местами
                        width: (angle % 180 === 0) ? parent.width : parent.height
                        height: (angle % 180 === 0) ? parent.height : parent.width
                    }

                    Text {
                        id: statusTextLabel
                        anchors.centerIn: parent
                        text: "Окно видео " + (index + 1)
                        font.pixelSize: 14
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        color: "black"
                        visible: vlcPlayer.source === ""
                    }

                    // Общая панель управления внизу по центру ячейки
                    Row {
                        id: controlBar
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottomMargin: 6
                        spacing: 4
                        z: 10
                        visible: vlcPlayer.source !== ""

                        property real ptzSpeed: 0.25 // Невысокая скорость для минимального шага

                        // Кнопка переворота кадра на 90°
                        Button {
                            id: rotateButton
                            width: 28
                            height: 28
                            padding: 0
                            contentItem: Text {
                                text: "↻"
                                color: "white"
                                font.pixelSize: 16
                                font.bold: true
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: rotateButton.pressed ? "#aa000000" : "#66000000"
                                border.color: "#88ffffff"
                                border.width: 1
                            }
                            onClicked: {
                                vlcPlayer.angle = (vlcPlayer.angle + 90) % 360
                                cameraManager.saveUiAngle(cameraCell.currentIp, vlcPlayer.angle)
                            }
                        }

                        // ВЛЕВО
                        Button {
                            id: btnLeft
                            width: 28
                            height: 28
                            padding: 0
                            contentItem: Text {
                                text: "◄"
                                color: "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: btnLeft.pressed ? "#aa000000" : "#66000000"
                                border.color: "#88ffffff"
                                border.width: 1
                            }
                            onPressed: cameraManager.ptzMove(index, controlBar.ptzSpeed, 0.0)
                            onReleased: cameraManager.ptzStop(index)
                            onCanceled: cameraManager.ptzStop(index)
                        }

                        // ВВЕРХ
                        Button {
                            id: btnUp
                            width: 28
                            height: 28
                            padding: 0
                            contentItem: Text {
                                text: "▲"
                                color: "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: btnUp.pressed ? "#aa000000" : "#66000000"
                                border.color: "#88ffffff"
                                border.width: 1
                            }
                            onPressed: cameraManager.ptzMove(index, 0.0, controlBar.ptzSpeed)
                            onReleased: cameraManager.ptzStop(index)
                            onCanceled: cameraManager.ptzStop(index)
                        }

                        // ВНИЗ
                        Button {
                            id: btnDown
                            width: 28
                            height: 28
                            padding: 0
                            contentItem: Text {
                                text: "▼"
                                color: "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: btnDown.pressed ? "#aa000000" : "#66000000"
                                border.color: "#88ffffff"
                                border.width: 1
                            }
                            onPressed: cameraManager.ptzMove(index, 0.0, -controlBar.ptzSpeed)
                            onReleased: cameraManager.ptzStop(index)
                            onCanceled: cameraManager.ptzStop(index)
                        }

                        // ВПРАВО
                        Button {
                            id: btnRight
                            width: 28
                            height: 28
                            padding: 0
                            contentItem: Text {
                                text: "►"
                                color: "white"
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            background: Rectangle {
                                radius: 4
                                color: btnRight.pressed ? "#aa000000" : "#66000000"
                                border.color: "#88ffffff"
                                border.width: 1
                            }
                            onPressed: cameraManager.ptzMove(index, -controlBar.ptzSpeed, 0.0)
                            onReleased: cameraManager.ptzStop(index)
                            onCanceled: cameraManager.ptzStop(index)
                        }
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
                    rect.currentIp = ip

                    if (connected && rtspUrl !== "") {
                        rect.statusTextLabel.text = "ЗАГРУЗКА ПОТОКА...\n" + ip
                        rect.mediaPlayer.angle = cameraManager.loadUiAngle(ip)
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
                        width: cameraListView.width
                        topPadding: 0
                        bottomPadding: 0
                        leftPadding: 4
                        rightPadding: 4
                        spacing: 4
                        text: "Камера IP: " + modelData
                        font.italic: true
                        font.pixelSize: 14

                        contentItem: Text {
                            text: parent.text
                            font: parent.font
                            verticalAlignment: Text.AlignVCenter
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
