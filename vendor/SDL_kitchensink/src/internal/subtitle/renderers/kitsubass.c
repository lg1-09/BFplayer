#include <assert.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <SDL_surface.h>

#include "bfplayer/text_encoding.h"

#include "kitchensink2/internal/kitlibstate.h"
#include "kitchensink2/internal/subtitle/kitatlas.h"
#include "kitchensink2/internal/subtitle/renderers/kitsubass.h"
#include "kitchensink2/internal/utils/kithelpers.h"
#include "kitchensink2/kiterror.h"

typedef struct Kit_ASSSubtitleRenderer {
    ASS_Renderer *renderer;
    ASS_Track *track;
    SDL_mutex *decoder_lock;
    unsigned char **cached_items;
    SDL_Rect *cached_dst_rects;
    SDL_Rect *cached_src_rects;
    unsigned int cached_items_size;
} Kit_ASSSubtitleRenderer;

static void Kit_ProcessAssImage(unsigned char *dst, const ASS_Image *img, int pitch) {
    const unsigned char r = ((img->color) >> 24) & 0xFF;
    const unsigned char g = ((img->color) >> 16) & 0xFF;
    const unsigned char b = ((img->color) >> 8) & 0xFF;
    const unsigned char a = 0xFF - ((img->color) & 0xFF);
    const unsigned char *src = img->bitmap;

    for(int y = 0; y < img->h; y++) {
        for(int x = 0; x < img->w; x++) {
            const int rx = x * 4;
            dst[rx + 0] = r;
            dst[rx + 1] = g;
            dst[rx + 2] = b;
            dst[rx + 3] = (a * src[x]) >> 8;
        }
        src += img->stride;
        dst += pitch;
    }
}

static char *Kit_DecodeSubtitleText(const char *text, size_t size, int *decoded_size) {
    const size_t cap = size * 3U;
    char *decoded;
    size_t length;
    *decoded_size = 0;
    if (!text || size == 0 || size > (SIZE_MAX / 3U) || bfplayer_text_is_utf8(text, size)) {
        return NULL;
    }
    decoded = malloc(cap + 1U);
    if (!decoded) {
        return NULL;
    }
    length = bfplayer_text_decode_utf8(text, size, decoded, cap);
    if (length == 0 || length > (size_t)INT_MAX) {
        free(decoded);
        return NULL;
    }
    decoded[length] = '\0';
    *decoded_size = (int)length;
    return decoded;
}

static void ren_render_ass_cb(Kit_SubtitleRenderer *renderer, void *src, double pts, double start, double end) {
    assert(renderer != NULL);
    assert(src != NULL);

    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    const AVSubtitle *sub = src;

    // Read incoming subtitle packets to libASS
    SDL_LockMutex(ass_renderer->decoder_lock);
    const long long start_ms = (start + pts) * 1000;
    const long long end_ms = end * 1000;
    for(unsigned int r = 0; r < sub->num_rects; r++) {
        if(sub->rects[r]->ass == NULL)
            continue;

        // This requires the sub_text_format codec_opt set for ffmpeg.
        // Chinese text subtitles are often GBK; libass only accepts UTF-8.
        {
            const char *ass_text = sub->rects[r]->ass;
            const size_t ass_size = strlen(ass_text);
            int decoded_size = 0;
            char *decoded = Kit_DecodeSubtitleText(ass_text, ass_size, &decoded_size);
            ass_process_chunk(
                ass_renderer->track,
                decoded ? decoded : sub->rects[r]->ass,
                decoded ? decoded_size : (int)ass_size,
                start_ms,
                end_ms);
            free(decoded);
        }
    }
    SDL_UnlockMutex(ass_renderer->decoder_lock);
}

static void ren_close_ass_cb(Kit_SubtitleRenderer *renderer) {
    if(!renderer || !renderer->userdata)
        return;
    Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    ass_free_track(ass_renderer->track);
    ass_renderer_done(ass_renderer->renderer);
    SDL_DestroyMutex(ass_renderer->decoder_lock);
    for(unsigned int i = 0; i < ass_renderer->cached_items_size; i++) {
        free(ass_renderer->cached_items[i]);
    }
    free(ass_renderer->cached_items);
    free(ass_renderer->cached_dst_rects);
    free(ass_renderer->cached_src_rects);
    free(ass_renderer);
}

