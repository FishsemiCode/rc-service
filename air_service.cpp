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

#include "config_loader.h"
#include "board_control.h"
#include "service.h"
#include "rc_utils.h"
#include <signal.h>
#include <time.h>
#include <linux/serial.h>
#include <linux/un.h>

struct radio_msg {
    uint8_t rssi;
    uint8_t noise;
};

struct rc_info {
    int tty_fd;
    bool update_flag;
    /* sbus or ppm data */
    uint8_t rc_data[SBUS_DATA_LEN];
};

struct service_config {
    /* radio_config */
    float filter;
    int snr_hys_min;
    int snr_hys_max;
    int rssi_hys_min;
    int rssi_hys_max;
    /* sbus_config */
    bool is_low_speed;
    int sbus_count;
    char sbus_port[2][20];
    bool sbus_passthrough[2];
    /* other_config */
    int rc_inet_udp_port;
    char radio_unix_udp_name[20];
    int control_sbus;
};

static timer_t g_timer;
static bool g_stop_flag = false;
static struct rc_info g_rc[2];
static struct service_config g_cfg;
static pthread_mutex_t sbus_lock;
static pthread_mutex_t bc_lock;
static pthread_cond_t bc_cond;

static int sbus_init(void)
{
    int i;

    for (i = 0; i < g_cfg.sbus_count; i++) {
        g_rc[i].update_flag = false;
        if (!strcmp(g_cfg.sbus_port[i], ""))
            continue;
        g_rc[i].tty_fd = sbus_port_init(g_cfg.sbus_port[i]);
        if (g_rc[i].tty_fd < 0) {
            ALOGE("open %s failed to connect error=%s\n", g_cfg.sbus_port[i], strerror(errno));
            return -ENXIO;
        }

        ALOGI("%s sbus init success\n", g_cfg.sbus_port[i]);
    }

    return 0;
}

static void output_sbus_singal(union sigval val)
{
    static uint8_t sbusdata[2][SBUS_DATA_LEN];
    int i;

    for (i = 0; i < 2; i++) {
        if (g_rc[i].update_flag) {
            pthread_mutex_lock(&sbus_lock);
            memcpy(sbusdata[i], g_rc[i].rc_data, sizeof(sbusdata[0]));
            pthread_mutex_unlock(&sbus_lock);
            g_rc[i].update_flag = false;
        }
    }

    if (!g_stop_flag) {
        if (g_rc[0].tty_fd && (write(g_rc[0].tty_fd, sbusdata[0], sizeof(sbusdata[0])) < 0))
            ALOGE("send sbus0 singal failed, err:%s\n", strerror(errno));
        if (g_rc[1].tty_fd && (write(g_rc[1].tty_fd, sbusdata[1], sizeof(sbusdata[1])) < 0))
            ALOGE("send sbus1 singal failed, err:%s\n", strerror(errno));
    }
}

static void timer_init(void)
{
    struct sigevent evp;
    struct itimerspec ts;

    memset(&evp, 0, sizeof(struct sigevent));
    memset(&ts, 0, sizeof(struct itimerspec));

    evp.sigev_value.sival_ptr = &g_timer;
    evp.sigev_notify = SIGEV_THREAD;
    evp.sigev_notify_function = output_sbus_singal;
    timer_create(CLOCK_MONOTONIC, &evp, &g_timer);

    ts.it_interval.tv_sec = 0;
    ts.it_interval.tv_nsec = (g_cfg.is_low_speed) ? (1000000000 / 70) : (1000000000 / 140);
    ts.it_value.tv_sec = 1;
    ts.it_value.tv_nsec = 0;

    timer_settime(g_timer, 0, &ts, NULL);
}

