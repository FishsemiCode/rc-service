/*
 * Copyright (C) 2019 Pinecone Inc. All rights reserved.
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

#ifndef JOYSTICKCONFIGMANAGER_H
#define JOYSTICKCONFIGMANAGER_H

#include "config_loader.h"

typedef struct {
    float roll;
    float pitch;
    float yaw;
    float throttle;
    float wheel;
} Controls_t;

class JoystickConfigManager
{
public:
    JoystickConfigManager(const string &filename);
    ~JoystickConfigManager();

    typedef struct Calibration_t {
        int     min;
        int     max;
        int     center;
        int     deadband;
        bool    reversed;
        Calibration_t() : min(-32767), max(32767), center(0), deadband(0), reversed(false) {}
    } Calibration_t;

    typedef enum {
        rollFunction,
        pitchFunction,
        yawFunction,
        throttleFunction,
        wheelFunction,
        maxFunction
    } AxisFunction_t;

    typedef enum {
        ThrottleModeCenterZero,
        ThrottleModeDownZero,
        ThrottleModeMax
    } ThrottleMode_t;

    void reloadSettings();
    void setAxisValue(int axis, int value);
    int getFunctionChannel(int function);
    void getJoystickControls(Controls_t *controls);
    int getMinChannelValue();
    int getMaxChannelValue();
    float getMessageFrequency();

private:
    void loadSettings();
    int mapFunctionMode(int mode, int function);
    void remapAxes(int currentMode, int newMode, int (&newMapping)[maxFunction]);
    float adjustRange(int value, Calibration_t calibration, bool withDeadbands);

    static const char*  sFunctionSettingsKey[maxFunction];
    bool mCalibrated;
    int mTransmitterMode;

    Calibration_t *mCalibrations;
    int mFunctionAxis[maxFunction];
    int mFunctionChannels[4];

    float mExponential;
    bool mAccumulator;
    bool mDeadband;
    bool mCenterZeroSupport;
    ThrottleMode_t mThrottleMode;
    bool mNegativeThrust;
    float mFrequency;

    /* min and max value of functions */
    int mMinChannelValue;
    int mMaxChannelValue;

    string mFileName;
    ConfigLoader *mLoader;
    int mAxisCount;
    int *mAxisValues;
};

#endif
