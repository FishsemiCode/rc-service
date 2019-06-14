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

#include "message_sender.h"
#include "rc_utils.h"

uint16_t MessageSender::mChannelValues[2][16] = { {0}, {0} };

MessageSender::MessageSender(int sbusNum)
    : mSendSbusNum(sbusNum)
{
    bzero(&mSockaddr, sizeof(mSockaddr));
}

MessageSender::~MessageSender()
{
    if (mSocketFd > -1) {
        close(mSocketFd);
    }
}

int MessageSender::openSocket(const char *ip, unsigned long port)
{
    mSocketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (mSocketFd < 0) {
        return -1;
    }

    mSockaddr.sin_family = AF_INET;
    mSockaddr.sin_addr.s_addr = inet_addr(ip);
    mSockaddr.sin_port = htons(port);

    return mSocketFd;
}

void MessageSender::startThread()
{
    pthread_t thread;

    if (pthread_create(&thread, NULL, threadLoop, (void *)this)) {
        ALOGE("Failed to create thread to send sbus mesage.");
        return;
    }
}

void *MessageSender::threadLoop(void *arg)
{
    MessageSender *sender = (MessageSender *)arg;

    while (1) {
        sender->sendMessage();

        int mswait = (int)(1000.0f / sender->mFrequency);
        usleep(mswait * 1000);
    }
}

int MessageSender::sendMessage()
{
    int ret;
    struct rc_msg msg;
    memset(&msg, 0, sizeof(struct rc_msg));

    for (int i = 0; i < mSendSbusNum; i++) {
        pack_rc_msg(i, mChannelValues[i], &msg);

        ret = sendto(mSocketFd, &msg, sizeof(msg), 0, (struct sockaddr *)&mSockaddr, sizeof(mSockaddr));
        if (ret < 0) {
            ALOGE("Could not send rc message to air side: %s", strerror(errno));
        }
    }

    return 0;
}

int MessageSender::sendMessage(int sbus)
{
    int ret;
    struct rc_msg msg;
    memset(&msg, 0, sizeof(struct rc_msg));

    pack_rc_msg(sbus, mChannelValues[sbus], &msg);

    ret = sendto(mSocketFd, &msg, sizeof(msg), 0, (struct sockaddr *)&mSockaddr, sizeof(mSockaddr));
    if (ret < 0) {
        ALOGE("Could not send rc message to air side: %s", strerror(errno));
    }

    return ret;
}

int MessageSender::sendMessage(int sbus, uint8_t (&data)[25])
{
    int ret;
    struct rc_msg msg;
    memset(&msg, 0, sizeof(struct rc_msg));

    msg.type_idex = (sbus & CHANNEL_IDEX) | SBUS_MODE;
    memcpy(msg.rc_data, data, sizeof(data));

    ret = sendto(mSocketFd, &msg, sizeof(msg), 0, (struct sockaddr *)&mSockaddr, sizeof(mSockaddr));
    if (ret < 0) {
        ALOGE("Could not send rc message to air side: %s", strerror(errno));
    }

    return ret;
}

void MessageSender::setChannelValue(int sbus, int ch, uint16_t value)
{
    mChannelValues[sbus - 1][ch - 1] = value;
}

int MessageSender::getChannelValue(int sbus, int ch)
{
    return mChannelValues[sbus - 1][ch - 1];
}