static void process_radio_msg(struct radio_msg *radio_status)
{
    static int rssi = INT_MAX, snr = INT_MAX;
    int tmp_rssi, tmp_noise, tmp_snr;

    if (!radio_status) {
        ALOGE("%s invalid param", __FUNCTION__);
        return;
    }

    tmp_rssi = -radio_status->rssi;
    tmp_noise = -radio_status->noise;
    tmp_snr = tmp_rssi - tmp_noise;

    /* using one order lag filtering algorithm for rssi and snr */
    if (rssi != INT_MAX) {
        rssi = (int)(rssi * (1.0 - g_cfg.filter) + tmp_rssi * g_cfg.filter);
        snr = (int)(snr * (1.0 - g_cfg.filter) + tmp_snr * g_cfg.filter);
    } else {
        /* first */
        rssi = tmp_rssi;
        snr = tmp_snr;
    }

    /* using hysteresis comparator for rssi and snr */
    if ((snr < g_cfg.snr_hys_min) || (rssi < g_cfg.rssi_hys_min)) {
        g_stop_flag =  true;
    } else if ((snr > g_cfg.snr_hys_max) && (rssi > g_cfg.rssi_hys_max)) {
        g_stop_flag = false;
    }
    g_rc[0].update_flag = true;
    g_rc[1].update_flag = true;

    ALOGI("radio status r:%d, cr:%d, s:%d, cs:%d, fs:%d, fu:%d,%d\n",
          tmp_rssi, rssi, tmp_snr, snr, (int)g_stop_flag, (int)g_rc[0].update_flag, (int)g_rc[1].update_flag);
    ALOGI("radio threshold filter:%.2f, snr_hmin:%d, snr_hmax:%d, rssi_hmin:%d, rssi_hmax:%d\n",
          g_cfg.filter, g_cfg.snr_hys_min, g_cfg.snr_hys_max, g_cfg.rssi_hys_min, g_cfg.rssi_hys_max);
}

static int socket_init(uint16_t family, int port, char *name)
{
    int ret = -1, sfd;
    socklen_t len;

    sfd = socket(family, SOCK_DGRAM, 0);
    if (sfd < 0) {
        return sfd;
    }

    if (family == AF_INET) {
        struct sockaddr_in inet_addr;
        len = sizeof(struct sockaddr_in);
        memset(&inet_addr, 0, len);
        inet_addr.sin_family = family;
        inet_addr.sin_addr.s_addr = htonl(INADDR_ANY);
        inet_addr.sin_port = htons(port);
        ret = ::bind(sfd, (struct sockaddr *)&inet_addr, len);
    } else if (family == AF_UNIX) {
        struct sockaddr_un unix_addr;
        len = sizeof(struct sockaddr_un);
        memset(&unix_addr, 0, len);
        unlink(name);
        unix_addr.sun_family = family;
        strncpy(unix_addr.sun_path, name, strlen(name));
        ret = ::bind(sfd, (struct sockaddr *)&unix_addr, len);
    }

    if (ret < 0) {
        ALOGE("socket bind failed\n");
        close(sfd);
        return ret;
    }
    return sfd;
}

static void *handle_control(void *data)
{
    string filename = (char *)data;
    uint8_t sbusdata[25] = "\0";
    BoardControl * bc = new BoardControl(filename);

    while (1) {
        pthread_mutex_lock(&bc_lock);
        pthread_cond_wait(&bc_cond, &bc_lock);
        pthread_mutex_unlock(&bc_lock);

        pthread_mutex_lock(&sbus_lock);
        memcpy(sbusdata, g_rc[bc->mControlSbus].rc_data, sizeof(sbusdata));
        pthread_mutex_unlock(&sbus_lock);

        bc->controlDev(&sbusdata);
    }
}

static void *recv_radio_msg(void *)
{
    struct sockaddr_un fromaddr;
    struct radio_msg msg;
    socklen_t len = sizeof(struct sockaddr_un);
    ssize_t size = sizeof(struct radio_msg);
    int sfd;

    sfd = socket_init(AF_UNIX, 0, g_cfg.radio_unix_udp_name);
    if (sfd < 0) {
        ALOGE("create radio unix sock_dgram failed\n");
        goto fail;
    }

    while (1) {
        if (recvfrom(sfd, &msg, size, 0, (struct sockaddr *)&fromaddr, &len) == size) {
            process_radio_msg(&msg);
        }
        memset(&msg, '0', size);
    }

fail:
    if (sfd > 0)
        close(sfd);

    exit(-1);
}

