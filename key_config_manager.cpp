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

#include <map>
#include "key_config_manager.h"
#include "message_sender.h"

#define SHORT_PRESS_POSTFIX "_short_press"
#define LONG_PRESS_POSTFIX "_long_press"

KeyConfigManager::KeyConfigManager(const string &filename, map<int, string> available_keys)
    : mFileName(filename)
    , mAvailableKeys(available_keys)
{
    mKeyCount = mAvailableKeys.size();
    ALOGD("supported keys count : %d", mKeyCount);
    mKeyActionNames = new string[mKeyCount * 2];

    map<int, string>::iterator it;
    int i;
    for (i = 0, it = mAvailableKeys.begin(); it != mAvailableKeys.end(); i++, it++) {
        mKeyActionNames[i * 2] = it->second + SHORT_PRESS_POSTFIX;
        mKeyActionNames[i * 2 + 1] = it->second + LONG_PRESS_POSTFIX;
    }
    for (int i = 0; i < mKeyCount * 2; i++) {
        KeySetting_t key_setting;
        memset(&key_setting, 0, sizeof(key_setting));
        mKeySettingsMap.insert(make_pair(mKeyActionNames[i], key_setting));
    }

    mLoader = new ConfigLoader();
    loadSettings();
}

KeyConfigManager::~KeyConfigManager()
{
    delete mKeyActionNames;
    delete mLoader;
}

void KeyConfigManager::loadSettings()
{
    if (mKeySettingsMap.empty()) {
        return;
    }

    mLoader->loadConfig(mFileName);
    for (int i = 0; i < mKeyCount * 2; i++) {
        KeySetting_t *setting = &mKeySettingsMap[mKeyActionNames[i]];
        mLoader->beginSection(mKeyActionNames[i]);
        setting->sbus = mLoader->getInt("sbus");
        setting->channel = mLoader->getInt("channel");
        setting->value = mLoader->getInt("value");
        setting->switchType = mLoader->getInt("switchType");
        setting->defaultValue = mLoader->getInt("defaultValue");
        mLoader->endSection();
    }

    mLoader->beginSection("scrollwheel");
    mScrollWheelSetting.sbus = mLoader->getInt("sbus");
    mScrollWheelSetting.channel = mLoader->getInt("channel");
    mLoader->endSection();
}

void KeyConfigManager::reloadSettings()
{
    loadSettings();
}

map<int, int> KeyConfigManager::getSbusDefaultValues(int sbus)
{
    map<int, int> sbus_map;
    int channel;

    for (int i = 0; i < mKeyCount * 2; i++) {
        KeySetting_t *setting = &mKeySettingsMap[mKeyActionNames[i]];
        if (sbus == setting->sbus) {
            channel = setting->channel;
            if (channel != 0) {
                if (setting->switchType > 0) {
                    sbus_map[channel] = setting->defaultValue;
                } else {
                    if ((sbus_map.find(channel) != sbus_map.end() && sbus_map[channel] > setting->value)
                        || sbus_map.find(channel) == sbus_map.end()) {
                        sbus_map[channel] = setting->value;
                    }
                }
            }
        }
    }

    if (mScrollWheelSetting.sbus > 0 && mScrollWheelSetting.channel > 0) {
        if (sbus == mScrollWheelSetting.sbus)
            sbus_map[mScrollWheelSetting.channel] = 1000;
    }

    return sbus_map;
}

bool KeyConfigManager::getChannelValue(int keyCode, KeyAction_t action, int* sbus, int* channel, int* value)
{
    string key_action = getKeyActionStr(keyCode, action);

    if (mKeySettingsMap.find(key_action) == mKeySettingsMap.end()) {
        return false;
    }

    KeySetting_t setting = mKeySettingsMap[key_action];
    if (setting.switchType != 2 && (action == KeyAction_Down || action == KeyAction_Up)) {
        return false;
    }

    *sbus = setting.sbus;
    *channel = setting.channel;

    if (setting.switchType == 2) {
        if (action == KeyAction_Down) {
            *value = setting.value;
        } else if (action == KeyAction_Up) {
            *value = setting.defaultValue;
        } else {
            return false;
        }
    } else if (setting.switchType == 1) {
        if (currentChannelValue(setting.sbus, setting.channel) == setting.value) {
            *value = setting.defaultValue;
        } else {
            *value = setting.value;
        }
    } else {
        *value = setting.value;
    }

    return (*channel != 0);
}

bool KeyConfigManager::getScrollWheelSetting(int *sbus, int *channel)
{
    *sbus = mScrollWheelSetting.sbus;
    *channel = mScrollWheelSetting.channel;

    return (*sbus != 0);
}

string KeyConfigManager::getKeyActionStr(int keyCode, int action)
{
    if (mAvailableKeys.find(keyCode) == mAvailableKeys.end()) {
        return "";
    }

    string key_action = mAvailableKeys[keyCode];
    /* to check hold mode setting, just use the setting for short */
    if (action == KeyAction_Down || action == KeyAction_Up) {
        action = 0;
    }
    if (action == 0) {
        key_action += SHORT_PRESS_POSTFIX;
    } else if (action == 1) {
        key_action += LONG_PRESS_POSTFIX;
    }

    return key_action;
}

int KeyConfigManager::currentChannelValue(int sbus, int channel)
{
    return MessageSender::getChannelValue(sbus, channel);
}
