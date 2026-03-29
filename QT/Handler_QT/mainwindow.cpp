#include "mainwindow.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMetaObject>
#include <QPixmap>
#include <QThread>
#include <QVBoxLayout>
#include <algorithm>
#include <cstring>
#include <QStringList>
#include <vector>

MainWindow::MainWindow(QWidget *parent)
    : QWidget(parent), videoTimer(new QTimer(this))
{
    setupUi();
    isCapturing = false;
    frameCounter = 0;
    usbCtx = nullptr;
    usbHandle = nullptr;
    interfaceClaimed = false;
    connect(videoTimer, &QTimer::timeout, this, &MainWindow::onVideoTick);
}

MainWindow::~MainWindow()
{
    stopCapture();
}

void MainWindow::setupUi()
{
    setWindowTitle("USBHS Camera Viewer");
    resize(680, 420);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QGroupBox *settingsGroup = new QGroupBox("Capture Settings");
    QVBoxLayout *settingsLayout = new QVBoxLayout();
    QHBoxLayout *settingsRow = new QHBoxLayout();
    settingsRow->addWidget(new QLabel("Poll FPS:"));
    fpsInput = new QSpinBox();
    fpsInput->setRange(1, 120);
    fpsInput->setValue(30);
    settingsRow->addWidget(fpsInput);
    settingsRow->addStretch();
    settingsLayout->addLayout(settingsRow);
    settingsGroup->setLayout(settingsLayout);
    mainLayout->addWidget(settingsGroup);

    QHBoxLayout *controlRow = new QHBoxLayout();
    startBtn = new QPushButton("Start USB Capture");
    stopBtn = new QPushButton("Stop");
    stopBtn->setEnabled(false);
    controlRow->addWidget(startBtn);
    controlRow->addWidget(stopBtn);
    mainLayout->addLayout(controlRow);

    previewLabel = new QLabel("No frame yet");
    previewLabel->setMinimumSize(IMG_WIDTH * 2, IMG_HEIGHT * 2);
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("QLabel { background: #111; color: #bbb; border: 1px solid #333; }");
    mainLayout->addWidget(previewLabel);

    QGroupBox *logGroup = new QGroupBox("Console");
    QVBoxLayout *logLayout = new QVBoxLayout();
    consoleLog = new QTextEdit();
    consoleLog->setReadOnly(true);
    logLayout->addWidget(consoleLog);
    logGroup->setLayout(logLayout);
    mainLayout->addWidget(logGroup);

    connect(startBtn, &QPushButton::clicked, this, &MainWindow::startCapture);
    connect(stopBtn, &QPushButton::clicked, this, &MainWindow::stopCapture);
}

void MainWindow::logMessage(const QString &msg)
{
    const QString ts = QDateTime::currentDateTime().toString("hh:mm:ss");
    consoleLog->append(QString("[%1] %2").arg(ts, msg));
}

void MainWindow::appendLogMainThread(const QString &msg)
{
    if (QThread::currentThread() == thread())
    {
        logMessage(msg);
        return;
    }

    QMetaObject::invokeMethod(this, [this, msg]() {
        logMessage(msg);
    }, Qt::QueuedConnection);
}

bool MainWindow::initUsb()
{
    int res = libusb_init(&usbCtx);
    if (res < 0)
    {
        logMessage(QString("libusb_init failed: %1").arg(libusb_error_name(res)));
        return false;
    }

    usbHandle = libusb_open_device_with_vid_pid(usbCtx, VID, PID);
    if (!usbHandle)
    {
        logMessage("USB device not found. Check VID/PID and Drivers.");
        libusb_exit(usbCtx);
        usbCtx = nullptr;
        return false;
    }

    libusb_set_auto_detach_kernel_driver(usbHandle, 1);
    res = libusb_claim_interface(usbHandle, 0);
    if (res < 0)
    {
        logMessage(QString("Failed to claim interface: %1").arg(libusb_error_name(res)));
        libusb_close(usbHandle);
        usbHandle = nullptr;
        libusb_exit(usbCtx);
        usbCtx = nullptr;
        return false;
    }

    interfaceClaimed = true;
    logMessage("USB connected.");
    return true;
}

