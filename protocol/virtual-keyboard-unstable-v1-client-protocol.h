/* Generated-compatible declarations for virtual-keyboard-unstable-v1. */
#ifndef VIRTUAL_KEYBOARD_UNSTABLE_V1_CLIENT_PROTOCOL_H
#define VIRTUAL_KEYBOARD_UNSTABLE_V1_CLIENT_PROTOCOL_H

#include <stdint.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zwp_virtual_keyboard_v1;
struct zwp_virtual_keyboard_manager_v1;

extern const struct wl_interface zwp_virtual_keyboard_v1_interface;
extern const struct wl_interface zwp_virtual_keyboard_manager_v1_interface;

enum zwp_virtual_keyboard_v1_keymap_format {
	ZWP_VIRTUAL_KEYBOARD_V1_KEYMAP_FORMAT_NO_KEYMAP = 0,
	ZWP_VIRTUAL_KEYBOARD_V1_KEYMAP_FORMAT_XKB_V1 = 1
};

enum zwp_virtual_keyboard_v1_key_state {
	ZWP_VIRTUAL_KEYBOARD_V1_KEY_STATE_RELEASED = 0,
	ZWP_VIRTUAL_KEYBOARD_V1_KEY_STATE_PRESSED = 1
};

static inline void
zwp_virtual_keyboard_v1_keymap(struct zwp_virtual_keyboard_v1 *obj,
	uint32_t format, int32_t fd, uint32_t size)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 0, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, format, fd, size);
}

static inline void
zwp_virtual_keyboard_v1_key(struct zwp_virtual_keyboard_v1 *obj,
	uint32_t time, uint32_t key, uint32_t state)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 1, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, time, key, state);
}

static inline void
zwp_virtual_keyboard_v1_modifiers(struct zwp_virtual_keyboard_v1 *obj,
	uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 2, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0,
		depressed, latched, locked, group);
}

static inline void
zwp_virtual_keyboard_v1_destroy(struct zwp_virtual_keyboard_v1 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 3, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

static inline struct zwp_virtual_keyboard_v1 *
zwp_virtual_keyboard_manager_v1_create_virtual_keyboard(
	struct zwp_virtual_keyboard_manager_v1 *obj, struct wl_seat *seat)
{
	return (struct zwp_virtual_keyboard_v1 *)wl_proxy_marshal_flags(
		(struct wl_proxy *)obj, 0, &zwp_virtual_keyboard_v1_interface,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, seat, NULL);
}

static inline void
zwp_virtual_keyboard_manager_v1_destroy(
	struct zwp_virtual_keyboard_manager_v1 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 1, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

#ifdef __cplusplus
}
#endif
#endif
