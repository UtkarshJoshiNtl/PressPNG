#pragma once

#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QString>
#include <QThread>

#include <memory>

#include "gui_types.h"
#include "presspng/settings.h"
#include "presspng/transform.h"

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSlider;
class QSpinBox;
class QTimer;
class QToolButton;

namespace presspng::gui {

class PreviewView;
class QtSettingsStore;
class Worker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Opens an image given on the command line or handed over by the OS.
    void openPath(const QString& path);

signals:
    void jobRequested(presspng::gui::Job job);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private slots:
    void openFile();
    void schedulePreview();
    void onCompress();
    void onCancel();
    void onOpenFolder();
    void onCopyPath();
    void chooseOutputFolder();
    void onJobFinished(const presspng::gui::JobResult& result);
    void onProgress(quint64 generation, const QString& stage);

private:
    void buildToolbar();
    QWidget* buildSettingsPanel();
    void buildCentral();
    void buildStatusBar();
    void connectSignals();

    presspng::CompressionRequest currentRequest() const;
    void setSourcePath(const QString& path);
    void setOutputDirectory(const QString& directory);
    void startJob(presspng::gui::Job::Mode mode);
    void setBusy(bool busy);
    void updateResolutionWidgets(const QSize& size);
    void syncResolutionToTransform();
    void rotateBy(bool clockwise);
    void applyCropFromPreview();
    void loadClipboardImage();
    bool adoptImage(const QImage& image);
    void updateInfoLabel(const presspng::gui::JobResult& result);

    QToolButton* openButton_ = nullptr;
    QComboBox* formatCombo_ = nullptr;
    QSpinBox* targetSpin_ = nullptr;
    QSpinBox* widthSpin_ = nullptr;
    QSpinBox* heightSpin_ = nullptr;
    QToolButton* lockButton_ = nullptr;
    QToolButton* rotateLeftButton_ = nullptr;
    QToolButton* rotateRightButton_ = nullptr;
    QToolButton* flipHButton_ = nullptr;
    QToolButton* flipVButton_ = nullptr;
    QToolButton* cropButton_ = nullptr;
    QComboBox* cropAspectCombo_ = nullptr;
    QToolButton* resetCropButton_ = nullptr;
    QToolButton* outputButton_ = nullptr;
    QPushButton* compressButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;

    PreviewView* preview_ = nullptr;
    QToolButton* fitButton_ = nullptr;
    QToolButton* actualButton_ = nullptr;
    QSlider* zoomSlider_ = nullptr;
    QSlider* wipeSlider_ = nullptr;
    QToolButton* diffButton_ = nullptr;

    QLabel* statusLabel_ = nullptr;
    QLabel* infoLabel_ = nullptr;
    QProgressBar* progress_ = nullptr;
    QToolButton* openFolderButton_ = nullptr;
    QToolButton* copyPathButton_ = nullptr;

    QTimer* previewTimer_ = nullptr;

    QString sourcePath_;
    QString outputPath_;
    QString outputDirectory_;
    presspng::TransformSpec transform_;
    QSize sourceSize_;
    QSize previewSize_;
    bool pendingResolutionReset_ = false;
    bool updatingResolution_ = false;
    bool suppressPreview_ = false;
    bool updatingZoom_ = false;
    bool updatingWipe_ = false;

    quint64 generation_ = 0;
    bool busy_ = false;

    QThread workerThread_;
    Worker* worker_ = nullptr;
    std::unique_ptr<QtSettingsStore> settings_;
    presspng::AppSettings appSettings_;
};

} // namespace presspng::gui
