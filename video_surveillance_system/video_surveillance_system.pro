QT += core gui qml quick network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++23
CONFIG -= debug_and_release

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    archivecontroller.cpp \
    camera.cpp \
    cameramanager.cpp \
    camerascanner.cpp \
    main.cpp \
    onvifptzcontroller.cpp \
    settingsmanager.cpp \
    vlcvideoitem.cpp

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

RESOURCES += \
    resources.qrc

HEADERS += \
    archivecontroller.h \
    camera.h \
    cameramanager.h \
    camerascanner.h \
    onvifptzcontroller.h \
    settingsmanager.h \
    vlcvideoitem.h

VLC_DIR = E:/libraries/vlc-3.0.24

# Пути к SDK LibVLC
INCLUDEPATH += "$$VLC_DIR/sdk/include"
LIBS += -L"$$VLC_DIR/sdk/lib" -llibvlc -llibvlccore

win32 {
CONFIG(debug, debug|release) {
DEST_DIR = $$OUT_PWD
} else {
DEST_DIR = $$OUT_PWD
}

# Переводим слеши в формат Windows (\)
SRC_LIBVLC = $$shell_path($$VLC_DIR/libvlc.dll)
SRC_LIBVLCCORE = $$shell_path($$VLC_DIR/libvlccore.dll)
DST_PATH = $$shell_path($$DEST_DIR)

# Команды копирования после сборки
QMAKE_POST_LINK += copy /Y "$$SRC_LIBVLC" "$$DST_PATH" &&
QMAKE_POST_LINK += copy /Y "$$SRC_LIBVLCCORE" "$$DST_PATH"
QMAKE_POST_LINK += $$escape_expand(\n\t) xcopy /E /I /Y "$$shell_path($$VLC_DIR/plugins)" "$$shell_path($$DEST_DIR/plugins)"
}

QMAKE_CXXFLAGS += -fstack-protector-strong
QMAKE_LFLAGS += -fstack-protector-strong -lssp
