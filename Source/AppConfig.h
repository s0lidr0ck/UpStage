#pragma once

/** Project-wide compile-time constants.
 *  Include this in any file that references NUM_CHANNELS or NUM_SCENES.
 *
 *  JUCE_PLUGINHOST_VST3 must also be set as a preprocessor definition in your
 *  IDE project (VS2022: Project Properties → C/C++ → Preprocessor Definitions).
 *  The .jucer file already sets it for Projucer-generated builds.
 */

static constexpr int NUM_CHANNELS = 4;
static constexpr int NUM_SCENES   = 8;
