#include "emoji_colr.h"
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#if defined(__has_include)
  #if __has_include(<ft2build.h>)
    #include <ft2build.h>
    #include FT_FREETYPE_H
    #include FT_OUTLINE_H
    #include FT_TRUETYPE_TABLES_H
    #define EMOJI_HAS_FT 1
  #elif __has_include(<freetype2/ft2build.h>)
    #include <freetype2/ft2build.h>
    #include FT_FREETYPE_H
    #include FT_OUTLINE_H
    #include FT_TRUETYPE_TABLES_H
    #define EMOJI_HAS_FT 1
  #endif
#endif

#define EMOJI_RASTER_PX  40
#define EMOJI_UPEM       1024.0f
#define EMOJI_ADV_UNITS  1275.0f
#define EMOJI_Y_MIN      (-250.0f)
#define EMOJI_Y_MAX      1025.0f
#define EMOJI_CACHE_SIZE 64

#ifdef EMOJI_HAS_FT

typedef struct {
    float xx, yx;
    float xy, yy;
    float dx, dy;
} ColrAffine;

typedef struct {
    float r, g, b, a; /* straight [0..1] */
} ColrRGBA;

typedef struct {
    uint32_t cp;
    uint32_t last_used;
    vita2d_texture *tex;
} EmojiCacheEntry;

static FT_Library s_ft_lib = NULL;
static FT_Face    s_ft_face = NULL;

static uint8_t   *s_colr = NULL;
static uint32_t   s_colr_len = 0;
static uint32_t   s_bglist_off = 0;
static uint32_t   s_num_bg = 0;
static uint32_t   s_llist_off = 0;
static uint32_t   s_num_layers = 0;

static uint8_t   *s_cpal = NULL;
static uint32_t   s_cpal_len = 0;
static uint32_t   s_cpal_colors_off = 0;
static uint16_t   s_cpal_num_colors = 0;

static int        s_ready = 0;
static uint32_t   s_use_tick = 1;
static EmojiCacheEntry s_cache[EMOJI_CACHE_SIZE];

static inline uint16_t rd_u16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static inline int16_t rd_s16(const uint8_t *p) {
    return (int16_t)rd_u16(p);
}

static inline uint32_t rd_u24(const uint8_t *p) {
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static inline uint32_t rd_u32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |  (uint32_t)p[3];
}

static inline int32_t rd_s32(const uint8_t *p) {
    return (int32_t)rd_u32(p);
}

static inline float f2dot14(int16_t v) {
    return (float)v / 16384.0f;
}

static inline float fixed16(int32_t v) {
    return (float)v / 65536.0f;
}

static ColrAffine affine_identity(void) {
    ColrAffine m = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    return m;
}

/* M_out = A * B (apply B first, then A) */
static ColrAffine affine_mul(const ColrAffine *a, const ColrAffine *b) {
    ColrAffine r;
    r.xx = a->xx * b->xx + a->xy * b->yx;
    r.yx = a->yx * b->xx + a->yy * b->yx;
    r.xy = a->xx * b->xy + a->xy * b->yy;
    r.yy = a->yx * b->xy + a->yy * b->yy;
    r.dx = a->xx * b->dx + a->xy * b->dy + a->dx;
    r.dy = a->yx * b->dx + a->yy * b->dy + a->dy;
    return r;
}

static int affine_invert(const ColrAffine *m, ColrAffine *out) {
    float det = m->xx * m->yy - m->xy * m->yx;
    if (fabsf(det) < 1e-8f) return 0;
    float inv_det = 1.0f / det;
    out->xx =  m->yy * inv_det;
    out->yx = -m->yx * inv_det;
    out->xy = -m->xy * inv_det;
    out->yy =  m->xx * inv_det;
    out->dx = -(out->xx * m->dx + out->xy * m->dy);
    out->dy = -(out->yx * m->dx + out->yy * m->dy);
    return 1;
}

