/*
 *  Copyright (c) 2024 Rockchip Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#ifndef _SAMPLE_FEC_H_
#define _SAMPLE_FEC_H_

#define info(fmt, args...)                            \
    do {                                              \
        if (!options[OptSilent]) printf(fmt, ##args); \
    } while (0)

#define stderr_info(fmt, args...)                              \
    do {                                                       \
        if (!options[OptSilent]) fprintf(stderr, fmt, ##args); \
    } while (0)

/* Available options.

   Please keep the first part (options < 128) in alphabetical order.
   That makes it easier to see which short options are still free.

   In general the lower case is used to set something and the upper
   case is used to retrieve a setting. */
enum Option {
    // clang-format off
    OptSetFormat = 'f',
    OptSetWidth = 'w',
    OptSetHeight = 'h',
    OptHelp = 'z',

    OptStreamCount = 128,
    OptStreamTo,
    OptStreamFrom,
    OptSilent,
    OptLast = 512
    // clang-format on
};

#endif
