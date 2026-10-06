#include "../globalStuff.h"
#include "../components/fancurvechart.h"
#include <QApplication>
#include <QTemporaryDir>
#include <cassert>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QTemporaryDir dir;
    assert(dir.isValid());
    auto write = [&](const QString &name, const QByteArray &value) {
        QFile file(dir.filePath(name));
        assert(file.open(QIODevice::WriteOnly));
        assert(file.write(value) == value.size());
    };
    write("temp1_input", "45000\n");
    write("temp2_label", "mem\n");
    write("temp7_label", "junction\n");
    write("temp7_input", "65000\n");
    const HwmonAttributes sensors(dir.path() + "/");
    assert(sensors.hotspot == dir.filePath("temp7_input"));
    assert(readHwmonTemperature(sensors.temp1) == 45);
    assert(readHwmonTemperature(sensors.hotspot) == 65);
    write("temp7_input", "not a temperature");
    assert(readHwmonTemperature(sensors.hotspot) == -1);
    write("temp7_input", "-1000");
    assert(readHwmonTemperature(sensors.hotspot) == -1);
    assert(QFile::remove(sensors.hotspot));
    assert(readHwmonTemperature(sensors.hotspot) == -1);
    assert(readHwmonTemperature("") == -1);
    assert(QFile::remove(dir.filePath("temp7_label")));
    assert(HwmonAttributes(dir.path() + "/").hotspot.isEmpty());

    const FanProfileSteps curve{{40, 20}, {80, 100}};
    assert(fanSpeedAtTemperature(curve, 0) == 20);
    assert(fanSpeedAtTemperature(curve, 40) == 20);
    assert(fanSpeedAtTemperature(curve, 60) == 60);
    assert(fanSpeedAtTemperature(curve, 80) == 100);
    assert(fanSpeedAtTemperature(curve, 115) == 100);
    assert(fanSpeedAtTemperature(FanProfileSteps{{60, 50}}, 90) == 50);

    QTreeWidget table;
    table.setColumnCount(2);
    for (const auto &point : {QPoint(40, 30), QPoint(70, 60), QPoint(100, 100)})
        table.addTopLevelItem(new QTreeWidgetItem({QString::number(point.x()), QString::number(point.y())}));
    FanCurveChart view(&table);
    auto *series = new QLineSeries;
    view.chart()->addSeries(series);
    auto *x = new QValueAxis;
    auto *y = new QValueAxis;
    x->setRange(0, 120);
    y->setRange(0, 100);
    view.chart()->addAxis(x, Qt::AlignBottom);
    view.chart()->addAxis(y, Qt::AlignLeft);
    series->attachAxis(x);
    series->attachAxis(y);
    int edits = 0;
    auto update = [&] {
        series->clear();
        for (int i = 0; i < table.topLevelItemCount(); ++i) {
            auto *item = table.topLevelItem(i);
            series->append(item->text(0).toInt(), item->text(1).toInt());
        }
    };
    QObject::connect(&view, &FanCurveChart::curveEdited, [&] { ++edits; update(); });
    update();
    view.resize(800, 500);
    view.show();
    app.processEvents();
    auto mouse = [&](QEvent::Type type, QPointF value, Qt::MouseButton button, Qt::MouseButtons buttons) {
        const QPointF scene = view.chart()->mapToScene(view.chart()->mapToPosition(value, series));
        const QPointF position = view.mapFromScene(scene);
        QMouseEvent event(type, position, view.viewport()->mapToGlobal(position.toPoint()), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view.viewport(), &event);
    };
    mouse(QEvent::MouseButtonPress, {70, 60}, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, {80, 70}, Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, {80, 70}, Qt::LeftButton, Qt::NoButton);
    assert(table.topLevelItem(1)->text(0).toInt() == 80);
    assert(table.topLevelItem(1)->text(1).toInt() == 70);
    assert(series->at(1) == QPointF(80, 70));
    assert(edits > 0);
    mouse(QEvent::MouseButtonPress, {80, 70}, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, {150, -10}, Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, {150, -10}, Qt::LeftButton, Qt::NoButton);
    assert(table.topLevelItem(1)->text(0).toInt() == 99);
    assert(table.topLevelItem(1)->text(1).toInt() == 30);
    const int previousEdits = edits;
    mouse(QEvent::MouseButtonPress, {10, 90}, Qt::LeftButton, Qt::LeftButton);
    mouse(QEvent::MouseMove, {20, 50}, Qt::NoButton, Qt::LeftButton);
    mouse(QEvent::MouseButtonRelease, {20, 50}, Qt::LeftButton, Qt::NoButton);
    assert(edits == previousEdits);
}
