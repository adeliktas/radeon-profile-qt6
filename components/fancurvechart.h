#ifndef FANCURVECHART_H
#define FANCURVECHART_H

#include <QChartView>
#include <QLineSeries>
#include <QValueAxis>
#include <QTreeWidget>
#include <QMouseEvent>
#include <QLineF>
#include <algorithm>

class FanCurveChart : public QChartView {
    Q_OBJECT
    QTreeWidget *steps;
    int draggedRow = -1;

    QPointF chartPosition(const QPointF &position) const {
        return chart()->mapFromScene(mapToScene(position.toPoint()));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton && !chart()->series().isEmpty()) {
            auto *series = chart()->series().first();
            qreal nearest = 12;
            for (int i = 0; i < steps->topLevelItemCount(); ++i) {
                const auto *item = steps->topLevelItem(i);
                const QPointF value(item->text(0).toInt(), item->text(1).toInt());
                const qreal distance = QLineF(chartPosition(event->position()), chart()->mapToPosition(value, series)).length();
                if (distance < nearest) {
                    nearest = distance;
                    draggedRow = i;
                }
            }
            if (draggedRow >= 0) {
                steps->setCurrentItem(steps->topLevelItem(draggedRow));
                setCursor(Qt::ClosedHandCursor);
                event->accept();
                return;
            }
        }
        QChartView::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (draggedRow < 0) {
            QChartView::mouseMoveEvent(event);
            return;
        }
        auto *item = steps->topLevelItem(draggedRow);
        if (!item) {
            draggedRow = -1;
            unsetCursor();
            return;
        }
        const auto *previous = steps->topLevelItem(draggedRow - 1);
        const auto *next = steps->topLevelItem(draggedRow + 1);
        const QPointF value = chart()->mapToValue(chartPosition(event->position()), chart()->series().first());
        const int maximum = qRound(static_cast<QValueAxis*>(chart()->axes(Qt::Horizontal).first())->max());
        const int minTemperature = previous ? previous->text(0).toInt() + 1 : 0;
        const int maxTemperature = next ? next->text(0).toInt() - 1 : maximum;
        const int minSpeed = previous ? previous->text(1).toInt() : 0;
        const int maxSpeed = next ? next->text(1).toInt() : 100;
        if (minTemperature > maxTemperature || minSpeed > maxSpeed)
            return;
        const int temperature = std::clamp(qRound(value.x()), minTemperature, maxTemperature);
        const int speed = std::clamp(qRound(value.y()), minSpeed, maxSpeed);
        if (item->text(0).toInt() != temperature || item->text(1).toInt() != speed) {
            item->setText(0, QString::number(temperature));
            item->setText(1, QString::number(speed));
            emit curveEdited();
        }
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (draggedRow >= 0 && event->button() == Qt::LeftButton) {
            mouseMoveEvent(event);
            draggedRow = -1;
            unsetCursor();
            event->accept();
            return;
        }
        QChartView::mouseReleaseEvent(event);
    }

public:
    explicit FanCurveChart(QTreeWidget *table, QWidget *parent = nullptr)
        : QChartView(parent), steps(table) {
        setToolTip(tr("Drag a curve point to change its temperature and fan speed. Save to apply changes."));
    }

signals:
    void curveEdited();
};

#endif
