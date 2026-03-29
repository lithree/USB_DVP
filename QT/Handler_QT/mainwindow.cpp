#include "mainwindow.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QThread>
#include <QVBoxLayout>
#include "libusb.h"

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
    previewLabel->setMinimumSize(IMG_WIDTH, IMG_HEIGHT);
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

    sendEp3Command(0x02);
    QThread::msleep(20);
    drainInEndpoint(USB_DRAIN_MAX_PACKETS, USB_DRAIN_PACKET_TIMEOUT_MS);

    if (!sendEp3Command(0x01))
        return false;

    logMessage("USB reconnect succeeded.");
    return true;
}

void MainWindow::drainInEndpoint(int maxPackets, int timeoutMs)
{
    if (!interfaceClaimed || !usbHandle)
        return;

    unsigned char buffer[512];
    for (int packet = 0; packet < maxPackets; ++packet)
    {
        int transferred = 0;
        const int res = libusb_bulk_transfer(
            usbHandle,
            EP_IN,
            buffer,
            sizeof(buffer),
            &transferred,
            timeoutMs);

        if (res == LIBUSB_ERROR_TIMEOUT || transferred <= 0)
            break;

        if (res == LIBUSB_ERROR_PIPE)
        {
            libusb_clear_halt(usbHandle, EP_IN);
            break;
        }

        if (res < 0)
            break;
    }
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
        USB_CMD_TIMEOUT_MS);

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

    unsigned char payload[1] = {cmd};
    int lastErr = LIBUSB_ERROR_OTHER;

    for (int attempt = 1; attempt <= 3; ++attempt)
    {
        int transferred = 0;
        const int res = libusb_bulk_transfer(
            usbHandle,
            EP_OUT,
            payload,
            1,
            &transferred,
            USB_CMD_TIMEOUT_MS);

        if (res == 0 && transferred == 1)
            return true;

        lastErr = res;

        if (res == LIBUSB_ERROR_PIPE)
        {
            libusb_clear_halt(usbHandle, EP_OUT);
        }

        if (res == LIBUSB_ERROR_TIMEOUT || res == LIBUSB_ERROR_PIPE)
        {
            QThread::msleep(20);
            continue;
        }

        break;
    }

    logMessage(QString("EP3 command 0x%1 send failed after retries: %2")
                   .arg(cmd, 2, 16, QLatin1Char('0'))
                   .arg(libusb_error_name(lastErr)));
    return false;
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
    QByteArray packet(VIDEO_ROW_PACKET_BYTES, 0);
    uint8_t rowSeen[IMG_HEIGHT] = {0};

    if (firstByteMs)
        *firstByteMs = -1;
    if (readMs)
        *readMs = 0;

    int assembledRows = 0;
    int timeoutRetries = 0;
    int resyncAttempts = 0;
    bool haltRecovered = false;
    bool deviceRecovered = false;

    int targetFrameId = -1;
    while (assembledRows < IMG_HEIGHT)
    {
        int packetOffset = 0;
        while (packetOffset < VIDEO_ROW_PACKET_BYTES)
        {
            int transferred = 0;
            const unsigned int transferTimeout = firstPayloadSeen ? USB_FRAME_TIMEOUT_MS : USB_FIRST_PAYLOAD_TIMEOUT_MS;
            int res = libusb_bulk_transfer(
                usbHandle,
                EP_IN,
                reinterpret_cast<unsigned char *>(packet.data() + packetOffset),
                VIDEO_ROW_PACKET_BYTES - packetOffset,
                &transferred,
                transferTimeout);

            if (res < 0)
            {
                if ((res == LIBUSB_ERROR_NO_DEVICE) && !deviceRecovered)
                {
                    if (!recoverUsbSession())
                    {
                        logMessage("USB reconnect failed.");
                        return false;
                    }

                    assembledRows = 0;
                    memset(rowSeen, 0, sizeof(rowSeen));
                    targetFrameId = -1;
                    packetOffset = 0;
                    timeoutRetries = 0;
                    haltRecovered = false;
                    deviceRecovered = true;
                    continue;
                }

                if ((res == LIBUSB_ERROR_PIPE) && !haltRecovered)
                {
                    libusb_clear_halt(usbHandle, EP_IN);
                    sendEp3Command(0x01);
                    haltRecovered = true;
                    continue;
                }

                if (res == LIBUSB_ERROR_TIMEOUT && timeoutRetries < FRAME_READ_TIMEOUT_RETRIES)
                {
                    timeoutRetries++;
                    continue;
                }

                if ((res == LIBUSB_ERROR_TIMEOUT) && (resyncAttempts < FRAME_RESYNC_MAX_ATTEMPTS))
                {
                    const bool gotPartialFrame = firstPayloadSeen;
                    logMessage(QString("Frame timeout after %1 ms (%2, rows=%3/%4, packet=%5/%6). Resync %7/%8.")
                                   .arg(readTimer.elapsed())
                                   .arg(gotPartialFrame ? "partial frame" : "no first payload")
                                   .arg(assembledRows)
                                   .arg(IMG_HEIGHT)
                                   .arg(packetOffset)
                                   .arg(VIDEO_ROW_PACKET_BYTES)
                                   .arg(resyncAttempts + 1)
                                   .arg(FRAME_RESYNC_MAX_ATTEMPTS));

                    sendEp3Command(0x02);
                    QThread::msleep(20);
                    drainInEndpoint(USB_DRAIN_MAX_PACKETS, USB_DRAIN_PACKET_TIMEOUT_MS);
                    if (!sendEp3Command(0x01))
                    {
                        logMessage("Frame restart request failed.");
                        return false;
                    }

                    QThread::msleep(40);

                    assembledRows = 0;
                    memset(rowSeen, 0, sizeof(rowSeen));
                    targetFrameId = -1;
                    packetOffset = 0;
                    timeoutRetries = 0;
                    resyncAttempts++;
                    haltRecovered = false;
                    deviceRecovered = false;
                    firstPayloadSeen = false;
                    readTimer.restart();
                    if (firstByteMs)
                        *firstByteMs = -1;
                    continue;
                }

                logMessage(QString("USB IN transfer failed: %1").arg(libusb_error_name(res)));
                return false;
            }

            if (transferred <= 0)
            {
                if (timeoutRetries < FRAME_READ_TIMEOUT_RETRIES)
                {
                    timeoutRetries++;
                    continue;
                }

                logMessage("USB IN transfer timeout.");
                return false;
            }

            if (!firstPayloadSeen)
            {
                firstPayloadSeen = true;
                if (firstByteMs)
                    *firstByteMs = static_cast<int>(readTimer.elapsed());
            }

            packetOffset += transferred;
            timeoutRetries = 0;
            haltRecovered = false;
            deviceRecovered = false;
        }

        if (memcmp(packet.constData(), "OV26", 4) != 0)
        {
            logMessage("USB row packet lost sync. Draining and retrying.");
            sendEp3Command(0x02);
            QThread::msleep(20);
            drainInEndpoint(USB_DRAIN_MAX_PACKETS, USB_DRAIN_PACKET_TIMEOUT_MS);
            if (!sendEp3Command(0x01))
            {
                logMessage("Frame restart request failed after sync loss.");
                return false;
            }

            assembledRows = 0;
            memset(rowSeen, 0, sizeof(rowSeen));
            targetFrameId = -1;
            timeoutRetries = 0;
            resyncAttempts++;
            firstPayloadSeen = false;
            readTimer.restart();
            continue;
        }

        const uint8_t frameId = static_cast<uint8_t>(packet[4]);
        const uint16_t rowIndex = static_cast<uint8_t>(packet[5]) |
                                  (static_cast<uint16_t>(static_cast<uint8_t>(packet[6])) << 8);

        if (rowIndex >= IMG_HEIGHT)
        {
            continue;
        }

        if (targetFrameId < 0)
        {
            targetFrameId = frameId;
        }
        else if (frameId != targetFrameId)
        {
            memset(rowSeen, 0, sizeof(rowSeen));
            assembledRows = 0;
            targetFrameId = frameId;
        }

        if (rowSeen[rowIndex] == 0U)
        {
            memcpy(frameOut.data() + (rowIndex * (IMG_WIDTH * 2)),
                   packet.constData() + VIDEO_ROW_HDR_BYTES,
                   (IMG_WIDTH * 2));
            rowSeen[rowIndex] = 1U;
            assembledRows++;
        }

        resyncAttempts = 0;
    }

    if (readMs)
        *readMs = static_cast<int>(readTimer.elapsed());

    return true;
}

