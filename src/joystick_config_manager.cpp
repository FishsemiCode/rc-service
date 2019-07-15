/*
 * Copyright (C) 2019 FishSemi Inc. All rights reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cmath>
#include <utils/Log.h>
#include "joystick_config_manager.h"

#define DEFAULT_MIN_CHANNEL_VALUE 364
#define DEFAULT_MAX_CHANNEL_VALUE 1684

const char* JoystickConfigManager::sFunctionSettingsKey[JoystickConfigManager::maxFunction] = {
    "RollAxis",
    "PitchAxis",
    "YawAxis",
    "ThrottleAxis",
    "WheelAxis"
};

JoystickConfigManager::JoystickConfigManager(const string &filename)
    :mFileName(filename)
{
    mAxisCount = maxFunction;
    mCalibrations = new Calibration_t[mAxisCount];
    mAxisValues = new int[mAxisCount];

    for (int i = 0; i < mAxisCount; i++) {
        mAxisValues[i] = 0;
    }

    mLoader = new ConfigLoader();
    loadSettings();
}

JoystickConfigManager::~JoystickConfigManager()
{
    delete[] mCalibrations;
    delete[] mAxisValues;
    delete mLoader;
}

void JoystickConfigManager::setAxisValue(int axis, int value)
{
    mAxisValues[axis] = value;
}

int JoystickConfigManager::getFunctionChannel(int function)
{
    if (mFunctionChannels[function] > 0 && mFunctionChannels[function] <= 4) {
        return mFunctionChannels[function];
    }

    return function + 1;
}

void JoystickConfigManager::getJoystickControls(Controls_t *controls)
{
    int     axis = mFunctionAxis[rollFunction];
    float   roll = adjustRange(mAxisValues[axis], mCalibrations[axis], mDeadband);

            axis = mFunctionAxis[pitchFunction];
    float   pitch = adjustRange(mAxisValues[axis], mCalibrations[axis], mDeadband);

            axis = mFunctionAxis[yawFunction];
    float   yaw = adjustRange(mAxisValues[axis], mCalibrations[axis], mDeadband);

            axis = mFunctionAxis[throttleFunction];
    float   throttle = adjustRange(mAxisValues[axis], mCalibrations[axis], mThrottleMode == ThrottleModeDownZero ? false : mDeadband);

            axis = mFunctionAxis[wheelFunction];
    float   wheel = adjustRange(mAxisValues[axis], mCalibrations[axis], mDeadband);

    if (mAccumulator) {
        static float throttle_accu = 0.f;

        throttle_accu += throttle * (40 / 1000.f);
        throttle_accu = max(static_cast<float>(-1.f), min(throttle_accu, static_cast<float>(1.f)));
        throttle = throttle_accu;
    }

    if (mCircleCorrection) {
        float roll_limited = std::max(static_cast<float>(-M_PI_4), std::min(roll, static_cast<float>(M_PI_4)));
        float pitch_limited = std::max(static_cast<float>(-M_PI_4), std::min(pitch, static_cast<float>(M_PI_4)));
        float yaw_limited = std::max(static_cast<float>(-M_PI_4), std::min(yaw, static_cast<float>(M_PI_4)));
        float throttle_limited = std::max(static_cast<float>(-M_PI_4), std::min(throttle, static_cast<float>(M_PI_4)));
        float wheel_limited = std::max(static_cast<float>(-M_PI_4), std::min(wheel, static_cast<float>(M_PI_4)));

        /* Map from unit circle to linear range and limit */
        roll =      std::max(-1.0f, std::min(tanf(asinf(roll_limited)), 1.0f));
        pitch =     std::max(-1.0f, std::min(tanf(asinf(pitch_limited)), 1.0f));
        yaw =       std::max(-1.0f, std::min(tanf(asinf(yaw_limited)), 1.0f));
        throttle =  std::max(-1.0f, std::min(tanf(asinf(throttle_limited)), 1.0f));
        wheel =     std::max(-1.0f, std::min(tanf(asinf(wheel_limited)), 1.0f));
    }

    if (mExponential != 0) {
        roll =      -mExponential*powf(roll,3) + (1+mExponential) * roll;
        pitch =     -mExponential*powf(pitch,3) + (1+mExponential) * pitch;
        yaw =       -mExponential*powf(yaw,3) + (1+mExponential) * yaw;
        wheel =     -mExponential*powf(wheel,3) + (1+mExponential) * wheel;
    }

    if (mThrottleMode == ThrottleModeCenterZero && mCenterZeroSupport) {
        if (!mCenterZeroSupport || !mNegativeThrust) {
            throttle = std::max(0.0f, throttle);
        }
    } else {
        throttle = (throttle + 1.0f) / 2.0f;
    }

    controls->roll = roll;
    controls->pitch = pitch;
    controls->yaw = yaw;
    controls->throttle = throttle;
    controls->wheel = wheel;
}

int JoystickConfigManager::getMinChannelValue()
{
    return mMinChannelValue;
}

