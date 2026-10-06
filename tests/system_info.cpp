// Run: g++ -std=c++17 -fPIC tests/system_info.cpp $(pkg-config --cflags --libs Qt6Core) -o /tmp/radeon-profile-system-info-test && /tmp/radeon-profile-system-info-test
#include "../globalStuff.h"
#include "../rpevent.h"
#include <QTemporaryDir>
#include <cassert>

static int warnings = 0;

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &, const QString &) {
        if (type == QtWarningMsg)
            ++warnings;
    });
    assert(globalStuff::grabSystemInfo("printf 'first\\nsecond\\n'") == (QStringList{"first", "second"}));
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("RP_TEST", "value with spaces");
    assert(globalStuff::grabSystemInfo("printf '%s' \"$RP_TEST\"", env) == QStringList{"value with spaces"});
    assert(globalStuff::grabSystemInfo("printf ok; printf warning >&2") == QStringList{"ok"});
    assert(warnings == 0);
    assert(globalStuff::grabSystemInfo("exit 1") == QStringList{""});
    assert(warnings == 1);
    assert(RPValue().unit == NONE);
    const RPEvent event;
    assert(!event.enabled && event.fixedFanSpeedChange == 0 && event.activationTemperature == 0);

    QByteArray edid(128, '\0');
    assert(!globalStuff::validEdid(edid));
    edid.replace(0, 8, QByteArray::fromHex("00ffffffffffff00"));
    assert(globalStuff::validEdid(edid));
    assert(!globalStuff::validEdid(edid.left(127)));
    const quint8 byte = 255;
    const quint16 shortValue = 1234;
    const unsigned long longValue = 0x12345678;
    assert(globalStuff::xrandrPropertyValue(&byte, 8) == byte);
    assert(globalStuff::xrandrPropertyValue(&shortValue, 16) == shortValue);
    assert(globalStuff::xrandrPropertyValue(&longValue, 32) == longValue);
    assert(globalStuff::xrandrPropertyValue(nullptr, 32) == 0);
    assert(globalStuff::xrandrPropertyValue(&byte, 7) == 0);

    QTemporaryDir dir;
    assert(dir.isValid());
    QDir devices(dir.path());
    assert(globalStuff::renderNodeForDevice(dir.path()).isEmpty());
    assert(devices.mkdir("card12"));
    assert(devices.mkdir("renderDgarbage"));
    assert(globalStuff::renderNodeForDevice(dir.path()).isEmpty());
    assert(devices.mkdir("renderD135"));
    assert(globalStuff::renderNodeForDevice(dir.path()) == "/dev/dri/renderD135");

    const auto command = globalStuff::daemonServiceCommand();
    if (!command.isEmpty()) {
        assert(QFile::exists(command.first()));
        if (QDir("/run/systemd/system").exists()) {
            assert(command.mid(1) == (QStringList{"start", "radeon-profile-daemon.service"}));
        } else {
            assert(command.mid(1) == (QStringList{"radeon-profile-daemon", "start"}));
        }
    }
}
