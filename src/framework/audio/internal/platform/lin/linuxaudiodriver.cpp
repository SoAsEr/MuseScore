/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore BVBA and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "linuxaudiodriver.h"

#define ALSA_PCM_NEW_HW_PARAMS_API
#include <alsa/asoundlib.h>

#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <pthread.h>

#include "log.h"
#include "runtime.h"

static constexpr char DEFAULT_DEVICE_ID[] = "default";

using namespace mu::audio;

namespace {
struct ALSAData
{
    float* buffer = nullptr;
    snd_pcm_t* alsaDeviceHandle = nullptr;
    unsigned long samples = 0;
    int channels = 0;
    bool audioProcessingDone = false;
    pthread_t threadHandle = 0;
    IAudioDriver::Callback callback;
    void* userdata = nullptr;
};

static ALSAData* s_alsaData{ nullptr };
IAudioDriver::Spec s_format;

static void* alsaThread(void* aParam)
{
    mu::runtime::setThreadName("audio_driver");
    ALSAData* data = static_cast<ALSAData*>(aParam);

    int ret = snd_pcm_wait(data->alsaDeviceHandle, 1000);
    IF_ASSERT_FAILED(ret > 0) {
        return nullptr;
    }

    while (!data->audioProcessingDone)
    {
        uint8_t* stream = (uint8_t*)data->buffer;
        int len = data->samples * data->channels * sizeof(float);

        data->callback(data->userdata, stream, len);

        snd_pcm_sframes_t pcm = snd_pcm_writei(data->alsaDeviceHandle, data->buffer, data->samples);
        if (pcm != -EPIPE) {
        } else {
            snd_pcm_prepare(data->alsaDeviceHandle);
        }
    }

    LOGI() << "exit";
    return nullptr;
}

static void alsaCleanup()
{
    if (!s_alsaData) {
        return;
    }

    s_alsaData->audioProcessingDone = true;
    if (s_alsaData->threadHandle) {
        pthread_join(s_alsaData->threadHandle, nullptr);
    }
    snd_pcm_drain(s_alsaData->alsaDeviceHandle);
    snd_pcm_close(s_alsaData->alsaDeviceHandle);

    if (nullptr != s_alsaData->buffer) {
        delete[] s_alsaData->buffer;
    }

    delete s_alsaData;
    s_alsaData = nullptr;
}
}

LinuxAudioDriver::LinuxAudioDriver()
{
    m_deviceId = DEFAULT_DEVICE_ID;
}

LinuxAudioDriver::~LinuxAudioDriver()
{
    alsaCleanup();
}

void LinuxAudioDriver::init()
{
    m_devicesListener.startWithCallback([this]() {
        return availableOutputDevices();
    });

    m_devicesListener.devicesChanged().onNotify(this, [this]() {
        m_availableOutputDevicesChanged.notify();
    });
}

std::string LinuxAudioDriver::name() const
{
    return "MUAUDIO(ALSA)";
}

bool LinuxAudioDriver::open(const Spec& spec, Spec* activeSpec)
{
    s_alsaData = new ALSAData();
    s_alsaData->samples = spec.samples;
    s_alsaData->channels = spec.channels;
    s_alsaData->callback = spec.callback;
    s_alsaData->userdata = spec.userdata;

    int rc;
    snd_pcm_t* handle;
    rc = snd_pcm_open(&handle, outputDevice().c_str(), SND_PCM_STREAM_PLAYBACK, 0);
    if (rc < 0) {
        return false;
    }

    s_alsaData->alsaDeviceHandle = handle;

    snd_pcm_hw_params_t* params;
    snd_pcm_hw_params_alloca(&params);
    snd_pcm_hw_params_any(handle, params);

    snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
    snd_pcm_hw_params_set_format(handle, params, SND_PCM_FORMAT_FLOAT_LE);
    snd_pcm_hw_params_set_channels(handle, params, spec.channels);

    unsigned int aSamplerate = spec.sampleRate;
    unsigned int val = aSamplerate;
    int dir = 0;
    rc = snd_pcm_hw_params_set_rate_near(handle, params, &val, &dir);
    if (rc < 0) {
        return false;
    }

    snd_pcm_hw_params_set_buffer_size_near(handle, params, &s_alsaData->samples);

    rc = snd_pcm_hw_params(handle, params);
    if (rc < 0) {
        return false;
    }

    snd_pcm_hw_params_get_rate(params, &val, &dir);
    aSamplerate = val;

    s_alsaData->buffer = new float[s_alsaData->samples * s_alsaData->channels];
    //_alsaData->sampleBuffer = new short[_alsaData->samples * _alsaData->channels];

    if (activeSpec) {
        *activeSpec = spec;
        activeSpec->format = Format::AudioF32;
        activeSpec->sampleRate = aSamplerate;
        s_format = *activeSpec;
    }

    s_alsaData->threadHandle = 0;
    int ret = pthread_create(&s_alsaData->threadHandle, NULL, alsaThread, (void*)s_alsaData);

    if (0 != ret) {
        return false;
    }

    LOGD() << "Connected to " << outputDevice();
    return true;
}

