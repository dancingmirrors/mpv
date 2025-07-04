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

#ifndef MPLAYER_CSPUTILS_H
#define MPLAYER_CSPUTILS_H

#include <stdbool.h>
#include <stdint.h>

#include "options/m_option.h"

/* NOTE: the csp and levels AUTO values are converted to specific ones
 * above vf/vo level. At least vf_scale relies on all valid settings being
 * nonzero at vf/vo level.
 */

enum mp_csp {
    MP_CSP_AUTO,
    MP_CSP_BT_601,
    MP_CSP_BT_709,
    MP_CSP_SMPTE_240M,
    MP_CSP_BT_2020_NC,
    MP_CSP_BT_2020_C,
    MP_CSP_RGB,
    MP_CSP_XYZ,
    MP_CSP_YCGCO,
    MP_CSP_COUNT
};

extern const struct m_opt_choice_alternatives mp_csp_names[];

enum mp_csp_levels {
    MP_CSP_LEVELS_AUTO,
    MP_CSP_LEVELS_TV,
    MP_CSP_LEVELS_PC,
    MP_CSP_LEVELS_COUNT,
};

extern const struct m_opt_choice_alternatives mp_csp_levels_names[];

enum mp_csp_prim {
    MP_CSP_PRIM_AUTO,
    MP_CSP_PRIM_BT_601_525,
    MP_CSP_PRIM_BT_601_625,
    MP_CSP_PRIM_BT_709,
    MP_CSP_PRIM_BT_2020,
    MP_CSP_PRIM_BT_470M,
    MP_CSP_PRIM_APPLE,
    MP_CSP_PRIM_ADOBE,
    MP_CSP_PRIM_PRO_PHOTO,
    MP_CSP_PRIM_CIE_1931,
    MP_CSP_PRIM_DCI_P3,
    MP_CSP_PRIM_DISPLAY_P3,
    MP_CSP_PRIM_V_GAMUT,
    MP_CSP_PRIM_S_GAMUT,
    MP_CSP_PRIM_EBU_3213,
    MP_CSP_PRIM_FILM_C,
    MP_CSP_PRIM_ACES_AP0,
    MP_CSP_PRIM_ACES_AP1,
    MP_CSP_PRIM_COUNT
};

extern const struct m_opt_choice_alternatives mp_csp_prim_names[];

enum mp_csp_trc {
    MP_CSP_TRC_AUTO,
    MP_CSP_TRC_BT_1886,
    MP_CSP_TRC_SRGB,
    MP_CSP_TRC_LINEAR,
    MP_CSP_TRC_GAMMA18,
    MP_CSP_TRC_GAMMA20,
    MP_CSP_TRC_GAMMA22,
    MP_CSP_TRC_GAMMA24,
    MP_CSP_TRC_GAMMA26,
    MP_CSP_TRC_GAMMA28,
    MP_CSP_TRC_PRO_PHOTO,
    MP_CSP_TRC_PQ,
    MP_CSP_TRC_HLG,
    MP_CSP_TRC_V_LOG,
    MP_CSP_TRC_S_LOG1,
    MP_CSP_TRC_S_LOG2,
    MP_CSP_TRC_ST428,
    MP_CSP_TRC_COUNT
};

extern const struct m_opt_choice_alternatives mp_csp_trc_names[];

enum mp_csp_light {
    MP_CSP_LIGHT_AUTO,
    MP_CSP_LIGHT_DISPLAY,
    MP_CSP_LIGHT_SCENE_HLG,
    MP_CSP_LIGHT_SCENE_709_1886,
    MP_CSP_LIGHT_SCENE_1_2,
    MP_CSP_LIGHT_COUNT
};

extern const struct m_opt_choice_alternatives mp_csp_light_names[];

// These constants are based on the ICC specification (Table 23) and match
// up with the API of LittleCMS, which treats them as integers.
enum mp_render_intent {
    MP_INTENT_PERCEPTUAL = 0,
    MP_INTENT_RELATIVE_COLORIMETRIC = 1,
    MP_INTENT_SATURATION = 2,
    MP_INTENT_ABSOLUTE_COLORIMETRIC = 3
};

