#include "main_window.h"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMimeData>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <cmath>
#include <filesystem>
#include <string>

#include "icons.h"
#include "preview_view.h"
#include "qt_settings_store.h"
#include "worker.h"
#include "presspng/format_bytes.h"

namespace presspng::gui {

namespace {

namespace fs = std::filesystem;

fs::path toPath(const QString& text) {
#ifdef _WIN32
    return fs::path(text.toStdWString());
#else
    return fs::path(text.toStdString());
#endif
}

presspng::UserFormat formatFromIndex(int index) {
    switch (index) {
        case 1: return presspng::UserFormat::Jpeg;
        case 2: return presspng::UserFormat::Png;
        case 3: return presspng::UserFormat::Qoi;
        case 4: return presspng::UserFormat::Ppm;
        default: return presspng::UserFormat::Keep;
    }
}

int indexFromFormat(presspng::UserFormat format) {
    switch (format) {
        case presspng::UserFormat::Jpeg: return 1;
        case presspng::UserFormat::Png:  return 2;
        case presspng::UserFormat::Qoi:  return 3;
        case presspng::UserFormat::Ppm:  return 4;
        case presspng::UserFormat::Keep: return 0;
    }
    return 0;
}

QToolButton* toolButton(const QString& iconName, const QString& tip,
                        const QColor& color, bool checkable = false) {
    auto* button = new QToolButton;
    button->setIcon(makeIcon(iconName, color));
    button->setIconSize(QSize(18, 18));
    button->setToolTip(tip);
    button->setAutoRaise(true);
    button->setCheckable(checkable);
    return button;
}

QToolButton* textButton(const QString& text, const QString& tip) {
    auto* button = new QToolButton;
    button->setText(text);
    button->setToolTip(tip);
    button->setAutoRaise(true);
    return button;
}

QString humanBytes(std::uint64_t bytes) {
    return QString::fromStdString(presspng::formatBytes(bytes));
}

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("PressPNG"));
    setMinimumSize(900, 600);
    resize(1200, 800);
    setAcceptDrops(true);

    settings_ = std::make_unique<QtSettingsStore>();
    appSettings_ = settings_->load();

    buildCentral();
    buildToolbar();
    buildStatusBar();
    connectSignals();

    targetSpin_->setValue(
        qMax(1, QString::fromStdString(appSettings_.targetSizeText).toInt()));
    formatCombo_->setCurrentIndex(indexFromFormat(appSettings_.format));

    worker_ = new Worker;
    worker_->moveToThread(&workerThread_);
    connect(&workerThread_, &QThread::finished, worker_,
            &QObject::deleteLater);
    connect(this, &MainWindow::jobRequested, worker_, &Worker::process);
    connect(worker_, &Worker::finished, this, &MainWindow::onJobFinished);
    connect(worker_, &Worker::progress, this, &MainWindow::onProgress);
    workerThread_.start();

    previewTimer_ = new QTimer(this);
    previewTimer_->setSingleShot(true);
    previewTimer_->setInterval(180);
    connect(previewTimer_, &QTimer::timeout, this, [this] {
        // A compress owns the worker; wait it out instead of queueing behind.
        if (busy_) {
            previewTimer_->start();
            return;
        }
        startJob(Job::Estimate);
    });

    setBusy(false);
    statusLabel_->setText(tr("Drop an image, paste with Ctrl+V, or press Open."));
}

MainWindow::~MainWindow() {
    if (worker_ != nullptr) {
        worker_->requestCancel();
    }
    workerThread_.quit();
    workerThread_.wait();
    if (settings_) {
        settings_->save(appSettings_);
    }
}

