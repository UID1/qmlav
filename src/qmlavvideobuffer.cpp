#include "qmlavvideobuffer.h"
#include "qmlavformat.h"
#include "qmlavutils.h"

extern "C" {
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <cstring>

// Qt6 pixel format mapping
QVideoFrameFormat::PixelFormat QmlAVVideoFrameFactory::mapPixelFormat(const QmlAVPixelFormat &avFormat)
{
    AVPixelFormat fmt = static_cast<AVPixelFormat>(static_cast<int>(avFormat));
    
    switch (fmt) {
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
        return QVideoFrameFormat::Format_YUV420P;
    case AV_PIX_FMT_YUV422P:
    case AV_PIX_FMT_YUVJ422P:
        return QVideoFrameFormat::Format_YUV422P;
    case AV_PIX_FMT_NV12:
        return QVideoFrameFormat::Format_NV12;
    case AV_PIX_FMT_NV21:
        return QVideoFrameFormat::Format_NV21;
    case AV_PIX_FMT_UYVY422:
        return QVideoFrameFormat::Format_UYVY;
    case AV_PIX_FMT_YUYV422:
        return QVideoFrameFormat::Format_YUYV;
    case AV_PIX_FMT_RGB24:
        return QVideoFrameFormat::Format_RGBX8888;
    case AV_PIX_FMT_BGR24:
        return QVideoFrameFormat::Format_BGRX8888;
    case AV_PIX_FMT_RGBA:
    case AV_PIX_FMT_RGB0:
        return QVideoFrameFormat::Format_RGBA8888;
    case AV_PIX_FMT_BGRA:
    case AV_PIX_FMT_BGR0:
        return QVideoFrameFormat::Format_BGRA8888;
    case AV_PIX_FMT_ARGB:
    case AV_PIX_FMT_0RGB:
        return QVideoFrameFormat::Format_ARGB8888;
    case AV_PIX_FMT_ABGR:
    case AV_PIX_FMT_0BGR:
        return QVideoFrameFormat::Format_ABGR8888;
    case AV_PIX_FMT_P010LE:
        return QVideoFrameFormat::Format_P010;
    case AV_PIX_FMT_GRAY8:
        return QVideoFrameFormat::Format_Y8;
    case AV_PIX_FMT_GRAY16LE:
        return QVideoFrameFormat::Format_Y16;
    default:
        // Fall back to YUV420P - we'll convert
        return QVideoFrameFormat::Format_YUV420P;
    }
}

AVFramePtr QmlAVVideoFrameFactory::swsScale(const QmlAVVideoFrame &videoFrame, AVPixelFormat dstFormat)
{
    AVFramePtr avFrameSws;
    AVPixelFormat srcFormat = static_cast<AVPixelFormat>(videoFrame.avFrame()->format);
    
    SwsContext *swsCtx = sws_getContext(
        videoFrame.avFrame()->width, videoFrame.avFrame()->height, srcFormat,
        videoFrame.avFrame()->width, videoFrame.avFrame()->height, dstFormat,
        SWS_BILINEAR, nullptr, nullptr, nullptr);
    
    if (swsCtx) {
        avFrameSws->width = videoFrame.avFrame()->width;
        avFrameSws->height = videoFrame.avFrame()->height;
        avFrameSws->format = dstFormat;
        av_frame_get_buffer(avFrameSws, FFMPEG_ALIGNMENT);
        
        sws_scale(swsCtx, videoFrame.avFrame()->data, videoFrame.avFrame()->linesize, 
                  0, videoFrame.avFrame()->height, 
                  avFrameSws->data, avFrameSws->linesize);
        sws_freeContext(swsCtx);
    }
    
    return avFrameSws;
}

AVFramePtr QmlAVVideoFrameFactory::transferFromGPU(const QmlAVVideoFrame &videoFrame)
{
    AVFramePtr avFrameSw;
    
    if (videoFrame.isHWDecoded()) {
        avFrameSw->format = videoFrame.swPixelFormat();
        
        int ret = av_hwframe_transfer_data(avFrameSw, videoFrame.avFrame().get(), 0);
        if (ret == 0) {
            av_frame_copy_props(avFrameSw, videoFrame.avFrame().get());
            return avFrameSw;
        } else {
            qCritical() << QString("Failed to transfer data from GPU: \"%1\" (%2)")
                            .arg(av_err2str(ret)).arg(ret);
        }
    }
    
    return AVFramePtr(); // Return empty if not HW or failed
}

void QmlAVVideoFrameFactory::copyPlaneData(QVideoFrame &frame, const AVFrame *avFrame, 
                                            const QVideoFrameFormat::PixelFormat &format)
{
    if (!frame.map(QVideoFrame::WriteOnly)) {
        qCritical() << "Failed to map QVideoFrame for writing";
        return;
    }
    
    int planeCount = frame.planeCount();
    
    for (int plane = 0; plane < planeCount && plane < QMLAV_NUM_DATA_POINTERS; ++plane) {
        if (!avFrame->data[plane]) continue;
        
        uchar *dst = frame.bits(plane);
        const uint8_t *src = avFrame->data[plane];
        
        int dstBytesPerLine = frame.bytesPerLine(plane);
        int srcBytesPerLine = avFrame->linesize[plane];
        
        // Calculate plane height based on format
        int planeHeight = avFrame->height;
        if (plane > 0) {
            // For YUV formats, chroma planes are typically half height
            switch (format) {
            case QVideoFrameFormat::Format_YUV420P:
            case QVideoFrameFormat::Format_NV12:
            case QVideoFrameFormat::Format_NV21:
            case QVideoFrameFormat::Format_P010:
                planeHeight = (avFrame->height + 1) / 2;
                break;
            default:
                break;
            }
        }
        
        // Copy line by line to handle different strides
        int copyWidth = qMin(dstBytesPerLine, srcBytesPerLine);
        for (int y = 0; y < planeHeight; ++y) {
            std::memcpy(dst + y * dstBytesPerLine, src + y * srcBytesPerLine, copyWidth);
        }
    }
    
    frame.unmap();
}

QVideoFrame QmlAVVideoFrameFactory::createFrame(const QmlAVVideoFrame &avVideoFrame, 
                                                 std::shared_ptr<QmlAVHWOutput> hwOutput)
{
    const AVFrame *srcFrame = avVideoFrame.avFrame().get();
    AVFramePtr convertedFrame;
    
    // Handle hardware-decoded frames
    if (avVideoFrame.isHWDecoded()) {
        convertedFrame = transferFromGPU(avVideoFrame);
        if (convertedFrame && convertedFrame->data[0]) {
            srcFrame = convertedFrame.get();
        } else {
            // GPU transfer failed, return invalid frame
            return QVideoFrame();
        }
    }
    
    // Determine target pixel format
    QmlAVPixelFormat srcPixelFormat(srcFrame->format);
    QVideoFrameFormat::PixelFormat qtFormat = mapPixelFormat(srcPixelFormat);
    
    // Check if we need format conversion
    AVPixelFormat targetAVFormat = AV_PIX_FMT_NONE;
    switch (qtFormat) {
    case QVideoFrameFormat::Format_YUV420P:
        targetAVFormat = AV_PIX_FMT_YUV420P;
        break;
    case QVideoFrameFormat::Format_NV12:
        targetAVFormat = AV_PIX_FMT_NV12;
        break;
    case QVideoFrameFormat::Format_RGBA8888:
        targetAVFormat = AV_PIX_FMT_RGBA;
        break;
    case QVideoFrameFormat::Format_BGRA8888:
        targetAVFormat = AV_PIX_FMT_BGRA;
        break;
    default:
        targetAVFormat = AV_PIX_FMT_YUV420P;
        qtFormat = QVideoFrameFormat::Format_YUV420P;
        break;
    }
    
    // Convert if necessary
    if (static_cast<AVPixelFormat>(srcFrame->format) != targetAVFormat &&
        static_cast<AVPixelFormat>(srcFrame->format) != AV_PIX_FMT_YUVJ420P) { // YUVJ420P is compatible with YUV420P
        AVFramePtr scaledFrame = swsScale(avVideoFrame, targetAVFormat);
        if (scaledFrame && scaledFrame->data[0]) {
            // If we already had a converted frame, swap it
            convertedFrame = scaledFrame;
            srcFrame = convertedFrame.get();
        }
    }
    
    // Create Qt6 video frame
    QVideoFrameFormat frameFormat(QSize(srcFrame->width, srcFrame->height), qtFormat);
    QVideoFrame frame(frameFormat);
    
    // Copy pixel data
    copyPlaneData(frame, srcFrame, qtFormat);
    
    return frame;
}

// Legacy compatibility implementations

QmlAVVideoBuffer_CPU::QmlAVVideoBuffer_CPU(const QmlAVVideoFrame &videoFrame)
    : m_videoFrame(videoFrame)
    , m_swsCtx(nullptr)
{
}

QVideoFrame QmlAVVideoBuffer_CPU::toVideoFrame()
{
    return QmlAVVideoFrameFactory::createFrame(m_videoFrame);
}

QmlAVPixelFormat QmlAVVideoBuffer_CPU::pixelFormat() const
{
    return m_videoFrame.pixelFormat().nearestQtNative();
}

QmlAVVideoBuffer_GPU::QmlAVVideoBuffer_GPU(const QmlAVVideoFrame &videoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput)
    : QmlAVVideoBuffer_CPU(videoFrame)
    , m_hwOutput(hwOutput)
{
}

QVideoFrame QmlAVVideoBuffer_GPU::toVideoFrame()
{
    return QmlAVVideoFrameFactory::createFrame(m_videoFrame, m_hwOutput);
}

QmlAVPixelFormat QmlAVVideoBuffer_GPU::pixelFormat() const
{
    if (m_hwOutput) {
        return m_hwOutput->pixelFormat();
    }
    return m_videoFrame.swPixelFormat().nearestQtNative();
}
