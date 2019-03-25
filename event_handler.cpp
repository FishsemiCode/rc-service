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

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <linux/input.h>
#include "event_handler.h"

#define INPUT_PATH "/dev/input"

#define KEYACTION_DOWN KeyConfigManager::KeyAction_Down
#define KEYACTION_UP KeyConfigManager::KeyAction_Up
#define SHORT_PRESS KeyConfigManager::KeyAction_ShortPress
#define LONG_PRESS KeyConfigManager::KeyAction_LongPress

static const char *INPUT_DEVICES_NAME[] = { "gpio-keys", "mlx_joystick" };
int EventHandler::sAxisCodes[] = {X, Y, Z, RZ, WHEEL};

inline static float avg(float x, float y) {
    return (x + y) / 2;
}

EventHandler::EventHandler(struct gnd_service_config *config)
    :mDeviceNum(0),
     mConfig(config)
{
    char key_filename[PATH_MAX];
    char js_filename[PATH_MAX];
    char *filename;

    strcpy(key_filename, mConfig->config_dir);
    filename = key_filename + strlen(key_filename);
    *filename++ = '/';
    strcpy(filename, mConfig->key_filename);

    strcpy(js_filename, mConfig->config_dir);
    filename = js_filename + strlen(js_filename);
    *filename++ = '/';
    strcpy(filename, mConfig->js_filename);

    mKeyConfig = new KeyConfigManager(key_filename);
    mJoystickConfig = new JoystickConfigManager(js_filename);
    mMessageSender = new MessageSender(this);

    mAxisCount = sizeof(sAxisCodes)/sizeof(sAxisCodes[0]);
    mAxisValues = new int[mAxisCount];
    memset(mAxisValues, 0, sizeof(int)*mAxisCount);
    int count = sizeof(INPUT_DEVICES_NAME)/sizeof(char *);
    mDeviceFds = new int[count];
    memset(mDeviceFds, 0, sizeof(int)*count);

    map<int, string>::iterator it;
    for (it = KeyConfigManager::sKeyCodeNameMap.begin(); it != KeyConfigManager::sKeyCodeNameMap.end(); it++) {
        struct KeyState key_state;
        memset(&key_state, 0, sizeof(key_state));
        key_state.keyCode = it->first;
        mKeyStatesMap.insert(make_pair(it->first, key_state));
    }

    pthread_mutex_init(&mLock, NULL);

}

EventHandler::~EventHandler()
{
    for (int i = 0; i < mDeviceNum; i++) {
        if (mDeviceFds[i] > 0) {
            close(mDeviceFds[i]);
        }
    }
    mKeyStatesMap.clear();
    mAxisInfoMap.clear();

    delete mKeyConfig;
    delete mJoystickConfig;
    delete mMessageSender;
    delete[] mAxisValues;
    delete[] mDeviceFds;

    pthread_mutex_destroy(&mLock);
}

int EventHandler::initialize()
{
    setChannelDefaultValues();

    scanDir(INPUT_PATH);
    for (int i = 0; i < mDeviceNum; i++) {
        getAxisInfo(mDeviceFds[i]);
    }

    startLongPressThread();
    notifyConfigChange();

    if (mMessageSender->openSocket(mConfig->ip, mConfig->port) < 0) {
        ALOGE("open socket failed, ip: %s.", mConfig->ip);
        return -1;
    }
    mMessageSender->startThread();
    return 0;
}

int EventHandler::getInputDeviceFds(int **fds)
{
    *fds = mDeviceFds;
    return mDeviceNum;
}

int EventHandler::scanDir(const char *dirname)
{
    char devname[20];
    char *filename;
    DIR *dir;
    struct dirent *de;

    dir = opendir(dirname);
    if (dir == NULL)
        return -ENXIO;
    strcpy(devname, dirname);
    filename = devname + strlen(devname);
    *filename++ = '/';
    while ((de = readdir(dir))) {
        if (de->d_name[0] == '.' &&
                (de->d_name[1] == '\0' ||
                 (de->d_name[1] == '.' && de->d_name[2] == '\0')))
            continue;
        strcpy(filename, de->d_name);
        findDevice(devname);
    }
    closedir(dir);
    return 0;
}