void MainWindow::buildToolbar() {
    const QColor text = palette().color(QPalette::ButtonText);

    auto* bar = addToolBar(tr("Main"));
    bar->setMovable(false);
    bar->setFloatable(false);
    bar->setIconSize(QSize(18, 18));
    bar->setToolButtonStyle(Qt::ToolButtonIconOnly);

    openButton_ = toolButton(QStringLiteral("open"), tr("Open an image"),
                             text);
    openButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    openButton_->setText(tr("Open"));
    bar->addWidget(openButton_);
    bar->addSeparator();

    outputButton_ = new QToolButton;
    outputButton_->setText(tr("Same folder"));
    outputButton_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    outputButton_->setIcon(makeIcon(QStringLiteral("folder"), text));
    outputButton_->setIconSize(QSize(18, 18));
    outputButton_->setPopupMode(QToolButton::InstantPopup);
    outputButton_->setToolTip(tr("Where compressed files are written"));
    auto* outputMenu = new QMenu(outputButton_);
    QAction* sameFolder = outputMenu->addAction(tr("Same folder as source"));
    connect(sameFolder, &QAction::triggered, this,
            [this] { setOutputDirectory(QString()); });
    QAction* pickFolder = outputMenu->addAction(tr("Choose folder..."));
    connect(pickFolder, &QAction::triggered, this,
            &MainWindow::chooseOutputFolder);
    outputButton_->setMenu(outputMenu);
    bar->addWidget(outputButton_);

    auto* spacer = new QWidget;
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    bar->addWidget(spacer);

    cancelButton_ = new QPushButton(tr("Cancel"));
    cancelButton_->setVisible(false);
    bar->addWidget(cancelButton_);

    compressButton_ = new QPushButton(tr("Compress"));
    compressButton_->setObjectName(QStringLiteral("primary"));
    bar->addWidget(compressButton_);
}

QWidget* MainWindow::buildSettingsPanel() {
    const QColor text = palette().color(QPalette::ButtonText);
    const int minDim = presspng::Image::kMinDimension;

    auto* panel = new QWidget;
    panel->setObjectName(QStringLiteral("settingsPanel"));
    panel->setFixedWidth(268);

    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(16, 14, 16, 14);
    layout->setSpacing(7);

    auto caption = [](const QString& title) {
        auto* label = new QLabel(title);
        label->setObjectName(QStringLiteral("caption"));
        return static_cast<QWidget*>(label);
    };
    auto row = [] {
        auto* holder = new QWidget;
        auto* box = new QHBoxLayout(holder);
        box->setContentsMargins(0, 0, 0, 0);
        box->setSpacing(6);
        return holder;
    };

    layout->addWidget(caption(tr("OUTPUT")));

    formatCombo_ = new QComboBox;
    formatCombo_->addItems(
        {tr("Keep format"), tr("JPEG"), tr("PNG"), tr("QOI"), tr("PPM")});
    formatCombo_->setToolTip(tr("Output format"));
    layout->addWidget(formatCombo_);

    targetSpin_ = new QSpinBox;
    targetSpin_->setRange(1, 1000000);
    targetSpin_->setSuffix(tr(" KB"));
    targetSpin_->setToolTip(tr("Target file size"));
    layout->addWidget(targetSpin_);

    layout->addSpacing(9);
    layout->addWidget(caption(tr("SIZE")));

    QWidget* sizeRow = row();
    auto* sizeLayout = static_cast<QHBoxLayout*>(sizeRow->layout());
    widthSpin_ = new QSpinBox;
    widthSpin_->setRange(minDim, 100000);
    widthSpin_->setToolTip(tr("Output width"));
    heightSpin_ = new QSpinBox;
    heightSpin_->setRange(minDim, 100000);
    heightSpin_->setToolTip(tr("Output height"));
    lockButton_ = toolButton(QStringLiteral("lock"),
                             tr("Keep the aspect ratio"), text, true);
    lockButton_->setChecked(true);
    sizeLayout->addWidget(widthSpin_, 1);
    sizeLayout->addWidget(new QLabel(QStringLiteral("\u00d7")));
    sizeLayout->addWidget(heightSpin_, 1);
    sizeLayout->addWidget(lockButton_);
    layout->addWidget(sizeRow);

    layout->addSpacing(9);
    layout->addWidget(caption(tr("TRANSFORM")));

    QWidget* rotateRow = row();
    auto* rotateLayout = static_cast<QHBoxLayout*>(rotateRow->layout());
    rotateLeftButton_ =
        toolButton(QStringLiteral("rotateLeft"), tr("Rotate left"), text);
    rotateRightButton_ =
        toolButton(QStringLiteral("rotateRight"), tr("Rotate right"), text);
    flipHButton_ = toolButton(QStringLiteral("flipH"),
                              tr("Flip horizontally"), text, true);
    flipVButton_ = toolButton(QStringLiteral("flipV"), tr("Flip vertically"),
                              text, true);
    rotateLayout->addWidget(rotateLeftButton_);
    rotateLayout->addWidget(rotateRightButton_);
    rotateLayout->addWidget(flipHButton_);
    rotateLayout->addWidget(flipVButton_);
    rotateLayout->addStretch(1);
    layout->addWidget(rotateRow);

    QWidget* cropRow = row();
    auto* cropLayout = static_cast<QHBoxLayout*>(cropRow->layout());
    cropButton_ = toolButton(QStringLiteral("crop"), tr("Crop"), text, true);
    cropAspectCombo_ = new QComboBox;
    cropAspectCombo_->addItem(tr("Free"), 0.0);
    cropAspectCombo_->addItem(QStringLiteral("1:1"), 1.0);
    cropAspectCombo_->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
    cropAspectCombo_->addItem(QStringLiteral("3:2"), 3.0 / 2.0);
    cropAspectCombo_->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    cropAspectCombo_->setToolTip(tr("Crop aspect ratio"));
    cropAspectCombo_->setEnabled(false);
    resetCropButton_ = textButton(tr("Reset"), tr("Reset the crop"));
    cropLayout->addWidget(cropButton_);
    cropLayout->addWidget(cropAspectCombo_, 1);
    cropLayout->addWidget(resetCropButton_);
    layout->addWidget(cropRow);

    layout->addStretch(1);
    return panel;
}

