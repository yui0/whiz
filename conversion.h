/*
 * Shared segmented-conversion state for the GTK and Wayland frontends.
 * Include this after engine/whiz.c so the public whiz_* API is visible.
 */
#ifndef WHIZ_CONVERSION_H
#define WHIZ_CONVERSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#define WHIZ_CONV_SEGMENTS 64
#define WHIZ_CONV_CANDIDATES 20
#define WHIZ_CONV_WORD 256
#define WHIZ_CONV_READING 256

typedef struct {
	char reading[WHIZ_CONV_READING];
	char candidates[WHIZ_CONV_CANDIDATES][WHIZ_CONV_WORD];
	int candidate_count;
	int selected;
} WhizConversionSegment;

typedef struct {
	WhizConversionSegment segments[WHIZ_CONV_SEGMENTS];
	int segment_count;
	int active;
} WhizConversion;

static inline void whiz_conv_clear(WhizConversion *conversion)
{
	memset(conversion, 0, sizeof(*conversion));
	conversion->active = -1;
}

static inline void whiz_conv_copy(char *dst, size_t cap, const char *src)
{
	if (!cap) return;
	if (!src) src = "";
	size_t n = strlen(src);
	if (n >= cap) n = cap - 1;
	memmove(dst, src, n);
	dst[n] = '\0';
}

static inline bool whiz_conv_has_candidate(
	const WhizConversionSegment *segment, const char *word)
{
	for (int i = 0; i < segment->candidate_count; i++)
		if (!strcmp(segment->candidates[i], word)) return true;
	return false;
}

static inline void whiz_conv_load_candidates(
	WhizConversionSegment *segment, const char *preferred)
{
	segment->candidate_count = 0;
	segment->selected = 0;
	if (preferred && *preferred) {
		whiz_conv_copy(segment->candidates[segment->candidate_count++],
		               WHIZ_CONV_WORD, preferred);
	}

	const char *raw_api = whiz_segment_candidates(
		segment->reading, WHIZ_CONV_CANDIDATES);
	char raw[4096];
	whiz_conv_copy(raw, sizeof(raw), raw_api);
	char *line = raw;
	while (*line && segment->candidate_count < WHIZ_CONV_CANDIDATES) {
		char *newline = strchr(line, '\n');
		if (newline) *newline = '\0';
		if (*line && !whiz_conv_has_candidate(segment, line))
			whiz_conv_copy(segment->candidates[segment->candidate_count++],
			               WHIZ_CONV_WORD, line);
		if (!newline) break;
		line = newline + 1;
	}
	if (!segment->candidate_count)
		whiz_conv_copy(segment->candidates[segment->candidate_count++],
		               WHIZ_CONV_WORD, segment->reading);
}

static inline bool whiz_conv_begin(WhizConversion *conversion, const char *reading)
{
	whiz_conv_clear(conversion);
	const char *raw_api = whiz_convert_segments(reading);
	if (!raw_api || !*raw_api) return false;
	char raw[4096];
	whiz_conv_copy(raw, sizeof(raw), raw_api);

	char *line = raw;
	while (*line && conversion->segment_count < WHIZ_CONV_SEGMENTS) {
		char *newline = strchr(line, '\n');
		if (newline) *newline = '\0';
		char *tab = strchr(line, '\t');
		if (tab && tab != line && tab[1]) {
			*tab = '\0';
			WhizConversionSegment *segment =
				&conversion->segments[conversion->segment_count++];
			whiz_conv_copy(segment->reading, sizeof(segment->reading), tab + 1);
			whiz_conv_load_candidates(segment, line);
		}
		if (!newline) break;
		line = newline + 1;
	}
	conversion->active = conversion->segment_count ? 0 : -1;
	return conversion->segment_count > 0;
}

static inline const char *whiz_conv_selected(const WhizConversionSegment *segment)
{
	return segment->candidate_count
		? segment->candidates[segment->selected] : segment->reading;
}

static inline void whiz_conv_compose(
	const WhizConversion *conversion, char *out, size_t cap)
{
	size_t used = 0;
	if (!cap) return;
	out[0] = '\0';
	for (int i = 0; i < conversion->segment_count; i++) {
		const char *word = whiz_conv_selected(&conversion->segments[i]);
		size_t n = strlen(word);
		if (n >= cap - used) n = cap - used - 1;
		memcpy(out + used, word, n);
		used += n;
		out[used] = '\0';
		if (used + 1 >= cap) break;
	}
}

static inline void whiz_conv_select_segment(WhizConversion *conversion, int delta)
{
	if (!conversion->segment_count) return;
	conversion->active =
		(conversion->active + delta + conversion->segment_count)
		% conversion->segment_count;
}

static inline void whiz_conv_select_candidate(WhizConversion *conversion, int delta)
{
	if (conversion->active < 0) return;
	WhizConversionSegment *segment = &conversion->segments[conversion->active];
	if (!segment->candidate_count) return;
	segment->selected =
		(segment->selected + delta + segment->candidate_count)
		% segment->candidate_count;
}

static inline size_t whiz_conv_first_char_bytes(const char *text)
{
	unsigned char c = (unsigned char)text[0];
	if (c < 0x80) return c ? 1 : 0;
	if ((c & 0xe0) == 0xc0) return 2;
	if ((c & 0xf0) == 0xe0) return 3;
	if ((c & 0xf8) == 0xf0) return 4;
	return 1;
}

static inline size_t whiz_conv_last_char_offset(const char *text)
{
	size_t n = strlen(text);
	if (!n) return 0;
	size_t p = n - 1;
	while (p && ((unsigned char)text[p] & 0xc0) == 0x80) p--;
	return p;
}

/*
 * Resize the boundary after the active segment. Negative shortens it,
 * positive takes one character from the following segment.
 */
static inline bool whiz_conv_resize(WhizConversion *conversion, int delta)
{
	int i = conversion->active;
	if (i < 0 || i + 1 >= conversion->segment_count || !delta) return false;
	WhizConversionSegment *left = &conversion->segments[i];
	WhizConversionSegment *right = &conversion->segments[i + 1];
	char moved[8] = "";
	if (delta < 0) {
		size_t p = whiz_conv_last_char_offset(left->reading);
		if (!p) return false; /* Keep at least one character in each segment. */
		whiz_conv_copy(moved, sizeof(moved), left->reading + p);
		left->reading[p] = '\0';
		char joined[WHIZ_CONV_READING];
		snprintf(joined, sizeof(joined), "%s%s", moved, right->reading);
		whiz_conv_copy(right->reading, sizeof(right->reading), joined);
	} else {
		size_t n = whiz_conv_first_char_bytes(right->reading);
		if (!n || !right->reading[n]) return false;
		memcpy(moved, right->reading, n);
		moved[n] = '\0';
		if (strlen(left->reading) + n >= sizeof(left->reading)) return false;
		strcat(left->reading, moved);
		memmove(right->reading, right->reading + n,
		        strlen(right->reading + n) + 1);
	}
	const char *best = whiz_convert(left->reading);
	char left_best[WHIZ_CONV_WORD];
	whiz_conv_copy(left_best, sizeof(left_best), best);
	best = whiz_convert(right->reading);
	char right_best[WHIZ_CONV_WORD];
	whiz_conv_copy(right_best, sizeof(right_best), best);
	whiz_conv_load_candidates(left, left_best);
	whiz_conv_load_candidates(right, right_best);
	return true;
}

#endif
