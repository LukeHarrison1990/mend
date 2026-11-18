// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Luke Harrison and Mend contributors
#include "AudioFile.hpp"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#elif defined(__APPLE__)
#include <AudioToolbox/ExtendedAudioFile.h>
#include <CoreFoundation/CoreFoundation.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

#include <opusfile.h>

#include <cstdio>
#include <cstdlib>

namespace mend::editor {

namespace {

#if defined(_WIN32)
const char* kFormatsHint = "Mend reads WAV, AIFF, FLAC, MP3, Ogg Vorbis, Opus, AAC and ALAC (M4A) and WMA.";
#elif defined(__APPLE__)
const char* kFormatsHint = "Mend reads WAV, AIFF, CAF, FLAC, MP3, Ogg Vorbis, Opus, and AAC and ALAC (M4A).";
#else
const char* kFormatsHint = "Mend reads WAV, AIFF, FLAC, MP3, Ogg Vorbis and Opus, plus AAC, ALAC, WMA and more when FFmpeg is installed.";
#endif

std::string lowerExt(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
        return {};
    std::string e = path.substr(dot + 1);
    for (auto& c : e)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return e;
}

#ifdef _WIN32
std::wstring widen(const std::string& s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}
#endif

FILE* openRead(const std::string& path)
{
#ifdef _WIN32
    return _wfopen(widen(path).c_str(), L"rb");
#else
    return std::fopen(path.c_str(), "rb");
#endif
}

// First bytes of the file, for telling containers apart.
std::string sniff(const std::string& path, size_t n)
{
    std::string head(n, '\0');
    FILE* f = openRead(path);
    if (f == nullptr)
        return {};
    head.resize(std::fread(&head[0], 1, n, f));
    std::fclose(f);
    return head;
}

bool deinterleave(const float* x, size_t frames, int nch, AudioBuffer& buf)
{
    for (size_t i = 0; i < frames; ++i)
        for (int c = 0; c < nch; ++c)
            buf.channels[static_cast<size_t>(c)].push_back(x[i * nch + c]);
    return true;
}

// ---- miniaudio: WAV, AIFF, FLAC, MP3, Ogg Vorbis ----------------------------
bool decodeMiniaudio(const std::string& path, AudioBuffer& out)
{
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0); // native channels and rate
    ma_decoder dec;
#ifdef _WIN32
    const ma_result opened = ma_decoder_init_file_w(widen(path).c_str(), &cfg, &dec);
#else
    const ma_result opened = ma_decoder_init_file(path.c_str(), &cfg, &dec);
#endif
    if (opened != MA_SUCCESS)
        return false;
    const int nch = static_cast<int>(dec.outputChannels);
    const double rate = dec.outputSampleRate;
    if (nch < 1 || rate <= 0) {
        ma_decoder_uninit(&dec);
        return false;
    }
    AudioBuffer buf;
    buf.sampleRate = rate;
    buf.channels.assign(static_cast<size_t>(nch), {});
    ma_uint64 expected = 0;
    if (ma_decoder_get_length_in_pcm_frames(&dec, &expected) == MA_SUCCESS && expected > 0)
        for (auto& c : buf.channels)
            c.reserve(static_cast<size_t>(expected));
    std::vector<float> chunk(static_cast<size_t>(8192) * nch);
    for (;;) {
        ma_uint64 got = 0;
        const ma_result r = ma_decoder_read_pcm_frames(&dec, chunk.data(), 8192, &got);
        deinterleave(chunk.data(), static_cast<size_t>(got), nch, buf);
        if (r != MA_SUCCESS || got < 8192)
            break;
    }
    ma_decoder_uninit(&dec);
    if (buf.empty())
        return false;
    out = std::move(buf);
    return true;
}

// ---- Opus (Ogg), via opusfile ----------------------------------------------
bool decodeOpus(const std::string& path, AudioBuffer& out, std::string& error)
{
    int err = 0;
    OggOpusFile* of = op_open_file(path.c_str(), &err); // UTF-8 paths on every platform
    if (of == nullptr) {
        error = "This Opus file couldn't be read (it may be damaged).";
        return false;
    }
    // chained files can change channel count between links: use stereo then
    int nch = op_channel_count(of, 0);
    bool stereo = false;
    for (int l = 1; l < op_link_count(of); ++l)
        if (op_channel_count(of, l) != nch)
            stereo = true;
    if (stereo)
        nch = 2;
    AudioBuffer buf;
    buf.sampleRate = 48000.0; // Opus always decodes at 48 kHz
    buf.channels.assign(static_cast<size_t>(nch), {});
    const ogg_int64_t total = op_pcm_total(of, -1);
    if (total > 0)
        for (auto& c : buf.channels)
            c.reserve(static_cast<size_t>(total));
    std::vector<float> pcm(static_cast<size_t>(5760) * std::max(nch, 2) * 2);
    for (;;) {
        const int got = stereo ? op_read_float_stereo(of, pcm.data(), static_cast<int>(pcm.size()))
                               : op_read_float(of, pcm.data(), static_cast<int>(pcm.size()), nullptr);
        if (got == OP_HOLE)
            continue; // a gap in the stream: skip it
        if (got <= 0)
            break;
        deinterleave(pcm.data(), static_cast<size_t>(got), nch, buf);
    }
    op_free(of);
    if (buf.empty()) {
        error = "The file has no audio.";
        return false;
    }
    out = std::move(buf);
    return true;
}

// ---- the operating system's decoders (AAC, ALAC, WMA, ...) ------------------
#if defined(_WIN32)
template <typename T>
void release(T*& p)
{
    if (p != nullptr) {
        p->Release();
        p = nullptr;
    }
}

// Media Foundation is loaded at run time, so Mend still starts on Windows
// "N" editions that ship without it.
struct MfApi {
    HMODULE plat = nullptr, rw = nullptr;
    HRESULT(WINAPI* startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* shutdown)() = nullptr;
    HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* createReader)(LPCWSTR, IMFAttributes*, IMFSourceReader**) = nullptr;
    bool ok() const { return startup && shutdown && createMediaType && createReader; }
};

