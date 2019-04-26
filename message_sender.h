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

#ifndef MESSAGESENDER_H
#define MESSAGESENDER_H

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "event_handler.h"

class EventHandler;

class MessageSender
{
public:
    MessageSender(EventHandler *handler, int sbusNum);
    ~MessageSender();

    static void setChannelValue(int sbus, int ch, uint16_t value);
    static int getChannelValue(int sbus, int ch);
    int openSocket(const char *ip, unsigned long port);
    void startThread();
    void setMinChannelValue(int value) { mMinChannelValue = value; }
    void setMaxChannelValue(int value) { mMaxChannelValue = value; }
    void setMessageFrequency(float freq) { mFrequency = freq; }

private:
    static void *threadLoop(void *arg);
    void setManualControl(Controls_t controls);
    int sendMessage();
    uint16_t adjustRange(uint16_t value, float half);

    int mSocketFd;
    struct sockaddr_in mSockaddr;
    EventHandler *mEventHandler;

    int mMinChannelValue;
    int mMaxChannelValue;
    float mFrequency;
    int mSendSbusNum;

    static uint16_t mChannelValues[2][16];
};

#endif
