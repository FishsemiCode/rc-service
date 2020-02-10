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

#ifndef RC_UTILS_H
#define RC_UTILS_H

#include <termios.h>
#include <sys/epoll.h>

int sbus_port_init(const char *sbus_port);
int tty_port_init(const char *tty_port);
int add_epoll_fd(int epoll_fd, int device_fd, epoll_data_t data);
int del_epoll_fd(int epoll_fd, int device_fd);

bool setValue(const std::string &filename, int value);
bool getValue(const std::string &filename, int *value);
void pack_rc_msg(int sbus, uint16_t (&channels)[16], struct rc_msg *msg);
void debug_sbus_data(int index, uint8_t *s);
void debug_sbus_data_interval(int index, uint8_t *s, int interval);

int ppm_to_sbus(int ppm);
int sbus_to_ppm(int sbus);

bool check_crc(uint8_t buffer[]);
uint8_t bcc_sum(uint8_t data[], size_t size);

#endif
