/*
 * This file is part of mpv.
 *
 * Get path to config dir/file.
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

#include "misc/mp_assert.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

#include "config.h"

#include "common/common.h"
#include "common/global.h"
#include "common/msg.h"
#include "options/options.h"
#include "options/path.h"
#include "libmpv/mpv_talloc.h"
#include "osdep/io.h"
#include "osdep/path.h"
#include "misc/ctype.h"

// In order of decreasing priority: the first has highest priority.
static const mp_get_platform_path_cb path_resolvers[] = {
#if HAVE_COCOA
    mp_get_platform_path_osx,
#endif
#if defined __APPLE__
    mp_get_platform_path_darwin,
#elif !defined(_WIN32) || defined(__CYGWIN__)
    mp_get_platform_path_unix,
#endif
#if HAVE_UWP
    mp_get_platform_path_uwp,
#elif defined(_WIN32)
    mp_get_platform_path_win,
#endif
};

// from highest (most preferred) to lowest priority
static const char *const config_dirs[] = {
    "home",
    "old_home",
    "osxbundle",
    "exe_dir",
    "global",
};
// types that configdir replaces (if set)
// These are not part of any fallback order so need to be overriden separately.
static const char *const config_dir_replaces[] = {
    "state",
    "cache",
};

// Return a platform specific path using a path type as defined in osdep/path.h.
// Keep in mind that the only way to free the return value is freeing talloc_ctx
// (or its children), as this function can return a statically allocated string.
static const char *mp_get_platform_path(void *talloc_ctx,
                                        struct mpv_global *global,
                                        const char *type)
{
    mp_assert(talloc_ctx);

    if (global->configdir) {
        // force all others to NULL, only first returns the path
        for (int n = 0; n < MP_ARRAY_SIZE(config_dirs); n++) {
            if (strcmp(config_dirs[n], type) == 0)
                return (n == 0 && global->configdir[0]) ? global->configdir : NULL;
        }
        for (int n = 0; n < MP_ARRAY_SIZE(config_dir_replaces); n++) {
            if (strcmp(config_dir_replaces[n], type) == 0)
                return global->configdir[0] ? global->configdir : NULL;
        }
    }

    // Return the native config path if the platform doesn't support the
    // type we are trying to fetch.
    const char *fallback_type = NULL;
    if (!strcmp(type, "cache") || !strcmp(type, "state"))
        fallback_type = "home";

    for (int n = 0; n < MP_ARRAY_SIZE(path_resolvers); n++) {
        const char *path = path_resolvers[n](talloc_ctx, type);
        if (path && path[0])
            return path;
    }

    if (fallback_type) {
        mp_assert(strcmp(fallback_type, type) != 0);
        return mp_get_platform_path(talloc_ctx, global, fallback_type);
    }
    return NULL;
}

void mp_init_paths(struct mpv_global *global, struct MPOpts *opts)
{
    TA_FREEP(&global->configdir);

    const char *force_configdir = getenv("MPV_HOME");
    if (opts->force_configdir && opts->force_configdir[0])
        force_configdir = opts->force_configdir;
    if (!opts->load_config)
        force_configdir = "";

    global->configdir = talloc_strdup(global, force_configdir);
}

char *mp_find_user_file(void *talloc_ctx, struct mpv_global *global,
                        const char *type, const char *filename)
{
    void *tmp = talloc_new(NULL);
    char *res = (char *)mp_get_platform_path(tmp, global, type);
    if (res)
        res = mp_path_join(talloc_ctx, res, filename);
    talloc_free(tmp);
    MP_DBG(global, "%s path: '%s' -> '%s'\n", type, filename, res ? res : "-");
    return res;
}

static char **mp_find_all_config_files_limited(void *talloc_ctx,
                                               struct mpv_global *global,
                                               int max_files,
                                               const char *filename)
{
    char **ret = talloc_array(talloc_ctx, char*, 2); // 2 preallocated
    int num_ret = 0;

    for (int i = 0; i < MP_ARRAY_SIZE(config_dirs); i++) {
        const char *dir = mp_get_platform_path(ret, global, config_dirs[i]);
        bstr s = bstr0(filename);
        while (dir && num_ret < max_files && s.len) {
            bstr fn;
            bstr_split_tok(s, "|", &fn, &s);

            char *file = mp_path_join_bstr(ret, bstr0(dir), fn);
            if (mp_path_exists(file)) {
                MP_DBG(global, "config path: '%.*s' -> '%s'\n",
                        BSTR_P(fn), file);
                MP_TARRAY_APPEND(NULL, ret, num_ret, file);
            } else {
                MP_DBG(global, "config path: '%.*s' -/-> '%s'\n",
                        BSTR_P(fn), file);
            }
        }
    }

    MP_TARRAY_GROW(NULL, ret, num_ret);
    ret[num_ret] = NULL;

    for (int n = 0; n < num_ret / 2; n++)
        MPSWAP(char*, ret[n], ret[num_ret - n - 1]);
    return ret;
}

char **mp_find_all_config_files(void *talloc_ctx, struct mpv_global *global,
                                const char *filename)
{
    return mp_find_all_config_files_limited(talloc_ctx, global, 64, filename);
}

char *mp_find_config_file(void *talloc_ctx, struct mpv_global *global,
                          const char *filename)
{
    char **l = mp_find_all_config_files_limited(talloc_ctx, global, 1, filename);
    char *r = l && l[0] ? talloc_steal(talloc_ctx, l[0]) : NULL;
    talloc_free(l);
    return r;
}

char *mp_get_user_path(void *talloc_ctx, struct mpv_global *global,
                       const char *path)
{
    if (!path)
        return NULL;
    char *res = NULL;
    bstr bpath = bstr0(path);
    if (bstr_eatstart0(&bpath, "~")) {
        // parse to "~" <prefix> "/" <rest>
        bstr prefix, rest;
        if (bstr_split_tok(bpath, "/", &prefix, &rest)) {
            const char *rest0 = rest.start; // ok in this case
            if (bstr_equals0(prefix, "~")) {
                res = mp_find_config_file(talloc_ctx, global, rest0);
                if (!res) {
                    void *tmp = talloc_new(NULL);
                    const char *p = mp_get_platform_path(tmp, global, "home");
                    res = mp_path_join_bstr(talloc_ctx, bstr0(p), rest);
                    talloc_free(tmp);
                }
            } else if (bstr_equals0(prefix, "")) {
                char *home = getenv("HOME");
                if (!home)
                    home = getenv("USERPROFILE");
                res = mp_path_join_bstr(talloc_ctx, bstr0(home), rest);
            } else if (bstr_eatstart0(&prefix, "~")) {
                void *tmp = talloc_new(NULL);
                char type[80];
                snprintf(type, sizeof(type), "%.*s", BSTR_P(prefix));
                const char *p = mp_get_platform_path(tmp, global, type);
                res = mp_path_join_bstr(talloc_ctx, bstr0(p), rest);
                talloc_free(tmp);
            }
        }
    }
    if (!res)
        res = talloc_strdup(talloc_ctx, path);
    MP_DBG(global, "user path: '%s' -> '%s'\n", path, res);
    return res;
}

char *mp_basename(const char *path)
{
    char *s;

#if HAVE_DOS_PATHS
    if (!mp_is_url(bstr0(path))) {
        s = strrchr(path, '\\');
        if (s)
            path = s + 1;
        s = strrchr(path, ':');
        if (s)
            path = s + 1;
    }
#endif
    s = strrchr(path, '/');
    return s ? s + 1 : (char *)path;
}

struct bstr mp_dirname(const char *path)
{
    struct bstr ret = {
        (uint8_t *)path, mp_basename(path) - path
    };
    if (ret.len == 0)
        return bstr0(".");
    return ret;
}


#if HAVE_DOS_PATHS
static const char mp_path_separators[] = "\\/";
#else
static const char mp_path_separators[] = "/";
#endif

// Mutates path and removes a trailing '/' (or '\' on Windows)
void mp_path_strip_trailing_separator(char *path)
{
    size_t len = strlen(path);
    if (len > 0 && strchr(mp_path_separators, path[len - 1]))
        path[len - 1] = '\0';
}

char *mp_splitext(const char *path, bstr *root)
{
    mp_assert(path);
    const char *split = strrchr(path, '.');
    if (!split || !split[1] || strchr(split, '/'))
        return NULL;
    if (root)
        *root = (bstr){(char *)path, split - path};
    return (char *)split + 1;
}

bool mp_path_is_absolute(struct bstr path)
{
    if (path.len && strchr(mp_path_separators, path.start[0]))
        return true;

#if HAVE_DOS_PATHS
    // Note: "X:filename" is a path relative to the current working directory
    //       of drive X, and thus is not an absolute path. It needs to be
    //       followed by \ or /.
    if (path.len >= 3 && path.start[1] == ':' &&
        strchr(mp_path_separators, path.start[2]))
        return true;
#endif

    return false;
}

char *mp_path_join_bstr(void *talloc_ctx, struct bstr p1, struct bstr p2)
{
    if (p1.len == 0)
        return bstrdup0(talloc_ctx, p2);
    if (p2.len == 0)
        return bstrdup0(talloc_ctx, p1);

    if (mp_path_is_absolute(p2))
        return bstrdup0(talloc_ctx, p2);

    bool have_separator = strchr(mp_path_separators, p1.start[p1.len - 1]);
#if HAVE_DOS_PATHS
    // "X:" only => path relative to "X:" current working directory.
    if (p1.len == 2 && p1.start[1] == ':')
        have_separator = true;
#endif

    return talloc_asprintf(talloc_ctx, "%.*s%s%.*s", BSTR_P(p1),
                           have_separator ? "" : "/", BSTR_P(p2));
}

char *mp_path_join(void *talloc_ctx, const char *p1, const char *p2)
{
    return mp_path_join_bstr(talloc_ctx, bstr0(p1), bstr0(p2));
}

char *mp_getcwd(void *talloc_ctx)
{
    char *e_wd = getenv("PWD");
    if (e_wd)
        return talloc_strdup(talloc_ctx, e_wd);

    char *wd = talloc_array(talloc_ctx, char, 20);
    while (getcwd(wd, talloc_get_size(wd)) == NULL) {
        if (errno != ERANGE) {
            talloc_free(wd);
            return NULL;
        }
        wd = talloc_realloc(talloc_ctx, wd, char, talloc_get_size(wd) * 2);
    }
    return wd;
}

char *mp_normalize_path(void *talloc_ctx, const char *path)
{
    if (mp_is_url(bstr0(path)))
        return talloc_strdup(talloc_ctx, path);

    return mp_path_join(talloc_ctx, mp_getcwd(talloc_ctx), path);
}

bool mp_path_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

bool mp_path_isdir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// Return false if it's considered a normal local filesystem path.
bool mp_is_url(bstr path)
{
    int proto = bstr_find0(path, "://");
    if (proto < 1)
        return false;
    // Per RFC3986, the first character of the protocol must be alphabetic.
    // The rest must be alphanumeric plus -, + and .
    for (int i = 0; i < proto; i++) {
        unsigned char c = path.start[i];
        if ((i == 0 && !mp_isalpha(c)) ||
            (!mp_isalnum(c) && c != '.' && c != '-' && c != '+'))
        {
            return false;
        }
    }
    return true;
}

// Return the protocol part of path, e.g. "http" if path is "http://...".
// On success, out_url (if not NULL) is set to the part after the "://".
bstr mp_split_proto(bstr path, bstr *out_url)
{
    if (!mp_is_url(path))
        return (bstr){0};
    bstr r;
    bstr_split_tok(path, "://", &r, out_url ? out_url : &(bstr){0});
    return r;
}

void mp_mkdirp(const char *dir)
{
    char *path = talloc_strdup(NULL, dir);
    char *cdir = path + 1;

    while (cdir) {
        cdir = strchr(cdir, '/');
        if (cdir)
            *cdir = 0;

        mkdir(path, 0700);

        if (cdir)
            *cdir++ = '/';
    }

    talloc_free(path);
}

void mp_mk_user_dir(struct mpv_global *global, const char *type, char *subdir)
{
    char *dir = mp_find_user_file(NULL, global, type, subdir);
    if (dir)
        mp_mkdirp(dir);
    talloc_free(dir);
}
