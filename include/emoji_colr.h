#ifndef EMOJI_COLR_H
#define EMOJI_COLR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize COLRv1 + CPAL + FreeType color emoji renderer using
 * NotoColorEmoji-Regular.ttf.
 * Returns 0 on success, negative on failure.
 */
int emoji_colr_init(void);

/**
 * Release cached emoji GPU textures, COLR/CPAL tables, and FreeType resources.
 */
void emoji_colr_term(void);

/**
 * Check whether the given Unicode codepoint has a COLRv1 color glyph
 * in NotoColorEmoji-Regular.ttf.
 */
int emoji_colr_has_codepoint(uint32_t cp);

/**
 * Return horizontal advance width in pixels for a given font size.
 */
float emoji_colr_advance(unsigned int size);

/**
 * Render (from LRU texture cache or on-demand COLRv1 rasterization) a color
 * emoji aligned to the text baseline (x, y) at the requested font size.
 * Respects the caller's alpha channel in `color`.
 */
void emoji_colr_draw(uint32_t cp, float x, float y, unsigned int size, unsigned int color);

#ifdef __cplusplus
}
#endif

#endif /* EMOJI_COLR_H */
