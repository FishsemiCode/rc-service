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

#ifndef KEYCONFIGMANAGER_H
#define KEYCONFIGMANAGER_H

#include <map>
#include <string>
#include "config_loader.h"

class KeyConfigManager
{
public:
    typedef enum {
        KeyAction_ShortPress,
        KeyAction_LongPress,
        KeyAction_Down,
        KeyAction_Up,
    } KeyAction_t;

    enum SwitchType {
        TYPE_DEFAULT = 0,
        TYPE_TOGGLE,
        TYPE_MOMENTARY,
        TYPE_MAX,
    };

    typedef struct
    {
        int sbus;
        int channel;
        int value;
        int switchType;
        int defaultValue;
    } KeySetting_t;

    typedef struct
    {
        int sbus;
        int channel;
    } ScrollWheelSetting_t;

    KeyConfigManager(const string &filename, map<int, string> available_keys);
    ~KeyConfigManager();
    void reloadSettings();
    bool getChannelValue(int keyCode, KeyAction_t action, int* sbus, int* channel, int* value);
    bool getScrollWheelSetting(int *sbus, int *channel);
    /* get all channels and default values already configured to keys */
    map<int, int> getSbusDefaultValues(int sbus);

private:
    void loadSettings();
    string getKeyActionStr(int keyCode, int action);
    int currentChannelValue(int sbus, int channel);

    string mFileName;
    ConfigLoader *mLoader;
    int mKeyCount;
    map<int, string> mAvailableKeys;
    string *mKeyActionNames;
    map<string, KeySetting_t> mKeySettingsMap;
    ScrollWheelSetting_t mScrollWheelSetting;
};

#endif
