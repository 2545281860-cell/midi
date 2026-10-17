/**
 * @file main.cpp
 * @brief 程序入口：创建 QApplication 与 MainWindow。
 */

#include "mainwindow.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("organ_console"));
    app.setOrganizationName(QStringLiteral("shengxuejisuan"));

    MainWindow window;
    window.show();
    return app.exec();
}
