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

#ifndef HANDLER_H
#define HANDLER_H

#include "service.h"
#include "message_sender.h"

class Handler
{
public:
    Handler(struct gnd_service_config *config);
    virtual ~Handler();

    virtual int initialize() = 0;

protected:
    struct gnd_service_config *mConfig;
    MessageSender *mSender;

private:
};

#endif
