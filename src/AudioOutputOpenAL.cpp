/******************************************************************************
    AudioOutputOpenAL.cpp: description
    Copyright (C) 2012-2014 Wang Bin <wbsecg1@gmail.com>
    Copyright (C) 2014 Stefan Ladage <sladage@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/


#include "QtAV/AudioOutputOpenAL.h"
#include "private/AudioOutput_p.h"
#include "prepost.h"
#include <QtCore/QVector>
#include <QtCore/QElapsedTimer>

#if defined(HEADER_OPENAL_PREFIX)
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

namespace QtAV {

extern AudioOutputId AudioOutputId_OpenAL;
FACTORY_REGISTER_ID_AUTO(AudioOutput, OpenAL, "OpenAL")

void RegisterAudioOutputOpenAL_Man()
{
    FACTORY_REGISTER_ID_MAN(AudioOutput, OpenAL, "OpenAL")
}

// TODO: planar
static ALenum audioFormatToAL(const AudioFormat& fmt)
{
    ALenum format = 0;
    ALCenum err;
    if (fmt.sampleFormat() == AudioFormat::SampleFormat_Unsigned8) {
        if (fmt.channels() == 1)
                format = AL_FORMAT_MONO8;
        else if (fmt.channels() == 2)
            format = AL_FORMAT_STEREO8;
        else if (alIsExtensionPresent("AL_EXT_MCFORMATS")) {
            if (fmt.channels() == 4)
                format = alGetEnumValue("AL_FORMAT_QUAD8");
            else if (fmt.channels() == 6)
                format = alGetEnumValue("AL_FORMAT_51CHN8");
            else if (fmt.channels() == 7)
                format = alGetEnumValue("AL_FORMAT_71CHN8");
            else if (fmt.channels() == 8)
                format = alGetEnumValue("AL_FORMAT_81CHN8");
        }

    } else if (fmt.sampleFormat() == AudioFormat::SampleFormat_Signed16) {
        if (fmt.channels() == 1)
            format = AL_FORMAT_MONO16;
        else if (fmt.channels() == 2)
            format = AL_FORMAT_STEREO16;
        else if (alIsExtensionPresent("AL_EXT_MCFORMATS")) {
            if (fmt.channels() == 4)
                format = alGetEnumValue("AL_FORMAT_QUAD16");
            else if (fmt.channels() == 6)
                format = alGetEnumValue("AL_FORMAT_51CHN16");
            else if (fmt.channels() == 7)
                format = alGetEnumValue("AL_FORMAT_61CHN16");
            else if (fmt.channels() == 8)
                format = alGetEnumValue("AL_FORMAT_71CHN16");
        }
    } else if (fmt.sampleFormat() == AudioFormat::SampleFormat_Float) {
        if (alIsExtensionPresent("AL_EXT_float32")) {
            if (fmt.channels() == 1)
                format = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            else if (fmt.channels() == 2)
                format = alGetEnumValue("AL_FORMAT_STEREO_FLOAT32");
            else if (alIsExtensionPresent("AL_EXT_MCFORMATS")) {
                if (fmt.channels() == 4)
                    format = alGetEnumValue("AL_FORMAT_QUAD32");
                else if (fmt.channels() == 6)
                    format = alGetEnumValue("AL_FORMAT_51CHN32");
                else if (fmt.channels() == 7)
                    format = alGetEnumValue("AL_FORMAT_61CHN32");
                else if (fmt.channels() == 8)
                    format = alGetEnumValue("AL_FORMAT_71CHN32");
            }
        }
    } else if (fmt.sampleFormat() == AudioFormat::SampleFormat_Double) {
        if (alIsExtensionPresent("AL_EXT_double")) {
            if (fmt.channels() == 1)
                format = alGetEnumValue("AL_FORMAT_MONO_DOUBLE_EXT");
            else if (fmt.channels() == 2)
                format = alGetEnumValue("AL_FORMAT_STEREO_DOUBLE_EXT");
        }
    }
    /*if (format == 0) {
        qWarning("AudioOutputOpenAL Error: No OpenAL format available for audio data format %s %s. Will sample to 16bit audio."
                 , qPrintable(fmt.sampleFormatName())
                 , qPrintable(fmt.channelLayoutName()));
    }
    qDebug("OpenAL audio format: %#x ch:%d, sample format: %s", format, fmt.channels(), qPrintable(fmt.sampleFormatName()));*/
    /*ALCenum */err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning("OpenAL audioFormatToAL error: %s", alGetString(err));
    }
    return format;
}

