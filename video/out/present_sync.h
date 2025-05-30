/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MP_PRESENT_SYNC_H
#define MP_PRESENT_SYNC_H

#include <stdbool.h>
#include <stdint.h>
#include "vo.h"

/* Generic helpers for obtaining presentation feedback from
 * backend APIs. This requires ust/msc values. */

struct mp_present {
    int64_t current_ust;
    int64_t current_msc;
    int64_t last_ust;
    int64_t last_msc;
    int64_t vsync_duration;
    int64_t last_skipped_vsyncs;
    int64_t last_queue_display_time;
};

// Used during the get_vsync call to deliver the presentation statistics to the VO.
void present_sync_get_info(struct mp_present *present, struct vo_vsync_info *info);

// Called after every buffer swap to update presentation statistics.
void present_sync_swap(struct mp_present *present);

// Called anytime the backend delivers new ust/msc values.
void present_update_sync_values(struct mp_present *present, int64_t ust,
                                int64_t msc);

#endif /* MP_PRESENT_SYNC_H */