void MainWindow::renderRgb565Frame(const QByteArray &frame)
{
    /* OV2640 with 0xDA=0x09 outputs byte-swapped (little-endian) RGB565,
     * which matches Qt's Format_RGB16 on little-endian hosts directly. */
    QImage img(reinterpret_cast<const uchar *>(frame.constData()),
               IMG_WIDTH,
               IMG_HEIGHT,
               IMG_WIDTH * 2,
               QImage::Format_RGB16);

    previewLabel->setPixmap(QPixmap::fromImage(img));
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

    sendEp3Command(0x02);
    QThread::msleep(20);
    drainInEndpoint(USB_DRAIN_MAX_PACKETS, USB_DRAIN_PACKET_TIMEOUT_MS);
    QThread::msleep(20);

    if (!sendEp3Command(0x01))
    {
        logMessage("Initial frame stream start failed.");
        closeUsb();
        return;
    }

    int fps = fpsInput->value();
    int intervalMs = qMax(1, 1000 / fps);

    isCapturing = true;
    frameCounter = 0;
    captureWaitAccumMs = 0;
    usbReadAccumMs = 0;
    renderAccumMs = 0;
    frameLoopAccumMs = 0;
    perfWindowTimer.start();
    videoTimer->start(intervalMs);

    startBtn->setEnabled(false);
    stopBtn->setEnabled(true);
    fpsInput->setEnabled(false);

    logMessage(QString("Capture started at %1 FPS poll.").arg(fps));
}