static int load_config_file(const string &filename)
{
    ConfigLoader config_loader;

    if (filename.empty())
        return -EINVAL;

    if (!config_loader.loadConfig(filename)) {
        ALOGE("Loading config file:%s failed\n", filename.c_str());
        return -ENXIO;
    }

    config_loader.beginSection("Radio_config");
    g_cfg.filter = config_loader.getFloat("filter", 0.25);
    g_cfg.snr_hys_min = config_loader.getInt("snr.hys.min", -10);
    g_cfg.snr_hys_max = config_loader.getInt("snr.hys.max", -5);
    g_cfg.rssi_hys_min = config_loader.getInt("rssi.hys.min", -130);
    g_cfg.rssi_hys_max = config_loader.getInt("rssi.hys.max", -125);
    config_loader.endSection();

    config_loader.beginSection("SBUS_config");
    g_cfg.is_low_speed = !strcmp(config_loader.getStr("is_low_speed", "low").c_str(), "low");
    g_cfg.sbus_count = config_loader.getInt("sbus_count", 2);
    strcpy(g_cfg.sbus_port[0], config_loader.getStr("sbus1_port", "").c_str());
    strcpy(g_cfg.sbus_port[1], config_loader.getStr("sbus2_port", "").c_str());
    g_cfg.sbus_passthrough[0] = config_loader.getBool("sbus1_passthrough", true);
    g_cfg.sbus_passthrough[1] = config_loader.getBool("sbus2_passthrough", false);
    config_loader.endSection();

    config_loader.beginSection("Other_config");
    g_cfg.rc_inet_udp_port = config_loader.getInt("rc_inet_udp_port", 16666);
    strcpy(g_cfg.radio_unix_udp_name, config_loader.getStr("radio_unix_udp_name", "").c_str());
    g_cfg.control_sbus = config_loader.getInt("board_control_sbus", 0) - 1;
    config_loader.endSection();

    ALOGI("config info -> filter:%.2f, snr_hmin:%d, snr_hmax:%d, rssi_hmin:%d, rssi_hmax:%d, is_low_speed:%d, sbus1_port:%s, sbus2_port:%s, rc_inet_udp_port:%d, radio_unix_udp_name:%s\n",
          g_cfg.filter, g_cfg.snr_hys_min, g_cfg.snr_hys_max, g_cfg.rssi_hys_min, g_cfg.rssi_hys_max,
          g_cfg.is_low_speed, g_cfg.sbus_port[0], g_cfg.sbus_port[1], g_cfg.rc_inet_udp_port, g_cfg.radio_unix_udp_name);

    return 0;
}

int air_main(int argc, char *argv[])
{
    pthread_t radio_thread, board_control_thread;
    struct sockaddr_in fromaddr;
    struct rc_msg msg;
    int sfd = 0, idx, res;
    socklen_t len = sizeof(struct sockaddr_in);
    ssize_t size = sizeof(struct rc_msg);

    if (!argc)
        return -EINVAL;

    res = load_config_file(argv[0]);
    if (res < 0)
        return res;

    res = sbus_init();
    if (res < 0)
        return res;

    pthread_mutex_init(&sbus_lock, NULL);

    timer_init();

    res = pthread_create(&radio_thread, NULL, recv_radio_msg, NULL);
    if (res < 0) {
        ALOGE("create radio thread failed\n");
        goto radio_thread_fail;
    }

    if ((!g_cfg.sbus_passthrough[0] || !g_cfg.sbus_passthrough[1]) && (g_cfg.control_sbus > 0)) {
        pthread_mutex_init(&bc_lock, NULL);
        pthread_cond_init(&bc_cond, NULL);
        res = pthread_create(&board_control_thread, NULL, handle_control, argv[0]);
        if (res < 0) {
            ALOGE("create board control thread failed\n");
            goto board_control_thread_fail;
        }
    }

    sfd = socket_init(AF_INET, g_cfg.rc_inet_udp_port, NULL);
    if (sfd < 0) {
        ALOGE("create rc inet sock_dgram failed\n");
        goto socket_fail;
    }

    while (1) {
        if (recvfrom(sfd, &msg, size, 0, (struct sockaddr *)&fromaddr, &len) == size) {
             if (msg.type_idex & SBUS_MODE) {
                 idx = msg.type_idex & CHANNEL_IDEX;
                 pthread_mutex_lock(&sbus_lock);
                 memcpy(g_rc[idx].rc_data, msg.rc_data, SBUS_DATA_LEN);
                 pthread_mutex_unlock(&sbus_lock);
                 g_rc[idx].update_flag = true;
                 debug_sbus_data_interval(idx, g_rc[idx].rc_data + 1, 70);
                 if (!g_cfg.sbus_passthrough[idx]) {
                     pthread_mutex_lock(&bc_lock);
                     pthread_cond_signal(&bc_cond);
                     pthread_mutex_unlock(&bc_lock);
                 }
             }
        }
        memset(&msg, '0', size);
    }

    return 0;

socket_fail:
    if (!g_cfg.sbus_passthrough[0] || !g_cfg.sbus_passthrough[1])
        pthread_exit(&board_control_thread);
board_control_thread_fail:
    pthread_exit(&radio_thread);
radio_thread_fail:
    if (g_rc[0].tty_fd)
        close(g_rc[0].tty_fd);

    if (g_rc[1].tty_fd)
        close(g_rc[1].tty_fd);

    timer_delete(g_timer);

    if (sfd)
        close(sfd);

    return -1;
}
