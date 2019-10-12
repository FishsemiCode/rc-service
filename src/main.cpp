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
#include "config_loader.h"

int main(int argc, char *argv[])
{
    int ret;
    char unit_type[5] = "\0";
    ConfigLoader config_loader;

    if (argc < 2) {
        ALOGE("%s:the count of parameter is not enough\n", __FUNCTION__);
        return -EINVAL;
    }

    if (!config_loader.loadConfig(argv[1])) {
        ALOGE("Loading config file:%s failed\n", argv[1]);
        return -ENXIO;
    }

    config_loader.beginSection("Device");
    strncpy(unit_type, config_loader.getStr("UnitType", "").c_str(), 3);
    config_loader.endSection();

    if (!strncmp("air", unit_type, 3)) {
        ALOGI("starting air rc service\n");
        ret = air_main(argc - 1, &argv[1]);
        if (ret < 0) {
            ALOGI("start air rc service failed\n");
            return ret;
        }
    } else if (!strncmp("gnd", unit_type, 3)) {
        ALOGI("starting gnd rc service\n");
        ret = gnd_main(argc - 1, &argv[1]);
        if (ret < 0) {
            ALOGI("start ground rc service failed\n");
            return ret;
        }
    } else {
        ALOGI("invalid parameter, please check !!!\n");
    }
    return 0;
}