static ColrRGBA cpal_get_color(uint16_t pal_idx, float alpha_scale) {
    ColrRGBA c = {1.0f, 1.0f, 1.0f, 1.0f};
    if (pal_idx == 0xFFFF || !s_cpal || pal_idx >= s_cpal_num_colors) {
        c.a = (pal_idx == 0xFFFF) ? alpha_scale : 0.0f;
        return c;
    }
    uint32_t off = s_cpal_colors_off + ((uint32_t)pal_idx * 4u);
    if (off + 4u > s_cpal_len) {
        c.a = 0.0f;
        return c;
    }
    /* CPAL records are BGRA */
    c.b = (float)s_cpal[off + 0] / 255.0f;
    c.g = (float)s_cpal[off + 1] / 255.0f;
    c.r = (float)s_cpal[off + 2] / 255.0f;
    c.a = ((float)s_cpal[off + 3] / 255.0f) * alpha_scale;
    if (c.a < 0.0f) c.a = 0.0f;
    if (c.a > 1.0f) c.a = 1.0f;
    return c;
}

static ColrRGBA eval_color_line(uint32_t cl_off, float t) {
    ColrRGBA out = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!s_colr || cl_off + 3u > s_colr_len) return out;

    uint8_t extend = s_colr[cl_off];
    uint16_t nstops = rd_u16(s_colr + cl_off + 1);
    if (nstops == 0 || cl_off + 3u + (uint32_t)nstops * 6u > s_colr_len) return out;

    const uint8_t *stops = s_colr + cl_off + 3;
    float min_o = f2dot14(rd_s16(stops));
    float max_o = f2dot14(rd_s16(stops + (nstops - 1) * 6));

    if (extend == 1 && max_o > min_o) { /* REPEAT */
        float span = max_o - min_o;
        t = min_o + fmodf(fmodf(t - min_o, span) + span, span);
    } else if (extend == 2 && max_o > min_o) { /* REFLECT */
        float span = max_o - min_o;
        float period = span * 2.0f;
        float m = fmodf(fmodf(t - min_o, period) + period, period);
        t = (m <= span) ? (min_o + m) : (min_o + period - m);
    }

    if (t <= min_o || nstops == 1) {
        uint16_t pal = rd_u16(stops + 2);
        float a = f2dot14(rd_s16(stops + 4));
        return cpal_get_color(pal, a);
    }
    if (t >= max_o) {
        const uint8_t *last = stops + (nstops - 1) * 6;
        uint16_t pal = rd_u16(last + 2);
        float a = f2dot14(rd_s16(last + 4));
        return cpal_get_color(pal, a);
    }

    for (uint16_t i = 0; i + 1 < nstops; i++) {
        const uint8_t *s0 = stops + i * 6;
        const uint8_t *s1 = stops + (i + 1) * 6;
        float o0 = f2dot14(rd_s16(s0));
        float o1 = f2dot14(rd_s16(s1));
        if (t <= o1) {
            ColrRGBA c0 = cpal_get_color(rd_u16(s0 + 2), f2dot14(rd_s16(s0 + 4)));
            ColrRGBA c1 = cpal_get_color(rd_u16(s1 + 2), f2dot14(rd_s16(s1 + 4)));
            float u = (o1 > o0 + 1e-6f) ? ((t - o0) / (o1 - o0)) : 1.0f;
            if (u < 0.0f) u = 0.0f;
            if (u > 1.0f) u = 1.0f;
            /* Interpolate in premultiplied alpha space, then return straight */
            float p0r = c0.r * c0.a, p0g = c0.g * c0.a, p0b = c0.b * c0.a;
            float p1r = c1.r * c1.a, p1g = c1.g * c1.a, p1b = c1.b * c1.a;
            float pr = p0r + (p1r - p0r) * u;
            float pg = p0g + (p1g - p0g) * u;
            float pb = p0b + (p1b - p0b) * u;
            float pa = c0.a + (c1.a - c0.a) * u;
            if (pa > 1e-6f) {
                out.r = pr / pa;
                out.g = pg / pa;
                out.b = pb / pa;
                out.a = pa;
            }
            return out;
        }
    }
    return out;
}

static uint32_t find_base_glyph_paint(uint16_t gid) {
    if (!s_colr || s_num_bg == 0) return 0;
    uint32_t lo = 0, hi = s_num_bg;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        uint32_t rec = s_bglist_off + 4u + mid * 6u;
        if (rec + 6u > s_colr_len) return 0;
        uint16_t mgid = rd_u16(s_colr + rec);
        if (mgid == gid) {
            return s_bglist_off + rd_u32(s_colr + rec + 2);
        } else if (mgid < gid) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return 0;
}