void LinuxAudioDriver::close()
{
    alsaCleanup();
}

bool LinuxAudioDriver::isOpened() const
{
    return s_alsaData != nullptr;
}

AudioDeviceID LinuxAudioDriver::outputDevice() const
{
    return m_deviceId;
}

bool LinuxAudioDriver::selectOutputDevice(const AudioDeviceID& deviceId)
{
    if (m_deviceId == deviceId) {
        return true;
    }

    bool reopen = isOpened();
    close();
    m_deviceId = deviceId;

    bool ok = true;
    if (reopen) {
        ok = open(s_format, &s_format);
    }

    if (ok) {
        m_outputDeviceChanged.notify();
    }

    return ok;
}

bool LinuxAudioDriver::resetToDefaultOutputDevice()
{
    return selectOutputDevice(DEFAULT_DEVICE_ID);
}

mu::async::Notification LinuxAudioDriver::outputDeviceChanged() const
{
    return m_outputDeviceChanged;
}

AudioDeviceList LinuxAudioDriver::availableOutputDevices() const
{
    AudioDeviceList devices;

    snd_ctl_card_info_t* cardInfo;
    snd_ctl_card_info_alloca(&cardInfo);
    snd_pcm_info_t* pcmInfo;
    snd_pcm_info_alloca(&pcmInfo);
    int cardIndex = -1;

    while (!snd_card_next(&cardIndex) && cardIndex >= 0)
    {
        const QString dev = "hw:" + QString::number(cardIndex);
        snd_ctl_t* ctl;
        if (snd_ctl_open(&ctl, dev.toLocal8Bit(), 0)) {
            continue;
        }

        if (!snd_ctl_card_info(ctl, cardInfo)) {
            const QString cardName = snd_ctl_card_info_get_name(cardInfo);
            int devIndex = -1;
            while (!snd_ctl_pcm_next_device(ctl, &devIndex) && devIndex >= 0)
            {
                snd_pcm_info_set_device(pcmInfo, static_cast<unsigned>(devIndex));
                snd_pcm_info_set_stream(pcmInfo, SND_PCM_STREAM_PLAYBACK);

                if (snd_ctl_pcm_info(ctl, pcmInfo) >= 0) {
                    const QString pcmName = snd_pcm_info_get_name(pcmInfo);
                    QString deviceId = dev + "," + QString::number(devIndex);
                    QString name = cardName + (!pcmName.isEmpty() ? QString(": ") + snd_pcm_info_get_name(pcmInfo) : QString());
                    devices.push_back({ deviceId.toStdString(), name.toStdString() });
                }
            }
        }

        snd_ctl_close(ctl);
    }

    if (devices.size() > 0) {
        devices.insert(devices.begin(), { DEFAULT_DEVICE_ID, DEFAULT_DEVICE_ID });
    }

    return devices;
}

mu::async::Notification LinuxAudioDriver::availableOutputDevicesChanged() const
{
    return m_availableOutputDevicesChanged;
}

void LinuxAudioDriver::resume()
{
}

void LinuxAudioDriver::suspend()
{
}