template <typename F>
void loadFn(F& f, HMODULE m, const char* name)
{
    f = m != nullptr ? reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(m, name))) : nullptr;
}

const MfApi& mf()
{
    static const MfApi api = [] {
        MfApi a;
        a.plat = LoadLibraryW(L"mfplat.dll");
        a.rw = LoadLibraryW(L"mfreadwrite.dll");
        loadFn(a.startup, a.plat, "MFStartup");
        loadFn(a.shutdown, a.plat, "MFShutdown");
        loadFn(a.createMediaType, a.plat, "MFCreateMediaType");
        loadFn(a.createReader, a.rw, "MFCreateSourceReaderFromURL");
        return a;
    }();
    return api;
}

bool decodePlatform(const std::string& path, AudioBuffer& out, std::string& error)
{
    const MfApi& api = mf();
    if (!api.ok()) {
        error = std::string("Windows Media Foundation isn't installed, so this format can't be read. ") + kFormatsHint;
        return false;
    }
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool coOwned = SUCCEEDED(co);
    if (FAILED(api.startup(MF_VERSION, MFSTARTUP_LITE))) {
        if (coOwned)
            CoUninitialize();
        error = "Windows Media Foundation isn't available, so this format can't be read.";
        return false;
    }
    bool ok = false;
    IMFSourceReader* reader = nullptr;
    IMFMediaType* want = nullptr;
    IMFMediaType* got = nullptr;
    const DWORD audio = static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM);
    do {
        if (FAILED(api.createReader(widen(path).c_str(), nullptr, &reader)))
            break;
        reader->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
        if (FAILED(reader->SetStreamSelection(audio, TRUE)))
            break;
        if (FAILED(api.createMediaType(&want)))
            break;
        want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float);
        want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
        if (FAILED(reader->SetCurrentMediaType(audio, nullptr, want)))
            break;
        if (FAILED(reader->GetCurrentMediaType(audio, &got)))
            break;
        UINT32 nch = 0, rate = 0;
        got->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &nch);
        got->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate);
        if (nch == 0 || rate == 0)
            break;
        AudioBuffer buf;
        buf.sampleRate = rate;
        buf.channels.assign(nch, {});
        bool failed = false;
        for (;;) {
            DWORD flags = 0;
            IMFSample* sample = nullptr;
            if (FAILED(reader->ReadSample(audio, 0, nullptr, &flags, nullptr, &sample))) {
                failed = true;
                break;
            }
            if (sample != nullptr) {
                IMFMediaBuffer* mb = nullptr;
                if (SUCCEEDED(sample->ConvertToContiguousBuffer(&mb))) {
                    BYTE* data = nullptr;
                    DWORD len = 0;
                    if (SUCCEEDED(mb->Lock(&data, nullptr, &len))) {
                        deinterleave(reinterpret_cast<const float*>(data), len / (4 * nch), static_cast<int>(nch), buf);
                        mb->Unlock();
                    }
                    release(mb);
                }
                release(sample);
            }
            if (flags & (MF_SOURCE_READERF_ENDOFSTREAM | MF_SOURCE_READERF_ERROR))
                break;
        }
        if (!failed && !buf.empty()) {
            out = std::move(buf);
            ok = true;
        }
    } while (false);
    release(got);
    release(want);
    release(reader);
    api.shutdown();
    if (coOwned)
        CoUninitialize();
    if (!ok)
        error = std::string("Couldn't read this file. ") + kFormatsHint;
    return ok;
}
#elif defined(__APPLE__)
bool decodePlatform(const std::string& path, AudioBuffer& out, std::string& error)
{
    error = std::string("Couldn't read this file. ") + kFormatsHint;
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(nullptr, reinterpret_cast<const UInt8*>(path.c_str()),
                                                           static_cast<CFIndex>(path.size()), false);
    if (url == nullptr)
        return false;
    ExtAudioFileRef f = nullptr;
    const OSStatus st = ExtAudioFileOpenURL(url, &f);
    CFRelease(url);
    if (st != noErr || f == nullptr)
        return false;
    AudioStreamBasicDescription in {};
    UInt32 size = sizeof(in);
    bool ok = false;
    if (ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &size, &in) == noErr && in.mChannelsPerFrame > 0
        && in.mSampleRate > 0) {
        const UInt32 nch = in.mChannelsPerFrame;
        AudioStreamBasicDescription client {};
        client.mSampleRate = in.mSampleRate;
        client.mFormatID = kAudioFormatLinearPCM;
        client.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        client.mBitsPerChannel = 32;
        client.mChannelsPerFrame = nch;
        client.mFramesPerPacket = 1;
        client.mBytesPerFrame = 4 * nch;
        client.mBytesPerPacket = 4 * nch;
        if (ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof(client), &client) == noErr) {
            AudioBuffer buf;
            buf.sampleRate = in.mSampleRate;
            buf.channels.assign(nch, {});
            std::vector<float> chunk(static_cast<size_t>(8192) * nch);
            for (;;) {
                AudioBufferList abl;
                abl.mNumberBuffers = 1;
                abl.mBuffers[0].mNumberChannels = nch;
                abl.mBuffers[0].mDataByteSize = static_cast<UInt32>(chunk.size() * 4);
                abl.mBuffers[0].mData = chunk.data();
                UInt32 frames = 8192;
                if (ExtAudioFileRead(f, &frames, &abl) != noErr || frames == 0)
                    break;
                deinterleave(chunk.data(), frames, static_cast<int>(nch), buf);
            }
            if (!buf.empty()) {
                out = std::move(buf);
                ok = true;
            }
        }
    }
    ExtAudioFileDispose(f);
    return ok;
}
#else
// Linux and others: hand the file to FFmpeg if it's installed.
bool decodePlatform(const std::string& path, AudioBuffer& out, std::string& error)
{
    char tmpl[] = "/tmp/mend-decode-XXXXXX";
    const int fd = mkstemp(tmpl);
    if (fd < 0) {
        error = "Couldn't create a temporary file.";
        return false;
    }
    close(fd);
    const std::string tmp = std::string(tmpl);
    const std::string wav = tmp + ".wav";
    std::vector<std::string> args { "ffmpeg", "-nostdin", "-v", "error", "-y", "-i", path, "-vn", "-c:a", "pcm_f32le", wav };
    std::vector<char*> argv;
    for (auto& a : args)
        argv.push_back(&a[0]);
    argv.push_back(nullptr);
    pid_t pid = 0;
    bool ok = false;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0); // keep FFmpeg's messages out of our stderr
    const int spawned = posix_spawnp(&pid, "ffmpeg", &fa, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (spawned == 0) {
        int status = 0;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
            ok = decodeMiniaudio(wav, out);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 127)
            pid = 0;
    } else {
        pid = 0;
    }
    std::remove(wav.c_str());
    std::remove(tmp.c_str());
    if (!ok)
        error = pid == 0 ? std::string("This format needs FFmpeg on Linux (install the 'ffmpeg' package). ") + kFormatsHint
                         : std::string("Couldn't read this file. ") + kFormatsHint;
    return ok;
}
#endif

} // namespace

