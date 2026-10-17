# organ_console.pro
# 合并 serial_test（直接控制）与 midi_prase（自动演奏），并增加外接 MIDI 实时控制。
QT       += core gui serialport widgets
greaterThan(QT_MAJOR_VERSION, 4): QT += widgets
CONFIG   += c++17
TEMPLATE = app
TARGET   = organ_console

SOURCES += \
    main.cpp \
    mainwindow.cpp \
    serialclient.cpp \
    keymapping.cpp \
    keystateaggregator.cpp \
    midiparser.cpp \
    midiplayer.cpp \
    midiinput.cpp

HEADERS += \
    mainwindow.h \
    serialclient.h \
    keymapping.h \
    keystateaggregator.h \
    midiparser.h \
    midiplayer.h \
    midiinput.h

# Windows 多媒体 MIDI 输入（winmm）
win32: LIBS += -lwinmm
