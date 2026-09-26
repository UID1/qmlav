#include "qmlavformat.h"

#include <QDebug>

#include <unordered_map>

extern "C" {
#include <libavutil/pixdesc.h>
}

// Qt6 pixel format mapping
// Note: Qt6 uses QVideoFrameFormat::PixelFormat instead of QVideoFrame::PixelFormat
const std::vector<QmlAVPixelFormat::PixelFormatMap> QmlAVPixelFormat::m_pixelFormatMap = {
    {AV_PIX_FMT_NONE, QVideoFrameFormat::Format_Invalid},
    // YUV formats
    {AV_PIX_FMT_YUV420P, QVideoFrameFormat::Format_YUV420P, true},
    {AV_PIX_FMT_YUV422P, QVideoFrameFormat::Format_YUV422P, true},
    {AV_PIX_FMT_NV12, QVideoFrameFormat::Format_NV12, true},
    {AV_PIX_FMT_NV21, QVideoFrameFormat::Format_NV21, true},
    {AV_PIX_FMT_UYVY422, QVideoFrameFormat::Format_UYVY, true},
    {AV_PIX_FMT_YUYV422, QVideoFrameFormat::Format_YUYV, true},
    {AV_PIX_FMT_P010LE, QVideoFrameFormat::Format_P010, true},
    // RGB/RGBA formats
    {AV_PIX_FMT_RGBA, QVideoFrameFormat::Format_RGBA8888, true},
    {AV_PIX_FMT_RGB0, QVideoFrameFormat::Format_RGBX8888, true},
    {AV_PIX_FMT_BGRA, QVideoFrameFormat::Format_BGRA8888, true},
    {AV_PIX_FMT_BGR0, QVideoFrameFormat::Format_BGRX8888, true},
    {AV_PIX_FMT_ARGB, QVideoFrameFormat::Format_ARGB8888, true},
    {AV_PIX_FMT_0RGB, QVideoFrameFormat::Format_XRGB8888, true},
    {AV_PIX_FMT_ABGR, QVideoFrameFormat::Format_ABGR8888, true},
    {AV_PIX_FMT_0BGR, QVideoFrameFormat::Format_XBGR8888, true},
    // Grayscale
    {AV_PIX_FMT_GRAY8, QVideoFrameFormat::Format_Y8, true},
    {AV_PIX_FMT_GRAY16LE, QVideoFrameFormat::Format_Y16, true},
};

QmlAVPixelFormat::QmlAVPixelFormat(AVPixelFormat avPixelFormat)
    : m_avPixelFormat(normalize(avPixelFormat))
{
}

QmlAVPixelFormat::QmlAVPixelFormat(int avPixelFormat)
    : QmlAVPixelFormat(static_cast<AVPixelFormat>(avPixelFormat))
{
}

QmlAVPixelFormat::QmlAVPixelFormat(QVideoFrameFormat::PixelFormat pixelFormat)
    : m_avPixelFormat(avFormatFromPixelFormat(pixelFormat))
{
}

bool QmlAVPixelFormat::isHWAccel() const
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_avPixelFormat);
    return desc && (desc->flags & AV_PIX_FMT_FLAG_HWACCEL);
}

bool QmlAVPixelFormat::isQtNative() const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.avFormat == m_avPixelFormat) {
            return pfm.qtNative;
        }
    }

    return false;
}

QmlAVPixelFormat QmlAVPixelFormat::nearestQtNative() const
{
    if (!isQtNative()) {
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(m_avPixelFormat);
        if (desc && (desc->flags & AV_PIX_FMT_FLAG_RGB)) {
            return AV_PIX_FMT_RGBA;
        }

        return AV_PIX_FMT_YUV420P;
    }

    return *this;
}