#define ALERROR_RETURN_F(alf) { \
    alf; \
    ALenum err = alGetError(); \
    if (err != AL_NO_ERROR) { \
        qWarning("AudioOutputOpenAL Error (%d/%d) %s : %s", err, AL_NO_ERROR, #alf, alGetString(err)); \
        return false; \
    }}

const int kBufferSize = 4096*2;
const ALsizei kBufferCount = 3;

class  AudioOutputOpenALPrivate : public AudioOutputPrivate
{
public:
    AudioOutputOpenALPrivate()
        : AudioOutputPrivate()
        , format_al(AL_FORMAT_STEREO16)
        , state(0)
        , last_duration(0)
    {
        //retreive supported format info
        qDebug("Retreive OpenAL supported format info.");

        supported_channel_count = 2;
        pref_channel_layout = AudioFormat::ChannelLayout_Stero;

        if (alIsExtensionPresent("AL_EXT_MCFORMATS")) {
            //Supports more than 2 channels
            supported_channel_count = 8;
        }

        supported_formats << AudioFormat::SampleFormat_Unsigned8 << AudioFormat::SampleFormat_Signed16;
        pref_format = AudioFormat::SampleFormat_Signed16;

        if (alIsExtensionPresent("AL_EXT_float32")) {
            //supports float32
            supported_formats << AudioFormat::SampleFormat_Float;
        }

        if (alIsExtensionPresent("AL_EXT_double")) {
            //supports double
            supported_formats << AudioFormat::SampleFormat_Double;
        }


    }
    ~AudioOutputOpenALPrivate() {
    }


    /*bool isFormatSupported(const AudioFormat &fmt)
    {
        if (audioFormatToAL(fmt) != 0)
            return true;

        //not supported
        if (fmt.sampleFormat() == AudioFormat::SampleFormat_Unsigned8)
            pref_format = AudioFormat::SampleFormat_Unsigned8;
        else
            pref_format = AudioFormat::SampleFormat_Signed16;

        if (fmt.channels() > 2)
            pref_channel_layout = AudioFormat::ChannelLayout_Stero;
        else
            pref_channel_layout = fmt.channelLayout();

        resample = true;
    }*/

    ALenum format_al;
    ALuint buffer[kBufferCount];
    ALuint source;
    ALint state;
    QElapsedTimer time;
    qint64 last_duration; //us, micro second
    qint64 err;
    QMutex mutex;
    QWaitCondition cond;
    AudioFormat::SampleFormat pref_format;
    AudioFormat::ChannelLayout pref_channel_layout;
    //bool resample;
    QList<AudioFormat::SampleFormat> supported_formats;
    int supported_channel_count;
};

AudioOutputOpenAL::AudioOutputOpenAL()
    :AudioOutput(*new AudioOutputOpenALPrivate())
{
}

AudioOutputOpenAL::~AudioOutputOpenAL()
{
    close();
}

