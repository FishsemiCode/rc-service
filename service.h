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

struct gnd_service_config {
    char config_dir[PATH_MAX];
    char key_filename[PATH_MAX];
    char js_filename[PATH_MAX];

    char ip[20];
    unsigned long port;
};

int air_main(int argc, char *argv[]);
int gnd_main(int argc, char *argv[]);

#endif