// We lose information about the deprecated format, but the new format combined with
// the AVFrame "color_range" field is a convenient replacement that does not raise FFmpeg warnings.
AVPixelFormat QmlAVPixelFormat::normalize(AVPixelFormat avPixelFormat) const
{
    static std::unordered_map<AVPixelFormat, AVPixelFormat> map = {
        {AV_PIX_FMT_YUVJ420P, AV_PIX_FMT_YUV420P},
        {AV_PIX_FMT_YUVJ422P, AV_PIX_FMT_YUV422P},
        {AV_PIX_FMT_YUVJ444P, AV_PIX_FMT_YUV444P},
        {AV_PIX_FMT_YUVJ440P, AV_PIX_FMT_YUV440P},
        {AV_PIX_FMT_YUVJ411P, AV_PIX_FMT_YUV411P}
    };

    // Replace deprecated AV_PIX_FMT_YUVJXXXP formats
    if (auto pair = map.find(avPixelFormat); pair != map.end()) {
        return pair->second;
    }

    return avPixelFormat;
}

QVideoFrameFormat::PixelFormat QmlAVPixelFormat::pixelFormatFromAVFormat(AVPixelFormat avPixelFormat) const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.avFormat == avPixelFormat) {
            return pfm.format;
        }
    }

    return QVideoFrameFormat::Format_Invalid;
}

AVPixelFormat QmlAVPixelFormat::avFormatFromPixelFormat(QVideoFrameFormat::PixelFormat pixelFormat) const
{
    for (const auto &pfm : m_pixelFormatMap) {
        if (pfm.format == pixelFormat) {
            return pfm.avFormat;
        }
    }

    return AV_PIX_FMT_NONE;
}

QmlAVColorSpace::QmlAVColorSpace(AVColorSpace avColorSpace)
    : m_avColorSpace(avColorSpace)
{
}

QmlAVColorSpace::operator QVideoFrameFormat::ColorSpace() const
{
    switch (m_avColorSpace) {
    case AVCOL_SPC_BT709:
        return QVideoFrameFormat::ColorSpace_BT709;
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
    case AVCOL_SPC_SMPTE240M:
        return QVideoFrameFormat::ColorSpace_BT601;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return QVideoFrameFormat::ColorSpace_BT2020;
    default:
        return QVideoFrameFormat::ColorSpace_Undefined;
    }
}

QAudioFormat::SampleFormat QmlAVSampleFormat::audioFormatFromAVFormat(AVSampleFormat sampleFormat)
{
    // Qt6 uses QAudioFormat::SampleFormat enum
    switch (sampleFormat) {
    case AV_SAMPLE_FMT_U8:
    case AV_SAMPLE_FMT_U8P:
        return QAudioFormat::UInt8;
    case AV_SAMPLE_FMT_S16:
    case AV_SAMPLE_FMT_S16P:
        return QAudioFormat::Int16;
    case AV_SAMPLE_FMT_S32:
    case AV_SAMPLE_FMT_S32P:
        return QAudioFormat::Int32;
    case AV_SAMPLE_FMT_FLT:
    case AV_SAMPLE_FMT_FLTP:
        return QAudioFormat::Float;
    default:
        return QAudioFormat::Unknown;
    }
}

#ifndef QT_NO_DEBUG_STREAM
QDebug operator<<(QDebug dbg, const QmlAVPixelFormat &pixelFormat)
{
    dbg.nospace() << QString("AV_PIX_FMT_%1").arg(av_get_pix_fmt_name(pixelFormat)).toUpper();
    return dbg.nospace() << " (QVideoFrameFormat::" << static_cast<QVideoFrameFormat::PixelFormat>(pixelFormat) << ")";
}

QDebug operator<<(QDebug dbg, const QmlAVColorSpace &colorSpace)
{
    dbg.nospace() << QString("AVCOL_SPC_%1").arg(av_color_space_name(colorSpace)).toUpper();
    return dbg.nospace() << " (QVideoFrameFormat::" << static_cast<QVideoFrameFormat::ColorSpace>(colorSpace) << ")";
}
#endif