bool AudioOutputOpenAL::open()
{
    DPTR_D(AudioOutputOpenAL);

    ALCenum err;

    d.available = false; // TODO: d.reset()
    QVector<QByteArray> _devices;
    const char *p = NULL;
    // maybe defined in alext.h
#ifdef ALC_ALL_DEVICES_SPECIFIER
    p = alcGetString(NULL, ALC_ALL_DEVICES_SPECIFIER);
#else
    if (alcIsExtensionPresent(NULL, "ALC_ENUMERATE_ALL_EXT")) {
        // avoid using enum ALC_ALL_DEVICES_SPECIFIER directly
        ALenum param = alcGetEnumValue(NULL, "ALC_ALL_DEVICES_SPECIFIER");
        p = alcGetString(NULL, param);
    } else {
        //alcIsExtensionPresent(NULL, "ALC_ENUMERATION_EXT")
        //ALenum param = alcGetEnumValue(NULL, "ALC_DEVICE_SPECIFIER");
        p = alcGetString(NULL, ALC_DEVICE_SPECIFIER);
    }
#endif

    err = alGetError();
    if (err != ALC_NO_ERROR) {
        qWarning("OpenAL alcGetString error: %s", alGetString(err));
    }

    while (p && *p) {
        _devices.push_back(p);
        p += _devices.last().size() + 1;
    }
    qDebug("OpenAL devices available: %d", _devices.size());
    for (int i = 0; i < _devices.size(); i++) {
        qDebug("device %d: %s", i, _devices[i].constData());
    }
    const ALCchar *default_device = alcGetString(NULL, ALC_DEFAULT_DEVICE_SPECIFIER);
    qDebug("AudioOutputOpenAL Opening default device: %s", default_device);
    ALCdevice *dev = alcOpenDevice(NULL); //parameter: NULL or default_device
    if (!dev) {
        qWarning("AudioOutputOpenAL Failed to open sound device: %s", alcGetString(0, alcGetError(0)));
        return false;
    }

    qDebug("AudioOutputOpenAL creating context...");
    ALCcontext *ctx = alcCreateContext(dev, NULL);
    err = alcGetError(dev);
    if (err != ALC_NO_ERROR) {
        qWarning("AudioOutputOpenAL context Error: %s", alcGetString(dev, err));
        return false;
    }

    alcMakeContextCurrent(ctx);
    //alcProcessContext(ctx); //used when dealing witg multiple contexts
    err = alcGetError(dev);
    if (err != ALC_NO_ERROR) {
        qWarning("AudioOutputOpenAL makeCurrent Error: %s", alcGetString(dev, err));
        return false;
    }
    //init params. move to another func?
    d.format_al = audioFormatToAL(audioFormat());
    //d.setALFormat();
    qDebug("OpenAL audio format: %#x ch:%d, sample format: %s", d.format_al, audioFormat().channels(), qPrintable(audioFormat().sampleFormatName()));

    alGenBuffers(kBufferCount, d.buffer);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning("Failed to generate OpenAL buffers: %s", alGetString(err));
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(dev);
        return false;
    }
    alGenSources(1, &d.source);
    err = alGetError();
    if (err != AL_NO_ERROR) {
        qWarning("Failed to generate OpenAL source: %s", alGetString(err));
        alDeleteBuffers(kBufferCount, d.buffer);
        alcMakeContextCurrent(NULL);
        alcDestroyContext(ctx);
        alcCloseDevice(dev);
        return false;
    }

    alSourcei(d.source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSourcei(d.source, AL_ROLLOFF_FACTOR, 0);
    alSource3f(d.source, AL_POSITION, 0.0, 0.0, 0.0);
    alSource3f(d.source, AL_VELOCITY, 0.0, 0.0, 0.0);
    alListener3f(AL_POSITION, 0.0, 0.0, 0.0);
    qDebug("AudioOutputOpenAL open ok...");
    d.state = 0;
    d.available = true;
    return true;
}

bool AudioOutputOpenAL::close()
{
    if (!isAvailable())
        return true;
    DPTR_D(AudioOutputOpenAL);
    d.state = 0;
    d.available = false;
    do {
        alGetSourcei(d.source, AL_SOURCE_STATE, &d.state);
    } while (alGetError() == AL_NO_ERROR && d.state == AL_PLAYING);
    alDeleteSources(1, &d.source);
    alDeleteBuffers(kBufferCount, d.buffer);

    ALCcontext *ctx = alcGetCurrentContext();
    ALCdevice *dev = alcGetContextsDevice(ctx);
    alcMakeContextCurrent(NULL);
    if (ctx) {
        alcDestroyContext(ctx);
        ALCenum err = alcGetError(dev);
        if (err != ALC_NO_ERROR) { //ALC_INVALID_CONTEXT
            qWarning("AudioOutputOpenAL Failed to destroy context: %s", alcGetString(dev, err));
            return false;
        }
    }
    if (dev) {
        alcCloseDevice(dev);
        ALCenum err = alcGetError(dev);
        if (err != ALC_NO_ERROR) { //ALC_INVALID_DEVICE
            qWarning("AudioOutputOpenAL Failed to close device: %s", alcGetString(dev, err));
            return false;
        }
    }
    return true;
}

bool AudioOutputOpenAL::isSupported(const AudioFormat& format) const
{
    return isSupported(format.sampleFormat()) && isSupported(format.channelLayout());
}

bool AudioOutputOpenAL::isSupported(AudioFormat::SampleFormat sampleFormat) const
{
    DPTR_D(const AudioOutputOpenAL);
    return d.supported_formats.contains(sampleFormat);
}

