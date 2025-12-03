QT       += core gui network sql

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

CONFIG += c++17

DESTDIR = ./bin

# You can make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

SOURCES += \
    addfrienddialog.cpp \
    chatbubble.cpp \
    chatdialog.cpp \
    forgetdialog.cpp \
    friendmanager.cpp \
    global.cpp \
    httpmgr.cpp \
    logindialog.cpp \
    main.cpp \
    mainwindow.cpp \
    messagelistview.cpp \
    regdialog.cpp \
    sidebarbutton.cpp \
    tcpmgr.cpp \
    timerbtn.cpp \
    usermgr.cpp \
    localdb.cpp

HEADERS += \
    QObjectSingleton.h \
    addfrienddialog.h \
    chatbubble.h \
    chatdialog.h \
    forgetdialog.h \
    friendmanager.h \
    global.h \
    httpmgr.h \
    logindialog.h \
    mainwindow.h \
    messagelistview.h \
    regdialog.h \
    sidebarbutton.h \
    singleton.h \
    tcpmgr.h \
    timerbtn.h \
    usermgr.h \
    localdb.h

FORMS += \
    forgetdialog.ui \
    logindialog.ui \
    mainwindow.ui \
    regdialog.ui

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

DISTFILES += \
    config.ini \

win32:CONFIG(release, debug | release)
{
    #指定要拷贝的文件目录为工程目录下release目录下的所有dll、lib文件，例如工程目录在D:\QT\Test
    #PWD就为D:/QT/Test，DllFile = D:/QT/Test/release/*.dll
    TargetConfig = $${PWD}/config.ini
    #将输入目录中的"/"替换为"\"
    TargetConfig = $$replace(TargetConfig, /, \\)
    #将输出目录中的"/"替换为"\"
    OutputDir =  $${OUT_PWD}/$${DESTDIR}
    OutputDir = $$replace(OutputDir, /, \\)
    //执行copy命令
    QMAKE_POST_LINK += copy /Y \"$$TargetConfig\" \"$$OutputDir\"
}

RESOURCES += \
    src.qrc

