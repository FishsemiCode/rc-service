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
#include <math.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include "service.h"
#include "rc_utils.h"

#define SCALE_OFFSET 874
#define SCALE_FACTOR 0.625

using namespace std;

int sbus_port_init(const char *sbus_port)
{
    struct termios2 tio;
    struct termios opt;

    int tty_fd = open(sbus_port, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty_fd < 0) {
        ALOGE("open %s failed to connect error=%s\n", sbus_port, strerror(errno));
        return -ENXIO;
    }

    tcgetattr(tty_fd, &opt);
    cfmakeraw(&opt);
    tcsetattr(tty_fd, TCSANOW, &opt);

    if (ioctl(tty_fd, TCGETS2, &tio) < 0) {
        ALOGE("get tty info failed error=%s\n", strerror(errno));
        goto failed;
    }

    tio.c_iflag &= ~(INLCR | IGNCR);
    tio.c_cflag &= ~(CBAUDEX | CSTOPB | PARENB | PARODD | CRTSCTS | CBAUD | CSIZE);
    tio.c_cflag |= CSTOPB | PARENB | BOTHER | CS8;
    tio.c_ispeed = SBUS_BAUD;
    tio.c_ospeed = SBUS_BAUD;

    if (ioctl(tty_fd, TCSETS2, &tio) < 0) {
        ALOGE("set baud info failed error=%s\n", strerror(errno));
        goto failed;
    }

    if (ioctl(tty_fd, TCFLSH, TCIOFLUSH) == -1) {
        ALOGE("Could not flush terminal (%m)");
        goto failed;
    }

    ALOGI("%s sbus uart init success\n", sbus_port);

    return tty_fd;

failed:
    close(tty_fd);
    return -ENXIO;
}

int tty_port_init(const char *tty_port)
{
    struct termios2 tio;
    struct termios opt;

    int tty_fd = open(tty_port, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty_fd < 0) {
        ALOGE("open %s failed to connect error=%s\n", tty_port, strerror(errno));
        return -ENXIO;
    }

    tcgetattr(tty_fd, &opt);
    cfmakeraw(&opt);
    cfsetispeed(&opt, B115200);
    tcsetattr(tty_fd, TCSANOW, &opt);

    if (ioctl(tty_fd, TCFLSH, TCIOFLUSH) == -1) {
        ALOGE("Could not flush terminal (%m)");
        goto failed;
    }

    ALOGI("%s tty uart init success\n", tty_port);

    return tty_fd;

failed:
    close(tty_fd);
    return -ENXIO;
}

int add_epoll_fd(int epoll_fd, int device_fd, epoll_data_t data)
{
    int ret = 0;
    struct epoll_event event;
    memset(&event, 0, sizeof(event));

    event.events = EPOLLIN;
    event.data = data;

    ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, device_fd, &event);
    if (ret != 0) {
        ALOGE("Connot add fd %d to epoll instance : %s", device_fd, strerror(errno));
    }

    return ret;
}

int del_epoll_fd(int epoll_fd, int device_fd)
{
    int ret = 0;

    ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, device_fd, NULL);
    if (ret != 0) {
        ALOGE("Could not del device fd %d to epoll instance: %s", device_fd, strerror(errno));
    }

    return ret;
}

bool setValue(const string &filename, int value)
{
    ofstream oFile(filename.c_str());

    if (!oFile) {
        ALOGE("open %s failed error=%s\n", filename.c_str(), strerror(errno));
        return false;
    }

    oFile << value <<endl;
    oFile.close();

    return true;
}

bool getValue(const string &filename, int *value)
{
    char buf[10] = "\0";
    ifstream inFile(filename.c_str());

    if (!inFile) {
        ALOGE("open %s failed error=%s\n", filename.c_str(), strerror(errno));
        return false;
    }

    inFile >> buf;
    inFile.close();
    *value = atoi(buf);

    return true;
}

