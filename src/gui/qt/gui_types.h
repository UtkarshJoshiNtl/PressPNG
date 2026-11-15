#pragma once

#include <QImage>
#include <QMetaType>
#include <QString>
#include <QtGlobal>

#include <cstdint>

#include "presspng/compression_request.h"

namespace presspng::gui {

// One unit of work for the background thread.
struct Job {
    enum Mode { Preview, Estimate, Compress };

    presspng::CompressionRequest request;
    quint64 generation = 0;
    Mode mode = Preview;
};

// Everything the window needs after a job: the images plus the numbers.
struct JobResult {
    bool ok = false;
    bool cancelled = false;
    quint64 generation = 0;
    Job::Mode mode = Job::Preview;

    QImage original; // untouched source, used for crop editing
    QImage before;   // source with the current transform applied
    QImage after;    // decoded output (empty when not available)

    int sourceWidth = 0;
    int sourceHeight = 0;
    int resultWidth = 0;
    int resultHeight = 0;

    bool targetMet = false;
    std::uint64_t inputBytes = 0;
    std::uint64_t outputBytes = 0;
    int quality = 0;
    double scale = 1.0;
    int elapsedMs = 0;
    double reductionPercent = 0.0;

    QString outputPath;
    QString message;
};

void registerMetaTypes();

} // namespace presspng::gui

Q_DECLARE_METATYPE(presspng::gui::Job)
Q_DECLARE_METATYPE(presspng::gui::JobResult)
