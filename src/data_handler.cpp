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
#include <sys/epoll.h>
#include <cutils/uevent.h>
#include "rc_utils.h"
#include "data_handler.h"

#define PPM_DATA_NUM 8

static const char *PPM_INPUT_PATH[2] = { "/sys/bus/i2c/drivers/rc-io/5-0035/rcio_ppm0",
                                        "/sys/bus/i2c/drivers/rc-io/5-0035/rcio_ppm1" };

static const char *SBUS_MSG_FORMAT = "SBUS%d_ENABLE=";
static const char *PPM_MSG_FORMAT = "PPM%d_ENABLE=";

static const uint32_t EPOLL_ID_UEVENT = 0x80000002;
static const int EPOLL_SIZE_HINT = 8;
static const int EPOLL_MAX_EVENTS = 16;
static int epollFd;

DataHandler::DataHandler(struct gnd_service_config *config)
    : Handler(config)
    , mPPMMask(0)
{
    memset(mSbusFds, 0, sizeof(mSbusFds));
    pthread_mutex_init(&mPPMLock, NULL);
}

DataHandler::~DataHandler()
{
    for (int i =0; i < 2; i++) {
        if (mSbusFds[i] > 0) {
            close(mSbusFds[i]);
        }
    }

    timer_delete(mTimer);
    pthread_mutex_destroy(&mPPMLock);
}

int DataHandler::initialize()
{
    if (mSender->openSocket(mConfig->ip, mConfig->port) < 0) {
        ALOGE("open socket failed, ip: %s.", mConfig->ip);
        return -1;
    }

    initTimer();
    startPollThread();

    return 0;
}

void DataHandler::initTimer()
{
    struct sigevent evp;
    struct itimerspec ts;

    memset(&evp, 0, sizeof(struct sigevent));
    memset(&ts, 0, sizeof(struct itimerspec));

    evp.sigev_value.sival_ptr = (void *)this;
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = timerNotifyCb;
    timer_create(CLOCK_MONOTONIC, &evp, &mTimer);

    ts.it_value.tv_sec = 1;
    ts.it_value.tv_nsec = 0;
    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = (1000000000 / 50);
    timer_settime(mTimer, 0, &ts, NULL);
}

void DataHandler::timerNotifyCb(union sigval val)
{
    DataHandler *handler = (DataHandler *)val.sival_ptr;

    handler->readAndSendPPMData();
}

void DataHandler::startPollThread()
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, pollThreadFunc, (void *)this)) {
        ALOGE("Failed to create thread to read sbus data.");
        return;
    }
}

void *DataHandler::pollThreadFunc(void *arg)
{
    int result;
    DataHandler *handler = (DataHandler *)arg;

    epollFd = epoll_create(EPOLL_SIZE_HINT);
    if (epollFd < 0) {
        ALOGE("Could not create epoll fd: %s, end poll thread.", strerror(errno));
        return NULL;
    }

    int uevent_fd = uevent_open_socket(64*1024, true);
    if (uevent_fd < 0) {
        ALOGE("open uevent socket failed.");
    } else {
        fcntl(uevent_fd, F_SETFL, O_NONBLOCK);
        epoll_data_t data;
        data.u32 = EPOLL_ID_UEVENT;
        add_epoll_fd(epollFd, uevent_fd, data);
    }

    int eventCount;
    int size_already_read = 0, res = 0, pos = 0;
    struct epoll_event eventItems[EPOLL_MAX_EVENTS];
    uint8_t sbus_data[2][SBUS_DATA_LEN] = {"\0", "\0"};
    uint8_t temp_sbus_data[2][SBUS_DATA_LEN] = {"\0", "\0"};

    ALOGD("Entering data epoll loop.");
    while (1) {
        eventCount = epoll_wait(epollFd, eventItems, EPOLL_MAX_EVENTS, -1);
        for (int i = 0; i < eventCount; i++) {
            const struct epoll_event& eventItem = eventItems[i];
            if (eventItem.data.u32 == EPOLL_ID_UEVENT) {
                if (eventItem.events & EPOLLIN) {
                    handler->handleUevent(uevent_fd);
                    continue;
                }
            }

            if (eventItem.events & EPOLLIN) {
                uint32_t sbus = eventItem.data.u32;
                if (sbus >= 2) {
                    ALOGE("unexpected event.");
                    continue;
                }

                res = read(handler->mSbusFds[sbus], sbus_data[sbus] + size_already_read, SBUS_DATA_LEN - size_already_read);
                if (res >= 0) {
                    size_already_read += res;
                }

                if (size_already_read >= SBUS_DATA_LEN) {
                    if (sbus_data[sbus][0] != SBUS_STARTBYTE || sbus_data[sbus][SBUS_DATA_LEN - 1] != SBUS_ENDBYTE) {
                        memcpy(temp_sbus_data[sbus], sbus_data[sbus], SBUS_DATA_LEN);
                        for (pos = 1; pos < size_already_read; pos++) {
                            if (temp_sbus_data[sbus][pos] == SBUS_STARTBYTE) {
                                memset(sbus_data[sbus], 0, SBUS_DATA_LEN);
                                memcpy(sbus_data[sbus], temp_sbus_data[sbus] + pos, size_already_read - pos);
                                size_already_read -= pos;
                                continue;
                            }
                        }
                        if (pos >= SBUS_DATA_LEN) {
                            memset(sbus_data[sbus], 0, SBUS_DATA_LEN);
                            memset(temp_sbus_data[sbus], 0, SBUS_DATA_LEN);
                            size_already_read = 0;
                        }
                    } else {
                        handler->mSender->sendMessage(sbus, sbus_data[sbus]);
                        memset(sbus_data[sbus], 0, SBUS_DATA_LEN);
                        memset(temp_sbus_data[sbus], 0, SBUS_DATA_LEN);
                        size_already_read = 0;
                    }
                }
            }
        }
    }
    close(epollFd);
    return NULL;
}