// The numeric values (except -1) match the Matroska StereoMode element value.
enum mp_stereo3d_mode {
    MP_STEREO3D_INVALID = -1,
    /* only modes explicitly referenced in the code are listed */
    MP_STEREO3D_MONO = 0,
    MP_STEREO3D_SBS2L = 1,
    MP_STEREO3D_AB2R = 2,
    MP_STEREO3D_AB2L = 3,
    MP_STEREO3D_SBS2R = 11,
    /* no explicit enum entries for most valid values */
    MP_STEREO3D_COUNT = 15, // 14 is last valid mode
};

extern const struct m_opt_choice_alternatives mp_stereo3d_names[];

#define MP_STEREO3D_NAME_DEF(x, def) m_opt_choice_str_def(mp_stereo3d_names, x, def)

struct mp_hdr_metadata {
    // HDR10
    // Mastering display metadata
    float min_luma, max_luma;       // min/max luminance (in cd/m²)

    // Content light level
    float max_cll;                  // max content light level (in cd/m²)
    float max_fall;                 // max frame average light level (in cd/m²)

    // HDR10+
    float scene_max[3];             // maxRGB in cd/m² per component (RGB)
    float scene_avg;                // average of maxRGB in cd/m²

    // CIE Y
    float max_pq_y;                 // maximum PQ luminance (in PQ, 0-1)
    float avg_pq_y;                 // averaged PQ luminance (in PQ, 0-1)
};

struct mp_colorspace {
    enum mp_csp space;
    enum mp_csp_levels levels;
    enum mp_csp_prim primaries;
    enum mp_csp_trc gamma;
    enum mp_csp_light light;
    float sig_peak; // highest relative value in signal. 0 = unknown/auto
};

// For many colorspace conversions, in particular those involving HDR, an
// implicit reference white level is needed. Since this magic constant shows up
// a lot, give it an explicit name. The value of 203 cd/m² comes from ITU-R
// Report BT.2408, and the value for HLG comes from the cited HLG 75% level
// (transferred to scene space).
#define MP_REF_WHITE 203.0
#define MP_REF_WHITE_HLG 3.17955

// Replaces unknown values in the first struct by those of the second struct
void mp_colorspace_merge(struct mp_colorspace *orig, struct mp_colorspace *new);

struct mp_csp_params {
    struct mp_colorspace color; // input colorspace
    enum mp_csp_levels levels_out; // output device
    float brightness;
    float contrast;
    float hue;
    float saturation;
    float gamma;
    // discard U/V components
    bool gray;
    // input is already centered and range-expanded
    bool is_float;
    // texture_bits/input_bits is for rescaling fixed point input to range [0,1]
    int texture_bits;
    int input_bits;
};

#define MP_CSP_PARAMS_DEFAULTS {                                \
    .color = { .space = MP_CSP_BT_601,                          \
               .levels = MP_CSP_LEVELS_TV },                    \
    .levels_out = MP_CSP_LEVELS_PC,                             \
    .brightness = 0, .contrast = 1, .hue = 0, .saturation = 1,  \
    .gamma = 1, .texture_bits = 8, .input_bits = 8}

struct mp_image_params;
void mp_csp_set_image_params(struct mp_csp_params *params,
                             const struct mp_image_params *imgparams);

bool mp_colorspace_equal(struct mp_colorspace c1, struct mp_colorspace c2);

enum mp_chroma_location {
    MP_CHROMA_AUTO,
    MP_CHROMA_TOPLEFT,  // uhd
    MP_CHROMA_LEFT,     // mpeg2/4, h264
    MP_CHROMA_CENTER,   // mpeg1, jpeg
    MP_CHROMA_COUNT,
};

extern const struct m_opt_choice_alternatives mp_chroma_names[];

enum mp_alpha_type {
    MP_ALPHA_AUTO,
    MP_ALPHA_STRAIGHT,
    MP_ALPHA_PREMUL,
};

