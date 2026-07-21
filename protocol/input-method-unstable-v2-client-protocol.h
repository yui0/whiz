/* Generated-compatible client declarations for input-method-unstable-v2.
 * Kept in-tree so building Whiz does not require wayland-protocols. */
#ifndef INPUT_METHOD_UNSTABLE_V2_CLIENT_PROTOCOL_H
#define INPUT_METHOD_UNSTABLE_V2_CLIENT_PROTOCOL_H

#include <stdint.h>
#include <wayland-client.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zwp_input_method_v2;
struct zwp_input_popup_surface_v2;
struct zwp_input_method_keyboard_grab_v2;
struct zwp_input_method_manager_v2;

extern const struct wl_interface zwp_input_method_v2_interface;
extern const struct wl_interface zwp_input_popup_surface_v2_interface;
extern const struct wl_interface zwp_input_method_keyboard_grab_v2_interface;
extern const struct wl_interface zwp_input_method_manager_v2_interface;

enum zwp_input_method_keyboard_grab_v2_keymap_format {
	ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEYMAP_FORMAT_NO_KEYMAP = 0,
	ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEYMAP_FORMAT_XKB_V1 = 1
};

enum zwp_input_method_keyboard_grab_v2_key_state {
	ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEY_STATE_RELEASED = 0,
	ZWP_INPUT_METHOD_KEYBOARD_GRAB_V2_KEY_STATE_PRESSED = 1
};

struct zwp_input_method_v2_listener {
	void (*activate)(void *data, struct zwp_input_method_v2 *obj);
	void (*deactivate)(void *data, struct zwp_input_method_v2 *obj);
	void (*surrounding_text)(void *data, struct zwp_input_method_v2 *obj,
	                        const char *text, uint32_t cursor, uint32_t anchor);
	void (*text_change_cause)(void *data, struct zwp_input_method_v2 *obj,
	                          uint32_t cause);
	void (*content_type)(void *data, struct zwp_input_method_v2 *obj,
	                    uint32_t hint, uint32_t purpose);
	void (*done)(void *data, struct zwp_input_method_v2 *obj);
	void (*unavailable)(void *data, struct zwp_input_method_v2 *obj);
};

struct zwp_input_popup_surface_v2_listener {
	void (*text_input_rectangle)(void *data,
		struct zwp_input_popup_surface_v2 *obj,
		int32_t x, int32_t y, int32_t width, int32_t height);
};

struct zwp_input_method_keyboard_grab_v2_listener {
	void (*keymap)(void *data, struct zwp_input_method_keyboard_grab_v2 *obj,
	               uint32_t format, int32_t fd, uint32_t size);
	void (*key)(void *data, struct zwp_input_method_keyboard_grab_v2 *obj,
	            uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
	void (*modifiers)(void *data, struct zwp_input_method_keyboard_grab_v2 *obj,
	                  uint32_t serial, uint32_t depressed, uint32_t latched,
	                  uint32_t locked, uint32_t group);
	void (*repeat_info)(void *data, struct zwp_input_method_keyboard_grab_v2 *obj,
	                    int32_t rate, int32_t delay);
};

static inline int
zwp_input_method_v2_add_listener(struct zwp_input_method_v2 *obj,
	const struct zwp_input_method_v2_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *)obj,
		(void (**)(void))listener, data);
}

static inline int
zwp_input_popup_surface_v2_add_listener(struct zwp_input_popup_surface_v2 *obj,
	const struct zwp_input_popup_surface_v2_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *)obj,
		(void (**)(void))listener, data);
}

static inline int
zwp_input_method_keyboard_grab_v2_add_listener(
	struct zwp_input_method_keyboard_grab_v2 *obj,
	const struct zwp_input_method_keyboard_grab_v2_listener *listener, void *data)
{
	return wl_proxy_add_listener((struct wl_proxy *)obj,
		(void (**)(void))listener, data);
}

static inline void
zwp_input_method_v2_commit_string(struct zwp_input_method_v2 *obj,
	const char *text)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 0, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, text);
}

static inline void
zwp_input_method_v2_set_preedit_string(struct zwp_input_method_v2 *obj,
	const char *text, int32_t cursor_begin, int32_t cursor_end)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 1, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0,
		text, cursor_begin, cursor_end);
}

static inline void
zwp_input_method_v2_delete_surrounding_text(struct zwp_input_method_v2 *obj,
	uint32_t before_length, uint32_t after_length)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 2, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0,
		before_length, after_length);
}

static inline void
zwp_input_method_v2_commit(struct zwp_input_method_v2 *obj, uint32_t serial)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 3, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, serial);
}

static inline struct zwp_input_popup_surface_v2 *
zwp_input_method_v2_get_input_popup_surface(struct zwp_input_method_v2 *obj,
	struct wl_surface *surface)
{
	return (struct zwp_input_popup_surface_v2 *)wl_proxy_marshal_flags(
		(struct wl_proxy *)obj, 4, &zwp_input_popup_surface_v2_interface,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, NULL, surface);
}

static inline struct zwp_input_method_keyboard_grab_v2 *
zwp_input_method_v2_grab_keyboard(struct zwp_input_method_v2 *obj)
{
	return (struct zwp_input_method_keyboard_grab_v2 *)wl_proxy_marshal_flags(
		(struct wl_proxy *)obj, 5, &zwp_input_method_keyboard_grab_v2_interface,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, NULL);
}

static inline void
zwp_input_method_v2_destroy(struct zwp_input_method_v2 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 6, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

static inline void
zwp_input_popup_surface_v2_destroy(struct zwp_input_popup_surface_v2 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 0, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

static inline void
zwp_input_method_keyboard_grab_v2_release(
	struct zwp_input_method_keyboard_grab_v2 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 0, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

static inline struct zwp_input_method_v2 *
zwp_input_method_manager_v2_get_input_method(
	struct zwp_input_method_manager_v2 *obj, struct wl_seat *seat)
{
	return (struct zwp_input_method_v2 *)wl_proxy_marshal_flags(
		(struct wl_proxy *)obj, 0, &zwp_input_method_v2_interface,
		wl_proxy_get_version((struct wl_proxy *)obj), 0, seat, NULL);
}

static inline void
zwp_input_method_manager_v2_destroy(struct zwp_input_method_manager_v2 *obj)
{
	wl_proxy_marshal_flags((struct wl_proxy *)obj, 1, NULL,
		wl_proxy_get_version((struct wl_proxy *)obj),
		WL_MARSHAL_FLAG_DESTROY);
}

#ifdef __cplusplus
}
#endif
#endif
