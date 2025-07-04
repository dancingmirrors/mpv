/*
 * Cocoa Application Event Handling
 *
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

// Carbon header is included but Carbon is NOT linked to mpv's binary. This
// file only needs this include to use the keycode definitions in keymap.
#import <Carbon/Carbon.h>

// Media keys definitions
#import <IOKit/hidsystem/ev_keymap.h>
#import <Cocoa/Cocoa.h>

#include "libmpv/mpv_talloc.h"
#include "input/event.h"
#include "input/input.h"
#include "player/client.h"
#include "input/keycodes.h"
// doesn't make much sense, but needed to access keymap functionality
#include "video/out/vo.h"

#include "osdep/macosx_compat.h"
#import "osdep/macosx_events_objc.h"
#import "osdep/macosx_application_objc.h"

#include "config.h"

#if HAVE_MACOS_COCOA_CB
#include "osdep/macOS_swift.h"
#endif

@interface EventsResponder ()
{
    struct input_ctx *_inputContext;
    struct mpv_handle *_ctx;
    BOOL _is_application;
    NSCondition *_input_lock;
}

- (NSEvent *)handleKey:(NSEvent *)event;
- (BOOL)setMpvHandle:(struct mpv_handle *)ctx;
- (void)readEvents;
- (void)startMediaKeys;
- (void)stopMediaKeys;
- (int)mapKeyModifiers:(int)cocoaModifiers;
- (int)keyModifierMask:(NSEvent *)event;
@end


#define NSLeftAlternateKeyMask  (0x000020 | NSEventModifierFlagOption)
#define NSRightAlternateKeyMask (0x000040 | NSEventModifierFlagOption)

static bool LeftAltPressed(int mask)
{
    return (mask & NSLeftAlternateKeyMask) == NSLeftAlternateKeyMask;
}

static bool RightAltPressed(int mask)
{
    return (mask & NSRightAlternateKeyMask) == NSRightAlternateKeyMask;
}

static const struct mp_keymap keymap[] = {
    // special keys
    {kVK_Return, MP_KEY_ENTER}, {kVK_Escape, MP_KEY_ESC},
    {kVK_Delete, MP_KEY_BACKSPACE}, {kVK_Option, MP_KEY_BACKSPACE},
    {kVK_Control, MP_KEY_BACKSPACE}, {kVK_Shift, MP_KEY_BACKSPACE},
    {kVK_Tab, MP_KEY_TAB},

    // cursor keys
    {kVK_UpArrow, MP_KEY_UP}, {kVK_DownArrow, MP_KEY_DOWN},
    {kVK_LeftArrow, MP_KEY_LEFT}, {kVK_RightArrow, MP_KEY_RIGHT},

    // navigation block
    {kVK_Help, MP_KEY_INSERT}, {kVK_ForwardDelete, MP_KEY_DELETE},
    {kVK_Home, MP_KEY_HOME}, {kVK_End, MP_KEY_END},
    {kVK_PageUp, MP_KEY_PAGE_UP}, {kVK_PageDown, MP_KEY_PAGE_DOWN},

    // F-keys
    {kVK_F1, MP_KEY_F + 1}, {kVK_F2, MP_KEY_F + 2}, {kVK_F3, MP_KEY_F + 3},
    {kVK_F4, MP_KEY_F + 4}, {kVK_F5, MP_KEY_F + 5}, {kVK_F6, MP_KEY_F + 6},
    {kVK_F7, MP_KEY_F + 7}, {kVK_F8, MP_KEY_F + 8}, {kVK_F9, MP_KEY_F + 9},
    {kVK_F10, MP_KEY_F + 10}, {kVK_F11, MP_KEY_F + 11}, {kVK_F12, MP_KEY_F + 12},
    {kVK_F13, MP_KEY_F + 13}, {kVK_F14, MP_KEY_F + 14}, {kVK_F15, MP_KEY_F + 15},
    {kVK_F16, MP_KEY_F + 16}, {kVK_F17, MP_KEY_F + 17}, {kVK_F18, MP_KEY_F + 18},
    {kVK_F19, MP_KEY_F + 19}, {kVK_F20, MP_KEY_F + 20},

    // numpad
    {kVK_ANSI_KeypadPlus, '+'}, {kVK_ANSI_KeypadMinus, '-'},
    {kVK_ANSI_KeypadMultiply, '*'}, {kVK_ANSI_KeypadDivide, '/'},
    {kVK_ANSI_KeypadEnter, MP_KEY_KPENTER},
    {kVK_ANSI_KeypadDecimal, MP_KEY_KPDEC},
    {kVK_ANSI_Keypad0, MP_KEY_KP0}, {kVK_ANSI_Keypad1, MP_KEY_KP1},
    {kVK_ANSI_Keypad2, MP_KEY_KP2}, {kVK_ANSI_Keypad3, MP_KEY_KP3},
    {kVK_ANSI_Keypad4, MP_KEY_KP4}, {kVK_ANSI_Keypad5, MP_KEY_KP5},
    {kVK_ANSI_Keypad6, MP_KEY_KP6}, {kVK_ANSI_Keypad7, MP_KEY_KP7},
    {kVK_ANSI_Keypad8, MP_KEY_KP8}, {kVK_ANSI_Keypad9, MP_KEY_KP9},

    {0, 0}
};

static int convert_key(unsigned key, unsigned charcode)
{
    int mpkey = lookup_keymap_table(keymap, key);
    if (mpkey)
        return mpkey;
    return charcode;
}

void cocoa_init_media_keys(void)
{
    [[EventsResponder sharedInstance] startMediaKeys];
}

void cocoa_uninit_media_keys(void)
{
    [[EventsResponder sharedInstance] stopMediaKeys];
}

void cocoa_put_key(int keycode)
{
    [[EventsResponder sharedInstance] putKey:keycode];
}

void cocoa_put_key_with_modifiers(int keycode, int modifiers)
{
    keycode |= [[EventsResponder sharedInstance] mapKeyModifiers:modifiers];
    cocoa_put_key(keycode);
}

void cocoa_set_input_context(struct input_ctx *input_context)
{
    [[EventsResponder sharedInstance] setInputContext:input_context];
}

static void wakeup(void *context)
{
    [[EventsResponder sharedInstance] readEvents];
}

void cocoa_set_mpv_handle(struct mpv_handle *ctx)
{
    if ([[EventsResponder sharedInstance] setMpvHandle:ctx]) {
        mpv_observe_property(ctx, 0, "duration", MPV_FORMAT_DOUBLE);
        mpv_observe_property(ctx, 0, "time-pos", MPV_FORMAT_DOUBLE);
        mpv_observe_property(ctx, 0, "pause", MPV_FORMAT_FLAG);
        mpv_set_wakeup_callback(ctx, wakeup, NULL);
    }
}

@implementation EventsResponder

@synthesize remoteCommandCenter = _remoteCommandCenter;

+ (EventsResponder *)sharedInstance
{
    static EventsResponder *responder = nil;
    static dispatch_once_t onceToken;
    dispatch_once(&onceToken, ^{
        responder = [EventsResponder new];
    });
    return responder;
}

- (id)init
{
    self = [super init];
    if (self) {
        _input_lock = [NSCondition new];
    }
    return self;
}

- (void)waitForInputContext
{
    [_input_lock lock];
    while (!_inputContext)
        [_input_lock wait];
    [_input_lock unlock];
}

- (void)setInputContext:(struct input_ctx *)ctx
{
    [_input_lock lock];
    _inputContext = ctx;
    [_input_lock signal];
    [_input_lock unlock];
}

- (void)wakeup
{
    [_input_lock lock];
    if (_inputContext)
        mp_input_wakeup(_inputContext);
    [_input_lock unlock];
}

- (bool)queueCommand:(char *)cmd
{
    bool r = false;
    [_input_lock lock];
    if (_inputContext) {
        mp_cmd_t *cmdt = mp_input_parse_cmd(_inputContext, bstr0(cmd), "");
        mp_input_queue_cmd(_inputContext, cmdt);
        r = true;
    }
    [_input_lock unlock];
    return r;
}

- (void)putKey:(int)keycode
{
    [_input_lock lock];
    if (_inputContext)
        mp_input_put_key(_inputContext, keycode);
    [_input_lock unlock];
}

- (BOOL)useAltGr
{
    BOOL r = YES;
    [_input_lock lock];
    if (_inputContext)
        r = mp_input_use_alt_gr(_inputContext);
    [_input_lock unlock];
    return r;
}

- (void)setIsApplication:(BOOL)isApplication
{
    _is_application = isApplication;
}

- (BOOL)setMpvHandle:(struct mpv_handle *)ctx
{
    if (_is_application) {
        dispatch_sync(dispatch_get_main_queue(), ^{
            _ctx = ctx;
            [NSApp setMpvHandle:ctx];
        });
        return YES;
    } else {
        mpv_destroy(ctx);
        return NO;
    }
}

- (void)readEvents
{
    dispatch_async(dispatch_get_main_queue(), ^{
        while (_ctx) {
            mpv_event *event = mpv_wait_event(_ctx, 0);
            if (event->event_id == MPV_EVENT_NONE)
                break;
            [self processEvent:event];
        }
    });
}

-(void)processEvent:(struct mpv_event *)event
{
    if(_is_application) {
        [NSApp processEvent:event];
    }

    if (_remoteCommandCenter) {
        [_remoteCommandCenter processEvent:event];
    }

    switch (event->event_id) {
    case MPV_EVENT_SHUTDOWN: {
#if HAVE_MACOS_COCOA_CB
        if ([(Application *)NSApp cocoaCB].isShuttingDown) {
            _ctx = nil;
            return;
        }
#endif
        mpv_destroy(_ctx);
        _ctx = nil;
        break;
    }
    }
}

- (void)startMediaKeys
{
#if HAVE_MACOS_MEDIA_PLAYER
    // 10.12.2 runtime availability check
    if (_remoteCommandCenter == nil && [NSApp respondsToSelector:@selector(touchBar)]) {
        _remoteCommandCenter = [[RemoteCommandCenter alloc] init];
    }
#endif

    [_remoteCommandCenter start];
}

- (void)stopMediaKeys
{
    [_remoteCommandCenter stop];
}

- (int)mapKeyModifiers:(int)cocoaModifiers
{
    int mask = 0;
    if (cocoaModifiers & NSEventModifierFlagShift)
        mask |= MP_KEY_MODIFIER_SHIFT;
    if (cocoaModifiers & NSEventModifierFlagControl)
        mask |= MP_KEY_MODIFIER_CTRL;
    if (LeftAltPressed(cocoaModifiers) ||
        (RightAltPressed(cocoaModifiers) && ![self useAltGr]))
        mask |= MP_KEY_MODIFIER_ALT;
    if (cocoaModifiers & NSEventModifierFlagCommand)
        mask |= MP_KEY_MODIFIER_META;
    return mask;
}

- (int)mapTypeModifiers:(NSEventType)type
{
    NSDictionary *map = @{
        @(NSEventTypeKeyDown) : @(MP_KEY_STATE_DOWN),
        @(NSEventTypeKeyUp)   : @(MP_KEY_STATE_UP),
    };
    return [map[@(type)] intValue];
}

- (int)keyModifierMask:(NSEvent *)event
{
    return [self mapKeyModifiers:[event modifierFlags]] |
        [self mapTypeModifiers:[event type]];
}

-(BOOL)handleMPKey:(int)key withMask:(int)mask
{
    if (key > 0) {
        cocoa_put_key(key | mask);
        if (mask & MP_KEY_STATE_UP)
            cocoa_put_key(MP_INPUT_RELEASE_ALL);
        return YES;
    } else {
        return NO;
    }
}

- (NSEvent*)handleKey:(NSEvent *)event
{
    if ([event isARepeat]) return nil;

    NSString *chars;

    if ([self useAltGr] && RightAltPressed([event modifierFlags])) {
        chars = [event characters];
    } else {
        chars = [event charactersIgnoringModifiers];
    }

    struct bstr t = bstr0([chars UTF8String]);
    int key = convert_key([event keyCode], bstr_decode_utf8(t, &t));

    if (key > -1)
        [self handleMPKey:key withMask:[self keyModifierMask:event]];

    return nil;
}

- (bool)processKeyEvent:(NSEvent *)event
{
    if (event.type == NSEventTypeKeyDown || event.type == NSEventTypeKeyUp){
        if (![[NSApp mainMenu] performKeyEquivalent:event])
            [self handleKey:event];
        return true;
    }
    return false;
}

- (void)handleFilesArray:(NSArray *)files
{
    enum mp_dnd_action action = [NSEvent modifierFlags] &
                                NSEventModifierFlagShift ? DND_APPEND : DND_REPLACE;

    size_t num_files  = [files count];
    char **files_utf8 = talloc_array(NULL, char*, num_files);
    [files enumerateObjectsUsingBlock:^(NSString *p, NSUInteger i, BOOL *_){
        if ([p hasPrefix:@"file:///.file/id="])
            p = [[NSURL URLWithString:p] path];
        char *filename = (char *)[p UTF8String];
        size_t bytes   = [p lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        files_utf8[i]  = talloc_memdup(files_utf8, filename, bytes + 1);
    }];
    [_input_lock lock];
    if (_inputContext)
        mp_event_drop_files(_inputContext, num_files, files_utf8, action);
    [_input_lock unlock];
    talloc_free(files_utf8);
}

@end