extern const struct m_opt_choice_alternatives mp_alpha_names[];

extern const struct m_sub_options mp_csp_equalizer_conf;

struct mpv_global;
struct mp_csp_equalizer_state *mp_csp_equalizer_create(void *ta_parent,
                                                    struct mpv_global *global);
bool mp_csp_equalizer_state_changed(struct mp_csp_equalizer_state *state);
void mp_csp_equalizer_state_get(struct mp_csp_equalizer_state *state,
                                struct mp_csp_params *params);

struct mp_csp_col_xy {
    float x, y;
};

static inline float mp_xy_X(struct mp_csp_col_xy xy) {
    return xy.x / xy.y;
}

static inline float mp_xy_Z(struct mp_csp_col_xy xy) {
    return (1 - xy.x - xy.y) / xy.y;
}

struct mp_csp_primaries {
    struct mp_csp_col_xy red, green, blue, white;
};

enum mp_csp avcol_spc_to_mp_csp(int avcolorspace);
enum mp_csp_levels avcol_range_to_mp_csp_levels(int avrange);
enum mp_csp_prim avcol_pri_to_mp_csp_prim(int avpri);
enum mp_csp_trc avcol_trc_to_mp_csp_trc(int avtrc);

int mp_csp_to_avcol_spc(enum mp_csp colorspace);
int mp_csp_levels_to_avcol_range(enum mp_csp_levels range);
int mp_csp_prim_to_avcol_pri(enum mp_csp_prim prim);
int mp_csp_trc_to_avcol_trc(enum mp_csp_trc trc);

enum mp_csp mp_csp_guess_colorspace(int width, int height);
enum mp_csp_prim mp_csp_guess_primaries(int width, int height);

enum mp_chroma_location avchroma_location_to_mp(int avloc);
int mp_chroma_location_to_av(enum mp_chroma_location mploc);
void mp_get_chroma_location(enum mp_chroma_location loc, int *x, int *y);

struct mp_csp_primaries mp_get_csp_primaries(enum mp_csp_prim csp);
float mp_trc_nom_peak(enum mp_csp_trc trc);
bool mp_trc_is_hdr(enum mp_csp_trc trc);

/* Color conversion matrix: RGB = m * YUV + c
 * m is in row-major matrix, with m[row][col], e.g.:
 *     [ a11 a12 a13 ]     float m[3][3] = { { a11, a12, a13 },
 *     [ a21 a22 a23 ]                       { a21, a22, a23 },
 *     [ a31 a32 a33 ]                       { a31, a32, a33 } };
 * This is accessed as e.g.: m[2-1][1-1] = a21
 * In particular, each row contains all the coefficients for one of R, G, B,
 * while each column contains all the coefficients for one of Y, U, V:
 *     m[r,g,b][y,u,v] = ...
 * The matrix could also be viewed as group of 3 vectors, e.g. the 1st column
 * is the Y vector (1, 1, 1), the 2nd is the U vector, the 3rd the V vector.
 * The matrix might also be used for other conversions and colorspaces.
 */
struct mp_cmat {
    float m[3][3];
    float c[3];
};

void mp_get_rgb2xyz_matrix(struct mp_csp_primaries space, float m[3][3]);
void mp_get_cms_matrix(struct mp_csp_primaries src, struct mp_csp_primaries dest,
                       enum mp_render_intent intent, float cms_matrix[3][3]);

double mp_get_csp_mul(enum mp_csp csp, int input_bits, int texture_bits);
void mp_get_csp_uint_mul(enum mp_csp csp, enum mp_csp_levels levels,
                         int bits, int component, double *out_m, double *out_o);
void mp_get_csp_matrix(struct mp_csp_params *params, struct mp_cmat *out);

void mp_invert_matrix3x3(float m[3][3]);
void mp_invert_cmat(struct mp_cmat *out, struct mp_cmat *in);
void mp_map_fixp_color(struct mp_cmat *matrix, int ibits, int in[3],
                                               int obits, int out[3]);

#endif /* MPLAYER_CSPUTILS_H */
