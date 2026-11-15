#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include "gui_types.h"
#include "presspng/compressor.h"
#include "presspng/image.h"

namespace presspng::gui {

// Lives on a worker thread. Decodes the source once and reuses it while the
// same file stays selected, then previews, estimates or compresses.
class Worker : public QObject {
    Q_OBJECT
public:
    explicit Worker(QObject* parent = nullptr);

    // Safe to call from the GUI thread; the token is atomic.
    void requestCancel();

public slots:
    void process(presspng::gui::Job job);

signals:
    void progress(quint64 generation, const QString& stage);
    void finished(presspng::gui::JobResult result);

private:
    bool loadSource(const presspng::CompressionRequest& request, QString* error);

    presspng::Compressor compressor_;
    presspng::CancelToken cancel_;
    QString cachedPath_;
    presspng::Image cachedSource_;
    QImage cachedOriginal_;
};

} // namespace presspng::gui
