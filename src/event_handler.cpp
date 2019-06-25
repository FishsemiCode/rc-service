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

#include <fcntl.h>
#include <errno.h>
#include <dirent.h>
#include <linux/input.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include "event_handler.h"
#include "rc_utils.h"

#define INPUT_PATH "/dev/input"
#define SCAN_TIME 10
#define DEFAULT_MID_VALUE     1000.f

#define KEYACTION_DOWN KeyConfigManager::KeyAction_Down
#define KEYACTION_UP KeyConfigManager::KeyAction_Up
#define SHORT_PRESS KeyConfigManager::KeyAction_ShortPress
#define LONG_PRESS KeyConfigManager::KeyAction_LongPress

static const int EPOLL_SIZE_HINT = 8;
static const int EPOLL_MAX_EVENTS = 16;
static const uint32_t EPOLL_ID_INOTIFY = 0x80000001;

static const char *INPUT_DEVICES_NAME[] = { "gpio-keys", "mlx_joystick" };
int EventHandler::sAxisCodes[] = {X, Y, Z, RZ, WHEEL};

inline static float avg(float x, float y) {
    return (x + y) / 2;
}

EventHandler::EventHandler(struct gnd_service_config *config)
    : Handler(config)
    , mDeviceNum(0)
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

    mKeyConfig = new KeyConfigManager(key_filename, mConfig->supported_keys);
    mJoystickConfig = new JoystickConfigManager(js_filename);

    mAxisCount = sizeof(sAxisCodes)/sizeof(sAxisCodes[0]);
    mAxisValues = new int[mAxisCount];
    memset(mAxisValues, 0, sizeof(int)*mAxisCount);
    int count = sizeof(INPUT_DEVICES_NAME)/sizeof(char *);
    mDeviceFds = new int[count];
    memset(mDeviceFds, 0, sizeof(int)*count);

    map<int, string>::iterator it;
    for (it = mConfig->supported_keys.begin(); it != mConfig->supported_keys.end(); it++) {
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
    delete[] mAxisValues;
    delete[] mDeviceFds;

    pthread_mutex_destroy(&mLock);
}

int EventHandler::initialize()
{
    int device_num = sizeof(INPUT_DEVICES_NAME) / sizeof(char *);
    int try_count = 0;
    do {
        /* wait for all input device created */
        usleep(500000);
        scanDir(INPUT_PATH);
        try_count++;
    } while (mDeviceNum < device_num && try_count < SCAN_TIME);

    for (int i = 0; i < device_num; i++) {
        if (mDeviceFdMap.find(INPUT_DEVICES_NAME[i]) == mDeviceFdMap.end()) {
            ALOGE("input device '%s' not found.", INPUT_DEVICES_NAME[i]);
        }
    }

    for (int i = 0; i < mDeviceNum; i++) {
        getAxisInfo(mDeviceFds[i]);
    }

    notifyConfigChange();

    /* set initial value to send. */
    setKeyChannelDefaultValues();
    updateJoystickChannelValues();

    if (mConfig->long_press_enabled) {
        startLongPressThread();
    }
    startPollThread();

    if (mSender->openSocket(mConfig->ip, mConfig->port) < 0) {
        ALOGE("open socket failed, ip: %s.", mConfig->ip);
        return -1;
    }
    mSender->startThread();
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
            if (mDeviceFdMap.find(INPUT_DEVICES_NAME[i]) != mDeviceFdMap.end()) {
                goto close_fd;
            }
            ALOGD("find input device %s, name: %s", devicePath, INPUT_DEVICES_NAME[i]);
            mDeviceFds[mDeviceNum++] = fd;
            mDeviceFdMap[INPUT_DEVICES_NAME[i]] = fd;
            return 0;
        }
    }

close_fd:
    close(fd);
    return 0;
}

void EventHandler::startLongPressThread()
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, longPressThreadFunc, (void *)this)) {
        ALOGE("Failed to create thread to process long press.");
        return;
    }
}

void *EventHandler::longPressThreadFunc(void *arg)
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

void EventHandler::startPollThread()
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, pollThreadFunc, (void *)this)) {
        ALOGE("Failed to create thread to poll events.");
        return;
    }
}

