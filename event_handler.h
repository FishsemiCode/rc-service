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

#ifndef INPUTHANDLER_H
#define INPUTHANDLER_H

#include <map>
#include "service.h"
#include "key_config_manager.h"
#include "joystick_config_manager.h"
#include "message_sender.h"

class MessageSender;

enum {
    X = 0,
    Y = 1,
    Z = 2,
    RZ = 5,
    WHEEL = 8
};

class EventHandler
{
public:
    enum {
        ACTION_UP = 0,
        ACTION_DOWN = 1
    };

    struct KeyState{
        int keyCode;
        long pressedTime;
        bool isPressed;
        bool isLongPress;
    };

    struct AxisInfo {
        float scale;
        float offset;
        float min;
        float max;
    };
    EventHandler(struct gnd_service_config *config);
    ~EventHandler();

    int initialize();
    int getInputDeviceFds(int **fds);
    void handleKeyEvent(int keycode, int action);
    void handleAxisEvent(int axiscode, int value);
    void handleConfigEvent(const char *filename);
    bool getChannelValue(int keyCode, KeyConfigManager::KeyAction_t action, int *sbus, int *ch, int *value);
    void setChannelValue(int sbus, int ch, int value);
    bool getScrollWheelSetting(int *sbus, int *channel);
    void getJoystickControls(Controls_t *controls);
    int getFunctionChannel(int function);

private:
    int scanDir(const char *dirname);
    int findDevice(const char *devicePath);
    int getAxisInfo(int fd);
    void startLongPressThread();
    static void *threadLoop(void *arg);
    void setChannelDefaultValues();
    void notifyConfigChange();

    int *mDeviceFds;
    int mDeviceNum;
    map<string, int> mDeviceFdMap;
    map<int, struct KeyState> mKeyStatesMap;
    pthread_mutex_t mLock;
    struct gnd_service_config *mConfig;

    KeyConfigManager *mKeyConfig;
    JoystickConfigManager *mJoystickConfig;
    MessageSender *mMessageSender;

    static int sAxisCodes[];
    map<int, struct AxisInfo> mAxisInfoMap;
    int mAxisCount;
    int *mAxisValues;
};

#endif
