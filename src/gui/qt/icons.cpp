#include "icons.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPixmap>
#include <QPolygonF>

#include <cmath>
#include <functional>

namespace presspng::gui {

namespace {

constexpr double kGrid = 20.0;
constexpr double kStroke = 1.6;
constexpr double kPi = 3.14159265358979323846;

using DrawFn = std::function<void(QPainter&)>;

// Filled triangle whose tip sits on the arc, pointing along it.
void arrowHead(QPainter& painter, const QPointF& tip, double directionDegrees,
               double length) {
    const double radians = directionDegrees * kPi / 180.0;
    const QPointF forward(std::cos(radians), -std::sin(radians));
    const QPointF side(-forward.y(), forward.x());
    const QPointF base = tip - forward * length;
    QPolygonF head;
    head << tip << (base + side * (length * 0.5))
         << (base - side * (length * 0.5));
    painter.setBrush(painter.pen().color());
    painter.drawPolygon(head);
    painter.setBrush(Qt::NoBrush);
}

QPixmap render(const QColor& color, int size, const DrawFn& draw) {
    QPixmap pixmap(size, size);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.scale(size / kGrid, size / kGrid);
    QPen pen(color, kStroke);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    draw(painter);
    return pixmap;
}

void drawDocument(QPainter& p) {
    p.drawPolyline(QPolygonF({QPointF(5.5, 2.5), QPointF(11.5, 2.5),
                              QPointF(14.5, 5.5), QPointF(14.5, 17.5),
                              QPointF(5.5, 17.5), QPointF(5.5, 2.5)}));
    p.drawPolyline(QPolygonF({QPointF(11.5, 2.5), QPointF(11.5, 5.5),
                              QPointF(14.5, 5.5)}));
}

// Clockwise: the head sits at the start of a counter-clockwise stroke, so it
// points the way the rotation goes.
void drawRotateRight(QPainter& p) {
    p.drawArc(QRectF(4.5, 4.5, 11, 11), 80 * 16, 300 * 16);
    arrowHead(p,
              QPointF(10 + 5.5 * std::cos(80 * kPi / 180.0),
                      10 - 5.5 * std::sin(80 * kPi / 180.0)),
              -10.0, 4.0);
}

void drawRotateLeft(QPainter& p) {
    p.save();
    p.translate(kGrid, 0);
    p.scale(-1, 1);
    drawRotateRight(p);
    p.restore();
}

void drawFlipH(QPainter& p) {
    QPen dashed = p.pen();
    dashed.setStyle(Qt::DashLine);
    p.setPen(dashed);
    p.drawLine(QPointF(10, 3), QPointF(10, 17));
    p.setPen(QPen(p.pen().color(), kStroke, Qt::SolidLine, Qt::RoundCap,
                  Qt::RoundJoin));
    p.setBrush(p.pen().color());
    p.drawPolygon(QPolygonF({QPointF(3, 5.5), QPointF(3, 14.5),
                             QPointF(8.2, 10)}));
    p.setBrush(Qt::NoBrush);
    p.drawPolygon(QPolygonF({QPointF(17, 5.5), QPointF(17, 14.5),
                             QPointF(11.8, 10)}));
}

void drawFlipV(QPainter& p) {
    p.save();
    p.translate(kGrid, 0);
    p.rotate(90);
    drawFlipH(p);
    p.restore();
}

void drawCrop(QPainter& p) {
    p.drawPolyline(
        QPolygonF({QPointF(6, 3), QPointF(6, 14), QPointF(17, 14)}));
    p.drawPolyline(
        QPolygonF({QPointF(3, 6), QPointF(14, 6), QPointF(14, 17)}));
}

void drawLock(QPainter& p) {
    p.drawArc(QRectF(6.5, 3.5, 7, 7), 0, 180 * 16);
    p.drawRoundedRect(QRectF(5, 9.5, 10, 7.5), 1.6, 1.6);
}

void drawFolder(QPainter& p) {
    QPainterPath path;
    path.moveTo(2.5, 6);
    path.lineTo(8, 6);
    path.lineTo(9.5, 8);
    path.lineTo(17.5, 8);
    path.lineTo(17.5, 15.5);
    path.lineTo(2.5, 15.5);
    path.closeSubpath();
    p.drawPath(path);
}

void drawCopy(QPainter& p) {
    p.drawRoundedRect(QRectF(3.5, 3.5, 9, 9), 1.6, 1.6);
    p.drawRoundedRect(QRectF(7.5, 7.5, 9, 9), 1.6, 1.6);
}

void drawDiff(QPainter& p) {
    p.drawRoundedRect(QRectF(3, 3, 10, 10), 1.8, 1.8);
    QColor fill = p.pen().color();
    fill.setAlpha(95);
    p.setBrush(fill);
    p.drawRoundedRect(QRectF(7, 7, 10, 10), 1.8, 1.8);
    p.setBrush(Qt::NoBrush);
}

void drawCompress(QPainter& p) {
    p.drawLine(QPointF(10, 2.5), QPointF(10, 7.5));
    arrowHead(p, QPointF(10, 9), -90.0, 3.6);
    p.drawLine(QPointF(10, 17.5), QPointF(10, 12.5));
    arrowHead(p, QPointF(10, 11), 90.0, 3.6);
}

DrawFn lookup(const QString& name) {
    if (name == QLatin1String("open")) return drawDocument;
    if (name == QLatin1String("rotateLeft")) return drawRotateLeft;
    if (name == QLatin1String("rotateRight")) return drawRotateRight;
    if (name == QLatin1String("flipH")) return drawFlipH;
    if (name == QLatin1String("flipV")) return drawFlipV;
    if (name == QLatin1String("crop")) return drawCrop;
    if (name == QLatin1String("lock")) return drawLock;
    if (name == QLatin1String("folder")) return drawFolder;
    if (name == QLatin1String("copy")) return drawCopy;
    if (name == QLatin1String("diff")) return drawDiff;
    if (name == QLatin1String("compress")) return drawCompress;
    return {};
}

} // namespace

QIcon makeIcon(const QString& name, const QColor& color) {
    const DrawFn draw = lookup(name);
    if (!draw) {
        return QIcon();
    }
    QIcon icon;
    icon.addPixmap(render(color, 20, draw));
    icon.addPixmap(render(color, 40, draw));
    return icon;
}

} // namespace presspng::gui