bool AudioOutputOpenAL::isSupported(AudioFormat::ChannelLayout channelLayout) const
{
    DPTR_D(const AudioOutputOpenAL);
    if (d.supported_channel_count == 2 && (channelLayout == AudioFormat::ChannelLayout_Stero || channelLayout == AudioFormat::ChannelLayout_Mono))
        return true;
    else if (d.supported_channel_count > 2)
        return true;
    return false;
}

AudioFormat::SampleFormat AudioOutputOpenAL::preferredSampleFormat() const
{
    DPTR_D(const AudioOutputOpenAL);
    return d.pref_format;
}

AudioFormat::ChannelLayout AudioOutputOpenAL::preferredChannelLayout() const
{
    DPTR_D(const AudioOutputOpenAL);
    return d.pref_channel_layout;
}

QString AudioOutputOpenAL::name() const
{
    ALCcontext *ctx = alcGetCurrentContext();
    ALCdevice *dev = alcGetContextsDevice(ctx);
    const ALCchar *name = alcGetString(dev, ALC_DEVICE_SPECIFIER);
    return name;
}

// http://kcat.strangesoft.net/openal-tutorial.html
bool AudioOutputOpenAL::write()
{
    DPTR_D(AudioOutputOpenAL);
    if (d.data.isEmpty())
        return false;
    QMutexLocker lock(&d.mutex);
    Q_UNUSED(lock);
    if (d.state == 0) {
        //// Initial all buffers
        alSourcef(d.source, AL_GAIN, d.vol);
        for (int i = 0; i < kBufferCount; ++i) {
            ALERROR_RETURN_F(alBufferData(d.buffer[i], d.format_al, d.data, d.data.size(), audioFormat().sampleRate()));
            alSourceQueueBuffers(d.source, 1, &d.buffer[i]);
        }
        //alSourceQueueBuffers(d.source, 3, d.buffer);
        alGetSourcei(d.source, AL_SOURCE_STATE, &d.state); //update d.state
        alSourcePlay(d.source);
        d.last_duration = audioFormat().durationForBytes(d.data.size())/1000LL*kBufferCount;
        d.time.start();
        return true;
    }
#if QT_VERSION >= QT_VERSION_CHECK(4, 8, 0)
    qint64 dt = d.last_duration - d.time.nsecsElapsed()/1000LL;
    //qDebug("duration: %lld, dt: %lld", d.last_duration, dt);
    d.last_duration = audioFormat().durationForBytes(d.data.size());
    // TODO: how to control the error?
#else
    qint64 dt = d.last_duration - d.time.elapsed()*1000;
    //qDebug("duration: %lld, dt: %lld", d.last_duration, dt);
    d.last_duration = audioFormat().durationForBytes(d.data.size());
#endif //QT_VERSION >= QT_VERSION_CHECK(4, 8, 0)
    if (dt > 0LL)
        d.cond.wait(&d.mutex, (ulong)(dt/1000LL));

    ALint processed = 0;
    alGetSourcei(d.source, AL_BUFFERS_PROCESSED, &processed);
    if (processed <= 0) {
        alGetSourcei(d.source, AL_SOURCE_STATE, &d.state);
        if (d.state != AL_PLAYING) {
            alSourcePlay(d.source);
        }
        //return false;
    }
    const char* b = d.data.constData();
    int remain = d.data.size();
    while (processed--) {
        if (remain <= 0)
            break;
        ALuint buf;
        //unqueues a set of buffers attached to a source
        alSourceUnqueueBuffers(d.source, 1, &buf);
        alBufferData(buf, d.format_al, b, qMin(remain, kBufferSize), audioFormat().sampleRate());
        alSourceQueueBuffers(d.source, 1, &buf);
        b += kBufferSize;
        remain -= kBufferSize;
//        qDebug("remain: %d", remain);
        ALenum err = alGetError();
        if (err != AL_NO_ERROR) { //return ?
            qWarning("AudioOutputOpenAL Error: %s ---remain=%d", alGetString(err), remain);
            return false;
        }
    }
    d.time.restart();
    alGetSourcei(d.source, AL_SOURCE_STATE, &d.state);
    if (d.state != AL_PLAYING) {
        //qDebug("AudioOutputOpenAL: !AL_PLAYING alSourcePlay");
        alSourcePlay(d.source);
        d.time.restart();
    }
    return true;
}

} //namespace QtAV