static ASS_Image *Kit_BeginReadFrames(const Kit_SubtitleRenderer *renderer, int *change, const double current_pts) {
    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    const long long now = current_pts * 1000;

    // Tell ASS to render some images
    SDL_LockMutex(ass_renderer->decoder_lock);
    ASS_Image *src = ass_render_frame(ass_renderer->renderer, ass_renderer->track, now, change);
    SDL_UnlockMutex(ass_renderer->decoder_lock);
    return src;
}

static int ren_get_ass_data_cb(
    Kit_SubtitleRenderer *renderer, Kit_TextureAtlas *atlas, SDL_Texture *texture, double current_pts
) {
    int change = 0;
    const ASS_Image *src = Kit_BeginReadFrames(renderer, &change, current_pts);
    if(change == 0) {
        return 0;
    }

    // There was some change, clear old atlas and check it's the same size as texture.
    Kit_ClearAtlasContent(atlas);
    Kit_CheckAtlasTextureSize(atlas, texture);

    // Process images and add them to atlas
    SDL_Surface *dst = NULL;
    int dst_w = 0, dst_h = 0;
    for(; src; src = src->next) {
        if(src->w == 0 || src->h == 0)
            continue;

        // Don't recreate surface if we already have correctly sized one.
        if(dst == NULL || dst_w != src->w || dst_h != src->h) {
            dst_w = src->w;
            dst_h = src->h;
            SDL_FreeSurface(dst);
            dst = SDL_CreateRGBSurfaceWithFormat(0, dst_w, dst_h, 32, SDL_PIXELFORMAT_RGBA32);
        }

        Kit_ProcessAssImage(dst->pixels, src, dst->pitch);
        SDL_Rect target;
        target.x = src->dst_x;
        target.y = src->dst_y;
        target.w = dst->w;
        target.h = dst->h;
        Kit_AddAtlasItem(atlas, texture, dst, &target);
    }

    SDL_FreeSurface(dst);
    return 0;
}

static unsigned int Kit_GetASSFramesNum(const ASS_Image *src) {
    unsigned int count = 0;
    for(; src; src = src->next) {
        if(src->w == 0 || src->h == 0)
            continue;
        count++;
    }
    return count;
}

static int ren_get_ass_raw_frames_cb(
    Kit_SubtitleRenderer *renderer, unsigned char ***frames, SDL_Rect **sources, SDL_Rect **targets, double current_pts
) {
    Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    int change = 0;
    const ASS_Image *src = Kit_BeginReadFrames(renderer, &change, current_pts);
    if(change == 0) {
        goto get_cached;
    }

    // Something happened, so free up the old subtitle data first.
    for(unsigned int i = 0; i < ass_renderer->cached_items_size; i++) {
        free(ass_renderer->cached_items[i]);
    }

    // Figure out if we have any rects to render after we prune the zero-sized ones
    ass_renderer->cached_items_size = Kit_GetASSFramesNum(src);
    if(ass_renderer->cached_items_size == 0) {
        goto get_cached;
    }

    // Generate new RGBA images from libass surfaces and cache them.
    unsigned int index = 0;
    ass_renderer->cached_items =
        realloc(ass_renderer->cached_items, ass_renderer->cached_items_size * sizeof(unsigned char *));
    ass_renderer->cached_dst_rects =
        realloc(ass_renderer->cached_dst_rects, ass_renderer->cached_items_size * sizeof(SDL_Rect));
    ass_renderer->cached_src_rects =
        realloc(ass_renderer->cached_src_rects, ass_renderer->cached_items_size * sizeof(SDL_Rect));
    for(; src; src = src->next) {
        if(src->w == 0 || src->h == 0)
            continue;

        unsigned char *buf = malloc(src->w * src->h * 4);
        Kit_ProcessAssImage(buf, src, src->w * 4);
        ass_renderer->cached_items[index] = buf;
        ass_renderer->cached_src_rects[index] = (SDL_Rect){0, 0, src->w, src->h};
        ass_renderer->cached_dst_rects[index] = (SDL_Rect){src->dst_x, src->dst_y, src->w, src->h};
        index++;
    }

get_cached:
    *frames = ass_renderer->cached_items;
    *targets = ass_renderer->cached_dst_rects;
    *sources = ass_renderer->cached_src_rects;
    return ass_renderer->cached_items_size;
}

static void ren_set_ass_size_cb(Kit_SubtitleRenderer *renderer, int w, int h) {
    const Kit_ASSSubtitleRenderer *ass_renderer = renderer->userdata;
    SDL_LockMutex(ass_renderer->decoder_lock);
    ass_set_frame_size(ass_renderer->renderer, w, h);
    SDL_UnlockMutex(ass_renderer->decoder_lock);
}

