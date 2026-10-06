#include "radeon_profile.h"
#include <QApplication>
#include <QTranslator>
#include <QStandardPaths>

int main(int argc, char *argv[])
{
    qDebug() << "Creating application object";

    QApplication a(argc, argv);
    QTranslator translator;
    QLocale locale;

    if (locale.language() != QLocale::Language::English) {
        if (translator.load(locale, "strings", ".")
                || translator.load(locale, "strings", ".", QApplication::applicationDirPath())
                || translator.load(locale, "strings", ".", QStandardPaths::locate(
                    QStandardPaths::GenericDataLocation, "radeon-profile", QStandardPaths::LocateDirectory)))

            a.installTranslator(&translator);
        else
            qWarning() << "Translation not found.";
    }

    qDebug() << "Creating radeon_profile";
    radeon_profile w;

    return a.exec();
}