int JoystickConfigManager::getMaxChannelValue()
{
    return mMaxChannelValue;
}

float JoystickConfigManager::getMessageFrequency()
{
    return mFrequency;
}

void JoystickConfigManager::reloadSettings()
{
    loadSettings();
}

void JoystickConfigManager::loadSettings()
{
    mLoader->loadConfig(mFileName);

    mLoader->beginSection("Basic");
    mCalibrated = mLoader->getBool("calibrated");
    mTransmitterMode = mLoader->getInt("transmitterMode", 2);
    mLoader->endSection();

    char section[30];
    const char *str = "Axis%dCalibration";
    for (int axis = 0; axis < mAxisCount; axis++) {
        sprintf(section, str, axis);
        mLoader->beginSection(section);
        mCalibrations[axis].min = mLoader->getInt("AxisMin", -32768);
        mCalibrations[axis].max = mLoader->getInt("AxisMax", 32767);
        mCalibrations[axis].center = mLoader->getInt("AxisTrim");
        mCalibrations[axis].deadband = mLoader->getInt("AxisDeadband");
        mCalibrations[axis].reversed = mLoader->getBool("AxisRev");
        mLoader->endSection();
    }

    mLoader->beginSection("Function");
    for (int function = 0; function < maxFunction; function++) {
        int axis = mLoader->getInt(sFunctionSettingsKey[function], function);
        /* if function axis in config file is negative, use default values */
        mFunctionAxis[function] = axis < 0 ? function : axis;
    }
    mLoader->endSection();
    remapAxes(2, mTransmitterMode, mFunctionAxis);

    mLoader->beginSection("FunctionChannel");
    mFunctionChannels[rollFunction] = mLoader->getInt("RollChannel", 1);
    mFunctionChannels[pitchFunction] = mLoader->getInt("PitchChannel", 2);
    mFunctionChannels[yawFunction] = mLoader->getInt("YawChannel", 4);
    mFunctionChannels[throttleFunction] = mLoader->getInt("ThrottleChannel", 3);
    mMinChannelValue = mLoader->getInt("MinChannelValue", DEFAULT_MIN_CHANNEL_VALUE);
    mMaxChannelValue = mLoader->getInt("MaxChannelValue", DEFAULT_MAX_CHANNEL_VALUE);
    mLoader->endSection();

    mLoader->beginSection("Additional");
    mExponential = mLoader->getFloat("Exponential");
    mAccumulator = mLoader->getBool("Accumulator");
    mDeadband = mLoader->getBool("Deadband");
    mCenterZeroSupport = mLoader->getBool("CenterZeroSupport", true);
    mThrottleMode = (ThrottleMode_t)mLoader->getInt("ThrottleMode");
    mNegativeThrust = mLoader->getBool("NegativeThrust");
    mFrequency = mLoader->getFloat("Frequency", 25.0f);
    mCircleCorrection = mLoader->getBool("CircleCorrection", true);
    mLoader->endSection();
}

int JoystickConfigManager::mapFunctionMode(int mode, int function)
{
    static const int mapping[][5] = {
        { 2, 1, 0, 3, 4 },
        { 2, 3, 0, 1, 4 },
        { 0, 1, 2, 3, 4 },
        { 0, 3, 2, 1, 4 }};

    return mapping[mode-1][function];
}

void JoystickConfigManager::remapAxes(int currentMode, int newMode, int (&newMapping)[maxFunction])
{
    int temp[maxFunction];

    for (int function = 0; function < maxFunction; function++) {
        temp[mapFunctionMode(newMode, function)] = mFunctionAxis[mapFunctionMode(currentMode, function)];
    }

    for (int function = 0; function < maxFunction; function++) {
        newMapping[function] = temp[function];
    }
}

float JoystickConfigManager::adjustRange(int value, Calibration_t calibration, bool withDeadbands)
{
    float valueNormalized;
    float axisLength;
    float axisBasis;

    if (value > calibration.center) {
        axisBasis = 1.0f;
        valueNormalized = value - calibration.center;
        axisLength =  calibration.max - calibration.center;
    } else {
        axisBasis = -1.0f;
        valueNormalized = calibration.center - value;
        axisLength =  calibration.center - calibration.min;
    }

    float axisPercent;

    if (withDeadbands) {
        if (valueNormalized>calibration.deadband) {
            axisPercent = (valueNormalized - calibration.deadband) / (axisLength - calibration.deadband);
        } else if (valueNormalized<-calibration.deadband) {
            axisPercent = (valueNormalized + calibration.deadband) / (axisLength - calibration.deadband);
        } else {
            axisPercent = 0.f;
        }
    }
    else {
        axisPercent = valueNormalized / axisLength;
    }

    float correctedValue = axisBasis * axisPercent;

    if (calibration.reversed) {
        correctedValue *= -1.0f;
    }

    return max(-1.0f, min(correctedValue, 1.0f));
}