void MainWindow::closeUsb()
{
    if (interfaceClaimed && usbHandle)
    {
        libusb_release_interface(usbHandle, 0);
        interfaceClaimed = false;
    }

    if (usbHandle)
    {
        libusb_close(usbHandle);
        usbHandle = nullptr;
    }

    if (usbCtx)
    {
        libusb_exit(usbCtx);
        usbCtx = nullptr;
    }
}

bool MainWindow::recoverUsbSession()
{
    logMessage("USB device lost. Attempting reconnect...");
    closeUsb();

    if (!initUsb())
        return false;

    if (!sendModeSwitch(DISPLAY_MODE_VIDEO))
        return false;

    if (!sendEp3Command(USB_STREAM_CMD_START))
        return false;

    logMessage("USB reconnect succeeded.");
    return true;
}

bool MainWindow::sendModeSwitch(uint8_t mode)
{
    if (!interfaceClaimed || !usbHandle)
        return false;

    int res = libusb_control_transfer(
        usbHandle,
        USB_REQUEST_TYPE_VENDOR_OUT,
        USB_VENDOR_REQUEST_MODE_SWITCH,
        mode,
        0,
        nullptr,
        0,
        TIMEOUT_MS);

    if (res < 0)
    {
        logMessage(QString("Mode switch failed: %1").arg(libusb_error_name(res)));
        return false;
    }
    return true;
}

bool MainWindow::sendEp3Command(uint8_t cmd)
{
    if (!interfaceClaimed || !usbHandle)
        return false;

    int transferred = 0;
    unsigned char payload[1] = {cmd};
    int res = libusb_bulk_transfer(
        usbHandle,
        EP_OUT,
        payload,
        1,
        &transferred,
        TIMEOUT_MS);

    if (res < 0 || transferred != 1)
    {
        logMessage(QString("EP3 command send failed: %1").arg(libusb_error_name(res)));
        return false;
    }

    return true;
}

