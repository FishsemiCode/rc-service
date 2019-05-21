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

#include "service.h"
#include "event_handler.h"

static struct gnd_service_config g_config;

static int load_config(const string &filename)
{
    ConfigLoader loader;

    if (filename.empty())
        return -EINVAL;

    if (!loader.loadConfig(filename)) {
        ALOGE("Loading config file:%s failed\n", filename.c_str());
        return -ENXIO;
    }

    loader.beginSection("General");
    strcpy(g_config.config_dir, loader.getStr("ConfigDir", "").c_str());
    strcpy(g_config.key_filename, loader.getStr("KeyconfigName", "").c_str());
    strcpy(g_config.js_filename, loader.getStr("JoystickconfigName", "").c_str());
    loader.endSection();

    loader.beginSection("UdpConfig");
    strcpy(g_config.ip, loader.getStr("IpAddress", "").c_str());
    g_config.port = loader.getInt("Port", 16666);
    loader.endSection();

    loader.beginSection("SbusCtrl");
    bool sbus1_enable = !loader.getBool("Sbus1SendbyApp");
    g_config.send_sbus_num = sbus1_enable ? 2 : 1;
    loader.endSection();

    list<string> key_names;
    list<string>::iterator it;
    loader.beginSection("KeyConfig");
    key_names = loader.getSectionKeys();
    for (it = key_names.begin(); it != key_names.end(); it++) {
        int code = loader.getInt(it->c_str());
        g_config.supported_keys[code] = *it;
        ALOGD("got a available key, name : %s, code : %d", g_config.supported_keys[code].c_str(), code);
    }
    loader.endSection();

    return 0;
}

int gnd_main(int argc, char *argv[])
{
    int result;

    if (!argc)
        return -EINVAL;

    result = load_config(argv[0]);
    if (result < 0)
        return result;

    EventHandler handler(&g_config);
    handler.initialize();

    /* main thread sleep */
    while (1) {
        sleep(1000);
    }

    return 0;
}