int DataHandler::readAndSendPPMData()
{
    uint16_t data[PPM_DATA_NUM];
    int i;

    for (int ppm = 0; ppm < 2; ppm++) {
        if (getPPMEnabled(ppm)) {
            i = 0;
            ifstream in(PPM_INPUT_PATH[ppm]);
            if (!in) {
                ALOGE("Connot open file %s : %s", PPM_INPUT_PATH[ppm], strerror(errno));
                continue;
            }

            while (!in.eof() && i < PPM_DATA_NUM) {
                in >> data[i++];
            }
            in.close();

            for (int ch = 0; ch < PPM_DATA_NUM; ch++) {
                mSender->setChannelValue(ppm + 1, ch + 1, ppm_to_sbus(data[ch]));
            }
            mSender->sendMessage(ppm);
        }
    }

    return 0;
}

void DataHandler::updateSbusState(char *state)
{
    char str[30];
    char *s;

    for (int sbus = 0; sbus < 2; sbus++) {
        sprintf(str, SBUS_MSG_FORMAT, sbus);

        if ((s = strstr(state, str)) != NULL) {
            s += strlen(str);
            ALOGD("sbus%d state changed to %d", sbus, atoi(s));
            if (atoi(s) == 0) {
                /* disable sbus input */
                setSbusEnabled(sbus, false);
            } else if (atoi(s) == 1) {
                /* enable sbus input */
                setSbusEnabled(sbus, true);
            }
        }
    }
}

void DataHandler::setSbusEnabled(int index, bool enabled)
{
    /* if tty device not opened, open it. */
    if (mSbusFds[index] <= 0) {
        if ((mSbusFds[index] = sbus_port_init(mConfig->sbus_ports[index])) < 0) {
            ALOGE("Connot open sbus%d port %s : %s", index, mConfig->sbus_ports[index], strerror(errno));
            return;
        }
    }

    if (enabled) {
        epoll_data_t data;
        data.u32 = index;
        add_epoll_fd(epollFd, mSbusFds[index], data);
    } else {
        del_epoll_fd(epollFd, mSbusFds[index]);
    }
}

void DataHandler::updatePPMState(char *state)
{
    char str[30];
    char *s;

    for (int ppm = 0; ppm < 2; ppm++) {
        sprintf(str, PPM_MSG_FORMAT, ppm);

        if ((s = strstr(state, str)) != NULL) {
            s += strlen(str);
            ALOGD("ppm%d state changed to %d", ppm, atoi(s));
            if (atoi(s) == 0) {
                /* disable ppm input */
                setPPMEnabled(ppm, false);
            } else if (atoi(s) == 1) {
                /* enable ppm input */
                setPPMEnabled(ppm, true);
            }
        }
    }
}

void DataHandler::setPPMEnabled(int index, bool enabled)
{
    pthread_mutex_lock(&mPPMLock);
    if (enabled){
        mPPMMask |= (1 << index);
    } else {
        mPPMMask &= (0xff ^ (1 << index));
    }
    pthread_mutex_unlock(&mPPMLock);
}

bool DataHandler::getPPMEnabled(int index)
{
    bool ret;

    pthread_mutex_lock(&mPPMLock);
    ret = mPPMMask & (1 << index);
    pthread_mutex_unlock(&mPPMLock);

    return ret;
}

#define UEVENT_MSG_LEN 2048
void DataHandler::handleUevent(int ufd)
{
    char msg[UEVENT_MSG_LEN+2];
    char *cp;
    int n;

    n = uevent_kernel_multicast_recv(ufd, msg, UEVENT_MSG_LEN);
    if (n <= 0)
        return;
    if (n >= UEVENT_MSG_LEN)   /* overflow -- discard */
        return;

    msg[n] = '\0';
    msg[n+1] = '\0';
    cp = msg;

    while (*cp) {
        if (strstr(cp, "SBUS")) {
            updateSbusState(cp);
        }
        if (strstr(cp, "PPM")) {
            updatePPMState(cp);
        }

        /* advance to after the next \0 */
        while (*cp++)
            ;
    }
}
