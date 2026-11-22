import QtQuick 2.12
import QtQuick.Window 2.12
import QtQuick.Controls 2.12
import QtQuick.Layouts 1.12
import VlcComponents 1.0

Window {
    id: archiveWindow
    width: 820
    height: 540
    title: "Архив камеры: " + cameraIp

    property int slotIndex: -1
    property string cameraIp: ""
    property string selectedDate: Qt.formatDate(new Date(), "yyyy-MM-dd")
    property var recordingsModel: []
    property int currentIndex: -1
    property string baseReplayUrl: ""
    property int fallbackDurationMs: 0
    property int streamOffsetMs: 0

    readonly property int effectiveDurationMs: archivePlayer.durationMs > 0 ? archivePlayer.durationMs : fallbackDurationMs
    readonly property int effectivePositionMs: Math.min(effectiveDurationMs > 0 ? effectiveDurationMs : 999999999, streamOffsetMs + archivePlayer.positionMs)

    function formatTime(ms) {
        var totalSec = Math.max(0, Math.floor(ms / 1000))
        var min = Math.floor(totalSec / 60)
        var sec = totalSec % 60
        return (min < 10 ? "0" : "") + min + ":" + (sec < 10 ? "0" : "") + sec
    }

    function openRecord(idx) {
        if (idx < 0 || idx >= recordingsModel.length) return
        currentIndex = idx
        var item = recordingsModel[idx]
        baseReplayUrl = item.replayUrl
        fallbackDurationMs = item.durationMs || 0
        streamOffsetMs = 0

        archivePlayer.stop()
        archivePlayer.source = baseReplayUrl
        archivePlayer.play()
    }

    function seekArchive(targetMs) {
        if (baseReplayUrl === "") return
        var clampedMs = Math.max(0, effectiveDurationMs > 0 ? Math.min(targetMs, effectiveDurationMs - 1000) : targetMs)

        if (archivePlayer.isSeekable && archivePlayer.durationMs > 0) {
            archivePlayer.seekTo(clampedMs)
        } else {
            var offsetSec = Math.floor(clampedMs / 1000)
            streamOffsetMs = offsetSec * 1000
            archivePlayer.stop()
            archivePlayer.source = baseReplayUrl + "&offset=" + offsetSec
            archivePlayer.play()
        }
    }

    function loadCredentialsAndFetch() {
        userInput.text = cameraManager.loadCameraUser(cameraIp)
        passInput.text = cameraManager.loadCameraPassword(cameraIp)
        cameraManager.requestRecordings(slotIndex, selectedDate, userInput.text, passInput.text)
    }

    onClosing: archivePlayer.stop()

    RowLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 550
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: "black"
                clip: true

                VlcVideoPlayer {
                    id: archivePlayer
                    anchors.fill: parent
                }

                Text {
                    anchors.centerIn: parent
                    color: "#cccccc"
                    font.pixelSize: 14
                    text: "Выберите запись из списка справа"
                    visible: archivePlayer.source === ""
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 84
                color: "#1e1e1e"
                border.color: "#333333"
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 4

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Text {
                            color: "white"
                            font.pixelSize: 12
                            font.family: "monospace"
                            text: archiveWindow.formatTime(timelineSlider.pressed ? timelineSlider.value : archiveWindow.effectivePositionMs)
                        }

                        Slider {
                            id: timelineSlider
                            Layout.fillWidth: true
                            from: 0
                            to: Math.max(1000, archiveWindow.effectiveDurationMs)
                            enabled: archivePlayer.source !== ""
                            value: pressed ? value : archiveWindow.effectivePositionMs

                            onPressedChanged: {
                                if (!pressed && archivePlayer.source !== "") {
                                    archiveWindow.seekArchive(value)
                                }
                            }
                        }

                        Text {
                            color: "#aaaaaa"
                            font.pixelSize: 12
                            font.family: "monospace"
                            text: archiveWindow.formatTime(archiveWindow.effectiveDurationMs)
                        }
                    }

                    RowLayout {
                        Layout.alignment: Qt.AlignHCenter
                        spacing: 8

                        Button {
                            text: "⏮ Пред."
                            enabled: archiveWindow.currentIndex > 0
                            onClicked: archiveWindow.openRecord(archiveWindow.currentIndex - 1)
                        }

                        Button {
                            text: "⏪ -10с"
                            enabled: archivePlayer.source !== ""
                            onClicked: archiveWindow.seekArchive(archiveWindow.effectivePositionMs - 10000)
                        }

                        Button {
                            text: archivePlayer.isPlaying ? "⏸ Пауза" : "▶ Пуск"
                            enabled: archivePlayer.source !== "" || archiveWindow.currentIndex >= 0
                            onClicked: {
                                if (archivePlayer.source === "" && archiveWindow.currentIndex >= 0) {
                                    archiveWindow.openRecord(archiveWindow.currentIndex)
                                } else {
                                    archivePlayer.togglePause()
                                }
                            }
                        }

                        Button {
                            text: "⏹ Стоп"
                            enabled: archivePlayer.source !== ""
                            onClicked: {
                                archivePlayer.stop()
                                archiveWindow.streamOffsetMs = 0
                            }
                        }

                        Button {
                            text: "+10с ⏩"
                            enabled: archivePlayer.source !== ""
                            onClicked: archiveWindow.seekArchive(archiveWindow.effectivePositionMs + 10000)
                        }

                        Button {
                            text: "След. ⏭"
                            enabled: archiveWindow.currentIndex >= 0 && archiveWindow.currentIndex < archiveWindow.recordingsModel.length - 1
                            onClicked: archiveWindow.openRecord(archiveWindow.currentIndex + 1)
                        }
                    }
                }
            }
        }

        ColumnLayout {
            Layout.minimumWidth: 230
            Layout.maximumWidth: 230
            Layout.preferredWidth: 230
            Layout.fillHeight: true
            spacing: 8

            TextField {
                id: userInput
                Layout.fillWidth: true
                placeholderText: "Имя пользователя"
            }

            TextField {
                id: passInput
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: "Пароль устройства"
            }

            RowLayout {
                Layout.fillWidth: true
                TextField {
                    id: dateInput
                    Layout.fillWidth: true
                    text: archiveWindow.selectedDate
                    placeholderText: "ГГГГ-ММ-ДД"
                }
                Button {
                    text: "Найти"
                    onClicked: {
                        archiveWindow.selectedDate = dateInput.text
                        archiveWindow.currentIndex = -1
                        cameraManager.requestRecordings(archiveWindow.slotIndex, archiveWindow.selectedDate, userInput.text, passInput.text)
                    }
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: archiveWindow.recordingsModel

                delegate: ItemDelegate {
                    width: parent.width
                    highlighted: index === archiveWindow.currentIndex
                    text: modelData.title
                    onClicked: archiveWindow.openRecord(index)
                }
            }
        }
    }
}