void MainWindow::buildCentral() {
    const QColor text = palette().color(QPalette::ButtonText);

    auto* central = new QWidget;
    auto* root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(buildSettingsPanel());

    auto* viewer = new QWidget;
    auto* viewerLayout = new QVBoxLayout(viewer);
    viewerLayout->setContentsMargins(0, 0, 0, 0);
    viewerLayout->setSpacing(0);

    preview_ = new PreviewView;
    viewerLayout->addWidget(preview_, 1);

    auto* strip = new QWidget;
    strip->setObjectName(QStringLiteral("previewStrip"));
    auto* stripLayout = new QHBoxLayout(strip);
    stripLayout->setContentsMargins(10, 6, 10, 8);
    stripLayout->setSpacing(8);

    fitButton_ = textButton(tr("Fit"), tr("Fit the image to the window"));
    actualButton_ = textButton(tr("1:1"), tr("Show at 100%"));

    zoomSlider_ = new QSlider(Qt::Horizontal);
    zoomSlider_->setRange(5, 1600);
    zoomSlider_->setValue(100);
    zoomSlider_->setToolTip(tr("Zoom"));

    diffButton_ = toolButton(QStringLiteral("diff"),
                             tr("Show a difference heatmap"), text, true);

    wipeSlider_ = new QSlider(Qt::Horizontal);
    wipeSlider_->setRange(0, 1000);
    wipeSlider_->setValue(500);
    wipeSlider_->setToolTip(tr("Drag to compare before and after"));

    stripLayout->addWidget(fitButton_);
    stripLayout->addWidget(actualButton_);
    stripLayout->addWidget(zoomSlider_, 1);
    stripLayout->addWidget(diffButton_);
    stripLayout->addWidget(wipeSlider_, 1);

    viewerLayout->addWidget(strip);
    root->addWidget(viewer, 1);
    setCentralWidget(central);
}