void pack_rc_msg(int sbus, uint16_t (&channels)[16], struct rc_msg *msg)
{
    msg->type_idex = (sbus & CHANNEL_IDEX) | SBUS_MODE;
    /* sbus protocol start byte:0xF0 */
    msg->rc_data[0] = SBUS_STARTBYTE;
    /* sbus protocol data1: channel1 0-7 bit */
    msg->rc_data[1] =(channels[0] & 0xff);
    /* sbus protocol data2: channel1 8-10 bit and channel2 0-4 */
    msg->rc_data[2] =(((channels[0] >> 8) | (channels[1]<< 3)) & 0xff);
    /* sbus protocol data3: channel2 5-10 bit and channel3 0-1 */
    msg->rc_data[3] =(((channels[1] >> 5) | (channels[2] << 6)) & 0xff);
    /* sbus protocol data4: channel3 2-9 bit */
    msg->rc_data[4] =((channels[2] >> 2) & 0xff);
    /* sbus protocol data5: channel3 10 bit and channel4 0-6 */
    msg->rc_data[5] =(((channels[2] >> 10) | (channels[3] << 1)) & 0xff);
    /* sbus protocol data6: channel4 7-10 bit and channel5 0-3 */
    msg->rc_data[6] =(((channels[3] >> 7) | (channels[4] << 4)) & 0xff);
    /* sbus protocol data7: channel5 4-10 bit and channel6 0 */
    msg->rc_data[7] =(((channels[4] >> 4) | (channels[5] << 7)) & 0xff);
    /* sbus protocol data8: channel6 1-8 bit */
    msg->rc_data[8] =((channels[5] >> 1) & 0xff);
    /* sbus protocol data9: channel6 9-10 bit and channel7 0-5 */
    msg->rc_data[9] =(((channels[5] >> 9) | (channels[6] << 2)) & 0xff);
    /* sbus protocol data10: channel7 6-10 bit and channel8 0-2 */
    msg->rc_data[10] =(((channels[6] >> 6) | (channels[7] << 5)) & 0xff);
    /* sbus protocol data11: channel8 3-10 bit */
    msg->rc_data[11] =(((channels[7] >> 3)) & 0xff);

    /* sbus protocol data12: channel9 0-7 bit */
    msg->rc_data[12] =(channels[8] & 0xff);
    /* sbus protocol data13: channel9 8-10 bit and channel10 0-4 */
    msg->rc_data[13] =(((channels[8] >> 8) | (channels[9] << 3)) & 0xff);
    /* sbus protocol data14: channel10 5-10 bit and channel11 0-1 */
    msg->rc_data[14] =(((channels[9] >> 5) | (channels[10] << 6)) & 0xff);
    /* sbus protocol data15: channel11 2-9 bit */
    msg->rc_data[15] =((channels[10] >> 2) & 0xff);
    /* sbus protocol data16: channel11 10 bit and channel12 0-6 */
    msg->rc_data[16] =(((channels[10] >> 10) | (channels[11] << 1)) & 0xff);
    /* sbus protocol data17: channel12 7-10 bit and channel13 0-3 */
    msg->rc_data[17] =(((channels[11] >> 7) | (channels[12] << 4)) & 0xff);
    /* sbus protocol data18: channel13 4-10 bit and channel14 0 */
    msg->rc_data[18] =(((channels[12] >> 4) | (channels[13] << 7)) & 0xff);
    /* sbus protocol data19: channel14 1-8 bit */
    msg->rc_data[19] =((channels[13] >> 1) & 0xff);
    /* sbus protocol data20: channel14 9-10 bit and channel15 0-5 */
    msg->rc_data[20] =(((channels[13] >> 9) | (channels[14] << 2)) & 0xff);
    /* sbus protocol data21: channel15 6-10 bit and channel16 0-2 */
    msg->rc_data[21] =(((channels[14] >> 6) | (channels[15] << 5)) & 0xff);
    /* sbus protocol data22: channel16 3-10 bit */
    msg->rc_data[22] =((channels[15] >> 3) & 0xff);

    msg->rc_data[23] = 0x00;
    msg->rc_data[24] = SBUS_ENDBYTE;
}

void debug_sbus_data(int index, uint8_t *s)
{
    uint16_t channel_data[16];
    uint16_t *d = channel_data;

#define F(v, s) (((v) >> (s)) & 0x7ff)

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

    std::ostringstream log;
    for (int i = 0; i < 16; i++) {
        log << std::setw(4) << channel_data[i] << " ";
    }
    ALOGI("SBUS%d: %s", index, log.str().c_str());
}

/*
 * Nonthread-safe
 * Do not call this function in different threads.
 */
void debug_sbus_data_interval(int index, uint8_t *s, int interval)
{
    static int count[2] = { 0, 0 };

    if (index >= 2) {
        ALOGE("%s : uncorrect sbus index %d", __func__, index);
        return;
    }

    if (++count[index] < interval) {
        return;
    }

    debug_sbus_data(index, s);
    count[index] = 0;
}

int ppm_to_sbus(int ppm)
{
    int sbus = 0;

    if (ppm < 800 || ppm > 2200) {
        ALOGE("ppm value out of range");
        return sbus;
    }
    if (ppm >= 800 && ppm <= 874) {
        sbus = 0;
    } else if (ppm >= 875 && ppm <= 2152) {
        sbus = ceil((ppm - SCALE_OFFSET - 0.5f) / SCALE_FACTOR);
    } else if (ppm >= 2153 && ppm <= 2200) {
        sbus = 2047;
    }

    return sbus;
}

int sbus_to_ppm(int sbus)
{
    int ppm = 0;

    if (sbus < 0 || sbus > 2047) {
        ALOGE("sbus value out of range");
        return ppm;
    }
    if (sbus == 0) {
        ppm = 800;
    } else if (sbus == 2047) {
        ppm = 2200;
    } else {
        ppm = sbus * SCALE_FACTOR + SCALE_OFFSET + 0.5f;
    }

    return ppm;
}

uint16_t crc16_sum(uint8_t data[], size_t size)
{
    uint16_t sum = 0xffff;
    size_t i = 0;
    uint8_t tmp;

    while (i < size) {
        tmp = data[i] ^ (uint8_t)(sum & 0xff);
        tmp ^= (tmp << 4);
        sum = (sum >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4);
        i++;
    }

    return sum;
}

uint8_t bcc_sum(uint8_t data[], size_t size)
{
    uint8_t sum = 0x00;

    for (size_t i = 0; i < size; i++) {
        sum ^= data[i];
    }

    return sum;
}
