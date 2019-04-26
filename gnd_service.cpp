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

#include <fstream>
#include <linux/errno.h>
#include <linux/input.h>
#include <sys/inotify.h>
#include <sys/epoll.h>

#include "service.h"
#include "event_handler.h"

static const int EPOLL_SIZE_HINT = 8;
static const int EPOLL_MAX_EVENTS = 16;
static const uint32_t EPOLL_ID_INOTIFY = 0x80000001;

static struct gnd_service_config g_config;

static int load_config(const string &filename)
{
    ConfigLoader loader;

    if (filename.empty())
        return -EINVAL;

    if (!loader.loadConfig(filename)) {
        ALOGE("Loading config file:%s failed\n", filename.c_str());
        return -ENXIO;
    }

    loader.beginSection("General");
    strcpy(g_config.config_dir, loader.getStr("ConfigDir", "").c_str());
    strcpy(g_config.key_filename, loader.getStr("KeyconfigName", "").c_str());
    strcpy(g_config.js_filename, loader.getStr("JoystickconfigName", "").c_str());
    loader.endSection();

    loader.beginSection("UdpConfig");
    strcpy(g_config.ip, loader.getStr("IpAddress", "").c_str());
    g_config.port = loader.getInt("Port", 16666);
    loader.endSection();

    loader.beginSection("SbusCtrl");
    bool sbus1_enable = !loader.getBool("Sbus1SendbyApp");
    g_config.send_sbus_num = sbus1_enable ? 2 : 1;
    loader.endSection();

    return 0;
}

int gnd_main(int argc, char *argv[])
{
    int result;

    if (!argc)
        return -EINVAL;

    result = load_config(argv[0]);
    if (result < 0)
        return result;

    /* wait for all input device created */
    usleep(500000);

    EventHandler handler(&g_config);
    handler.initialize();

    int epollFd = epoll_create(EPOLL_SIZE_HINT);
    if (epollFd < 0) {
        ALOGE("Could not create epoll fd: %s", strerror(errno));
        return -1;
    }

    /* add input devices fd to epoll instance */
    int *fds;
    int count = handler.getInputDeviceFds(&fds);
    ALOGI("input device num: %d", count);
    for (int i = 0; i < count; i++) {
        struct epoll_event deviceEventItem;
        memset(&deviceEventItem, 0, sizeof(deviceEventItem));
        deviceEventItem.events = EPOLLIN;
        deviceEventItem.data.fd = fds[i];
        result = epoll_ctl(epollFd, EPOLL_CTL_ADD, fds[i], &deviceEventItem);
        if (result != 0) {
            ALOGE("Could not add device fd to epoll instance: %s", strerror(errno));
            return -1;
        }
    }

    /* add inotify fd of config file to epoll instance */
    int inotify_fd = inotify_init();
    result = inotify_add_watch(inotify_fd, g_config.config_dir, IN_MODIFY | IN_CLOSE_WRITE);
    if (result < 0) {
        ALOGE("Could not register INotify for %s: %s", g_config.config_dir, strerror(errno));
        return -1;
    }
    struct epoll_event notifyEventItem;
    memset(&notifyEventItem, 0, sizeof(notifyEventItem));
    notifyEventItem.events = EPOLLIN;
    notifyEventItem.data.u32 = EPOLL_ID_INOTIFY;
    result = epoll_ctl(epollFd, EPOLL_CTL_ADD, inotify_fd, &notifyEventItem);
    if (result != 0) {
        ALOGE("Could not add INotify to epoll instance: %s", strerror(errno));
        return -1;
    }

    char event_buf[512];
    int eventCount;
    struct epoll_event eventItems[EPOLL_MAX_EVENTS];
    struct input_event event;
    struct inotify_event *ievent;

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
                                handler.handleConfigEvent(ievent->name);
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
                    handler.handleKeyEvent(event.code, event.value);
                } else if (event.type == EV_ABS) {
                    handler.handleAxisEvent(event.code, event.value);
                }
            }
        }
    }

    return 0;
}