static int apply_transform_paint(uint32_t paint_off, const ColrAffine *cur,
                                 uint32_t *out_child, ColrAffine *out_m) {
    if (!s_colr || paint_off >= s_colr_len) return 0;
    uint8_t fmt = s_colr[paint_off];
    ColrAffine local = affine_identity();

    if (fmt == 12) { /* PaintTransform */
        if (paint_off + 7u > s_colr_len) return 0;
        *out_child = paint_off + rd_u24(s_colr + paint_off + 1);
        uint32_t toff = paint_off + rd_u24(s_colr + paint_off + 4);
        if (toff + 24u > s_colr_len) return 0;
        local.xx = fixed16(rd_s32(s_colr + toff + 0));
        local.yx = fixed16(rd_s32(s_colr + toff + 4));
        local.xy = fixed16(rd_s32(s_colr + toff + 8));
        local.yy = fixed16(rd_s32(s_colr + toff + 12));
        local.dx = fixed16(rd_s32(s_colr + toff + 16));
        local.dy = fixed16(rd_s32(s_colr + toff + 20));
    } else if (fmt == 14) { /* PaintTranslate */
        if (paint_off + 8u > s_colr_len) return 0;
        *out_child = paint_off + rd_u24(s_colr + paint_off + 1);
        local.dx = (float)rd_s16(s_colr + paint_off + 4);
        local.dy = (float)rd_s16(s_colr + paint_off + 6);
    } else if (fmt == 16) { /* PaintScale */
        if (paint_off + 8u > s_colr_len) return 0;
        *out_child = paint_off + rd_u24(s_colr + paint_off + 1);
        local.xx = f2dot14(rd_s16(s_colr + paint_off + 4));
        local.yy = f2dot14(rd_s16(s_colr + paint_off + 6));
    } else if (fmt == 18) { /* PaintScaleAroundCenter */
        if (paint_off + 12u > s_colr_len) return 0;
        *out_child = paint_off + rd_u24(s_colr + paint_off + 1);
        float sx = f2dot14(rd_s16(s_colr + paint_off + 4));
        float sy = f2dot14(rd_s16(s_colr + paint_off + 6));
        float cx = (float)rd_s16(s_colr + paint_off + 8);
        float cy = (float)rd_s16(s_colr + paint_off + 10);
        local.xx = sx;
        local.yy = sy;
        local.dx = cx - sx * cx;
        local.dy = cy - sy * cy;
    } else {
        return 0;
    }

    *out_m = affine_mul(cur, &local);
    return 1;
}