bool loadAudioFile(const std::string& path, AudioBuffer& out, std::string& error)
{
    const std::string head = sniff(path, 64);
    if (head.empty()) {
        error = "Couldn't open the file. Check it still exists and isn't locked by another program.";
        return false;
    }
    // Ogg Opus: magic "OggS" with an "OpusHead" packet in the first page
    if (head.compare(0, 4, "OggS") == 0 && head.size() >= 36 && head.compare(28, 8, "OpusHead") == 0)
        return decodeOpus(path, out, error);

    // Containers miniaudio doesn't understand go straight to the system decoders,
    // so a stray MP3-like byte pattern inside them can't be misread as audio.
    const std::string ext = lowerExt(path);
    static const char* platformOnly[] = { "m4a", "m4b", "mp4", "aac", "adts", "alac", "caf", "wma", "asf",
                                          "3gp", "3g2", "webm", "mka", "mkv", "ac3", "ec3", "amr", "mov" };
    bool platformFirst = std::find_if(std::begin(platformOnly), std::end(platformOnly),
                                      [&](const char* e) { return ext == e; })
        != std::end(platformOnly);
    if (head.size() >= 8 && head.compare(4, 4, "ftyp") == 0) // MP4 family, whatever the extension
        platformFirst = true;

    if (!platformFirst && decodeMiniaudio(path, out))
        return true;
    if (decodePlatform(path, out, error))
        return true;
    if (platformFirst && error.empty())
        error = std::string("Couldn't read this file. ") + kFormatsHint;
    if (error.empty())
        error = std::string("Couldn't read this file. ") + kFormatsHint;
    return false;
}

