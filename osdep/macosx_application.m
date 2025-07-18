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

#include <stdio.h>
#include <pthread.h>
#include "config.h"
#include "libmpv/mpv_talloc.h"

#include "common/msg.h"
#include "input/input.h"
#include "player/client.h"
#include "options/m_config.h"
#include "options/options.h"

#import "osdep/macosx_application_objc.h"
#include "osdep/macosx_compat.h"
#import "osdep/macosx_events_objc.h"
#include "osdep/threads.h"
#include "osdep/main-fn.h"

#if HAVE_MACOS_TOUCHBAR
#import "osdep/macosx_touchbar.h"
#endif
#if HAVE_MACOS_COCOA_CB
#include "osdep/macOS_swift.h"
#endif

#define MPV_PROTOCOL @"mpv://"

#define OPT_BASE_STRUCT struct macos_opts
const struct m_sub_options macos_conf = {
    .opts = (const struct m_option[]) {
        {"macos-title-bar-appearance", OPT_CHOICE(macos_title_bar_appearance,
            {"auto", 0}, {"aqua", 1}, {"darkAqua", 2},
            {"vibrantLight", 3}, {"vibrantDark", 4},
            {"aquaHighContrast", 5}, {"darkAquaHighContrast", 6},
            {"vibrantLightHighContrast", 7},
            {"vibrantDarkHighContrast", 8})},
        {"macos-title-bar-material", OPT_CHOICE(macos_title_bar_material,
            {"titlebar", 0}, {"selection", 1}, {"menu", 2},
            {"popover", 3}, {"sidebar", 4}, {"headerView", 5},
            {"sheet", 6}, {"windowBackground", 7}, {"hudWindow", 8},
            {"fullScreen", 9}, {"toolTip", 10}, {"contentBackground", 11},
            {"underWindowBackground", 12}, {"underPageBackground", 13},
            {"dark", 14}, {"light", 15}, {"mediumLight", 16},
            {"ultraDark", 17})},
        {"macos-title-bar-color", OPT_COLOR(macos_title_bar_color)},
        {"macos-fs-animation-duration",
            OPT_CHOICE(macos_fs_animation_duration, {"default", -1}),
            M_RANGE(0, 1000)},
        {"macos-force-dedicated-gpu", OPT_BOOL(macos_force_dedicated_gpu)},
        {"macos-app-activation-policy", OPT_CHOICE(macos_app_activation_policy,
            {"regular", 0}, {"accessory", 1}, {"prohibited", 2})},
        {"macos-geometry-calculation", OPT_CHOICE(macos_geometry_calculation,
            {"visible", FRAME_VISIBLE}, {"whole", FRAME_WHOLE})},
        {"cocoa-cb-sw-renderer", OPT_CHOICE(cocoa_cb_sw_renderer,
            {"auto", -1}, {"no", 0}, {"yes", 1})},
        {"cocoa-cb-10bit-context", OPT_BOOL(cocoa_cb_10bit_context)},
        {0}
    },
    .size = sizeof(struct macos_opts),
    .defaults = &(const struct macos_opts){
        .macos_title_bar_color = {0, 0, 0, 0},
        .macos_fs_animation_duration = -1,
        .cocoa_cb_sw_renderer = -1,
        .cocoa_cb_10bit_context = true
    },
};

// Whether the NSApplication singleton was created. If this is false, we are
// running in libmpv mode, and cocoa_main() was never called.
static bool application_instantiated;

static pthread_t playback_thread_id;

@interface Application ()
{
    EventsResponder *_eventsResponder;
}

@end

static Application *mpv_shared_app(void)
{
    return (Application *)[Application sharedApplication];
}

static void terminate_cocoa_application(void)
{
    dispatch_async(dispatch_get_main_queue(), ^{
        [NSApp hide:NSApp];
        [NSApp terminate:NSApp];
    });
}

@implementation Application
@synthesize menuBar = _menu_bar;
@synthesize openCount = _open_count;
@synthesize cocoaCB = _cocoa_cb;

- (void)sendEvent:(NSEvent *)event
{
    if ([self modalWindow] || ![_eventsResponder processKeyEvent:event])
        [super sendEvent:event];
    [_eventsResponder wakeup];
}

