/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef MPLAYER_X11_COMMON_H
#define MPLAYER_X11_COMMON_H

#include <stdint.h>
#include <stdbool.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include "common/common.h"
#include "osdep/atomic.h"

#include "config.h"
#if !HAVE_GPL
#error GPL only
#endif

struct vo;
struct mp_log;

#define MAX_DISPLAYS 32 // ought to be enough for everyone

struct xrandr_display {
    struct mp_rect rc;
    double fps;
    char *name;
    bool overlaps;
    int atom_id; // offset by location of primary
    int screen;
};

struct vo_x11_state {
    struct mp_log *log;
    struct input_ctx *input_ctx;
    struct m_config_cache *opts_cache;
    struct mp_vo_opts *opts;
    Display *display;
    int event_fd;
    int wakeup_pipe[2];
    Window window;
    Window rootwin;
    Window parent;  // embedded in this foreign window
    int screen;
    int display_is_local;
    int ws_width;
    int ws_height;
    double dpi_scale;
    struct mp_rect screenrc;
    char *window_title;

    struct xrandr_display displays[MAX_DISPLAYS];
    int num_displays;
    int current_screen;

    int xrandr_event;
    bool has_mesa;
    bool has_nvidia;

    bool screensaver_enabled;
    bool dpms_touched;
    double screensaver_time_last;

    struct mp_present *present;
    bool use_present;
    int present_code;

    XIM xim;
    XIC xic;
    bool no_autorepeat;

    Colormap colormap;

    int wm_type;
    bool hidden; // _NET_WM_STATE_HIDDEN
    bool window_hidden; // the window was mapped at least once
    bool pseudo_mapped; // not necessarily mapped, but known window size
    int fs;     // whether we assume the window is in fullscreen mode

    bool init_fs; // whether mpv was launched with --fs

    bool mouse_cursor_visible; // whether we want the cursor to be visible (only
                               // takes effect when the window is focused)
    bool mouse_cursor_set; // whether the cursor is *currently* *hidden*
    bool has_focus;
    long orig_layer;

    // Current actual window position (updated on window move/resize events).
    struct mp_rect winrc;
    double current_display_fps;

    int pending_vo_events;

    // last non-fullscreen extends (updated on fullscreen or reinitialization)
    struct mp_rect nofsrc;

    /* Keep track of original video width/height to determine when to
     * resize window when reconfiguring. Resize window when video size
     * changes, but don't force window size changes as long as video size
     * stays the same (even if that size is different from the current
     * window size after the user modified the latter). */
    int old_dw, old_dh;
    int old_x, old_y;
    /* Video size changed during fullscreen when we couldn't tell the new
     * size to the window manager. Must set window size when turning
     * fullscreen off. */
    bool size_changed_during_fs;
    bool pos_changed_during_fs;

    /* One of the autofit/geometry options changed at runtime. */
    bool geometry_change;

    XComposeStatus compose_status;

    /* XShm stuff */
    int ShmCompletionEvent;
    /* Number of outstanding XShmPutImage requests */
    /* Decremented when ShmCompletionEvent is received */
    /* Increment it before XShmPutImage */
    int ShmCompletionWaitCount;

    /* drag and drop */
    Atom dnd_requested_format;
    Atom dnd_requested_action;
    Window dnd_src_window;

    Atom icc_profile_property;
};

bool vo_x11_init(struct vo *vo);
void vo_x11_uninit(struct vo *vo);
void vo_x11_check_events(struct vo *vo);
bool vo_x11_screen_is_composited(struct vo *vo);
bool vo_x11_create_vo_window(struct vo *vo, XVisualInfo *vis,
                             const char *classname);
void vo_x11_config_vo_window(struct vo *vo);
bool vo_x11_check_visible(struct vo *vo);
int vo_x11_control(struct vo *vo, int *events, int request, void *arg);
void vo_x11_present(struct vo *vo);
void vo_x11_sync_swap(struct vo *vo);
void vo_x11_wakeup(struct vo *vo);
void vo_x11_wait_events(struct vo *vo, int64_t until_time_us);

void vo_x11_silence_xlib(int dir);

bool vo_x11_is_rgba_visual(XVisualInfo *v);

#endif /* MPLAYER_X11_COMMON_H */
