/*
 * DISTRHO Plugin Framework (DPF)
 * Copyright (C) 2012-2022 Filipe Coelho <falktx@falktx.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any purpose with
 * or without fee is hereby granted, provided that the above copyright notice and this
 * permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES WITH REGARD
 * TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 * DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DISTRHO_STANDALONE_UTILS_HPP_INCLUDED
#define DISTRHO_STANDALONE_UTILS_HPP_INCLUDED

#include "src/DistrhoDefines.h"

#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

/* ------------------------------------------------------------------------------------------------------------
 * Standalone plugin related utilities */

/**
   @defgroup StandalonePluginRelatedUtilities Plugin related utilities

   When the plugin is running as standalone and JACK is not available, a native audio handling is in place.
   It is a very simple handling, auto-connecting to the default audio interface for outputs.

   !!EXPERIMENTAL!!

   Still under development and testing.

   @{
 */

/**
   Check if the current standalone is using native audio methods.
   If this function returns false, you MUST NOT use any other function from this group.
*/
bool isUsingNativeAudio() noexcept;

/**
   Check if the current standalone supports audio input.
*/
bool supportsAudioInput();

/**
   Check if the current standalone supports dynamic buffer size changes.
*/
bool supportsBufferSizeChanges();

/**
   Check if the current standalone supports MIDI.
*/
bool supportsMIDI();

/**
   Check if the current standalone has audio input enabled.
*/
bool isAudioInputEnabled();

/**
   Check if the current standalone has MIDI enabled.
*/
bool isMIDIEnabled();

/**
   Get the current buffer size.
*/
uint getBufferSize();

/**
   Request permissions to use audio input.
   Only valid to call if audio input is supported but not currently enabled.
*/
bool requestAudioInput();

/**
   Request change to a new buffer size.
*/
bool requestBufferSizeChange(uint newBufferSize);

/**
   Request permissions to use MIDI.
   Only valid to call if MIDI is supported but not currently enabled.
*/
bool requestMIDI();

/** @} */

/* ------------------------------------------------------------------------------------------------------------
 * Mend addition: audio device choice for the standalone apps (native audio through mend-audio).
 * Outside a Mend standalone these are stubs: supportsAudioDeviceSelection() returns false. */

struct StandaloneAudioState {
    // the devices the system offers (filled when asked to list them)
    std::vector<std::string> inputs, outputs;
    std::string defaultInput, defaultOutput, backend;

    // the choice (as saved); empty device = system default
    bool inputOn = false;
    std::string input, output;
    int inputLeft = 1, inputRight = 2; // 1-based device channels; equal = mono
    uint sampleRate = 0;               // 0 = the device's own
    uint bufferSize = 256;

    // what is running
    bool running = false, inputActive = false;
    std::string activeInput, activeOutput;
    uint inputChannels = 0, activeSampleRate = 0, activeBufferSize = 0;
    double latencyMs = 0.0;
    std::string note; // why the running devices differ from the choice, or empty
};

/** True in a Mend standalone app using its own audio (not JACK). */
bool supportsAudioDeviceSelection();

/** The current choice and running state; with listDevices, also the device lists. */
StandaloneAudioState getStandaloneAudioState(bool listDevices);

/** Use and save a new choice (the device fields of @a wanted). False if it couldn't be used as asked;
    getStandaloneAudioState() then says what is running and why. */
bool setStandaloneAudio(const StandaloneAudioState& wanted);

/** Call regularly from the UI thread: follows unplugged and returning devices. True if audio was reopened. */
bool standaloneAudioIdle();

// -----------------------------------------------------------------------------------------------------------

END_NAMESPACE_DISTRHO

#endif // DISTRHO_STANDALONE_UTILS_HPP_INCLUDED