- (id)init
{
    if (self = [super init]) {
        _eventsResponder = [EventsResponder sharedInstance];

        NSAppleEventManager *em = [NSAppleEventManager sharedAppleEventManager];
        [em setEventHandler:self
                andSelector:@selector(getUrl:withReplyEvent:)
              forEventClass:kInternetEventClass
                 andEventID:kAEGetURL];
    }

    return self;
}

- (void)dealloc
{
    NSAppleEventManager *em = [NSAppleEventManager sharedAppleEventManager];
    [em removeEventHandlerForEventClass:kInternetEventClass
                             andEventID:kAEGetURL];
    [em removeEventHandlerForEventClass:kCoreEventClass
                             andEventID:kAEQuitApplication];
    [super dealloc];
}

static const char macosx_icon[] =
#include "generated/TOOLS/osxbundle/mpv.app/Contents/Resources/icon.icns.inc"
;

- (NSImage *)getMPVIcon
{
    // The C string contains a trailing null, so we strip it away
    NSData *icon_data = [NSData dataWithBytesNoCopy:(void *)macosx_icon
                                             length:sizeof(macosx_icon) - 1
                                       freeWhenDone:NO];
    return [[NSImage alloc] initWithData:icon_data];
}

#if HAVE_MACOS_TOUCHBAR
- (NSTouchBar *)makeTouchBar
{
    TouchBar *tBar = [[TouchBar alloc] init];
    [tBar setApp:self];
    tBar.delegate = tBar;
    tBar.customizationIdentifier = customID;
    tBar.defaultItemIdentifiers = @[play, previousItem, nextItem, seekBar];
    tBar.customizationAllowedItemIdentifiers = @[play, seekBar, previousItem,
        nextItem, previousChapter, nextChapter, cycleAudio, cycleSubtitle,
        currentPosition, timeLeft];
    return tBar;
}
#endif

- (void)processEvent:(struct mpv_event *)event
{
#if HAVE_MACOS_TOUCHBAR
    if ([self respondsToSelector:@selector(touchBar)])
        [(TouchBar *)self.touchBar processEvent:event];
#endif
    if (_cocoa_cb) {
        [_cocoa_cb processEvent:event];
    }
}

- (void)setMpvHandle:(struct mpv_handle *)ctx
{
#if HAVE_MACOS_COCOA_CB
    [NSApp setCocoaCB:[[CocoaCB alloc] init:ctx]];
#endif
}

+ (const struct m_sub_options *)getMacOSConf
{
    return &macos_conf;
}

+ (const struct m_sub_options *)getVoSubConf
{
    return &vo_sub_opts;
}

- (void)queueCommand:(char *)cmd
{
    [_eventsResponder queueCommand:cmd];
}

- (void)stopMPV:(char *)cmd
{
    if (![_eventsResponder queueCommand:cmd])
        terminate_cocoa_application();
}

- (void)applicationWillFinishLaunching:(NSNotification *)notification
{
    NSAppleEventManager *em = [NSAppleEventManager sharedAppleEventManager];
    [em setEventHandler:self
            andSelector:@selector(handleQuitEvent:withReplyEvent:)
          forEventClass:kCoreEventClass
             andEventID:kAEQuitApplication];
}

- (void)handleQuitEvent:(NSAppleEventDescriptor *)event
         withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    [self stopMPV:"quit"];
}

- (void)getUrl:(NSAppleEventDescriptor *)event
    withReplyEvent:(NSAppleEventDescriptor *)replyEvent
{
    NSString *url =
        [[event paramDescriptorForKeyword:keyDirectObject] stringValue];

    url = [url stringByReplacingOccurrencesOfString:MPV_PROTOCOL
                withString:@""
                   options:NSAnchoredSearch
                     range:NSMakeRange(0, [MPV_PROTOCOL length])];

    url = [url stringByRemovingPercentEncoding];
    [_eventsResponder handleFilesArray:@[url]];
}

- (void)application:(NSApplication *)sender openFiles:(NSArray *)filenames
{
    if (mpv_shared_app().openCount > 0) {
        mpv_shared_app().openCount--;
        return;
    }
    [self openFiles:filenames];
}