static ColrRGBA sample_fill_paint(uint32_t paint_off, const ColrAffine *m,
                                  float tx, float ty, int depth) {
    ColrRGBA zero = {0.0f, 0.0f, 0.0f, 0.0f};
    if (!s_colr || paint_off >= s_colr_len || depth > 20) return zero;

    uint32_t cur_off = paint_off;
    ColrAffine cur_m = *m;
    int guard = 0;
    while (cur_off < s_colr_len && guard++ < 12) {
        uint8_t fmt = s_colr[cur_off];
        if (fmt == 12 || fmt == 14 || fmt == 16 || fmt == 18) {
            uint32_t next_off = 0;
            ColrAffine next_m;
            if (!apply_transform_paint(cur_off, &cur_m, &next_off, &next_m)) return zero;
            cur_off = next_off;
            cur_m = next_m;
            continue;
        }
        break;
    }
    if (cur_off >= s_colr_len) return zero;

    uint8_t fmt = s_colr[cur_off];
    if (fmt == 2) { /* PaintSolid */
        if (cur_off + 5u > s_colr_len) return zero;
        uint16_t pal = rd_u16(s_colr + cur_off + 1);
        float a = f2dot14(rd_s16(s_colr + cur_off + 3));
        return cpal_get_color(pal, a);
    }

    ColrAffine inv;
    if (!affine_invert(&cur_m, &inv)) return zero;
    float gx = inv.xx * tx + inv.xy * ty + inv.dx;
    float gy = inv.yx * tx + inv.yy * ty + inv.dy;

    if (fmt == 4) { /* PaintLinearGradient */
        if (cur_off + 16u > s_colr_len) return zero;
        uint32_t cl_off = cur_off + rd_u24(s_colr + cur_off + 1);
        float x0 = (float)rd_s16(s_colr + cur_off + 4);
        float y0 = (float)rd_s16(s_colr + cur_off + 6);
        float x1 = (float)rd_s16(s_colr + cur_off + 8);
        float y1 = (float)rd_s16(s_colr + cur_off + 10);
        float x2 = (float)rd_s16(s_colr + cur_off + 12);
        float y2 = (float)rd_s16(s_colr + cur_off + 14);

        float v1x = x1 - x0, v1y = y1 - y0;
        float v2x = x2 - x0, v2y = y2 - y0;
        float nx, ny, denom;
        if (fabsf(v2x) < 1e-4f && fabsf(v2y) < 1e-4f) {
            nx = v1x;
            ny = v1y;
            denom = v1x * v1x + v1y * v1y;
        } else {
            nx = -v2y;
            ny =  v2x;
            denom = v1x * nx + v1y * ny;
        }
        if (fabsf(denom) < 1e-6f) return zero;
        float t = ((gx - x0) * nx + (gy - y0) * ny) / denom;
        return eval_color_line(cl_off, t);
    }

    if (fmt == 6) { /* PaintRadialGradient */
        if (cur_off + 16u > s_colr_len) return zero;
        uint32_t cl_off = cur_off + rd_u24(s_colr + cur_off + 1);
        float x0 = (float)rd_s16(s_colr + cur_off + 4);
        float y0 = (float)rd_s16(s_colr + cur_off + 6);
        float r0 = (float)rd_u16(s_colr + cur_off + 8);
        float x1 = (float)rd_s16(s_colr + cur_off + 10);
        float y1 = (float)rd_s16(s_colr + cur_off + 12);
        float r1 = (float)rd_u16(s_colr + cur_off + 14);

        float dx = x1 - x0, dy = y1 - y0, dr = r1 - r0;
        float fx = gx - x0, fy = gy - y0;
        float A = dx * dx + dy * dy - dr * dr;
        float B = -2.0f * (fx * dx + fy * dy + r0 * dr);
        float C = fx * fx + fy * fy - r0 * r0;
        float t = 0.0f;
        int valid = 0;

        if (fabsf(A) < 1e-5f) {
            if (fabsf(B) > 1e-5f) {
                t = -C / B;
                if (r0 + t * dr >= 0.0f) valid = 1;
            }
        } else {
            float disc = B * B - 4.0f * A * C;
            if (disc >= 0.0f) {
                float sdisc = sqrtf(disc);
                float t_a = (-B + sdisc) / (2.0f * A);
                float t_b = (-B - sdisc) / (2.0f * A);
                float t_hi = (t_a > t_b) ? t_a : t_b;
                float t_lo = (t_a > t_b) ? t_b : t_a;
                if (r0 + t_hi * dr >= -1e-4f) {
                    t = t_hi;
                    valid = 1;
                } else if (r0 + t_lo * dr >= -1e-4f) {
                    t = t_lo;
                    valid = 1;
                }
            }
        }
        if (!valid) return zero;
        return eval_color_line(cl_off, t);
    }

    return zero;
}

static inline void blend_src_over_pixel(float *dst_px, float sr, float sg, float sb, float sa) {
    /* Premultiplied SRC_OVER: out = src + dst * (1 - sa) */
    float inv = 1.0f - sa;
    dst_px[0] = sr + dst_px[0] * inv;
    dst_px[1] = sg + dst_px[1] * inv;
    dst_px[2] = sb + dst_px[2] * inv;
    dst_px[3] = sa + dst_px[3] * inv;
}

