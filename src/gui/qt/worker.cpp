#include "worker.h"

#include <string>

#include "image_utils.h"
#include "presspng/image_loader.h"
#include "presspng/transform.h"

namespace presspng::gui {

Worker::Worker(QObject* parent) : QObject(parent) {}

void Worker::requestCancel() {
    cancel_.cancel();
}

bool Worker::loadSource(const presspng::CompressionRequest& request,
                        QString* error) {
    const QString path = QString::fromStdString(request.input.u8string());
    if (!cachedSource_.empty() && path == cachedPath_) {
        return true;
    }
    try {
        cachedSource_ = presspng::ImageLoader::decode(
            presspng::ImageLoader::readFile(request.input));
        cachedOriginal_ = toQImage(cachedSource_);
        cachedPath_ = path;
        return true;
    } catch (const std::exception& failure) {
        cachedSource_ = presspng::Image();
        cachedOriginal_ = QImage();
        cachedPath_.clear();
        *error = QString::fromStdString(failure.what());
        return false;
    }
}

void Worker::process(presspng::gui::Job job) {
    JobResult out;
    out.generation = job.generation;
    out.mode = job.mode;

    QString error;
    if (!loadSource(job.request, &error)) {
        out.message = error;
        emit finished(out);
        return;
    }

    out.sourceWidth = cachedSource_.width();
    out.sourceHeight = cachedSource_.height();
    out.original = cachedOriginal_;

    cancel_.reset();

    // "Before" is the geometry the user asked for, before the resolution
    // ceiling and before compression, so the wipe compares like with like
    // and the resolution spin can be capped by the real editable size.
    presspng::TransformSpec geometry = job.request.transform;
    geometry.outputWidth = 0;
    geometry.outputHeight = 0;

    try {
        presspng::Image transformed =
            presspng::applyTransform(cachedSource_, geometry);
        out.before = toQImage(transformed);
    } catch (const std::exception& failure) {
        out.message = QString::fromStdString(failure.what());
        emit finished(out);
        return;
    }

    if (job.mode == Job::Compress) {
        const presspng::CompressionResult result = compressor_.compress(
            job.request,
            [this, gen = job.generation](const std::string& stage) {
                emit progress(gen, QString::fromStdString(stage));
            },
            &cancel_);

        out.ok = result.ok;
        out.cancelled = result.cancelled;
        out.targetMet = result.targetMet;
        out.inputBytes = result.inputBytes;
        out.outputBytes = result.outputBytes;
        out.quality = result.quality;
        out.scale = result.scale;
        out.elapsedMs = result.elapsedMs;
        out.reductionPercent = result.reductionPercent;
        out.outputPath = QString::fromStdString(result.outputPath.u8string());
        out.message = QString::fromStdString(result.message);

        if (result.ok) {
            try {
                presspng::Image image = presspng::ImageLoader::decode(
                    presspng::ImageLoader::readFile(result.outputPath));
                out.after = toQImage(image);
                out.resultWidth = image.width();
                out.resultHeight = image.height();
            } catch (const std::exception&) {
                // A result that decodes badly is still a written file.
            }
        }
    } else if (job.mode == Job::Estimate) {
        const presspng::EstimateResult estimate =
            compressor_.estimate(cachedSource_, job.request, &cancel_);

        out.ok = estimate.ok;
        out.targetMet = estimate.targetMet;
        out.inputBytes = estimate.inputBytes;
        out.outputBytes = estimate.outputBytes;
        out.quality = estimate.quality;
        out.scale = estimate.scale;
        out.message = QString::fromStdString(estimate.message);

        if (estimate.ok && !estimate.bytes.empty()) {
            try {
                presspng::Image image =
                    presspng::ImageLoader::decode(estimate.bytes);
                out.after = toQImage(image);
                out.resultWidth = image.width();
                out.resultHeight = image.height();
            } catch (const std::exception&) {
                // Preview only; the estimate itself was fine.
            }
        }
    } else {
        out.ok = true;
    }

    emit finished(out);
}

} // namespace presspng::gui
