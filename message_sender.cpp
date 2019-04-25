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

#include <arpa/inet.h>
#include <unistd.h>
#include <utils/Log.h>
#include "message_sender.h"

#define SBUS_STARTBYTE        0x0f
#define SBUS_ENDBYTE          0x00
#define DEFAULT_MID_VALUE     1000.f

uint16_t MessageSender::mChannelValues[2][16];

MessageSender::MessageSender(EventHandler *handler, int sbusNum)
    :mEventHandler(handler)
    ,mSendSbusNum(sbusNum)
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
        Controls_t controls;
        sender->mEventHandler->getJoystickControls(&controls);
        sender->setManualControl(controls);

        int mswait = (int)(1000.0f / sender->mFrequency);
        usleep(mswait * 1000);
    }
}

void MessageSender::setManualControl(Controls_t controls)
{
    /* Store scaling values for all 3 axes */
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
        mChannelValues[0][mEventHandler->getFunctionChannel(i) - 1] = ch[i];
    }

    int sbus, channel, channelValue;
    if (mEventHandler->getScrollWheelSetting(&sbus, &channel)) {
        channelValue = (int)(DEFAULT_MID_VALUE + controls.wheel * DEFAULT_MID_VALUE);
        setChannelValue(sbus, channel, channelValue);
    }

    sendMessage();
}

uint16_t MessageSender::adjustRange(uint16_t value, float half)
{
    uint16_t mid_value = mMinChannelValue + (mMaxChannelValue - mMinChannelValue) / 2;

    if (value <= half) {
        return (mid_value - ((half - value) / half * (mid_value - mMinChannelValue)));
    } else {
        return (mid_value + ((value - half) / half * (mMaxChannelValue - mid_value)));
    }
}

int MessageSender::sendMessage()
{
    int ret;
    struct rc_msg msg;
    memset(&msg, 0, sizeof(struct rc_msg));

    for (int i = 0; i < mSendSbusNum; i++) {
        msg.type_idex = (i & CHANNEL_IDEX) | SBUS_MODE;
        /* sbus protocol start byte:0xF0 */
        msg.rc_data[0] = SBUS_STARTBYTE;
        /* sbus protocol data1: channel1 0-7 bit */
        msg.rc_data[1] =(mChannelValues[i][0] & 0xff);
        /* sbus protocol data2: channel1 8-10 bit and channel2 0-4 */
        msg.rc_data[2] =(((mChannelValues[i][0] >> 8) | (mChannelValues[i][1]<< 3)) & 0xff);
        /* sbus protocol data3: channel2 5-10 bit and channel3 0-1 */
        msg.rc_data[3] =(((mChannelValues[i][1] >> 5) | (mChannelValues[i][2] << 6)) & 0xff);
        /* sbus protocol data4: channel3 2-9 bit */
        msg.rc_data[4] =((mChannelValues[i][2] >> 2) & 0xff);
        /* sbus protocol data5: channel3 10 bit and channel4 0-6 */
        msg.rc_data[5] =(((mChannelValues[i][2] >> 10) | (mChannelValues[i][3] << 1)) & 0xff);
        /* sbus protocol data6: channel4 7-10 bit and channel5 0-3 */
        msg.rc_data[6] =(((mChannelValues[i][3] >> 7) | (mChannelValues[i][4] << 4)) & 0xff);
        /* sbus protocol data7: channel5 4-10 bit and channel6 0 */
        msg.rc_data[7] =(((mChannelValues[i][4] >> 4) | (mChannelValues[i][5] << 7)) & 0xff);
        /* sbus protocol data8: channel6 1-8 bit */
        msg.rc_data[8] =((mChannelValues[i][5] >> 1) & 0xff);
        /* sbus protocol data9: channel6 9-10 bit and channel7 0-5 */
        msg.rc_data[9] =(((mChannelValues[i][5] >> 9) | (mChannelValues[i][6] << 2)) & 0xff);
        /* sbus protocol data10: channel7 6-10 bit and channel8 0-2 */
        msg.rc_data[10] =(((mChannelValues[i][6] >> 6) | (mChannelValues[i][7] << 5)) & 0xff);
        /* sbus protocol data11: channel8 3-10 bit */
        msg.rc_data[11] =(((mChannelValues[i][7] >> 3)) & 0xff);

        /* sbus protocol data12: channel9 0-7 bit */
        msg.rc_data[12] =(mChannelValues[i][8] & 0xff);
        /* sbus protocol data13: channel9 8-10 bit and channel10 0-4 */
        msg.rc_data[13] =(((mChannelValues[i][8] >> 8) | (mChannelValues[i][9] << 3)) & 0xff);
        /* sbus protocol data14: channel10 5-10 bit and channel11 0-1 */
        msg.rc_data[14] =(((mChannelValues[i][9] >> 5) | (mChannelValues[i][10] << 6)) & 0xff);
        /* sbus protocol data15: channel11 2-9 bit */
        msg.rc_data[15] =((mChannelValues[i][10] >> 2) & 0xff);
        /* sbus protocol data16: channel11 10 bit and channel12 0-6 */
        msg.rc_data[16] =(((mChannelValues[i][10] >> 10) | (mChannelValues[i][11] << 1)) & 0xff);
        /* sbus protocol data17: channel12 7-10 bit and channel13 0-3 */
        msg.rc_data[17] =(((mChannelValues[i][11] >> 7) | (mChannelValues[i][12] << 4)) & 0xff);
        /* sbus protocol data18: channel13 4-10 bit and channel14 0 */
        msg.rc_data[18] =(((mChannelValues[i][12] >> 4) | (mChannelValues[i][13] << 7)) & 0xff);
        /* sbus protocol data19: channel14 1-8 bit */
        msg.rc_data[19] =((mChannelValues[i][13] >> 1) & 0xff);
        /* sbus protocol data20: channel14 9-10 bit and channel15 0-5 */
        msg.rc_data[20] =(((mChannelValues[i][13] >> 9) | (mChannelValues[i][14] << 2)) & 0xff);
        /* sbus protocol data21: channel15 6-10 bit and channel16 0-2 */
        msg.rc_data[21] =(((mChannelValues[i][14] >> 6) | (mChannelValues[i][15] << 5)) & 0xff);
        /* sbus protocol data22: channel16 3-10 bit */
        msg.rc_data[22] =((mChannelValues[i][15] >> 3) & 0xff);

        msg.rc_data[23] = 0x00;
        msg.rc_data[24] = SBUS_ENDBYTE;

        ret = sendto(mSocketFd, &msg, sizeof(msg), 0, (struct sockaddr *)&mSockaddr, sizeof(mSockaddr));
        if (ret < 0) {
            ALOGE("Could not send rc message to air side: %s", strerror(errno));
        }
    }

    return 0;
}

void MessageSender::setChannelValue(int sbus, int ch, uint16_t value)
{
    mChannelValues[sbus - 1][ch - 1] = value;
}

int MessageSender::getChannelValue(int sbus, int ch)
{
    return mChannelValues[sbus - 1][ch - 1];
}