bool MainWindow::readCameraFrame(QByteArray &frameOut, int *firstByteMs, int *readMs)
{
    if (!interfaceClaimed || !usbHandle)
        return false;

    frameOut.resize(VIDEO_FRAME_BYTES);
    frameOut.fill(0);

    QElapsedTimer readTimer;
    readTimer.start();
    bool firstPayloadSeen = false;
    QByteArray packet(USB_PACKET_BYTES, 0);
    std::vector<int> rowBytes(IMG_HEIGHT, 0);
    std::vector<QStringList> rowChunkLogs(3);
    int packetCount = 0;
    int shortPacketCount = 0;
    int frameSwitchCount = 0;
    int restartCount = 0;
    int anomalyLogCount = 0;

    if (firstByteMs)
        *firstByteMs = -1;
    if (readMs)
        *readMs = 0;

    int assembledRows = 0;
    int targetFrameId = -1;
    int timeoutRetries = 0;
    bool haltRecovered = false;
    bool deviceRecovered = false;
    bool frameRestarted = false;
    while (assembledRows < IMG_HEIGHT)
    {
        int transferred = 0;
        int res = libusb_bulk_transfer(
            usbHandle,
            EP_IN,
            reinterpret_cast<unsigned char *>(packet.data()),
            USB_PACKET_BYTES,
            &transferred,
            TIMEOUT_MS);

        if (res < 0)
        {
            if ((res == LIBUSB_ERROR_NO_DEVICE) && !deviceRecovered)
            {
                if (!recoverUsbSession())
                {
                    appendLogMainThread("USB reconnect failed.");
                    return false;
                }

                assembledRows = 0;
                std::fill(rowBytes.begin(), rowBytes.end(), 0);
                targetFrameId = -1;
                timeoutRetries = 0;
                haltRecovered = false;
                deviceRecovered = true;
                restartCount++;
                if (anomalyLogCount < 5)
                {
                    appendLogMainThread("readCameraFrame: device reconnect, frame assembly reset.");
                    anomalyLogCount++;
                }
                continue;
            }

            if ((res == LIBUSB_ERROR_PIPE) && !haltRecovered)
            {
                libusb_clear_halt(usbHandle, EP_IN);
                sendEp3Command(USB_STREAM_CMD_START);
                haltRecovered = true;
                continue;
            }

            if ((res == LIBUSB_ERROR_TIMEOUT) && !frameRestarted)
            {
                sendEp3Command(USB_STREAM_CMD_STOP);
                if (!sendEp3Command(USB_STREAM_CMD_START))
                {
                    appendLogMainThread("Frame restart request failed.");
                    return false;
                }

                assembledRows = 0;
                std::fill(rowBytes.begin(), rowBytes.end(), 0);
                targetFrameId = -1;
                timeoutRetries = 0;
                haltRecovered = false;
                deviceRecovered = false;
                frameRestarted = true;
                restartCount++;
                if (anomalyLogCount < 5)
                {
                    appendLogMainThread(QString("readCameraFrame: timeout, restart stream at rows=%1 packets=%2")
                                   .arg(assembledRows)
                                   .arg(packetCount));
                    anomalyLogCount++;
                }
                continue;
            }

            if (res == LIBUSB_ERROR_TIMEOUT && timeoutRetries < FRAME_READ_TIMEOUT_RETRIES)
            {
                timeoutRetries++;
                continue;
            }

            appendLogMainThread(QString("USB IN transfer failed: %1").arg(libusb_error_name(res)));
            return false;
        }

        frameRestarted = false;
        packetCount++;

        if (transferred <= 0)
        {
            if (timeoutRetries < FRAME_READ_TIMEOUT_RETRIES)
            {
                timeoutRetries++;
                continue;
            }

            appendLogMainThread("USB IN transfer timeout.");
            return false;
        }

        if (!firstPayloadSeen)
        {
            firstPayloadSeen = true;
            if (firstByteMs)
                *firstByteMs = static_cast<int>(readTimer.elapsed());
        }

        if (transferred <= VIDEO_PACKET_HEADER_BYTES)
        {
            shortPacketCount++;
            sendEp3Command(USB_STREAM_CMD_STOP);
            if (!sendEp3Command(USB_STREAM_CMD_START))
            {
                appendLogMainThread("Short USB packet during frame receive.");
                return false;
            }

            if (anomalyLogCount < 5)
            {
                appendLogMainThread(QString("Short packet: len=%1 rows=%2 packets=%3")
                               .arg(transferred)
                               .arg(assembledRows)
                               .arg(packetCount));
                anomalyLogCount++;
            }

            assembledRows = 0;
            std::fill(rowBytes.begin(), rowBytes.end(), 0);
            targetFrameId = -1;
            timeoutRetries = 0;
            continue;
        }

        const unsigned char *src = reinterpret_cast<const unsigned char *>(packet.constData());
        if (src[0] != 'O' || src[1] != 'V' || src[2] != '2' || src[3] != '6')
        {
            sendEp3Command(USB_STREAM_CMD_STOP);
            if (!sendEp3Command(USB_STREAM_CMD_START))
            {
                appendLogMainThread("USB stream sync lost.");
                return false;
            }

            if (anomalyLogCount < 5)
            {
                appendLogMainThread(QString("Sync lost: hdr=%1 %2 %3 %4 len=%5 rows=%6 packets=%7")
                               .arg(static_cast<unsigned int>(src[0]), 2, 16, QLatin1Char('0'))
                               .arg(static_cast<unsigned int>(src[1]), 2, 16, QLatin1Char('0'))
                               .arg(static_cast<unsigned int>(src[2]), 2, 16, QLatin1Char('0'))
                               .arg(static_cast<unsigned int>(src[3]), 2, 16, QLatin1Char('0'))
                               .arg(transferred)
                               .arg(assembledRows)
                               .arg(packetCount));
                anomalyLogCount++;
            }

            assembledRows = 0;
            std::fill(rowBytes.begin(), rowBytes.end(), 0);
            targetFrameId = -1;
            timeoutRetries = 0;
            continue;
        }

        const int frameId = src[4];
        const int rowIndex = src[5] | (src[6] << 8);
        const int rowOffset = src[7] | (src[8] << 8);
        const int payloadLen = transferred - VIDEO_PACKET_HEADER_BYTES;

        if (rowIndex < 0 || rowIndex >= IMG_HEIGHT || rowOffset < 0 || rowOffset >= LINE_BYTES ||
            payloadLen <= 0 || (rowOffset + payloadLen) > LINE_BYTES)
        {
            sendEp3Command(USB_STREAM_CMD_STOP);
            if (!sendEp3Command(USB_STREAM_CMD_START))
            {
                appendLogMainThread("Invalid row packet metadata.");
                return false;
            }

            if (anomalyLogCount < 5)
            {
                appendLogMainThread(QString("Invalid metadata: fid=%1 row=%2 off=%3 payload=%4 len=%5")
                               .arg(frameId)
                               .arg(rowIndex)
                               .arg(rowOffset)
                               .arg(payloadLen)
                               .arg(transferred));
                anomalyLogCount++;
            }

            assembledRows = 0;
            std::fill(rowBytes.begin(), rowBytes.end(), 0);
            targetFrameId = -1;
            timeoutRetries = 0;
            continue;
        }

        if (targetFrameId < 0)
        {
            targetFrameId = frameId;
        }
        else if (frameId != targetFrameId)
        {
            frameSwitchCount++;
            if (anomalyLogCount < 5)
            {
                appendLogMainThread(QString("Frame switch during assembly: old=%1 new=%2 rows=%3 packets=%4")
                               .arg(targetFrameId)
                               .arg(frameId)
                               .arg(assembledRows)
                               .arg(packetCount));
                anomalyLogCount++;
            }
            targetFrameId = frameId;
            assembledRows = 0;
            std::fill(rowBytes.begin(), rowBytes.end(), 0);
            frameOut.fill(0);
        }

        if (rowOffset != rowBytes[static_cast<size_t>(rowIndex)])
        {
            sendEp3Command(USB_STREAM_CMD_STOP);
            if (!sendEp3Command(USB_STREAM_CMD_START))
            {
                appendLogMainThread("Row packet order mismatch.");
                return false;
            }

            if (anomalyLogCount < 5)
            {
                appendLogMainThread(QString("Row mismatch: fid=%1 row=%2 expect_off=%3 got_off=%4 payload=%5")
                               .arg(frameId)
                               .arg(rowIndex)
                               .arg(rowBytes[static_cast<size_t>(rowIndex)])
                               .arg(rowOffset)
                               .arg(payloadLen));
                anomalyLogCount++;
            }

            assembledRows = 0;
            std::fill(rowBytes.begin(), rowBytes.end(), 0);
            targetFrameId = -1;
            frameOut.fill(0);
            timeoutRetries = 0;
            continue;
        }

        memcpy(frameOut.data() + (rowIndex * LINE_BYTES) + rowOffset,
               packet.constData() + VIDEO_PACKET_HEADER_BYTES,
               static_cast<size_t>(payloadLen));

        if (rowIndex >= 0 && rowIndex < 3 && rowChunkLogs[static_cast<size_t>(rowIndex)].size() < 6)
        {
            rowChunkLogs[static_cast<size_t>(rowIndex)].append(
                QString("off=%1 len=%2").arg(rowOffset).arg(payloadLen));
        }

        rowBytes[static_cast<size_t>(rowIndex)] += payloadLen;
        if (rowBytes[static_cast<size_t>(rowIndex)] == LINE_BYTES)
        {
            assembledRows++;
        }

        timeoutRetries = 0;
        haltRecovered = false;
        deviceRecovered = false;
    }

    if (readMs)
        *readMs = static_cast<int>(readTimer.elapsed());

    return true;
}