int EventHandler::getAxisInfo(int fd)
{
    struct input_absinfo info;
    for (int i = 0; i < mAxisCount; i++) {
        if (mAxisInfoMap.find(sAxisCodes[i]) != mAxisInfoMap.end()) {
            /* just get axis info once */
            continue;
        }
        AxisInfo axis_info;
        if (!ioctl(fd, EVIOCGABS(sAxisCodes[i]), &info)) {
            if (info.minimum != info.maximum) {
                axis_info.scale = 2.0f / (info.maximum - info.minimum);
                axis_info.offset = avg(info.minimum, info.maximum) * -axis_info.scale;
                axis_info.min = -1.0f;
                axis_info.max = 1.0f;
                mAxisInfoMap.insert(make_pair(sAxisCodes[i], axis_info));
            }
        }
    }
    return 0;
}

int EventHandler::findDevice(const char *devicePath)
{
    char name[80];
    int fd = open(devicePath, O_RDWR | O_CLOEXEC);

    if (fd < 0) {
        ALOGE("Could not open %s : %s", devicePath, strerror(errno));
        return -ENXIO;
    }

    name[sizeof(name) - 1] = '\0';
    if (ioctl(fd, EVIOCGNAME(sizeof(name) - 1), &name) < 1) {
        name[0] = '\0';
    }

    for (size_t i = 0; i < sizeof(INPUT_DEVICES_NAME) / sizeof(char *); i++) {
        if (!strcmp(name, INPUT_DEVICES_NAME[i])) {
            ALOGD("find input device %s, name: %s", devicePath, INPUT_DEVICES_NAME[i]);
            mDeviceFds[mDeviceNum++] = fd;
            return 0;
        }
    }
    close(fd);
    return 0;
}

void EventHandler::startLongPressThread()
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, threadLoop, (void *)this)) {
        ALOGE("Failed to create thread to process long press.");
        return;
    }
}

void *EventHandler::threadLoop(void *arg)
{
    EventHandler *handler = (EventHandler *)arg;
    map<int, struct KeyState>::iterator it;
    struct KeyState *keyState;

    while (1) {
        pthread_mutex_lock(&handler->mLock);
        struct timeval time;
        gettimeofday(&time, NULL);
        long current = (time.tv_sec*1000000 + time.tv_usec) / 1000;
        long period = 0;
        for (it = handler->mKeyStatesMap.begin(); it != handler->mKeyStatesMap.end(); it++) {
            keyState = &it->second;
            period = current - keyState->pressedTime;
            if (keyState->isPressed && !keyState->isLongPress && (period >= 1000L)) {
                int sbus, ch, value;
                keyState->isLongPress = true;
                if (handler->getChannelValue(keyState->keyCode, LONG_PRESS, &sbus, &ch, &value)) {
                    handler->setChannelValue(sbus, ch, value);
                }
            }
        }
        pthread_mutex_unlock(&handler->mLock);
        usleep(100000);
    }
}

void EventHandler::setChannelDefaultValues()
{
    for (int sbus = 1; sbus <= 2; sbus++) {
        map<int, int> sbus_map = mKeyConfig->getSbusDefaultValues(sbus);
        for (int ch = 1; ch <= 16; ch++) {
            if (sbus == 1 && ch <= 4)
                continue;

            if (sbus_map.find(ch) != sbus_map.end()) {
                if (MessageSender::getChannelValue(sbus, ch) == 0) {
                    MessageSender::setChannelValue(sbus, ch, sbus_map[ch]);
                }
            } else {
                MessageSender::setChannelValue(sbus, ch, 0);
            }
        }
    }
}

