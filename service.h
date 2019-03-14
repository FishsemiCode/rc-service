#ifndef SERVICE_H
#define SERVICE_H

#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include <utils/Log.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <errno.h>
#include <pthread.h>
#include <arpa/inet.h>

#define PPM_MODE              0x10
#define SBUS_MODE             0x20
#define CHANNEL_IDEX          0x01
#define SBUS_BAUD             100000
#define SBUS_DATA_LEN         25

struct rc_msg {
    /* typs is MSB, idex is LSB */
    uint8_t type_idex;
    /* sbus or ppm data */
    uint8_t rc_data[SBUS_DATA_LEN];
};

int air_main(int argc, char *argv[]);

#endif