void MainWindow::buildStatusBar() {
    const QColor text = palette().color(QPalette::ButtonText);

    statusLabel_ = new QLabel;
    infoLabel_ = new QLabel;

    progress_ = new QProgressBar;
    progress_->setRange(0, 0);
    progress_->setTextVisible(false);
    progress_->setFixedWidth(140);
    progress_->setVisible(false);

    openFolderButton_ =
        toolButton(QStringLiteral("folder"), tr("Open the output folder"),
                   text);
    copyPathButton_ = toolButton(QStringLiteral("copy"),
                                 tr("Copy the output path"), text);
    openFolderButton_->setEnabled(false);
    copyPathButton_->setEnabled(false);

    statusBar()->setSizeGripEnabled(true);
    statusBar()->addWidget(statusLabel_, 1);
    statusBar()->addPermanentWidget(infoLabel_);
    statusBar()->addPermanentWidget(progress_);
    statusBar()->addPermanentWidget(openFolderButton_);
    statusBar()->addPermanentWidget(copyPathButton_);
}

void MainWindow::connectSignals() {
    connect(openButton_, &QToolButton::clicked, this, &MainWindow::openFile);
    connect(compressButton_, &QPushButton::clicked, this,
            &MainWindow::onCompress);
    connect(cancelButton_, &QPushButton::clicked, this, &MainWindow::onCancel);
    connect(openFolderButton_, &QToolButton::clicked, this,
            &MainWindow::onOpenFolder);
    connect(copyPathButton_, &QToolButton::clicked, this,
            &MainWindow::onCopyPath);

    connect(formatCombo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int index) {
                appSettings_.format = formatFromIndex(index);
                schedulePreview();
            });
    connect(targetSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                appSettings_.targetSizeText =
                    QString::number(value).toStdString();
                schedulePreview();
            });
    connect(widthSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (updatingResolution_) {
                    return;
                }
                if (lockButton_->isChecked() && previewSize_.width() > 0) {
                    const double ratio = static_cast<double>(
                                             previewSize_.height()) /
                                         previewSize_.width();
                    updatingResolution_ = true;
                    heightSpin_->setValue(qBound(
                        heightSpin_->minimum(),
                        static_cast<int>(std::lround(value * ratio)),
                        heightSpin_->maximum()));
                    updatingResolution_ = false;
                }
                syncResolutionToTransform();
                schedulePreview();
            });
    connect(heightSpin_, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this](int value) {
                if (updatingResolution_) {
                    return;
                }
                if (lockButton_->isChecked() && previewSize_.height() > 0) {
                    const double ratio = static_cast<double>(
                                             previewSize_.width()) /
                                         previewSize_.height();
                    updatingResolution_ = true;
                    widthSpin_->setValue(qBound(
                        widthSpin_->minimum(),
                        static_cast<int>(std::lround(value * ratio)),
                        widthSpin_->maximum()));
                    updatingResolution_ = false;
                }
                syncResolutionToTransform();
                schedulePreview();
            });

    connect(rotateLeftButton_, &QToolButton::clicked, this,
            [this] { rotateBy(false); });
    connect(rotateRightButton_, &QToolButton::clicked, this,
            [this] { rotateBy(true); });
    connect(flipHButton_, &QToolButton::toggled, this, [this](bool on) {
        transform_.flipH = on;
        schedulePreview();
    });
    connect(flipVButton_, &QToolButton::toggled, this, [this](bool on) {
        transform_.flipV = on;
        schedulePreview();
    });

    connect(cropButton_, &QToolButton::toggled, this, [this](bool on) {
        preview_->setCropEnabled(on);
        cropAspectCombo_->setEnabled(on);
        if (on) {
            preview_->setCropAspect(cropAspectCombo_->currentData().toDouble());
        }
    });
    connect(cropAspectCombo_,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this](int) {
                preview_->setCropAspect(
                    cropAspectCombo_->currentData().toDouble());
            });
    connect(resetCropButton_, &QToolButton::clicked, this,
            [this] { preview_->resetCrop(); });
    connect(preview_, &PreviewView::cropChanged, this,
            &MainWindow::applyCropFromPreview);

    connect(fitButton_, &QToolButton::clicked, this,
            [this] { preview_->zoomToFit(); });
    connect(actualButton_, &QToolButton::clicked, this,
            [this] { preview_->setZoom(1.0); });
    connect(zoomSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (updatingZoom_) {
            return;
        }
        preview_->setZoom(value / 100.0);
    });
    connect(preview_, &PreviewView::zoomChanged, this, [this](double factor) {
        updatingZoom_ = true;
        zoomSlider_->setValue(qBound(zoomSlider_->minimum(),
                                     static_cast<int>(std::lround(factor * 100)),
                                     zoomSlider_->maximum()));
        updatingZoom_ = false;
    });

    connect(diffButton_, &QToolButton::toggled, this,
            [this](bool on) { preview_->setDiffEnabled(on); });
    connect(wipeSlider_, &QSlider::valueChanged, this, [this](int value) {
        if (updatingWipe_) {
            return;
        }
        preview_->setWipe(value / 1000.0);
    });
    connect(preview_, &PreviewView::wipeChanged, this, [this](double fraction) {
        updatingWipe_ = true;
        wipeSlider_->setValue(static_cast<int>(std::lround(fraction * 1000)));
        updatingWipe_ = false;
    });
}