Kit_SubtitleRenderer *Kit_CreateASSSubtitleRenderer(
    const AVFormatContext *format_ctx, Kit_Decoder *dec, int video_w, int video_h, int screen_w, int screen_h
) {
    assert(dec != NULL);
    assert(video_w >= 0);
    assert(video_h >= 0);
    assert(screen_w >= 0);
    assert(screen_h >= 0);

    Kit_SubtitleRenderer *renderer;
    Kit_ASSSubtitleRenderer *ass_renderer;
    ASS_Renderer *render_handler;
    ASS_Track *render_track;
    SDL_mutex *decoder_lock;

    const Kit_LibraryState *state = Kit_GetLibraryState();
    if(state->libass_handle == NULL) {
        Kit_SetError("Libass library has not been initialized");
        return NULL;
    }
    if((ass_renderer = calloc(1, sizeof(Kit_ASSSubtitleRenderer))) == NULL) {
        Kit_SetError("Unable to allocate ass subtitle renderer");
        goto exit_0;
    }
    if((renderer = Kit_CreateSubtitleRenderer(
            dec,
            ren_render_ass_cb,
            ren_get_ass_data_cb,
            ren_get_ass_raw_frames_cb,
            ren_set_ass_size_cb,
            NULL,
            NULL,
            ren_close_ass_cb,
            ass_renderer
        )) == NULL) {
        goto exit_1;
    }
    if((render_handler = ass_renderer_init(state->libass_handle)) == NULL) {
        Kit_SetError("Unable to initialize libass renderer");
        goto exit_2;
    }
    if((decoder_lock = SDL_CreateMutex()) == NULL) {
        Kit_SetError("Unable to initialize libass decoder lock mutex");
        goto exit_3;
    }

    // Read fonts from attachment streams and give them to libass
    const AVStream *st = NULL;
    for(unsigned int j = 0; j < format_ctx->nb_streams; j++) {
        st = format_ctx->streams[j];
        const AVCodecParameters *codec = st->codecpar;
        if(Kit_StreamIsFontAttachment(st)) {
            const AVDictionaryEntry *tag = av_dict_get(st->metadata, "filename", NULL, AV_DICT_MATCH_CASE);
            if(tag) {
                ass_add_font(state->libass_handle, tag->value, (char *)codec->extradata, codec->extradata_size);
            }
        }
    }

    // Prefer the application-provided fallback so platforms without a system
    // Fontconfig database do not emit errors or lose missing glyph fallback.
    if(state->subtitle_fallback_font[0] != '\0') {
        ass_set_fonts(
            render_handler,
            state->subtitle_fallback_font,
            "Noto Sans",
            ASS_FONTPROVIDER_NONE,
            NULL,
            0);
    } else {
        ass_set_fonts(
            render_handler,
            NULL,
            "sans-serif",
            ASS_FONTPROVIDER_AUTODETECT,
            NULL,
            1);
    }
    ass_set_storage_size(render_handler, video_w, video_h);
    ass_set_frame_size(render_handler, screen_w, screen_h);
    ass_set_hinting(render_handler, state->font_hinting);

    if((render_track = ass_new_track(state->libass_handle)) == NULL) {
        Kit_SetError("Unable to initialize libass track");
        goto exit_4;
    }
    if(dec->codec_ctx->subtitle_header && dec->codec_ctx->subtitle_header_size > 0) {
        int decoded_size = 0;
        char *decoded = Kit_DecodeSubtitleText(
            (const char *)dec->codec_ctx->subtitle_header,
            (size_t)dec->codec_ctx->subtitle_header_size,
            &decoded_size);
        ass_process_codec_private(
            render_track,
            decoded ? decoded : (char *)dec->codec_ctx->subtitle_header,
            decoded ? decoded_size : dec->codec_ctx->subtitle_header_size);
        free(decoded);
    }

    ass_renderer->cached_items = NULL;
    ass_renderer->cached_dst_rects = NULL;
    ass_renderer->cached_src_rects = NULL;
    ass_renderer->cached_items_size = 0;
    ass_renderer->renderer = render_handler;
    ass_renderer->track = render_track;
    ass_renderer->decoder_lock = decoder_lock;
    return renderer;

exit_4:
    SDL_DestroyMutex(decoder_lock);
exit_3:
    ass_renderer_done(render_handler);
exit_2:
    Kit_CloseSubtitleRenderer(renderer);
exit_1:
    free(ass_renderer);
exit_0:
    return NULL;
}
