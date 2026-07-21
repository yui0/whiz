/* Generated-compatible protocol metadata for virtual-keyboard-unstable-v1. */
#include <stddef.h>
#include <wayland-util.h>

extern const struct wl_interface wl_seat_interface;
extern const struct wl_interface zwp_virtual_keyboard_v1_interface;

static const struct wl_interface *virtual_keyboard_types[] = {
	NULL, NULL, NULL, NULL, &wl_seat_interface,
	&zwp_virtual_keyboard_v1_interface,
};

static const struct wl_message virtual_keyboard_requests[] = {
	{ "keymap", "uhu", virtual_keyboard_types + 0 },
	{ "key", "uuu", virtual_keyboard_types + 0 },
	{ "modifiers", "uuuu", virtual_keyboard_types + 0 },
	{ "destroy", "", virtual_keyboard_types + 0 },
};

WL_EXPORT const struct wl_interface zwp_virtual_keyboard_v1_interface = {
	"zwp_virtual_keyboard_v1", 1,
	4, virtual_keyboard_requests,
	0, NULL,
};

static const struct wl_message virtual_keyboard_manager_requests[] = {
	{ "create_virtual_keyboard", "on", virtual_keyboard_types + 4 },
	{ "destroy", "", virtual_keyboard_types + 0 },
};

WL_EXPORT const struct wl_interface zwp_virtual_keyboard_manager_v1_interface = {
	"zwp_virtual_keyboard_manager_v1", 1,
	2, virtual_keyboard_manager_requests,
	0, NULL,
};