presspng::CompressionRequest MainWindow::currentRequest() const {
    presspng::CompressionRequest request;
    request.input = toPath(sourcePath_);
    if (!outputDirectory_.isEmpty()) {
        request.outputDirectory = toPath(outputDirectory_);
    }
    request.targetBytes =
        static_cast<std::uint64_t>(targetSpin_->value()) * 1000ULL;
    request.format = formatFromIndex(formatCombo_->currentIndex());
    request.transform = transform_;
    return request;
}

void MainWindow::setSourcePath(const QString& path) {
    suppressPreview_ = true;

    sourcePath_ = path;
    outputPath_.clear();
    sourceSize_ = QSize();
    previewSize_ = QSize();
    transform_ = presspng::TransformSpec{};
    pendingResolutionReset_ = true;

    updatingResolution_ = true;
    widthSpin_->setRange(presspng::Image::kMinDimension, 100000);
    heightSpin_->setRange(presspng::Image::kMinDimension, 100000);
    widthSpin_->setValue(presspng::Image::kMinDimension);
    heightSpin_->setValue(presspng::Image::kMinDimension);
    updatingResolution_ = false;

    cropButton_->setChecked(false);
    flipHButton_->setChecked(false);
    flipVButton_->setChecked(false);
    preview_->clearAll();
    preview_->resetCrop();

    openFolderButton_->setEnabled(false);
    copyPathButton_->setEnabled(false);
    infoLabel_->clear();

    const QFileInfo info(path);
    appSettings_.lastDirectory = info.absolutePath().toStdString();
    setWindowTitle(tr("%1 - PressPNG").arg(info.fileName()));
    statusLabel_->setText(tr("Loading %1...").arg(info.fileName()));

    suppressPreview_ = false;
    startJob(Job::Estimate);
}

void MainWindow::setOutputDirectory(const QString& directory) {
    outputDirectory_ = directory;
    if (outputButton_ != nullptr) {
        outputButton_->setText(directory.isEmpty()
                                   ? tr("Same folder")
                                   : QDir(directory).dirName());
    }
}

void MainWindow::startJob(Job::Mode mode) {
    if (sourcePath_.isEmpty() || worker_ == nullptr) {
        return;
    }
    Job job;
    job.request = currentRequest();
    job.generation = ++generation_;
    job.mode = mode;
    emit jobRequested(job);
}

void MainWindow::schedulePreview() {
    if (suppressPreview_ || sourcePath_.isEmpty() || busy_ ||
        previewTimer_ == nullptr) {
        return;
    }
    previewTimer_->start();
}

void MainWindow::onCompress() {
    if (sourcePath_.isEmpty() || busy_) {
        return;
    }
    setBusy(true);
    statusLabel_->setText(tr("Compressing..."));
    startJob(Job::Compress);
}

void MainWindow::onCancel() {
    if (!busy_ || worker_ == nullptr) {
        return;
    }
    worker_->requestCancel();
    statusLabel_->setText(tr("Cancelling..."));
}

