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

#include "board_control.h"
#include "rc_utils.h"
#include "service.h"
#include <sstream>

#define PWM_PATH(port, str_1, str_2, node) \
    port ? (str_1 = str_2 + "1/" + node) : (str_1 = str_2 + "0/" + node);

string PWM_BASE_PATH = "/sys/class/pwm/pwmchip0/pwm";
string PWM_EXPORT_PATH = "/sys/class/pwm/pwmchip0/export";

BoardControl::BoardControl(const string &filename)
    :mFileName(filename)
{
    mLoader = new ConfigLoader();
    /* Only support dual pwm device now */
    mDevInfo = new DevInfo_t[2];

    loadSettings();
}

BoardControl::~BoardControl()
{
    delete mLoader;
    delete[] mDevInfo;
}

void BoardControl::loadSettings()
{
    mLoader->loadConfig(mFileName);

    ALOGI("board control: load setting file:%s\n", mFileName.c_str());

    mLoader->beginSection("Other_config");
    mControlSbus = mLoader->getInt("board_control_sbus", 0) - 1;
    mLoader->endSection();

    /* get dual pwm config */
    for (int i = 0; i < 2; i++) {
        ostringstream oss;
        oss << "Device_pwm_" << (i + 1);
        mLoader->beginSection(oss.str());
        mDevInfo[i].pwm_port = i;
        mDevInfo[i].pwm_period = mLoader->getInt("pwm_period", 0);
        mDevInfo[i].sbus_channel = mLoader->getInt("sbus_channel", 0) - 1;
        ALOGI("board control pwm dev:%d,%d,%d\n", mDevInfo[i].pwm_port, mDevInfo[i].pwm_period, mDevInfo[i].sbus_channel);
        if ((mDevInfo[i].sbus_channel >= 0) && !initPwmDev(mDevInfo[i].pwm_port, mDevInfo[i].pwm_period))
            ALOGE("init pwm device failed\n");
        mLoader->endSection();
    }
}

bool BoardControl::initPwmDev(int port, int period)
{
    string period_str, duty_str, enable_str;

    if (port >= 2) {
        ALOGE("invalid parameter port:%d\n", port);
        return false;
    }

    if (!setValue(PWM_EXPORT_PATH, port))
        return false;

    /* set duty_cyce first(default value is 0), then set period */
    PWM_PATH(port, duty_str, PWM_BASE_PATH, "duty_cycle");
    if (!setValue(duty_str, 0))
        return false;

    PWM_PATH(port, period_str, PWM_BASE_PATH, "period");
    if (!setValue(period_str, period))
        return false;

    PWM_PATH(port, enable_str, PWM_BASE_PATH, "enable");
    if (!setValue(enable_str, 1))
        return false;

    return true;
}

void BoardControl::controlDev(uint8_t data[][25])
{
    string duty_str;

    if (data == NULL || !parseSbusData(data))
        return;

    /* control dual pwm device */
    for (int i = 0; i < 2; i++) {
        if ((mDevInfo[i].sbus_channel >= 0) && mDevInfo[i].pwm_duty != mSbusChannelData[mDevInfo[i].sbus_channel]) {
            PWM_PATH(mDevInfo[i].pwm_port, duty_str, PWM_BASE_PATH, "duty_cycle");
            if (mSbusChannelData[mDevInfo[i].sbus_channel] > 100)
               mSbusChannelData[mDevInfo[i].sbus_channel] = 100;
            setValue(duty_str, mSbusChannelData[mDevInfo[i].sbus_channel] * (mDevInfo[i].pwm_period / 100));
            mDevInfo[i].pwm_duty = mSbusChannelData[mDevInfo[i].sbus_channel];
        }
    }

    /* control other device */
}

/*
 * S.Bus serial port settings:
 *  100000bps inverted serial stream, 8 bits, even parity, 2 stop bits
 *  frame period is 7ms (HS) or 14ms (FS)
 *
 * Frame structure:
 *  1 byte  - 0x0f (start of frame byte)
 * 22 bytes - channel data (11 bit/channel, 16 channels, LSB first)
 *  1 byte  - bit flags:
 *                   0x01 - discrete channel 1,
 *                   0x02 - discrete channel 2,
 *                   0x04 - lost frame flag,
 *                   0x08 - failsafe flag,
 *                   0xf0 - reserved
 *  1 byte  - 0x00 (end of frame byte)
 *
 */
bool BoardControl::parseSbusData(uint8_t data[][25])
{
    uint16_t *d = mSbusChannelData;
    /* channel data */
    uint8_t *s = *data + 1;

    #define F(v, s) (((v) >> (s)) & 0x7ff)

    if (*data[0] != 0x0f || *data[24] != 0x00) {
        ALOGE("Error format sbus:%d data, and sof:0x%x, eof:0x%x\n", mControlSbus, *data[0], *data[24]);
        return false;
    }

    /* unroll channels 1-8 */
    *d++ = F(s[0] | s[1] << 8, 0);
    *d++ = F(s[1] | s[2] << 8, 3);
    *d++ = F(s[2] | s[3] << 8 | s[4] << 16, 6);
    *d++ = F(s[4] | s[5] << 8, 1);
    *d++ = F(s[5] | s[6] << 8, 4);
    *d++ = F(s[6] | s[7] << 8 | s[8] << 16, 7);
    *d++ = F(s[8] | s[9] << 8, 2);
    *d++ = F(s[9] | s[10] << 8, 5);

    /* unroll channels 9-16 */
    *d++ = F(s[11] | s[12] << 8, 0);
    *d++ = F(s[12] | s[13] << 8, 3);
    *d++ = F(s[13] | s[14] << 8 | s[15] << 16, 6);
    *d++ = F(s[15] | s[16] << 8, 1);
    *d++ = F(s[16] | s[17] << 8, 4);
    *d++ = F(s[17] | s[18] << 8 | s[19] << 16, 7);
    *d++ = F(s[19] | s[20] << 8, 2);
    *d++ = F(s[20] | s[21] << 8, 5);

    /* the data[23] don't care */
    return true;
}
