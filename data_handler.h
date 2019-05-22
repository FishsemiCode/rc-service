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

#ifndef DATAHANDLER_H
#define DATAHANDLER_H

#include "service.h"
#include "handler.h"
#include "message_sender.h"

using namespace std;

class DataHandler : public Handler
{
public:
    DataHandler(struct gnd_service_config *config);
    ~DataHandler();

    /* override */
    virtual int initialize();

private:
    static void *pollThreadFunc(void *arg);
    static void timerNotifyCb(union sigval val);
    void startPollThread();
    void initTimer();
    void handleUevent(int ufd);
    void updateSbusState(char *state);
    void setSbusEnabled(int index, bool enabled);
    void updatePPMState(char *state);
    int  readAndSendPPMData();
    void setPPMEnabled(int index, bool enabled);
    bool getPPMEnabled(int index);

    int mSbusFds[2];
    timer_t mTimer;
    uint8_t mPPMMask;
    pthread_mutex_t mPPMLock;
};

#endif