void MainWindow::onOpenFolder() {
    if (outputPath_.isEmpty()) {
        return;
    }
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QFileInfo(outputPath_).absolutePath()));
}

void MainWindow::onCopyPath() {
    if (outputPath_.isEmpty()) {
        return;
    }
    QApplication::clipboard()->setText(outputPath_);
    statusLabel_->setText(tr("Output path copied."));
}

void MainWindow::chooseOutputFolder() {
    const QString directory = QFileDialog::getExistingDirectory(
        this, tr("Choose output folder"),
        QString::fromStdString(appSettings_.lastDirectory));
    if (directory.isEmpty()) {
        return;
    }
    setOutputDirectory(directory);
}

void MainWindow::setBusy(bool busy) {
    busy_ = busy;
    if (compressButton_ != nullptr) {
        compressButton_->setEnabled(!busy);
    }
    if (cancelButton_ != nullptr) {
        cancelButton_->setVisible(busy);
    }
    if (progress_ != nullptr) {
        progress_->setVisible(busy);
    }
}

void MainWindow::syncResolutionToTransform() {
    if (previewSize_.isEmpty()) {
        return;
    }
    const bool full = widthSpin_->value() >= previewSize_.width() &&
                      heightSpin_->value() >= previewSize_.height();
    transform_.outputWidth = full ? 0 : widthSpin_->value();
    transform_.outputHeight = full ? 0 : heightSpin_->value();
}

void MainWindow::updateResolutionWidgets(const QSize& size) {
    if (size.isEmpty()) {
        return;
    }
    const int minDim = presspng::Image::kMinDimension;
    updatingResolution_ = true;
    widthSpin_->setRange(minDim, qMax(minDim, size.width()));
    heightSpin_->setRange(minDim, qMax(minDim, size.height()));

    const int wantedWidth =
        (pendingResolutionReset_ || transform_.outputWidth <= 0)
            ? size.width()
            : transform_.outputWidth;
    const int wantedHeight =
        (pendingResolutionReset_ || transform_.outputHeight <= 0)
            ? size.height()
            : transform_.outputHeight;
    widthSpin_->setValue(
        qBound(widthSpin_->minimum(), wantedWidth, widthSpin_->maximum()));
    heightSpin_->setValue(
        qBound(heightSpin_->minimum(), wantedHeight, heightSpin_->maximum()));
    updatingResolution_ = false;
}

void MainWindow::rotateBy(bool clockwise) {
    const int current = static_cast<int>(transform_.rotate);
    const int next = (current + (clockwise ? 1 : 3)) % 4;
    transform_.rotate = static_cast<presspng::Rotate>(next);
    schedulePreview();
}

void MainWindow::applyCropFromPreview() {
    const QRectF rect = preview_->cropRect();
    transform_.cropX = rect.left();
    transform_.cropY = rect.top();
    transform_.cropW = rect.width();
    transform_.cropH = rect.height();
    schedulePreview();
}

bool MainWindow::adoptImage(const QImage& image) {
    if (image.isNull()) {
        return false;
    }
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    const QString path =
        QDir(directory).filePath(QStringLiteral("presspng-paste-%1.png")
                                     .arg(QDateTime::currentMSecsSinceEpoch()));
    if (!image.save(path, "PNG")) {
        statusLabel_->setText(tr("Could not store the pasted image."));
        return false;
    }
    setSourcePath(path);
    return true;
}

void MainWindow::loadClipboardImage() {
    const QClipboard* clipboard = QApplication::clipboard();
    if (adoptImage(clipboard->image())) {
        return;
    }
    const QMimeData* mime = clipboard->mimeData();
    if (mime != nullptr && mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                setSourcePath(url.toLocalFile());
                return;
            }
        }
    }
    statusLabel_->setText(tr("The clipboard has no image."));
}