void MainWindow::renderRgb565Frame(const QByteArray &frame)
{
    QImage img(IMG_WIDTH, IMG_HEIGHT, QImage::Format_RGB888);
    const uint8_t *src = reinterpret_cast<const uint8_t *>(frame.constData());

    for (int y = 0; y < IMG_HEIGHT; ++y)
    {
        uchar *dst = img.scanLine(y);
        for (int x = 0; x < IMG_WIDTH; ++x)
        {
            const uint16_t p = (static_cast<uint16_t>(src[0]) << 8) | src[1];
            src += 2;

            const uint8_t r = static_cast<uint8_t>(((p >> 11) & 0x1F) * 255 / 31);
            const uint8_t g = static_cast<uint8_t>(((p >> 5) & 0x3F) * 255 / 63);
            const uint8_t b = static_cast<uint8_t>((p & 0x1F) * 255 / 31);

            dst[0] = r;
            dst[1] = g;
            dst[2] = b;
            dst += 3;
        }
    }

    QPixmap pix = QPixmap::fromImage(img).scaled(
        previewLabel->size(), Qt::KeepAspectRatio, Qt::FastTransformation);
    previewLabel->setPixmap(pix);
}

void MainWindow::startCapture()
{
    if (isCapturing)
        return;

    if (!initUsb())
        return;

    if (!sendModeSwitch(DISPLAY_MODE_VIDEO))
    {
        closeUsb();
        return;
    }

    if (!sendEp3Command(USB_STREAM_CMD_START))
    {
        logMessage("Initial frame stream start failed.");
        closeUsb();
        return;
    }

    int fps = fpsInput->value();

    isCapturing = true;
    stopRequested.store(false);
    stopQueued.store(false);
    latestFrame.clear();
    latestFrameReady = false;
    latestFirstByteMs = 0;
    latestReadMs = 0;
    frameCounter = 0;
    frameLogCounter = 0;
    captureWaitAccumMs = 0;
    usbReadAccumMs = 0;
    renderAccumMs = 0;
    captureThread = std::thread(&MainWindow::captureLoop, this);
    videoTimer->start(16);

    startBtn->setEnabled(false);
    stopBtn->setEnabled(true);
    fpsInput->setEnabled(false);

    logMessage(QString("Capture started in continuous mode (UI setting %1 FPS).").arg(fps));
}