static float blend_channel_separable(uint8_t mode, float cb, float cs) {
    switch (mode) {
        case 13: /* SCREEN */
            return cb + cs - cb * cs;
        case 14: /* OVERLAY */
            return (cb <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
        case 15: /* DARKEN */
            return (cb < cs) ? cb : cs;
        case 16: /* LIGHTEN */
            return (cb > cs) ? cb : cs;
        case 19: /* HARD_LIGHT */
            return (cs <= 0.5f) ? (2.0f * cb * cs) : (1.0f - 2.0f * (1.0f - cb) * (1.0f - cs));
        case 20: { /* SOFT_LIGHT */
            if (cs <= 0.5f) {
                return cb - (1.0f - 2.0f * cs) * cb * (1.0f - cb);
            }
            float d = (cb <= 0.25f)
                      ? (((16.0f * cb - 12.0f) * cb + 4.0f) * cb)
                      : sqrtf(cb);
            return cb + (2.0f * cs - 1.0f) * (d - cb);
        }
        case 23: /* MULTIPLY */
            return cb * cs;
        default:
            return cs;
    }
}

static void composite_buffers(const float *src, const float *dst, float *out_over, uint8_t mode) {
    const int n = EMOJI_RASTER_PX * EMOJI_RASTER_PX;
    for (int i = 0; i < n; i++) {
        const float *s = src + i * 4;
        const float *d = dst + i * 4;
        float sr = s[0], sg = s[1], sb = s[2], sa = s[3];
        float dr = d[0], dg = d[1], db = d[2], da = d[3];
        float or_ = 0.0f, og = 0.0f, ob = 0.0f, oa = 0.0f;

        if (mode == 3) { /* SRC_OVER */
            float inv = 1.0f - sa;
            or_ = sr + dr * inv;
            og  = sg + dg * inv;
            ob  = sb + db * inv;
            oa  = sa + da * inv;
        } else if (mode == 5) { /* SRC_IN */
            or_ = sr * da;
            og  = sg * da;
            ob  = sb * da;
            oa  = sa * da;
        } else if (mode == 12) { /* PLUS */
            or_ = fminf(1.0f, sr + dr);
            og  = fminf(1.0f, sg + dg);
            ob  = fminf(1.0f, sb + db);
            oa  = fminf(1.0f, sa + da);
        } else if (mode == 1) { /* SRC */
            or_ = sr; og = sg; ob = sb; oa = sa;
        } else if (mode == 2) { /* DEST */
            or_ = dr; og = dg; ob = db; oa = da;
        } else if (mode >= 13 && mode <= 23) {
            float cs_r = (sa > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, sr / sa)) : 0.0f;
            float cs_g = (sa > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, sg / sa)) : 0.0f;
            float cs_b = (sa > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, sb / sa)) : 0.0f;
            float cb_r = (da > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, dr / da)) : 0.0f;
            float cb_g = (da > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, dg / da)) : 0.0f;
            float cb_b = (da > 1e-6f) ? fminf(1.0f, fmaxf(0.0f, db / da)) : 0.0f;
            float br = blend_channel_separable(mode, cb_r, cs_r);
            float bg = blend_channel_separable(mode, cb_g, cs_g);
            float bb = blend_channel_separable(mode, cb_b, cs_b);
            or_ = (1.0f - da) * sr + (1.0f - sa) * dr + sa * da * br;
            og  = (1.0f - da) * sg + (1.0f - sa) * dg + sa * da * bg;
            ob  = (1.0f - da) * sb + (1.0f - sa) * db + sa * da * bb;
            oa  = sa + da - sa * da;
        } else {
            float inv = 1.0f - sa;
            or_ = sr + dr * inv;
            og  = sg + dg * inv;
            ob  = sb + db * inv;
            oa  = sa + da * inv;
        }

        blend_src_over_pixel(out_over + i * 4, or_, og, ob, oa);
    }
}

