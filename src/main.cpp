#include "main_window.hpp"

#include <AeroQt/stylesheet.h>
#include <QApplication>
#include <QLocale>
#include <QTranslator>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = QStringLiteral("Sunny_") + QLocale(locale).name();
        if (translator.load(QStringLiteral(":/i18n/") + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    Aero::registerStylesheet(&a);

    MainWindow w;
    w.show();
    return QApplication::exec();
}