const char* supportedFormatsText() { return kFormatsHint; }

bool saveWavFile(const std::string& path, const AudioBuffer& audio, WavFormat format, std::string& error)
{
    const int nch = audio.numChannels();
    if (nch == 0) {
        error = "Nothing to save.";
        return false;
    }
    const ma_format f = format == WavFormat::Int16 ? ma_format_s16 : format == WavFormat::Int24 ? ma_format_s24 : ma_format_f32;
    ma_encoder_config cfg = ma_encoder_config_init(ma_encoding_format_wav, f, static_cast<ma_uint32>(nch),
                                                   static_cast<ma_uint32>(std::lround(audio.sampleRate)));
    ma_encoder enc;
#ifdef _WIN32
    const ma_result opened = ma_encoder_init_file_w(widen(path).c_str(), &cfg, &enc);
#else
    const ma_result opened = ma_encoder_init_file(path.c_str(), &cfg, &enc);
#endif
    if (opened != MA_SUCCESS) {
        error = "Couldn't create the file. Check the folder exists and isn't read-only.";
        return false;
    }

    const size_t frames = audio.numFrames();
    const size_t block = 8192;
    const int bytesPerSample = format == WavFormat::Int16 ? 2 : format == WavFormat::Int24 ? 3 : 4;
    std::vector<uint8_t> chunk(block * nch * bytesPerSample);
    bool ok = true;
    for (size_t pos = 0; pos < frames && ok; pos += block) {
        const size_t n = std::min(block, frames - pos);
        for (size_t i = 0; i < n; ++i) {
            for (int c = 0; c < nch; ++c) {
                const float v = audio.channels[c][pos + i];
                uint8_t* dst = chunk.data() + (i * nch + c) * bytesPerSample;
                if (format == WavFormat::Float32) {
                    std::memcpy(dst, &v, 4);
                } else {
                    // TPDF-free plain rounding; clip to range
                    const double scale = format == WavFormat::Int16 ? 32767.0 : 8388607.0;
                    const double s = std::clamp(static_cast<double>(v), -1.0, 1.0) * scale;
                    const int32_t q = static_cast<int32_t>(std::lround(s));
                    dst[0] = static_cast<uint8_t>(q & 0xff);
                    dst[1] = static_cast<uint8_t>((q >> 8) & 0xff);
                    if (bytesPerSample == 3)
                        dst[2] = static_cast<uint8_t>((q >> 16) & 0xff);
                }
            }
        }
        ma_uint64 written = 0;
        if (ma_encoder_write_pcm_frames(&enc, chunk.data(), n, &written) != MA_SUCCESS || written != n)
            ok = false;
    }
    ma_encoder_uninit(&enc);
    if (!ok)
        error = "Writing the file failed (disk full?).";
    return ok;
}

} // namespace mend::editor