void MainWindow::updateInfoLabel(const JobResult& result) {
    if (result.inputBytes == 0 && result.outputBytes == 0) {
        infoLabel_->clear();
        return;
    }
    QString text = tr("%1 \u2192 %2")
                       .arg(humanBytes(result.inputBytes),
                            humanBytes(result.outputBytes));
    if (result.inputBytes > 0 && result.outputBytes > 0 &&
        result.outputBytes < result.inputBytes) {
        const double saved =
            100.0 *
            (1.0 - static_cast<double>(result.outputBytes) /
                       static_cast<double>(result.inputBytes));
        text += tr("   -%1%").arg(std::lround(saved));
    }
    if (result.resultWidth > 0) {
        text += tr("   %1\u00d7%2")
                    .arg(result.resultWidth)
                    .arg(result.resultHeight);
    }
    if (result.quality > 0) {
        text += tr("   q%1").arg(result.quality);
    }
    if (!result.targetMet && result.mode != Job::Preview) {
        text += tr("   target missed");
    }
    infoLabel_->setText(text);
}

void MainWindow::onProgress(quint64 generation, const QString& stage) {
    if (generation != generation_) {
        return;
    }
    statusLabel_->setText(stage);
}

void MainWindow::onJobFinished(const JobResult& result) {
    if (result.generation != generation_) {
        return;
    }

    if (result.cancelled) {
        setBusy(false);
        statusLabel_->setText(tr("Cancelled."));
        return;
    }
    if (!result.ok) {
        setBusy(false);
        statusLabel_->setText(result.message.isEmpty()
                                  ? tr("Could not process the image.")
                                  : result.message);
        return;
    }

    if (!result.original.isNull()) {
        preview_->setOriginal(result.original);
        sourceSize_ = result.original.size();
    }
    if (!result.before.isNull()) {
        preview_->setBefore(result.before);
        previewSize_ = result.before.size();

        // Rotation or cropping can shrink the ceiling under a size the user
        // already picked; fall back to the natural size instead of clipping.
        bool ceilingMoved = false;
        if (transform_.outputWidth > previewSize_.width()) {
            transform_.outputWidth = 0;
            ceilingMoved = true;
        }
        if (transform_.outputHeight > previewSize_.height()) {
            transform_.outputHeight = 0;
            ceilingMoved = true;
        }
        updateResolutionWidgets(previewSize_);
        pendingResolutionReset_ = false;
        if (ceilingMoved) {
            schedulePreview();
        }
    }
    if (!result.after.isNull()) {
        preview_->setAfter(result.after);
    } else {
        preview_->clearAfter();
    }

    if (!result.message.isEmpty()) {
        statusLabel_->setText(result.message);
    }
    updateInfoLabel(result);

    if (result.mode == Job::Compress) {
        setBusy(false);
        if (!result.outputPath.isEmpty()) {
            outputPath_ = result.outputPath;
            openFolderButton_->setEnabled(true);
            copyPathButton_->setEnabled(true);
        }
    }
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (event->mimeData()->hasImage() || event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent* event) {
    const QMimeData* mime = event->mimeData();
    if (mime->hasUrls()) {
        for (const QUrl& url : mime->urls()) {
            if (url.isLocalFile()) {
                setSourcePath(url.toLocalFile());
                event->acceptProposedAction();
                return;
            }
        }
    }
    if (adoptImage(qvariant_cast<QImage>(mime->imageData()))) {
        event->acceptProposedAction();
    }
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->matches(QKeySequence::Open)) {
        openFile();
        return;
    }
    if (event->matches(QKeySequence::Paste)) {
        loadClipboardImage();
        return;
    }
    if (event->key() == Qt::Key_Escape && busy_) {
        onCancel();
        return;
    }
    if (event->key() == Qt::Key_Return &&
        event->modifiers().testFlag(Qt::ControlModifier)) {
        onCompress();
        return;
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::openPath(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    setSourcePath(path);
}

void MainWindow::openFile() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open image"),
        QString::fromStdString(appSettings_.lastDirectory),
        tr("Images (*.png *.jpg *.jpeg *.bmp *.tga *.qoi *.gif *.psd *.hdr "
           "*.pnm);;All files (*)"));
    if (!path.isEmpty()) {
        setSourcePath(path);
    }
}

} // namespace presspng::gui
