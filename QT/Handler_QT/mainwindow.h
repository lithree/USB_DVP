#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QByteArray>
#include <QElapsedTimer>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTextEdit>
#include <QTimer>
#include <QWidget>
#include <cstdint>

struct libusb_context;
struct libusb_device_handle;

/* USB constants */
#define VID 0x1A86
#define PID 0x5537
#define EP_OUT 0x03
#define EP_IN 0x84
#define USB_CMD_TIMEOUT_MS 100
#define USB_FRAME_TIMEOUT_MS 120
#define USB_FIRST_PAYLOAD_TIMEOUT_MS 500
#define FRAME_READ_TIMEOUT_RETRIES 6
#define FRAME_RESYNC_MAX_ATTEMPTS 3
#define USB_DRAIN_PACKET_TIMEOUT_MS 5
#define USB_DRAIN_MAX_PACKETS 128

/* Device mode switch request */
#define USB_VENDOR_REQUEST_MODE_SWITCH 0x02
#define USB_REQUEST_TYPE_VENDOR_OUT 0x40
#define DISPLAY_MODE_VIDEO 3

/* Camera frame constants (RGB565 240x125, assembled on host). */
#define IMG_WIDTH 240
#define IMG_HEIGHT 125
#define VIDEO_ROW_HDR_BYTES 7
#define VIDEO_ROW_PACKET_BYTES (VIDEO_ROW_HDR_BYTES + (IMG_WIDTH * 2))
#define VIDEO_FRAME_BYTES (IMG_WIDTH * IMG_HEIGHT * 2)

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
    void drainInEndpoint(int maxPackets, int timeoutMs);
    bool readCameraFrame(QByteArray &frameOut, int *firstByteMs, int *readMs);
    void renderRgb565Frame(const QByteArray &frame);

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
    uint32_t frameCounter = 0;
    uint64_t captureWaitAccumMs = 0;
    uint64_t usbReadAccumMs = 0;
    uint64_t renderAccumMs = 0;
    uint64_t frameLoopAccumMs = 0;
    QByteArray rgb565LeFrame;
    QElapsedTimer perfWindowTimer;
};

#endif // MAINWINDOW_H