- (void)openFiles:(NSArray *)filenames
{
    SEL cmpsel = @selector(localizedStandardCompare:);
    NSArray *files = [filenames sortedArrayUsingSelector:cmpsel];
    [_eventsResponder handleFilesArray:files];
}
@end

struct playback_thread_ctx {
    int  *argc;
    char ***argv;
};

static void cocoa_run_runloop(void)
{
    NSAutoreleasePool *pool = [[NSAutoreleasePool alloc] init];
    [NSApp run];
    [pool drain];
}

static void *playback_thread(void *ctx_obj)
{
    mpthread_set_name("playback core (OSX)");
    @autoreleasepool {
        struct playback_thread_ctx *ctx = (struct playback_thread_ctx*) ctx_obj;
        int r = mpv_main(*ctx->argc, *ctx->argv);
        terminate_cocoa_application();
        // normally never reached - unless the cocoa mainloop hasn't started yet
        exit(r);
    }
}

void cocoa_register_menu_item_action(MPMenuKey key, void* action)
{
    if (application_instantiated)
        [[NSApp menuBar] registerSelector:(SEL)action forKey:key];
}

static void init_cocoa_application(bool regular)
{
    NSApp = mpv_shared_app();
    [NSApp setDelegate:NSApp];
    [NSApp setMenuBar:[[MenuBar alloc] init]];

    // Will be set to Regular from cocoa_common during UI creation so that we
    // don't create an icon when playing audio only files.
    [NSApp setActivationPolicy: regular ?
        NSApplicationActivationPolicyRegular :
        NSApplicationActivationPolicyAccessory];

    atexit_b(^{
        // Because activation policy has just been set to behave like a real
        // application, that policy must be reset on exit to prevent, among
        // other things, the menubar created here from remaining on screen.
        dispatch_async(dispatch_get_main_queue(), ^{
            [NSApp setActivationPolicy:NSApplicationActivationPolicyProhibited];
        });
    });
}

static bool bundle_started_from_finder(char **argv)
{
    NSString *binary_path = [NSString stringWithUTF8String:argv[0]];
    return [binary_path hasSuffix:@"mpv-bundle"];
}

static bool is_psn_argument(char *arg_to_check)
{
    NSString *arg = [NSString stringWithUTF8String:arg_to_check];
    return [arg hasPrefix:@"-psn_"];
}

static void setup_bundle(int *argc, char *argv[])
{
    if (*argc > 1 && is_psn_argument(argv[1])) {
        *argc = 1;
        argv[1] = NULL;
    }

    NSDictionary *env = [[NSProcessInfo processInfo] environment];
    NSString *path_bundle = [env objectForKey:@"PATH"];
    NSString *path_new = [NSString stringWithFormat:@"%@:%@:%@:%@:%@",
                                                    path_bundle,
                                                    @"/usr/local/bin",
                                                    @"/usr/local/sbin",
                                                    @"/opt/local/bin",
                                                    @"/opt/local/sbin"];
    setenv("PATH", [path_new UTF8String], 1);
    setenv("MPVBUNDLE", "true", 1);
}

int cocoa_main(int argc, char *argv[])
{
    @autoreleasepool {
        application_instantiated = true;
        [[EventsResponder sharedInstance] setIsApplication:YES];

        struct playback_thread_ctx ctx = {0};
        ctx.argc     = &argc;
        ctx.argv     = &argv;

        if (bundle_started_from_finder(argv)) {
            setup_bundle(&argc, argv);
            init_cocoa_application(true);
        } else {
            for (int i = 1; i < argc; i++)
                if (argv[i][0] != '-')
                    mpv_shared_app().openCount++;
            init_cocoa_application(false);
        }

        pthread_create(&playback_thread_id, NULL, playback_thread, &ctx);
        [[EventsResponder sharedInstance] waitForInputContext];
        cocoa_run_runloop();

        // This should never be reached: cocoa_run_runloop blocks until the
        // process is quit
        fprintf(stderr, "There was either a problem "
                "initializing Cocoa or the Runloop was stopped unexpectedly. "
                "Please report this issues to a developer.\n");
        pthread_join(playback_thread_id, NULL);
        return 1;
    }
}

