#ifndef QMLAVHWOUTPUT_H
#define QMLAVHWOUTPUT_H

#include "qmlavframe.h"
#include "qmlavformat.h"

class QmlAVHWOutput
{
public:
    enum Type
    {
        TypeUnknown,
        TypeVAAPI_GLX,
        TypeVAAPI_EGL
    };

    // Qt6: QAbstractVideoBuffer::HandleType no longer exists
    // Define our own handle type enum for hardware output
    enum HandleType
    {
        NoHandle,
        GLTextureHandle,
        // Add more as needed
    };

    struct Contract
    {
        int width = 0;
        int height = 0;
        AVPixelFormat swFormat = AV_PIX_FMT_NONE;

        Contract() = default;
        Contract(const QmlAVVideoFrame &videoFrame) {
            width = videoFrame.width();
            height = videoFrame.height();
            swFormat = videoFrame.swPixelFormat();
        }

        bool operator==(const Contract &other) const {
            return width == other.width &&
                   height == other.height &&
                   swFormat == other.swFormat;
        }

        bool operator!=(const Contract &other) const {
            return !(*this == other);
        }
    };

    QmlAVHWOutput() { }
    virtual ~QmlAVHWOutput() { }

    QmlAVHWOutput(const QmlAVHWOutput &other) = delete;
    QmlAVHWOutput &operator=(const QmlAVHWOutput &other) = delete;

    virtual Type type() const = 0;
    virtual QmlAVPixelFormat pixelFormat() const = 0;
    virtual HandleType handleType() const = 0;
    virtual QVariant handle(const QmlAVVideoFrame &videoFrame) = 0;

protected:
    void resetContract() { m_contract = Contract{}; }

    Contract m_contract;
};

#endif // QMLAVHWOUTPUT_H
