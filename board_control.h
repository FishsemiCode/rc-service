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

#ifndef BOARD_CONTROL_H
#define BOARD_CONTROL_H

#include <string>
#include "config_loader.h"

class BoardControl
{
public:
    typedef struct {
        /* pwm gpio ... */
        union {
            /* pwm device attr */
            struct {
                int pwm_port;
                int pwm_period;
                int pwm_duty;
                int sbus_channel;
            };
            /* other type device attr */
        };
    } DevInfo_t;

    BoardControl(const string &filename);
    ~BoardControl();
    int mControlSbus;
    void controlDev(uint8_t data[][25]);
private:
    DevInfo_t *mDevInfo;
    ConfigLoader *mLoader;
    string mFileName;
    uint16_t mSbusChannelData[16];

    bool parseSbusData(uint8_t data[][25]);
    void loadSettings();
    bool initPwmDev(int port, int period);
    bool setValue(const string &filename, int value);
    bool getValue(const string &filename, int* value);
};

#endif