static void render_paint_into(uint32_t paint_off, const ColrAffine *m,
                              float *canvas, uint8_t *mask_scratch, int depth) {
    if (!s_colr || paint_off >= s_colr_len || depth > 24) return;
    uint8_t fmt = s_colr[paint_off];

    if (fmt == 1) { /* PaintColrLayers */
        if (paint_off + 6u > s_colr_len) return;
        uint8_t num_l = s_colr[paint_off + 1];
        uint32_t first_l = rd_u32(s_colr + paint_off + 2);
        for (uint8_t i = 0; i < num_l; i++) {
            uint32_t idx = first_l + i;
            if (idx >= s_num_layers) break;
            uint32_t rec = s_llist_off + 4u + idx * 4u;
            if (rec + 4u > s_colr_len) break;
            uint32_t child = s_llist_off + rd_u32(s_colr + rec);
            render_paint_into(child, m, canvas, mask_scratch, depth + 1);
        }
        return;
    }

    if (fmt == 12 || fmt == 14 || fmt == 16 || fmt == 18) {
        uint32_t child = 0;
        ColrAffine next_m;
        if (apply_transform_paint(paint_off, m, &child, &next_m)) {
            render_paint_into(child, &next_m, canvas, mask_scratch, depth + 1);
        }
        return;
    }

    if (fmt == 32) { /* PaintComposite */
        if (paint_off + 8u > s_colr_len) return;
        uint32_t src_off = paint_off + rd_u24(s_colr + paint_off + 1);
        uint8_t mode = s_colr[paint_off + 4];
        uint32_t dst_off = paint_off + rd_u24(s_colr + paint_off + 5);

        size_t buf_bytes = (size_t)EMOJI_RASTER_PX * EMOJI_RASTER_PX * 4 * sizeof(float);
        float *src_buf = (float *)calloc(1, buf_bytes);
        float *dst_buf = (float *)calloc(1, buf_bytes);
        if (src_buf && dst_buf) {
            render_paint_into(src_off, m, src_buf, mask_scratch, depth + 1);
            render_paint_into(dst_off, m, dst_buf, mask_scratch, depth + 1);
            composite_buffers(src_buf, dst_buf, canvas, mode);
        }
        free(src_buf);
        free(dst_buf);
        return;
    }

    if (fmt == 10) { /* PaintGlyph */
        if (paint_off + 6u > s_colr_len) return;
        uint32_t child_off = paint_off + rd_u24(s_colr + paint_off + 1);
        uint16_t sub_gid = rd_u16(s_colr + paint_off + 4);

        if (FT_Load_Glyph(s_ft_face, sub_gid,
                          FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) {
            return;
        }
        if (s_ft_face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return;

        FT_Outline *outline = &s_ft_face->glyph->outline;
        float scale_px = (float)EMOJI_RASTER_PX / EMOJI_ADV_UNITS;
        float scale_26_6 = scale_px * 64.0f;

        for (int i = 0; i < outline->n_points; i++) {
            float fx = (float)outline->points[i].x;
            float fy = (float)outline->points[i].y;
            float tx = m->xx * fx + m->xy * fy + m->dx;
            float ty = m->yx * fx + m->yy * fy + m->dy;
            outline->points[i].x = (FT_Pos)lroundf(tx * scale_26_6);
            outline->points[i].y = (FT_Pos)lroundf((ty - EMOJI_Y_MIN) * scale_26_6);
        }

        memset(mask_scratch, 0, (size_t)EMOJI_RASTER_PX * EMOJI_RASTER_PX);
        FT_Bitmap bmp;
        memset(&bmp, 0, sizeof(bmp));
        bmp.rows = EMOJI_RASTER_PX;
        bmp.width = EMOJI_RASTER_PX;
        bmp.pitch = EMOJI_RASTER_PX;
        bmp.buffer = mask_scratch;
        bmp.num_grays = 256;
        bmp.pixel_mode = FT_PIXEL_MODE_GRAY;

        if (FT_Outline_Get_Bitmap(s_ft_lib, outline, &bmp) != 0) return;

        uint8_t cfmt = (child_off < s_colr_len) ? s_colr[child_off] : 0;
        if (cfmt == 2) {
            ColrRGBA solid = sample_fill_paint(child_off, m, 0.0f, 0.0f, depth + 1);
            if (solid.a <= 1e-5f) return;
            for (int py = 0; py < EMOJI_RASTER_PX; py++) {
                for (int px = 0; px < EMOJI_RASTER_PX; px++) {
                    uint8_t cov_u8 = mask_scratch[py * EMOJI_RASTER_PX + px];
                    if (cov_u8 == 0) continue;
                    float sa = solid.a * ((float)cov_u8 / 255.0f);
                    blend_src_over_pixel(canvas + (py * EMOJI_RASTER_PX + px) * 4,
                                         solid.r * sa, solid.g * sa, solid.b * sa, sa);
                }
            }
        } else {
            for (int py = 0; py < EMOJI_RASTER_PX; py++) {
                float ty = EMOJI_Y_MAX - ((float)py + 0.5f) / scale_px;
                for (int px = 0; px < EMOJI_RASTER_PX; px++) {
                    uint8_t cov_u8 = mask_scratch[py * EMOJI_RASTER_PX + px];
                    if (cov_u8 == 0) continue;
                    float tx = ((float)px + 0.5f) / scale_px;
                    ColrRGBA col = sample_fill_paint(child_off, m, tx, ty, depth + 1);
                    if (col.a <= 1e-5f) continue;
                    float sa = col.a * ((float)cov_u8 / 255.0f);
                    blend_src_over_pixel(canvas + (py * EMOJI_RASTER_PX + px) * 4,
                                         col.r * sa, col.g * sa, col.b * sa, sa);
                }
            }
        }
        return;
    }

    if (fmt == 11) { /* PaintColrGlyph */
        if (paint_off + 3u > s_colr_len) return;
        uint16_t ref_gid = rd_u16(s_colr + paint_off + 1);
        uint32_t ref_paint = find_base_glyph_paint(ref_gid);
        if (ref_paint > 0) {
            render_paint_into(ref_paint, m, canvas, mask_scratch, depth + 1);
        }
        return;
    }
}

static vita2d_texture *render_emoji_texture(uint32_t cp) {
    if (!s_ready || !s_ft_face) return NULL;
    FT_UInt gid = FT_Get_Char_Index(s_ft_face, (FT_ULong)cp);
    if (gid == 0) return NULL;
    uint32_t root_paint = find_base_glyph_paint((uint16_t)gid);
    if (root_paint == 0) return NULL;

    float *canvas = (float *)calloc((size_t)EMOJI_RASTER_PX * EMOJI_RASTER_PX * 4, sizeof(float));
    uint8_t *mask = (uint8_t *)malloc((size_t)EMOJI_RASTER_PX * EMOJI_RASTER_PX);
    if (!canvas || !mask) {
        free(canvas);
        free(mask);
        return NULL;
    }

    ColrAffine ident = affine_identity();
    render_paint_into(root_paint, &ident, canvas, mask, 0);

    vita2d_texture *tex = vita2d_create_empty_texture_format(
        EMOJI_RASTER_PX, EMOJI_RASTER_PX, SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ABGR);
    if (tex) {
        uint8_t *dst = (uint8_t *)vita2d_texture_get_datap(tex);
        uint32_t stride = vita2d_texture_get_stride(tex);
        if (dst) {
            for (int py = 0; py < EMOJI_RASTER_PX; py++) {
                uint8_t *row = dst + ((size_t)py * stride);
                for (int px = 0; px < EMOJI_RASTER_PX; px++) {
                    const float *p = canvas + ((size_t)py * EMOJI_RASTER_PX + px) * 4;
                    float a = p[3];
                    if (a > 1e-4f) {
                        float r = fminf(1.0f, fmaxf(0.0f, p[0] / a));
                        float g = fminf(1.0f, fmaxf(0.0f, p[1] / a));
                        float b = fminf(1.0f, fmaxf(0.0f, p[2] / a));
                        float ca = fminf(1.0f, fmaxf(0.0f, a));
                        row[px * 4 + 0] = (uint8_t)lroundf(r * 255.0f);
                        row[px * 4 + 1] = (uint8_t)lroundf(g * 255.0f);
                        row[px * 4 + 2] = (uint8_t)lroundf(b * 255.0f);
                        row[px * 4 + 3] = (uint8_t)lroundf(ca * 255.0f);
                    } else {
                        row[px * 4 + 0] = 0;
                        row[px * 4 + 1] = 0;
                        row[px * 4 + 2] = 0;
                        row[px * 4 + 3] = 0;
                    }
                }
            }
        }
    }

    free(canvas);
    free(mask);
    return tex;
}

#endif /* EMOJI_HAS_FT */

int emoji_colr_init(void) {
#ifdef EMOJI_HAS_FT
    memset(s_cache, 0, sizeof(s_cache));
    if (FT_Init_FreeType(&s_ft_lib) != 0) return -1;

    if (FT_New_Face(s_ft_lib, "app0:assets/fonts/NotoColorEmoji-Regular.ttf", 0, &s_ft_face) != 0) {
        if (FT_New_Face(s_ft_lib, "assets/fonts/NotoColorEmoji-Regular.ttf", 0, &s_ft_face) != 0) {
            FT_Done_FreeType(s_ft_lib);
            s_ft_lib = NULL;
            return -1;
        }
    }

    FT_ULong colr_len = 0;
    FT_ULong cpal_len = 0;
    if (FT_Load_Sfnt_Table(s_ft_face, FT_MAKE_TAG('C','O','L','R'), 0, NULL, &colr_len) != 0 || colr_len < 26 ||
        FT_Load_Sfnt_Table(s_ft_face, FT_MAKE_TAG('C','P','A','L'), 0, NULL, &cpal_len) != 0 || cpal_len < 12) {
        emoji_colr_term();
        return -1;
    }

    s_colr = (uint8_t *)malloc((size_t)colr_len);
    s_cpal = (uint8_t *)malloc((size_t)cpal_len);
    if (!s_colr || !s_cpal) {
        emoji_colr_term();
        return -1;
    }
    s_colr_len = (uint32_t)colr_len;
    s_cpal_len = (uint32_t)cpal_len;

    if (FT_Load_Sfnt_Table(s_ft_face, FT_MAKE_TAG('C','O','L','R'), 0, s_colr, &colr_len) != 0 ||
        FT_Load_Sfnt_Table(s_ft_face, FT_MAKE_TAG('C','P','A','L'), 0, s_cpal, &cpal_len) != 0) {
        emoji_colr_term();
        return -1;
    }

    uint16_t colr_ver = rd_u16(s_colr);
    if (colr_ver < 1) {
        emoji_colr_term();
        return -1;
    }

    s_bglist_off = rd_u32(s_colr + 14);
    s_llist_off  = rd_u32(s_colr + 18);
    if (s_bglist_off + 4u > s_colr_len || s_llist_off + 4u > s_colr_len) {
        emoji_colr_term();
        return -1;
    }
    s_num_bg     = rd_u32(s_colr + s_bglist_off);
    s_num_layers = rd_u32(s_colr + s_llist_off);

    s_cpal_num_colors = rd_u16(s_cpal + 6);
    s_cpal_colors_off = rd_u32(s_cpal + 8);
    if (s_cpal_colors_off >= s_cpal_len) {
        emoji_colr_term();
        return -1;
    }

    s_ready = 1;
    return 0;
#else
    return -1;
#endif
}

void emoji_colr_term(void) {
#ifdef EMOJI_HAS_FT
    for (int i = 0; i < EMOJI_CACHE_SIZE; i++) {
        if (s_cache[i].tex) {
            vita2d_free_texture(s_cache[i].tex);
            s_cache[i].tex = NULL;
        }
        s_cache[i].cp = 0;
        s_cache[i].last_used = 0;
    }
    free(s_colr);
    s_colr = NULL;
    s_colr_len = 0;
    free(s_cpal);
    s_cpal = NULL;
    s_cpal_len = 0;
    if (s_ft_face) { FT_Done_Face(s_ft_face); s_ft_face = NULL; }
    if (s_ft_lib)  { FT_Done_FreeType(s_ft_lib); s_ft_lib = NULL; }
    s_ready = 0;
#endif
}

int emoji_colr_has_codepoint(uint32_t cp) {
#ifdef EMOJI_HAS_FT
    if (!s_ready || !s_ft_face) return 0;
    FT_UInt gid = FT_Get_Char_Index(s_ft_face, (FT_ULong)cp);
    if (gid == 0) return 0;
    return find_base_glyph_paint((uint16_t)gid) != 0;
#else
    (void)cp;
    return 0;
#endif
}

float emoji_colr_advance(unsigned int size) {
    return (float)size * (EMOJI_ADV_UNITS / EMOJI_UPEM);
}

void emoji_colr_draw(uint32_t cp, float x, float y, unsigned int size, unsigned int color) {
#ifdef EMOJI_HAS_FT
    if (!s_ready || cp == 0 || size == 0) return;

    int found = -1;
    int empty_idx = -1;
    int lru_idx = 0;
    uint32_t min_tick = 0xFFFFFFFFu;
    for (int i = 0; i < EMOJI_CACHE_SIZE; i++) {
        if (s_cache[i].cp == cp && s_cache[i].tex != NULL) {
            found = i;
            break;
        }
        if (s_cache[i].tex == NULL) {
            if (empty_idx < 0) empty_idx = i;
            continue;
        }
        if (s_cache[i].last_used < min_tick) {
            min_tick = s_cache[i].last_used;
            lru_idx = i;
        }
    }

    if (found < 0) {
        int slot = (empty_idx >= 0) ? empty_idx : lru_idx;
        vita2d_texture *tex = render_emoji_texture(cp);
        if (!tex) return;
        if (s_cache[slot].tex) {
            vita2d_free_texture(s_cache[slot].tex);
        }
        s_cache[slot].cp = cp;
        s_cache[slot].tex = tex;
        found = slot;
    }

    s_cache[found].last_used = ++s_use_tick;
    float draw_px = (float)size * (EMOJI_ADV_UNITS / EMOJI_UPEM);
    float tex_scale = draw_px / (float)EMOJI_RASTER_PX;
    float top_y = y - ((float)size * (EMOJI_Y_MAX / EMOJI_UPEM));
    uint8_t alpha = (uint8_t)((color >> 24) & 0xFF);
    vita2d_draw_texture_tint_scale(s_cache[found].tex, x, top_y,
                                   tex_scale, tex_scale,
                                   RGBA8(255, 255, 255, alpha));
#else
    (void)cp; (void)x; (void)y; (void)size; (void)color;
#endif
}
