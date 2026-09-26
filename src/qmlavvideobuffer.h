#ifndef QMLAVVIDEOBUFFER_H
#define QMLAVVIDEOBUFFER_H

#include <QVideoFrame>
#include <QVideoFrameFormat>

#include "qmlavframe.h"
#include "qmlavhwoutput.h"

struct SwsContext;

// Qt6 Video Frame Factory
// In Qt6, we don't inherit from a buffer class. Instead, we create QVideoFrame objects directly.
class QmlAVVideoFrameFactory
{
public:
    static QVideoFrame createFrame(const QmlAVVideoFrame &avVideoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput = nullptr);
    
private:
    static QVideoFrameFormat::PixelFormat mapPixelFormat(const QmlAVPixelFormat &avFormat);
    static void copyPlaneData(QVideoFrame &frame, const AVFrame *avFrame, const QVideoFrameFormat::PixelFormat &format);
    static AVFramePtr swsScale(const QmlAVVideoFrame &videoFrame, AVPixelFormat dstFormat);
    static AVFramePtr transferFromGPU(const QmlAVVideoFrame &videoFrame);
};

// Legacy compatibility - these classes are kept for minimal code changes elsewhere
// but their implementation wraps the new factory approach

class QmlAVVideoBuffer_CPU
{
public:
    QmlAVVideoBuffer_CPU(const QmlAVVideoFrame &videoFrame);
    
    QVideoFrame toVideoFrame();
    QmlAVPixelFormat pixelFormat() const;

protected:
    QmlAVVideoFrame m_videoFrame;
    SwsContext *m_swsCtx;
};

class QmlAVVideoBuffer_GPU : public QmlAVVideoBuffer_CPU
{
public:
    QmlAVVideoBuffer_GPU(const QmlAVVideoFrame &videoFrame, std::shared_ptr<QmlAVHWOutput> hwOutput = nullptr);
    
    QVideoFrame toVideoFrame();
    QmlAVPixelFormat pixelFormat() const;

private:
    std::shared_ptr<QmlAVHWOutput> m_hwOutput;
};

#endif // QMLAVVIDEOBUFFER_H