void MainWindow::stopCapture()
{
    if (!isCapturing && !usbHandle)
        return;

    isCapturing = false;
    videoTimer->stop();

    closeUsb();

    startBtn->setEnabled(true);
    stopBtn->setEnabled(false);
    fpsInput->setEnabled(true);

    logMessage("Capture stopped.");
}

void MainWindow::onVideoTick()
{
    if (!isCapturing)
        return;

    QElapsedTimer frameLoopTimer;
    frameLoopTimer.start();

    QElapsedTimer renderTimer;
    QByteArray frame;
    int firstByteMs = 0;
    int readMs = 0;
    if (!readCameraFrame(frame, &firstByteMs, &readMs))
    {
        logMessage("Frame receive failed. Stopping capture.");
        stopCapture();
        return;
    }

    renderTimer.start();
    renderRgb565Frame(frame);
    const int renderMs = static_cast<int>(renderTimer.elapsed());
    const int frameLoopMs = static_cast<int>(frameLoopTimer.elapsed());

    frameCounter++;
    captureWaitAccumMs += static_cast<uint64_t>(qMax(0, firstByteMs));
    usbReadAccumMs += static_cast<uint64_t>(qMax(0, readMs));
    renderAccumMs += static_cast<uint64_t>(qMax(0, renderMs));
    frameLoopAccumMs += static_cast<uint64_t>(qMax(0, frameLoopMs));

    if ((frameCounter % 30U) == 0U)
    {
        const uint64_t sampleCount = 30U;
        const qint64 windowMs = qMax<qint64>(1, perfWindowTimer.elapsed());
        const double achievedFps = (sampleCount * 1000.0) / static_cast<double>(windowMs);
        const double usbMBps =
            (static_cast<double>(VIDEO_FRAME_BYTES) * static_cast<double>(sampleCount) * 1000.0) /
            (static_cast<double>(qMax<uint64_t>(1U, usbReadAccumMs)) * 1024.0 * 1024.0);

        logMessage(QString("Captured %1 frames. avg first-byte=%2 ms, avg usb-read=%3 ms, avg render=%4 ms, avg loop=%5 ms, fps=%6, usb=%7 MB/s")
                       .arg(frameCounter)
                       .arg(captureWaitAccumMs / sampleCount)
                       .arg(usbReadAccumMs / sampleCount)
                       .arg(renderAccumMs / sampleCount)
                       .arg(frameLoopAccumMs / sampleCount)
                       .arg(achievedFps, 0, 'f', 1)
                       .arg(usbMBps, 0, 'f', 2));

        perfWindowTimer.restart();
        captureWaitAccumMs = 0;
        usbReadAccumMs = 0;
        renderAccumMs = 0;
        frameLoopAccumMs = 0;
    }
}