void *EventHandler::pollThreadFunc(void *arg)
{
    int result;

    EventHandler *handler = (EventHandler *)arg;
    int epollFd = epoll_create(EPOLL_SIZE_HINT);
    if (epollFd < 0) {
        ALOGE("Could not create epoll fd: %s, end poll thread.", strerror(errno));
        return NULL;
    }

    /* add input devices fd to epoll instance */
    ALOGI("input device num: %d", handler->mDeviceNum);
    for (int i = 0; i < handler->mDeviceNum; i++) {
        epoll_data_t data;
        data.fd = handler->mDeviceFds[i];
        add_epoll_fd(epollFd, handler->mDeviceFds[i], data);
    }

    /* add inotify fd of config file to epoll instance */
    int inotify_fd = inotify_init();
    result = inotify_add_watch(inotify_fd, handler->mConfig->config_dir, IN_MODIFY | IN_CLOSE_WRITE);
    if (result < 0) {
        ALOGE("Could not register INotify for %s: %s", handler->mConfig->config_dir, strerror(errno));
    }
    epoll_data_t data;
    data.u32 = EPOLL_ID_INOTIFY;
    add_epoll_fd(epollFd, inotify_fd, data);

    char event_buf[512];
    int eventCount;
    struct epoll_event eventItems[EPOLL_MAX_EVENTS];
    struct input_event event;
    struct inotify_event *ievent;

    ALOGD("Entering epoll loop.");
    while (1) {
        eventCount = epoll_wait(epollFd, eventItems, EPOLL_MAX_EVENTS, -1);
        for (int i = 0; i < eventCount; i++) {
            const struct epoll_event& eventItem = eventItems[i];
            if (eventItem.data.u32 == EPOLL_ID_INOTIFY) {
                if (eventItem.events & EPOLLIN) {
                    int event_size;
                    int event_pos = 0;
                    int res = read(inotify_fd, &event_buf, sizeof(event_buf));
                    if (res < (int)sizeof(*ievent)) {
                        ALOGE("could not get event, %s\n", strerror(errno));
                        continue;
                    }
                    while (res >= (int)sizeof(*ievent)) {
                        ievent = (struct inotify_event *)(event_buf + event_pos);
                        if (ievent->len) {
                            if (ievent->mask & IN_CLOSE_WRITE)
                                handler->handleConfigEvent(ievent->name);
                        }
                        event_size = sizeof(*ievent) + ievent->len;
                        res -= event_size;
                        event_pos += event_size;
                    }
                } else {
                    ALOGW("Received unexpected epoll event 0x%08x for INotify.", eventItem.events);
                }
                continue;
            }

            if (eventItem.events & EPOLLIN) {
                int res = read(eventItem.data.fd, &event, sizeof(event));
                if (res < (int)sizeof(event)) {
                    ALOGE("Could not get event from fd %d", eventItem.data.fd);
                    continue;
                }
                if (event.type == EV_KEY) {
                    handler->handleKeyEvent(event.code, event.value);
                } else if (event.type == EV_ABS) {
                    handler->handleAxisEvent(event.code, event.value);
                }
            }
        }
    }
}

void EventHandler::setKeyChannelDefaultValues()
{
    for (int sbus = 1; sbus <= 2; sbus++) {
        map<int, int> sbus_map = mKeyConfig->getSbusDefaultValues(sbus);
        for (int ch = 1; ch <= 16; ch++) {
            if (sbus == 1 && ch <= 4)
                continue;

            if (sbus_map.find(ch) != sbus_map.end()) {
                if (MessageSender::getChannelValue(sbus, ch) == 0) {
                    setChannelValue(sbus, ch, sbus_map[ch]);
                }
            } else {
                setChannelValue(sbus, ch, 0);
            }
        }
    }
}

void EventHandler::notifyConfigChange()
{
    float frequency = mJoystickConfig->getMessageFrequency();
    if (frequency > 0) {
        mSender->setMessageFrequency(frequency);
    } else {
        mSender->setMessageFrequency(25.0);
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

uint16_t EventHandler::adjustRange(uint16_t value, float half)
{
    uint16_t minChannelValue = mJoystickConfig->getMinChannelValue();
    uint16_t maxChannelValue = mJoystickConfig->getMaxChannelValue();
    uint16_t mid_value = minChannelValue + (maxChannelValue - minChannelValue) / 2;

    if (value <= half) {
        return (mid_value - ((half - value) / half * (mid_value - minChannelValue)));
    } else {
        return (mid_value + ((value - half) / half * (maxChannelValue - mid_value)));
    }
}

void EventHandler::setManualControl(Controls_t controls)
{
    const float axesScaling = 1.0 * 1000.0;

    float ch[4];
    ch[0] = (controls.roll + 1) * axesScaling;
    ch[1] = (controls.pitch + 1) * axesScaling;
    ch[2] = (controls.yaw + 1) * axesScaling;
    ch[3] = controls.throttle * axesScaling;

    for (int i = 0; i < 4; i++) {
        if (i == JoystickConfigManager::throttleFunction) {
            ch[i] = adjustRange(ch[i], DEFAULT_MID_VALUE / 2);
        } else {
            ch[i] = adjustRange(ch[i], DEFAULT_MID_VALUE);
        }
        setChannelValue(1, getFunctionChannel(i), ch[i]);
    }

    int sbus, channel, channelValue;
    if (getScrollWheelSetting(&sbus, &channel)) {
        channelValue = (int)(DEFAULT_MID_VALUE + controls.wheel * DEFAULT_MID_VALUE);
        setChannelValue(sbus, channel, channelValue);
    }
}

void EventHandler::updateJoystickChannelValues()
{
    Controls_t controls;

    /* get values of 5 functions. */
    getJoystickControls(&controls);

    /* set values to MessageSender. */
    setManualControl(controls);
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

    updateJoystickChannelValues();
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
        setKeyChannelDefaultValues();
    }

    if (!strcmp(filename, mConfig->js_filename)) {
        ALOGD("Joystick config changed.");
        mJoystickConfig->reloadSettings();
    }

    notifyConfigChange();
}