void MainWindow::stopCapture()
{
    if (!isCapturing && !usbHandle)
        return;

    isCapturing = false;
    stopRequested.store(true);
    videoTimer->stop();

    if (captureThread.joinable())
    {
        captureThread.join();
    }

    if (interfaceClaimed && usbHandle)
    {
        sendEp3Command(USB_STREAM_CMD_STOP);
    }

    closeUsb();

    startBtn->setEnabled(true);
    stopBtn->setEnabled(false);
    fpsInput->setEnabled(true);

    logMessage("Capture stopped.");
}

void MainWindow::captureLoop()
{
    while (!stopRequested.load())
    {
        QByteArray frame;
        int firstByteMs = 0;
        int readMs = 0;
        if (!readCameraFrame(frame, &firstByteMs, &readMs))
        {
            if (!stopRequested.load() && !stopQueued.exchange(true))
            {
                QMetaObject::invokeMethod(this, [this]() {
                    logMessage("Frame receive failed. Stopping capture.");
                    stopCapture();
                }, Qt::QueuedConnection);
            }
            return;
        }

        if (stopRequested.load())
        {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(frameMutex);
            latestFrame = frame;
            latestFirstByteMs = firstByteMs;
            latestReadMs = readMs;
            latestFrameReady = true;
        }
    }
}

void MainWindow::onVideoTick()
{
    if (!isCapturing)
    {
        return;
    }

    QByteArray frameToRender;
    int firstByteMs = 0;
    int readMs = 0;
    {
        std::lock_guard<std::mutex> lock(frameMutex);
        if (!latestFrameReady)
        {
            return;
        }

        frameToRender = latestFrame;
        firstByteMs = latestFirstByteMs;
        readMs = latestReadMs;
        latestFrameReady = false;
    }

    QElapsedTimer renderTimer;
    renderTimer.start();
    renderRgb565Frame(frameToRender);
    const int renderMs = static_cast<int>(renderTimer.elapsed());

    frameCounter++;
    frameLogCounter++;
    captureWaitAccumMs += static_cast<uint64_t>(qMax(0, firstByteMs));
    usbReadAccumMs += static_cast<uint64_t>(qMax(0, readMs));
    renderAccumMs += static_cast<uint64_t>(qMax(0, renderMs));

    if ((frameLogCounter % 30U) == 0U)
    {
        const uint64_t sampleCount = 30U;
        const double usbMBps =
            (static_cast<double>(VIDEO_FRAME_BYTES) * static_cast<double>(sampleCount) * 1000.0) /
            (static_cast<double>(qMax<uint64_t>(1U, usbReadAccumMs)) * 1024.0 * 1024.0);

        logMessage(QString("Captured %1 frames. avg first-byte=%2 ms, avg usb-read=%3 ms, avg render=%4 ms, usb=%5 MiB/s")
                       .arg(frameCounter)
                       .arg(captureWaitAccumMs / sampleCount)
                       .arg(usbReadAccumMs / sampleCount)
                       .arg(renderAccumMs / sampleCount)
                       .arg(usbMBps, 0, 'f', 2));
        captureWaitAccumMs = 0;
        usbReadAccumMs = 0;
        renderAccumMs = 0;
    }
}
