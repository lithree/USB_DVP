#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include "libusb.h"

/* USB constants */
#define VID 0x1A86
#define PID 0x5537
#define EP_OUT 0x03
#define EP_IN 0x84
#define TIMEOUT_MS 3000
#define FRAME_READ_TIMEOUT_RETRIES 4

/* Device mode switch request */
#define USB_VENDOR_REQUEST_MODE_SWITCH 0x02
#define USB_REQUEST_TYPE_VENDOR_OUT 0x40
#define DISPLAY_MODE_VIDEO 3
#define USB_STREAM_CMD_START 0x01
#define USB_STREAM_CMD_STOP  0x02

/* Camera frame constants for the firmware's RGB565 640x480 stream. */
#define IMG_WIDTH 640
#define IMG_HEIGHT 480
#define LINE_BYTES (IMG_WIDTH * 2)
#define VIDEO_FRAME_BYTES (IMG_WIDTH * IMG_HEIGHT * 2)
#define VIDEO_PACKET_HEADER_BYTES 9
#define USB_PACKET_BYTES 512

class MainWindow : public QWidget
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void startCapture();
    void stopCapture();
    void onVideoTick();

private:
    void setupUi();
    void logMessage(const QString &msg);
    bool initUsb();
    bool recoverUsbSession();
    void closeUsb();
    bool sendModeSwitch(uint8_t mode);
    bool sendEp3Command(uint8_t cmd);
    bool readCameraFrame(QByteArray &frameOut, int *firstByteMs, int *readMs);
    void renderRgb565Frame(const QByteArray &frame);
    void captureLoop();
    void appendLogMainThread(const QString &msg);

    QSpinBox *fpsInput = nullptr;
    QPushButton *startBtn = nullptr;
    QPushButton *stopBtn = nullptr;
    QLabel *previewLabel = nullptr;
    QTextEdit *consoleLog = nullptr;
    QTimer *videoTimer = nullptr;

    libusb_context *usbCtx = nullptr;
    libusb_device_handle *usbHandle = nullptr;
    bool interfaceClaimed = false;
    bool isCapturing = false;
    std::atomic<bool> stopRequested = false;
    std::atomic<bool> stopQueued = false;
    std::thread captureThread;
    std::mutex frameMutex;
    QByteArray latestFrame;
    int latestFirstByteMs = 0;
    int latestReadMs = 0;
    bool latestFrameReady = false;
    uint32_t frameCounter = 0;
    uint64_t captureWaitAccumMs = 0;
    uint64_t usbReadAccumMs = 0;
    uint64_t renderAccumMs = 0;
    uint32_t frameLogCounter = 0;
};

#endif // MAINWINDOW_H