void EventHandler::notifyConfigChange()
{
    int minChannelValue = mJoystickConfig->getMinChannelValue();
    int maxChannelValue = mJoystickConfig->getMaxChannelValue();

    mMessageSender->setMinChannelValue(minChannelValue);
    mMessageSender->setMaxChannelValue(maxChannelValue);

    float frequency = mJoystickConfig->getMessageFrequency();
    if (frequency > 0) {
        mMessageSender->setMessageFrequency(frequency);
    } else {
        mMessageSender->setMessageFrequency(25.0);
    }
}

void EventHandler::handleKeyEvent(int keycode, int action)
{
    int sbus, ch, value;

    pthread_mutex_lock(&mLock);
    if (mKeyStatesMap.find(keycode) == mKeyStatesMap.end()) {
        ALOGE("Unsupported key code %d, do not process.", keycode);
        pthread_mutex_unlock(&mLock);
        return;
    }

    struct KeyState *key_state = &mKeyStatesMap[keycode];
    if (action == ACTION_DOWN) {
        struct timeval time;
        gettimeofday(&time, NULL);
        key_state->pressedTime = (time.tv_sec * 1000000 + time.tv_usec)/1000;
        key_state->isPressed = true;
        if (getChannelValue(keycode, KEYACTION_DOWN, &sbus, &ch, &value)) {
            setChannelValue(sbus, ch, value);
        }
    } else {
        if (getChannelValue(keycode, KEYACTION_UP, &sbus, &ch, &value)) {
            setChannelValue(sbus, ch, value);
        }
        if (!key_state->isLongPress) {
            if (getChannelValue(keycode, SHORT_PRESS, &sbus, &ch, &value)) {
                setChannelValue(sbus, ch, value);
            }
        }
        key_state->pressedTime = 0;
        key_state->isPressed = false;
        key_state->isLongPress = false;
    }
    pthread_mutex_unlock(&mLock);
}

bool EventHandler::getChannelValue(int keyCode, KeyConfigManager::KeyAction_t action, int *sbus, int *ch, int *value)
{
    bool ret = mKeyConfig->getChannelValue(keyCode, action, sbus, ch, value);
    if (ret) {
        ALOGD("get sbus %d ch %d value : %d", *sbus, *ch, *value);
    }
    return ret;
}

void EventHandler::setChannelValue(int sbus, int ch, int value)
{
    MessageSender::setChannelValue(sbus, ch, value);
}

bool EventHandler::getScrollWheelSetting(int *sbus, int *channel)
{
    return mKeyConfig->getScrollWheelSetting(sbus, channel);
}

void EventHandler::getJoystickControls(Controls_t *controls)
{
    mJoystickConfig->getJoystickControls(controls);
}

int EventHandler::getFunctionChannel(int function)
{
    return mJoystickConfig->getFunctionChannel(function);
}

void EventHandler::handleAxisEvent(int axiscode, int value)
{
    float axis_value;

    if (mAxisInfoMap.find(axiscode) != mAxisInfoMap.end()) {
        axis_value = value * mAxisInfoMap[axiscode].scale + mAxisInfoMap[axiscode].offset;
    } else {
        axis_value = 0.0f;
    }

    for (int i = 0; i < mAxisCount; i++) {
        if (axiscode == sAxisCodes[i]) {
            mAxisValues[i] = (int)(axis_value*32767.f);
            mJoystickConfig->setAxisValue(i, mAxisValues[i]);
        }
    }
}

void EventHandler::handleConfigEvent(const char *filename)
{
    if (filename == NULL) {
        ALOGE("invalid parameter");
        return;
    }

    if (!strcmp(filename, mConfig->key_filename)) {
        ALOGD("Key config changed.");
        mKeyConfig->reloadSettings();
        setChannelDefaultValues();
    }

    if (!strcmp(filename, mConfig->js_filename)) {
        ALOGD("Joystick config changed.");
        mJoystickConfig->reloadSettings();
    }

    notifyConfigChange();
}
