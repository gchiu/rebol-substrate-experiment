/* Glon AVP G5f - native SDL2 host for the Marine/Alien corridor.
 *
 * Glon owns the world data, player movement, circle/wall collision, ray/wall
 * intersection, occlusion, door state, Alien state/ray visibility, tracker
 * values and vertical span calculation. The host only:
 *   - supplies SDL window/input/timing,
 *   - runs pre-parsed Glon column blocks and one Glon tick block,
 *   - reads the bounded HOST_PRINT span stream,
 *   - rasterises those spans into a 320x200 framebuffer,
 *   - presents and measures the frame.
 *
 * The projection is sampled at 160 columns (2 framebuffer pixels each) for
 * interactive speed; the logical framebuffer remains 320x200.
 * This is not a texture/BSP renderer. */
#define _GNU_SOURCE
#include "r0_s1.h"
#include "m1_layout.h"
#include "textures.h"
#include "alien_sprites_g5s.h"
#include "weapon_sprites.h"
#include "orientation_test.h"
#include "corridor_textures.h"
#include "g5j_textures.h"
#include "object_sprites.h"
#ifdef DOOM_S1_PROFILE
#include "s1_prof.h"
#endif
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <SDL2/SDL_opengl.h>
#include <unistd.h>

#define FB_W 320
#define FB_H 200
#define PROJ_COLS 160
#define PROJ_SCALE 2
#define TICK_HZ 60
#define MAX_CAPTURE (1u << 20)
#define RENDER_INTS (PROJ_COLS * 5)
#define DETAIL_MAX 64

typedef struct {
    int top[PROJ_COLS], bot[PROJ_COLS], color[PROJ_COLS];
    int u[PROJ_COLS], shade[PROJ_COLS], material[PROJ_COLS];
    uint32_t hash;
} Spans;

typedef struct { int visible, x1, y1, x2, y2, color; } DetailLine;
typedef struct { int n; DetailLine line[DETAIL_MAX]; uint32_t hash; } Detail;

typedef struct {
    int valid[FB_H];
    int bx[FB_H], by[FB_H], sx[FB_H], sy[FB_H], shade[FB_H];
    uint32_t hash;
} TexRows;

typedef struct {
    int door_open, door_blocked, door_target;
    int ax, ay, health, state, detected;
    int ammo;
    int td2, tside, tdepth;
    int visible, left, right, top, bot;
    int light;
} Status;

typedef struct { int id, depth, left, right, top, bot, visible; int x,y,hw,hd,h; } Obj;
typedef struct { int depth, l, r, t, b, pct, flip; const uint32_t *src; int sw, sh; } SpriteDraw;
typedef struct {
    double sim_ms, proj_ms, texrow_ms, raster_ms, present_ms, frame_ms;
    double sprite_ms;
    long heap_high, gc_count, live_cells;
} Metrics;

typedef struct {
    int px, py, angle, simple, floor_c, ceil_c;
    Status st;
} ViewSig;

typedef struct {
    int valid;
    ViewSig sig;
    Spans s;
    TexRows tr;
} ViewCache;

#ifdef DOOM_S1_PROFILE
typedef struct {
    long op[8], host[16];
    long branches_taken;
    long hist[256];
    unsigned long long cycles;
} ProfAccum;
static int prof_on;
static ProfAccum prof_wall, prof_tex, prof_status, prof_tick;
static void prof_begin(void) { if (prof_on) s1_prof_reset(); }
static void prof_collect(ProfAccum *a) {
    if (!prof_on) return;
    for (int i = 0; i < 8; i++) a->op[i] += s1_prof_op_count[i];
    for (int i = 0; i < 16; i++) a->host[i] += s1_prof_host_count[i];
    a->branches_taken += s1_prof_branch_taken;
    for (int i = 0; i < 256; i++) a->hist[i] += s1_prof_ip_hist[i];
    a->cycles += s1_prof_run_cycles;
}
#endif

static const char *root_dir = "../..";
static int simple_mode = 0;
static int stationary_enabled = 0;
static int raw_wall_mode = 0;
static int checker_walls = 0;
static int linear_filter = 0;
static int new_textures = 0;
static int smooth_shading = 0;
static int flat_surfaces = 0;
static int gl_active = 0;
static int corridor_art = 1;
static int engineering_room_enabled = 1;
static int objects_mode = 0;
static Obj current_objs[6];
static int current_objs_valid = 0;
static int last_px = 320;
static int last_py = 320;
static double st_current_angle = 0;
static float gl_debug_pitch = 0;
static double gl_prof_finish_ms = 0;
static int get_objects(Obj out[6]);
static int alien_cover = 0;
static int cover_view = 0;
static int need_texrows(void) { return !simple_mode && !flat_surfaces; }
static uint32_t sim_tick = 0;
static double last_sprite_ms = 0.0;
static cell reset_block, sector_block, report_block, valid_block, collision_block;
static cell activate_block, status_block, detail_block, texrows_block;
static cell floor_line_block, ceil_line_block, objects_block, objects_on_block;
static cell alien_collision_block, los_wall_block, los_clear_block;
static cell render_blocks[2];
static cell tick_blocks[64];

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c);
static int wall_tex_v(int y, int top, int bot);
static int alien_frame_index(const Status *st);
static void alien_view_reset(void);
static void draw_alien_sprite(uint32_t *fb, const Status *st);
static const uint32_t *const alien_dir_tex[ALIEN_DIR_COUNT] = {
    alien_dir_idle, alien_dir_front1, alien_dir_front2, alien_dir_threeq,
    alien_dir_side, alien_dir_rear, alien_dir_attack
};

static long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

static uint32_t fnv1a(const void *data, size_t n) {
    const unsigned char *p = data;
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

static void fail(const char *what) {
    fprintf(stderr, "glon-doom: %s\n", what);
    exit(2);
}

/* ---- G6a ambient audio -------------------------------------------------- */
#define AUDIO_RATE 44100
#define AUDIO_CHANNELS 2
#define AUDIO_SAMPLES 1024
#define AUDIO_LAYERS 2

typedef struct {
    int16_t *samples;
    int frames;
    int pos;
    float gain;
} AudioLayer;

static AudioLayer audio_layers[AUDIO_LAYERS];
static int audio_nlayers;
static SDL_AudioDeviceID audio_dev;
static int audio_ready;
static SDL_atomic_t audio_volume;
static SDL_atomic_t audio_muted;
static int ambient_volume_pct = 60;
static int audio_disabled;
static int smoke_frames;

static int audio_load_loop(const char *name,const char *file,float gain,int fade_frames) {
    char path[4096];
    snprintf(path,sizeof path,"%s/experiments/glon-doom/assets/audio/%s",root_dir,file);
    SDL_AudioSpec spec;
    Uint8 *wav=NULL;
    Uint32 wav_len=0;
    if(!SDL_LoadWAV(path,&spec,&wav,&wav_len)){
        fprintf(stderr,"audio: cannot load %s: %s\n",path,SDL_GetError());
        return -1;
    }
    int16_t *pcm=NULL;
    int nsamp=0;
    SDL_AudioCVT cvt;
    int need=SDL_BuildAudioCVT(&cvt,spec.format,spec.channels,spec.freq,
                               AUDIO_S16SYS,1,AUDIO_RATE);
    if(need<0){
        fprintf(stderr,"audio: cannot convert %s: %s\n",file,SDL_GetError());
        SDL_FreeWAV(wav);
        return -1;
    }
    if(need==0){
        pcm=(int16_t*)malloc((size_t)wav_len);
        if(!pcm){ SDL_FreeWAV(wav); return -1; }
        memcpy(pcm,wav,wav_len);
        nsamp=(int)(wav_len/sizeof(int16_t));
    } else {
        cvt.len=(int)wav_len;
        cvt.buf=(Uint8*)malloc((size_t)cvt.len*cvt.len_mult);
        if(!cvt.buf){ SDL_FreeWAV(wav); return -1; }
        memcpy(cvt.buf,wav,wav_len);
        if(SDL_ConvertAudio(&cvt)!=0){
            fprintf(stderr,"audio: conversion failed for %s: %s\n",file,SDL_GetError());
            free(cvt.buf); SDL_FreeWAV(wav);
            return -1;
        }
        pcm=(int16_t*)cvt.buf;
        nsamp=(int)(cvt.len_cvt/sizeof(int16_t));
    }
    SDL_FreeWAV(wav);
    if(nsamp <= fade_frames+4){
        fprintf(stderr,"audio: %s is too short for a %d-frame crossfade\n",file,fade_frames);
        free(pcm);
        return -1;
    }
    /* Build a seamless loop by crossfading the tail into the head.  The
     * resulting buffer is shorter by fade_frames: its first fade region moves
     * from the source tail to the source head, and the wrap
     * (tail-1 -> tail) is already continuous in the source. */
    int loop_frames=nsamp-fade_frames;
    int16_t *loop=(int16_t*)malloc((size_t)loop_frames*sizeof(int16_t));
    if(!loop){ free(pcm); return -1; }
    for(int i=0;i<loop_frames;i++){
        if(i<fade_frames){
            float t=((float)i+0.5f)/(float)fade_frames;
            float a=(float)pcm[nsamp-fade_frames+i];
            float b=(float)pcm[i];
            loop[i]=(int16_t)(a*(1.0f-t)+b*t);
        } else {
            loop[i]=pcm[i];
        }
    }
    free(pcm);
    AudioLayer *L=&audio_layers[audio_nlayers++];
    L->samples=loop; L->frames=loop_frames; L->pos=0; L->gain=gain;
    printf("audio: loaded %s %d frames (%.3fs), %d-frame crossfade, gain %.2f\n",
           name,loop_frames,(double)loop_frames/(double)AUDIO_RATE,fade_frames,gain);
    return 0;
}

static void audio_free_layers(void) {
    for(int i=0;i<audio_nlayers;i++){ free(audio_layers[i].samples); audio_layers[i].samples=NULL; audio_layers[i].frames=0; audio_layers[i].pos=0; }
    audio_nlayers=0;
}

static int audio_load_layers(void) {
    audio_free_layers();
    /* Freesound 873687 by Tommaso Motteran, converted to 16-bit mono. */
    if(audio_load_loop("Freesound 873687 spaceship interior","ambient_loop.wav",3.0f,AUDIO_RATE)!=0) goto fail;
    return 0;
fail:
    audio_free_layers();
    return -1;
}

static void audio_mix(int16_t *out,int frames) {
    int v=SDL_AtomicGet(&audio_volume);
    if(SDL_AtomicGet(&audio_muted)) v=0;
    if(v<0)v=0; if(v>100)v=100;
    float master=(v/100.0f)*(v/100.0f);
    for(int i=0;i<frames;i++){
        float mix=0.0f;
        for(int l=0;l<audio_nlayers;l++){
            AudioLayer *L=&audio_layers[l];
            if(!L->samples || L->frames<=0) continue;
            mix += (float)L->samples[L->pos]*L->gain;
            if(++L->pos>=L->frames) L->pos=0;
        }
        int s=(int)(mix*master);
        if(s>32767)s=32767;
        if(s<-32768)s=-32768;
        out[i*2]=(int16_t)s;
        out[i*2+1]=(int16_t)s;
    }
}

static void audio_callback(void *userdata,Uint8 *stream,int len) {
    (void)userdata;
    audio_mix((int16_t*)stream,len/(int)(AUDIO_CHANNELS*sizeof(int16_t)));
}

static void audio_set_volume(int pct) {
    if(pct<0)pct=0; if(pct>100)pct=100;
    SDL_AtomicSet(&audio_volume,pct);
    if(!SDL_AtomicGet(&audio_muted))
        printf("audio: ambient volume %d%%\n",pct);
    fflush(stdout);
}

static void audio_toggle_mute(void) {
    int m=SDL_AtomicGet(&audio_muted);
    SDL_AtomicSet(&audio_muted,!m);
    printf("audio: %s\n",m?"unmuted":"muted");
    fflush(stdout);
}

static int audio_start(void) {
    if(audio_disabled){
        printf("audio: disabled by --no-audio\n");
        return 0;
    }
    if(SDL_InitSubSystem(SDL_INIT_AUDIO)!=0){
        fprintf(stderr,"audio: unavailable (%s); continuing without ambient audio\n",SDL_GetError());
        return 0;
    }
    if(audio_load_layers()!=0){
        fprintf(stderr,"audio: ambient assets unavailable; continuing without audio\n");
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_AudioSpec want,have;
    memset(&want,0,sizeof want);
    want.freq=AUDIO_RATE;
    want.format=AUDIO_S16SYS;
    want.channels=AUDIO_CHANNELS;
    want.samples=AUDIO_SAMPLES;
    want.callback=audio_callback;
    audio_dev=SDL_OpenAudioDevice(NULL,0,&want,&have,0);
    if(!audio_dev){
        fprintf(stderr,"audio: cannot open device (%s); continuing without ambient audio\n",SDL_GetError());
        audio_free_layers();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    if(have.freq!=AUDIO_RATE || have.format!=AUDIO_S16SYS || have.channels!=AUDIO_CHANNELS){
        fprintf(stderr,"audio: device format is not 44100 Hz S16 stereo (%d Hz, %d ch); continuing without audio\n",
                have.freq,have.channels);
        SDL_CloseAudioDevice(audio_dev); audio_dev=0;
        audio_free_layers();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return 0;
    }
    SDL_AtomicSet(&audio_volume,ambient_volume_pct);
    SDL_AtomicSet(&audio_muted,0);
    SDL_PauseAudioDevice(audio_dev,0);
    audio_ready=1;
    printf("audio: ambient started driver=%s %d Hz %dch volume=%d%%\n",
           SDL_GetCurrentAudioDriver(),have.freq,have.channels,ambient_volume_pct);
    fflush(stdout);
    return 0;
}

static void audio_stop(void) {
    if(audio_dev){
        SDL_PauseAudioDevice(audio_dev,1);
        SDL_CloseAudioDevice(audio_dev);
        audio_dev=0;
    }
    audio_free_layers();
    if(audio_ready || SDL_WasInit(SDL_INIT_AUDIO)) SDL_QuitSubSystem(SDL_INIT_AUDIO);
    audio_ready=0;
}

static int audio_selftest(void) {
    if(audio_load_layers()!=0){
        printf("audio-test: FAILED (assets)\n");
        return 1;
    }
    int fails=0;
    int16_t buf[AUDIO_SAMPLES*AUDIO_CHANNELS];
    SDL_AtomicSet(&audio_volume,60);
    SDL_AtomicSet(&audio_muted,0);
    audio_mix(buf,AUDIO_SAMPLES);
    int peak=0;
    long long sumsq=0;
    for(int i=0;i<AUDIO_SAMPLES*AUDIO_CHANNELS;i++){
        int a=buf[i]; if(a<0)a=-a; if(a>peak)peak=a;
        sumsq += (long long)buf[i]*buf[i];
    }
    if(peak<=0){ printf("audio-test: FAILED (silent mix)\n"); fails++; }
    if(peak>32767){ printf("audio-test: FAILED (mix overflow)\n"); fails++; }
    for(int l=0;l<audio_nlayers;l++){
        AudioLayer *L=&audio_layers[l];
        int seam=L->samples[L->frames-1]-L->samples[0];
        if(seam<0)seam=-seam;
        printf("audio-test: layer %d frames=%d (%.3fs) seam=%d peak=%d\n",
               l,L->frames,(double)L->frames/AUDIO_RATE,seam,peak);
        if(seam>4096) fails++;
    }
    SDL_AtomicSet(&audio_volume,0);
    audio_mix(buf,AUDIO_SAMPLES);
    for(int i=0;i<AUDIO_SAMPLES*AUDIO_CHANNELS;i++) if(buf[i]!=0){ printf("audio-test: FAILED (volume 0 not silent)\n"); fails++; break; }
    SDL_AtomicSet(&audio_volume,60);
    SDL_AtomicSet(&audio_muted,1);
    audio_mix(buf,AUDIO_SAMPLES);
    for(int i=0;i<AUDIO_SAMPLES*AUDIO_CHANNELS;i++) if(buf[i]!=0){ printf("audio-test: FAILED (mute not silent)\n"); fails++; break; }
    SDL_AtomicSet(&audio_muted,0);
    audio_set_volume(42);
    if(SDL_AtomicGet(&audio_volume)!=42){ printf("audio-test: FAILED (volume control)\n"); fails++; }
    audio_toggle_mute();
    if(!SDL_AtomicGet(&audio_muted)){ printf("audio-test: FAILED (mute control)\n"); fails++; }
    audio_toggle_mute();
    if(SDL_AtomicGet(&audio_muted)){ printf("audio-test: FAILED (unmute control)\n"); fails++; }
    audio_set_volume(60);
    long long mt0=now_ns();
    for(int k=0;k<1000;k++) audio_mix(buf,AUDIO_SAMPLES);
    double mix_ms=(double)(now_ns()-mt0)/1e6/1000.0;
    if(mix_ms>2.0){ printf("audio-test: FAILED (mixer too slow)\n"); fails++; }
    printf("audio-test: %s rms=%lld mix=%.4fms/block (budget %.2fms)\n",
           fails?"FAILED":"PASSED",(long long)sqrt((double)sumsq/(AUDIO_SAMPLES*AUDIO_CHANNELS)),
           mix_ms,1000.0*AUDIO_SAMPLES/AUDIO_RATE);
    audio_free_layers();
    return fails?1:0;
}

static int audio_device_test(void) {
    if(SDL_Init(SDL_INIT_TIMER)!=0) return 2;
    audio_start();
    if(audio_ready){
        printf("audio-device-test: playback opened; sleeping 300 ms\n");
        SDL_Delay(300);
    } else {
        printf("audio-device-test: no audio device; continued gracefully\n");
    }
    audio_stop();
    SDL_Quit();
    return 0;
}

/* ---- Glon loading / blocking ------------------------------------------- */

static char *read_source(const char *path, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    long n = ftell(f);
    if (n < 0 || n > (1L << 20)) { fclose(f); return NULL; }
    rewind(f);
    char *b = malloc((size_t)n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); fclose(f); return NULL; }
    fclose(f);
    b[n] = 0;
    *len = (size_t)n;
    return b;
}

static void strip_comments(char *s) {
    char *r = s, *w = s;
    while (*r) {
        if (r[0] == ';' && r[1] == ';') {
            while (*r && *r != '\n') r++;
            continue;
        }
        *w++ = *r++;
    }
    *w = 0;
}

static cell parse_source(const char *src, const char *label) {
    int err = 0;
    cell b = r0_s1_parse(src, &err);
    if (err) {
        r0_s1_outcome oc;
        r0_s1_session_parse_error(&oc);
        fprintf(stderr, "glon-doom: %s: parse error (%s)\n", label,
                r0_s1_outcome_detail_name(oc.detail));
        fprintf(stderr, "halt_reason=%d spmin=%ld rpmin=%ld code=%ld\n",
                r0_s1_halt_reason(), (long)r0_s1_sp_min(), (long)r0_s1_rp_min(),
                r0_s1_code_size());
        exit(2);
    }
    return b;
}

static void load_file(const char *path) {
    size_t n;
    char *src = read_source(path, &n);
    if (!src) { fprintf(stderr, "glon-doom: cannot read %s\n", path); exit(2); }
    strip_comments(src);
    cell block = parse_source(src, path);
    free(src);
    r0_s1_outcome oc;
    r0_s1_session_run_block(block, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon-doom: %s: load %s (%s)\n", path,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        fprintf(stderr, "halt_reason=%d spmin=%ld rpmin=%ld code=%ld\n",
                r0_s1_halt_reason(), (long)r0_s1_sp_min(), (long)r0_s1_rp_min(),
                r0_s1_code_size());
        exit(2);
    }
}

static int run_block(cell block, const char *label) {
    r0_s1_outcome oc;
    r0_s1_session_run_block(block, &oc);
    if (oc.status != R0S1_OUT_OK) {
        fprintf(stderr, "glon-doom: %s: %s (%s)\n", label,
                r0_s1_outcome_status_name(oc.status),
                r0_s1_outcome_detail_name(oc.detail));
        return -1;
    }
    return 0;
}

typedef struct { FILE *saved, *stream; char *buf; size_t len; } Capture;

static void capture_begin(Capture *c) {
    memset(c, 0, sizeof *c);
    c->saved = stdout;
    c->stream = open_memstream(&c->buf, &c->len);
    if (!c->stream) fail("open_memstream failed");
    stdout = c->stream;
}
static void capture_end(Capture *c) {
    fflush(stdout);
    fclose(c->stream);
    stdout = c->saved;
}

static int parse_ints(const Capture *c, int *out, int expected, const char *label) {
    char *p = c->buf, *end;
    int n = 0;
    while (p && *p && n < expected) {
        long v = strtol(p, &end, 10);
        if (end == p) break;
        out[n++] = (int)v;
        p = end;
    }
    while (p && (*p == '\n' || *p == ' ' || *p == '\t')) p++;
    if (n != expected || (p && *p)) {
        fprintf(stderr, "glon-doom: %s: expected %d integers, got %d\n", label, expected, n);
        return -1;
    }
    return 0;
}

static int run_capture(cell block, int *out, int expected, const char *label) {
    Capture c;
    capture_begin(&c);
    int rc = run_block(block, label);
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    rc = parse_ints(&c, out, expected, label);
    free(c.buf);
    return rc;
}

static void init_runtime(void) {
    cell main_entry = r0_s1_init();
    M[M1_MAIN_ENTRY_CELL] = main_entry;
    M[M1_CURSOR] = 0;
    M[M1_STRESS_CNT] = 0;
    for (int i = 0; i < M1_MAX_TASKS; i++)
        M[M1_TASK_TABLE + i * M1_TASK_REC_SIZE + TREC_STATE] = TASK_EMPTY;
}

static void load_session(void) {
    char path[4096];
    init_runtime();
    snprintf(path, sizeof path, "%s/jupyter/prelude.glon", root_dir);
    load_file(path);
    snprintf(path, sizeof path, "%s/experiments/glon-doom/doom.glon", root_dir);
    load_file(path);
    if (objects_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/objects.glon", root_dir);
        load_file(path);
    }
    if (raw_wall_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/raw-wall.glon", root_dir);
        load_file(path);
    }
}

static void build_blocks(void) {
    reset_block = parse_source("[ doom/reset ]", "reset");
    sector_block = parse_source("[ doom/sector-report ]", "sector");
    report_block = parse_source("[ doom/report ]", "report");
    valid_block = parse_source("[ doom/valid ]", "valid");
    activate_block = parse_source("[ doom/activate ]", "activate");
    status_block = parse_source("[ do doom/status-block ]", "status");
    objects_block = parse_source("[ do doom/object-report-block ]", "objects");
    objects_on_block = parse_source("[ doom/objects-on: 1 ]", "objects on");
    texrows_block = parse_source("[ do doom/texrows-block ]", "texrows");
    if (!objects_mode && !gl_active) {
    floor_line_block = parse_source("[ doom/detail-line 768 256 768 384 0 5 ]", "floor wall base");
    ceil_line_block = parse_source("[ doom/detail-line 768 256 768 384 128 5 ]", "ceiling wall top");

    const char *collision_src =
        "[ "
        "doom/hit: 0 doom/check-all 300 147 400 print doom/hit "
        "doom/hit: 0 doom/check-all 749 200 400 print doom/hit "
        "doom/hit: 0 doom/check-all 900 275 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1261 600 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1200 1005 400 print doom/hit "
        "doom/hit: 0 doom/check-all 1171 700 400 print doom/hit "
        "doom/hit: 0 doom/check-all 900 403 400 print doom/hit "
        "doom/hit: 0 doom/check-all 787 500 400 print doom/hit "
        "doom/hit: 0 doom/check-all 300 621 400 print doom/hit "
        "doom/hit: 0 doom/check-all 147 300 400 print doom/hit "
        "doom/hit: 0 doom/check-all 500 500 400 print doom/hit "
        "]";
    collision_block = parse_source(collision_src, "collision probes");

    const char *alien_probe_src =
        "[ "
        "doom/hit: 0 doom/check-all 300 143 256 print doom/hit "
        "doom/hit: 0 doom/check-all 753 200 256 print doom/hit "
        "doom/hit: 0 doom/check-all 900 271 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1265 600 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1200 1009 256 print doom/hit "
        "doom/hit: 0 doom/check-all 1167 700 256 print doom/hit "
        "doom/hit: 0 doom/check-all 900 399 256 print doom/hit "
        "doom/hit: 0 doom/check-all 783 500 256 print doom/hit "
        "doom/hit: 0 doom/check-all 300 625 256 print doom/hit "
        "doom/hit: 0 doom/check-all 143 300 256 print doom/hit "
        "doom/hit: 0 doom/check-all 500 500 256 print doom/hit "
        "]";
    alien_collision_block = parse_source(alien_probe_src, "alien collision probes");
    los_wall_block = parse_source(
        "[ doom/px: 320 doom/py: 440 doom/door-blocked: 0 doom/los 1200 440 print doom/occluded ]",
        "wall los");
    los_clear_block = parse_source(
        "[ doom/px: 320 doom/py: 320 doom/door-blocked: 0 doom/los 1120 320 print doom/occluded ]",
        "clear los");
    }

    int tick_count = (objects_mode && !gl_active) ? 16 : 64;
    for (int m = 0; m < tick_count; m++) {
        char src[128];
        snprintf(src, sizeof src, "[ doom/tick %d %d %d %d %d %d ]",
                 !!(m & 1), !!(m & 2), !!(m & 4), !!(m & 8), !!(m & 16), !!(m & 32));
        tick_blocks[m] = parse_source(src, "tick");
    }

    if (!gl_active) for (int b = 0; b < 2; b++) {
        size_t cap = 256 + (PROJ_COLS / 2) * 64;
        char *src = malloc(cap);
        if (!src) fail("out of memory building render block");
        size_t used = 0;
        used += (size_t)snprintf(src + used, cap - used, "[ ");
        for (int i = b * (PROJ_COLS / 2); i < (b + 1) * (PROJ_COLS / 2); i++) {
            int sx = i * PROJ_SCALE + PROJ_SCALE / 2;
            used += (size_t)snprintf(src + used, cap - used,
                                     "doom/sx: %d do %s ", sx,
                                     raw_wall_mode ? "doom/raw-column-block"
                                                   : "doom/render-column-block");
        }
        snprintf(src + used, cap - used, "]");
        render_blocks[b] = parse_source(src, "render");
        free(src);
    }
    if (objects_mode) {
        if (run_block(objects_on_block, "objects on") != 0) fail("objects enable failed");
        if (alien_cover) {
            cell b = parse_source("[ doom/alien-x: 920 doom/alien-y: 270 doom/alien-state: 1 doom/alien-detected: 1 ]", "alien cover");
            if (run_block(b, "alien cover") != 0) fail("alien cover failed");
        }
    }
}

static int get_state(int *x, int *y, int *a) {
    int v[3];
    if (run_capture(report_block, v, 3, "report") != 0) return -1;
    *x = v[0]; *y = v[1]; *a = v[2]; last_px = *x; last_py = *y;
    return 0;
}
static int get_sector(int *floor_c, int *ceil_c) {
    int v[2];
    if (run_capture(sector_block, v, 2, "sector") != 0) return -1;
    *floor_c = v[0]; *ceil_c = v[1];
    return 0;
}
static int get_valid(void) {
    int v;
    if (run_capture(valid_block, &v, 1, "valid") != 0) return -1;
    return v;
}
static int get_status(Status *st) {
    int v[18];
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int grc = run_capture(status_block, v, 18, "status");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_status);
#endif
    if (grc != 0) return -1;
    st->door_open = v[0]; st->door_blocked = v[1]; st->door_target = v[2];
    st->ax = v[3]; st->ay = v[4]; st->health = v[5]; st->state = v[6];
    st->detected = v[7]; st->td2 = v[8]; st->tside = v[9]; st->tdepth = v[10];
    st->visible = v[11]; st->left = v[12]; st->right = v[13];
    st->top = v[14]; st->bot = v[15]; st->light = v[16]; st->ammo = v[17];
    if (objects_mode && get_objects(current_objs) == 0) {
        current_objs_valid = 1;
        if (st->door_blocked && last_px < 768)
            for (int i = 0; i < 6; i++) current_objs[i].visible = 0;
    }
    return 0;
}
static int get_objects(Obj out[6]) {
    int v[72];
    if (run_capture(objects_block, v, 72, "objects") != 0) return -1;
    for (int i = 0; i < 6; i++) {
        int *p = v + i*12;
        out[i].id=p[0]; out[i].depth=p[1]; out[i].left=p[2]; out[i].right=p[3];
        out[i].top=p[4]; out[i].bot=p[5]; out[i].visible=p[6];
        out[i].x=p[7]; out[i].y=p[8]; out[i].hw=p[9]; out[i].hd=p[10]; out[i].h=p[11];
    }
    return 0;
}

static int get_detail(Detail *d) {
    Capture c;
    capture_begin(&c);
    int rc = run_block(detail_block, "detail");
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    char *end;
    long n = strtol(c.buf, &end, 10);
    if (end == c.buf || n < 0 || n > DETAIL_MAX) { free(c.buf); return -1; }
    int vals[1 + DETAIL_MAX * 6];
    if (parse_ints(&c, vals, 1 + (int)n * 6, "detail") != 0) { free(c.buf); return -1; }
    d->n = (int)n;
    for (int i = 0; i < d->n; i++) {
        d->line[i].visible = vals[1 + i * 6];
        d->line[i].x1 = vals[2 + i * 6];
        d->line[i].y1 = vals[3 + i * 6];
        d->line[i].x2 = vals[4 + i * 6];
        d->line[i].y2 = vals[5 + i * 6];
        d->line[i].color = vals[6 + i * 6];
    }
    d->hash = fnv1a(&vals[1], (size_t)n * 6 * sizeof(int));
    free(c.buf);
    return 0;
}

static int get_texrows(TexRows *r) {
    Capture c;
    capture_begin(&c);
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(texrows_block, "texrows");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_tex);
#endif
    capture_end(&c);
    if (rc != 0) { free(c.buf); return -1; }
    char *end;
    long n = strtol(c.buf, &end, 10);
    if (end == c.buf || n != FB_H) { free(c.buf); return -1; }
    int vals[1 + FB_H * 7];
    if (parse_ints(&c, vals, 1 + (int)n * 7, "texrows") != 0) { free(c.buf); return -1; }
    for (int y = 0; y < FB_H; y++) {
        r->valid[y] = 0; r->bx[y] = r->by[y] = r->sx[y] = r->sy[y] = 0;
        r->shade[y] = 0;
    }
    for (int i = 0; i < n; i++) {
        int y = vals[1 + i * 7];
        if (y < 0) { free(c.buf); return -1; }
        if (y >= FB_H) continue;
        r->valid[y] = vals[2 + i * 7];
        r->bx[y] = vals[3 + i * 7];
        r->by[y] = vals[4 + i * 7];
        r->sx[y] = vals[5 + i * 7];
        r->sy[y] = vals[6 + i * 7];
        r->shade[y] = vals[7 + i * 7];
    }
    r->hash = fnv1a(&vals[1], (size_t)n * 7 * sizeof(int));
    free(c.buf);
    return 0;
}

static int reset(void) {
    alien_view_reset();
    return run_block(reset_block, "reset");
}
static int tick(int mask) {
    if (objects_mode && !gl_active) mask &= 15;
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(tick_blocks[mask & 63], "tick");
#ifdef DOOM_S1_PROFILE
    prof_collect(&prof_tick);
#endif
    if (rc == 0) sim_tick++;
    return rc;
}

static int render_spans(Spans *s, long *ns) {
    int vals[RENDER_INTS];
    Capture c;
    long t0 = now_ns();
    capture_begin(&c);
#ifdef DOOM_S1_PROFILE
    prof_begin();
#endif
    int rc = run_block(render_blocks[0], "render");
#ifdef DOOM_S1_PROFILE
    if (rc == 0) prof_collect(&prof_wall);
    if (rc == 0) prof_begin();
#endif
    if (rc == 0) rc = run_block(render_blocks[1], "render");
#ifdef DOOM_S1_PROFILE
    if (rc == 0) prof_collect(&prof_wall);
#endif
    capture_end(&c);
    long t1 = now_ns();
    if (ns) *ns = t1 - t0;
    if (rc != 0) { free(c.buf); return -1; }
    rc = parse_ints(&c, vals, RENDER_INTS, "render");
    if (rc == 0) {
        for (int i = 0; i < PROJ_COLS; i++) {
            s->top[i] = vals[i * 5];
            s->bot[i] = vals[i * 5 + 1];
            s->color[i] = vals[i * 5 + 2];
            s->u[i] = vals[i * 5 + 3];
            int meta = vals[i * 5 + 4];
            s->shade[i] = meta & 3;
            s->material[i] = meta >> 2;
        }
        s->hash = fnv1a(vals, sizeof vals);
    }
    free(c.buf);
    return rc;
}

/* ---- Tests ------------------------------------------------------------- */

static int expect_near(const char *name, int got, int want, int tol) {
    if (got < want - tol || got > want + tol) {
        printf("FAIL %-24s got=%d want=%d+/-%d\n", name, got, want, tol);
        return 1;
    }
    printf("PASS %-24s got=%d\n", name, got);
    return 0;
}
static int expect_eq(const char *name, int got, int want) {
    return expect_near(name, got, want, 0);
}

static int run_ticks(int mask, int n) {
    for (int i = 0; i < n; i++) if (tick(mask) != 0) return -1;
    return 0;
}

static int approach_closed_door(void) {
    if (reset() != 0) return -1;
    return run_ticks(1, 60);
}
static int open_door_near(void) {
    if (approach_closed_door() != 0) return -1;
    if (run_block(activate_block, "activate") != 0) return -1;
    return run_ticks(0, 12);
}

static int self_test(void) {
    int fails = 0, x, y, a;
    Status st;
    fails += expect_eq("framebuffer width", FB_W, 320);
    fails += expect_eq("framebuffer height", FB_H, 200);
    fails += expect_eq("wall ray count", PROJ_COLS, 160);
    fails += expect_eq("pixels per wall ray", PROJ_SCALE, 2);
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;

    if (reset() != 0 || get_status(&st) != 0) return 2;
    fails += expect_eq("start x", (get_state(&x,&y,&a), x), 320);
    fails += expect_eq("start y", y, 320);
    fails += expect_eq("start angle", a, 0);
    fails += expect_eq("door initially closed progress", st.door_open, 0);
    fails += expect_eq("door initially blocked", st.door_blocked, 1);
    fails += expect_eq("door initially target", st.door_target, 0);
    fails += expect_eq("alien initial x", st.ax, 1120);
    fails += expect_eq("alien initial y", st.ay, 320);
    fails += expect_eq("alien initial health", st.health, 100);
    fails += expect_eq("Glon-owned ammunition", st.ammo, 24);
    fails += expect_eq("alien initial state", st.state, 0);
    fails += expect_eq("alien initially undetected", st.detected, 0);

    reset(); run_ticks(1, 10); get_state(&x, &y, &a);
    fails += expect_eq("forward x", x, 400); fails += expect_eq("forward y", y, 320);
    reset(); run_ticks(2, 10); get_state(&x, &y, &a);
    fails += expect_eq("backward x", x, 240);
    reset(); run_ticks(8, 10); get_state(&x, &y, &a);
    fails += expect_eq("strafe right y", y, 240);
    reset(); run_ticks(4, 10); get_state(&x, &y, &a);
    fails += expect_eq("strafe left y", y, 400);
    reset(); run_ticks(16, 1); get_state(&x, &y, &a);
    fails += expect_eq("turn left angle", a, 1);
    run_ticks(32, 1); get_state(&x, &y, &a);
    fails += expect_eq("turn right angle", a, 0);

    reset(); run_ticks(2, 100); get_state(&x, &y, &a);
    fails += expect_near("west wall", x, 152, 8);
    fails += expect_eq("west wall valid", get_valid(), 1);
    reset(); run_ticks(8, 100); get_state(&x, &y, &a);
    fails += expect_near("south wall", y, 152, 8);
    reset(); run_ticks(4, 100); get_state(&x, &y, &a);
    fails += expect_near("north wall", y, 616, 8);
    reset(); run_ticks(4, 15); run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("room east wall", x, 744, 8);
    fails += expect_eq("room east wall valid", get_valid(), 1);
    reset(); run_ticks(4, 15); run_ticks(1, 60); run_ticks(8, 60); run_ticks(1, 40);
    get_state(&x, &y, &a);
    fails += expect_near("corner x", x, 744, 8);
    fails += expect_near("corner y", y, 152, 8);
    fails += expect_eq("corner valid", get_valid(), 1);

    /* Closed door blocks the player before the corridor opening. */
    reset(); run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("closed door collision", x, 744, 8);
    fails += expect_eq("closed door player valid", get_valid(), 1);
    if (get_status(&st) != 0) return 2;
    fails += expect_eq("closed door status", st.door_blocked, 1);

    /* Activating far from the door does nothing. */
    reset(); run_block(activate_block, "activate"); get_status(&st);
    fails += expect_eq("far activation target", st.door_target, 0);

    /* Open the door and move through it. */
    if (open_door_near() != 0) return 2;
    get_status(&st);
    fails += expect_eq("door open progress", st.door_open, 48);
    fails += expect_eq("door open target", st.door_target, 64);
    fails += expect_eq("door open unblocked", st.door_blocked, 0);
    run_ticks(1, 20); get_state(&x, &y, &a);
    fails += expect_near("through open door x", x, 904, 8);
    fails += expect_eq("through open door valid", get_valid(), 1);

    /* Preserved G5a corridor traversal, now through the open door. */
    if (open_door_near() != 0) return 2;
    run_ticks(1, 100); get_state(&x, &y, &a);
    fails += expect_near("corridor end", x, 1256, 8);
    if (open_door_near() != 0) return 2;
    run_ticks(1, 60); run_ticks(16, 64); run_ticks(1, 100);
    get_state(&x, &y, &a);
    fails += expect_eq("corridor traversal x > door", x > 768, 1);
    fails += expect_near("corridor north end y", y, 1000, 8);
    fails += expect_eq("corridor angle", a, 64);
    fails += expect_eq("corridor valid", get_valid(), 1);

    /* Close the door again. */
    if (approach_closed_door() != 0) return 2;
    run_block(activate_block, "activate");
    run_ticks(0, 12);
    run_block(activate_block, "activate");
    run_ticks(0, 12);
    get_status(&st);
    fails += expect_eq("door closed target", st.door_target, 0);
    fails += expect_eq("door closed blocked", st.door_blocked, 1);

    /* Door visibility is consistent with collision state. */
    Spans s;
    long proj_ns;
    reset(); if (render_spans(&s, &proj_ns) != 0) return 2;
    fails += expect_eq("closed door centre colour", s.color[PROJ_COLS / 2], 4);
    fails += expect_eq("closed door hides alien", (get_status(&st), st.visible), 0);
    if (open_door_near() != 0 || render_spans(&s, &proj_ns) != 0) return 2;
    get_status(&st);
    fails += expect_eq("open door centre colour", s.color[PROJ_COLS / 2], 3);
    fails += expect_eq("open door shows alien sprite", st.visible, 1);
    int edge_h = s.bot[0] - s.top[0], centre_h = s.bot[PROJ_COLS/2] - s.top[PROJ_COLS/2];
    fails += expect_eq("open wall occlusion hierarchy", edge_h > centre_h, 1);
    fails += expect_eq("alien sprite rect bounds",
                       st.left < st.right && st.top < st.bot, 1);
    {
        Status fs = st;
        sim_tick = 0;
        alien_view_reset();
        fs.state = 0; fails += expect_eq("alien idle frame", alien_frame_index(&fs), ALIEN_DIR_IDLE);
        fs.state = 2; fails += expect_eq("alien attack frame", alien_frame_index(&fs), ALIEN_DIR_ATTACK);

        st_current_angle = 0; /* camera forward +x; right is -y */
        alien_view_reset();
        fs.state = 1; fs.ax = 1200; fs.ay = 700; fs.tside = 0; fs.tdepth = 600;
        (void)alien_frame_index(&fs);
        for (int i = 0; i < 4; i++) { fs.ax -= 2; (void)alien_frame_index(&fs); }
        int front_idx = alien_frame_index(&fs);
        fails += expect_eq("alien direct approach is frontal",
                           front_idx == ALIEN_DIR_FRONT1 || front_idx == ALIEN_DIR_FRONT2, 1);

        alien_view_reset(); fs.ax = 1200; fs.ay = 700; fs.tside = 250; fs.tdepth = 600;
        (void)alien_frame_index(&fs);
        for (int i = 0; i < 4; i++) { fs.ax -= 2; fs.ay += 2; (void)alien_frame_index(&fs); }
        fails += expect_eq("alien diagonal approach is three-quarter",
                           alien_frame_index(&fs), ALIEN_DIR_THREEQ);

        alien_view_reset(); fs.ax = 1200; fs.ay = 700; fs.tside = 500; fs.tdepth = 600;
        (void)alien_frame_index(&fs);
        for (int i = 0; i < 4; i++) { fs.ay += 2; (void)alien_frame_index(&fs); }
        fails += expect_eq("alien lateral crossing is side", alien_frame_index(&fs), ALIEN_DIR_SIDE);

        alien_view_reset(); fs.ax = 1200; fs.ay = 700; fs.tside = 0; fs.tdepth = 600;
        (void)alien_frame_index(&fs);
        for (int i = 0; i < 4; i++) { fs.ax += 2; (void)alien_frame_index(&fs); }
        fails += expect_eq("alien retreat is rear", alien_frame_index(&fs), ALIEN_DIR_REAR);

        alien_view_reset();
        fs.state = 0; fs.visible = 1; fs.left = 100; fs.right = 180; fs.top = 30; fs.bot = 150;
        uint32_t *tfb = malloc((size_t)FB_W * FB_H * sizeof *tfb);
        for (int i = 0; i < FB_W * FB_H; i++) tfb[i] = 0xff112233u;
        draw_alien_sprite(tfb, &fs);
        fails += expect_eq("alien sprite transparent corner", tfb[0], 0xff112233u);
        int changed = 0;
        for (int i = 0; i < FB_W * FB_H; i++) if (tfb[i] != 0xff112233u) changed++;
        fails += expect_eq("alien sprite draws pixels", changed > 0, 1);
        free(tfb);
    }

    /* Alien detection/movement: it approaches but respects the closed door. */
    if (approach_closed_door() != 0) return 2;
    get_status(&st);
    fails += expect_eq("alien detected near door", st.detected, 1);
    fails += expect_eq("alien detects in range state", st.state >= 1, 1);
    int ax_before = st.ax;
    run_ticks(0, 100);
    get_status(&st);
    fails += expect_eq("alien waits behind closed door", st.ax, ax_before);
    fails += expect_eq("alien blocked state not attack", st.state, 1);

    /* After opening, the Alien can approach and reach attack range. */
    if (open_door_near() != 0) return 2;
    get_status(&st);
    fails += expect_eq("open door reveals alien", st.visible, 1);
    int ax_open = st.ax;
    run_ticks(0, 60);
    get_status(&st);
    fails += expect_eq("alien approaches after opening", st.ax < ax_open, 1);
    run_ticks(0, 120);
    get_status(&st);
    fails += expect_eq("alien proximity attack state", st.state, 2);
    fails += expect_eq("alien proximity within attack range", st.td2 <= 9216, 1);
    fails += expect_eq("alien/player valid after proximity", get_valid(), 1);

    /* Tracker direction is non-zero when the Alien is off-axis. */
    if (open_door_near() != 0) return 2;
    run_ticks(4, 10); /* strafe north while facing east */
    get_status(&st);
    fails += expect_eq("tracker detected", st.detected, 1);
    fails += expect_eq("tracker direction non-zero", st.tside != 0, 1);
    fails += expect_eq("tracker distance non-zero", st.td2 > 0, 1);

    int probes[11];
    if (reset() != 0 || run_capture(collision_block, probes, 11, "collision probes") != 0) return 2;
    for (int i = 0; i < 10; i++) {
        char name[32]; snprintf(name, sizeof name, "linedef %d collision", i);
        fails += expect_eq(name, probes[i], 1);
    }
    fails += expect_eq("clear space", probes[10], 0);

    int aprobes[11];
    if (run_capture(alien_collision_block, aprobes, 11, "alien collision probes") != 0) return 2;
    for (int i = 0; i < 10; i++) {
        char name[40]; snprintf(name, sizeof name, "alien linedef %d collision", i);
        fails += expect_eq(name, aprobes[i], 1);
    }
    fails += expect_eq("alien clear space", aprobes[10], 0);

    int los;
    if (run_capture(los_wall_block, &los, 1, "wall los") != 0) return 2;
    fails += expect_eq("alien wall occlusion", los, 1);
    if (run_capture(los_clear_block, &los, 1, "clear los") != 0) return 2;
    fails += expect_eq("alien clear line of sight", los, 0);
    reset();

    Spans sa, sb, sf;
    reset();
    if (render_spans(&sa, &proj_ns) != 0) return 2;
    reset();
    if (render_spans(&sb, NULL) != 0) return 2;
    int bounds = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (sa.top[i] < 0 || sa.bot[i] > FB_H - 1 || sa.top[i] > sa.bot[i] ||
            sa.color[i] < 1 || sa.color[i] > 4 ||
            sa.shade[i] < 0 || sa.shade[i] > 3 ||
            sa.u[i] < 0 || sa.u[i] > 2048) bounds = 0;
    fails += expect_eq("framebuffer spans", bounds, 1);
    fails += expect_eq("wall panel world anchor", sa.u[PROJ_COLS / 2], 320);
    fails += expect_eq("wall panel u not screen x", sa.u[PROJ_COLS / 2] != 160, 1);
    fails += expect_eq("wall shade far/near level", sa.shade[PROJ_COLS / 2], 2);
    fails += expect_eq("edge wall visible", sa.color[0], 1);
    fails += expect_eq("door material selected", sa.material[PROJ_COLS / 2], 5);
    int material_ok = 1;
    for (int i = 0; i < PROJ_COLS; i++)
        if (sa.material[i] < 0 || sa.material[i] > 5) material_ok = 0;
    fails += expect_eq("wall material bounds", material_ok, 1);
    fails += expect_eq("wall material deterministic",
                       fnv1a(sa.material, sizeof sa.material) ==
                       fnv1a(sb.material, sizeof sb.material), 1);
    fails += expect_eq("identical frame wall hash", sa.hash == sb.hash, 1);
    fails += expect_eq("deterministic shading", sa.shade[0] == sb.shade[0] &&
                       sa.shade[PROJ_COLS / 2] == sb.shade[PROJ_COLS / 2], 1);
    fails += expect_eq("panel u spans world surface", sa.u[0] != sa.u[PROJ_COLS - 1], 1);

    /* Translation changes floor projections but not the centre door's world u. */
    reset(); run_ticks(1, 10);
    if (render_spans(&sf, NULL) != 0) return 2;
    fails += expect_eq("anchored door panel u under translation", sf.u[PROJ_COLS / 2], 320);
    fails += expect_eq("nearer door shade increases", sf.shade[PROJ_COLS / 2] >= sa.shade[PROJ_COLS / 2], 1);

    /* Turning changes projected ceiling detail. */
    reset(); run_ticks(16, 1);
    if (render_spans(&sf, NULL) != 0) return 2;
    reset(); run_ticks(1, 30);
    if (render_spans(&sf, NULL) != 0) return 2;
    fails += expect_eq("distance shading threshold", sf.shade[PROJ_COLS / 2], 3);
    fails += expect_eq("near shade brighter", sf.shade[PROJ_COLS / 2] > sa.shade[PROJ_COLS / 2], 1);

    /* Texture coordinate and framebuffer checks. */
    TexRows tr0, tr1, tr2;
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_texrows(&tr1) != 0 || get_status(&st) != 0) return 2;
    fails += expect_eq("texture row count/valid", tr0.valid[99] && !tr0.valid[100] &&
                       tr0.valid[101], 1);
    fails += expect_eq("texture rows deterministic", tr0.hash == tr1.hash, 1);
    int row_shade_ok = 1;
    for (int y = 0; y < FB_H; y++)
        if (tr0.shade[y] < 0 || tr0.shade[y] > 3) row_shade_ok = 0;
    fails += expect_eq("texture row shade bounds", row_shade_ok, 1);
    fails += expect_eq("wall texture v top", wall_tex_v(10, 10, 20), 0);
    fails += expect_eq("wall texture v bottom", wall_tex_v(20, 10, 20), 63);
    fails += expect_eq("wall texture v midpoint", wall_tex_v(15, 10, 20), 32);

    int wall_base = sa.bot[PROJ_COLS / 2];
    int wall_top = sa.top[PROJ_COLS / 2];
    int centre_sx = (PROJ_COLS / 2) * PROJ_SCALE + PROJ_SCALE - 1;
    int64_t fwx = (int64_t)tr0.bx[wall_base] + (int64_t)tr0.sx[wall_base] * centre_sx;
    int64_t fwy = (int64_t)tr0.by[wall_base] + (int64_t)tr0.sy[wall_base] * centre_sx;
    int64_t cwx = (int64_t)tr0.bx[wall_top] + (int64_t)tr0.sx[wall_top] * centre_sx;
    int64_t cwy = (int64_t)tr0.by[wall_top] + (int64_t)tr0.sy[wall_top] * centre_sx;
    fails += expect_near("floor/wall base world x", (int)(fwx >> 16), 768, 24);
    fails += expect_near("floor/wall base world y", (int)(fwy >> 16), 320, 24);
    fails += expect_near("ceiling/wall top world x", (int)(cwx >> 16), 768, 24);
    fails += expect_near("ceiling/wall top world y", (int)(cwy >> 16), 320, 24);

    reset(); run_ticks(1, 10); get_texrows(&tr2);
    fails += expect_eq("floor/ceiling rows move with translation", tr2.hash != tr0.hash, 1);
    reset(); run_ticks(16, 1); get_texrows(&tr2);
    fails += expect_eq("floor/ceiling rows respond to turn", tr2.hash != tr0.hash, 1);

    /* Native textured rasterisation hashes. */
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) return 2;
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    uint32_t fh1 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    reset();
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    uint32_t fh2 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("deterministic textured framebuffer", fh1 == fh2, 1);
    reset(); run_ticks(1, 10);
    if (render_spans(&sa, NULL) != 0 || get_texrows(&tr0) != 0 ||
        get_status(&st) != 0) { free(fb); return 2; }
    draw_buffer(fb, &sa, NULL, &tr0, &st, floor_c, ceil_c);
    fails += expect_eq("textured framebuffer changes with movement",
                       fh1 != fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), 1);
    free(fb);

    /* Determinism includes door, Alien and sprite status. */
    reset(); run_ticks(1, 60); run_block(activate_block, "activate");
    run_ticks(0, 12); run_ticks(1, 20);
    get_state(&x, &y, &a); get_status(&st); render_spans(&s, NULL);
    int x1=x, y1=y, a1=a, ax1=st.ax, ay1=st.ay, st1=st.state, vis1=st.visible;
    uint32_t h1=s.hash, sh1=fnv1a(&st, sizeof st);
    reset(); run_ticks(1, 60); run_block(activate_block, "activate");
    run_ticks(0, 12); run_ticks(1, 20);
    get_state(&x, &y, &a); get_status(&st); render_spans(&s, NULL);
    fails += expect_eq("repeated simulation determinism",
                       x1==x && y1==y && a1==a && ax1==st.ax && ay1==st.ay &&
                       st1==st.state && vis1==st.visible && h1==s.hash &&
                       sh1==fnv1a(&st, sizeof st), 1);

    cell loader0 = M[GC_LOADER_HP];
    for (int i = 0; i < 20; i++) {
        if (render_spans(&s, NULL) != 0 || get_status(&st) != 0) return 2;
        if (run_ticks(i & 63, 1) != 0) return 2;
    }
    cell loader1 = M[GC_LOADER_HP];
    fails += expect_eq("repeated frames loader growth", loader1 - loader0, 0);
    printf("INFO floor/ceiling colours: %d/%d; loader cells: %ld\n",
           floor_c, ceil_c, (long)loader0);

    long session_loader[3];
    for (int k = 0; k < 3; k++) {
        load_session();
        session_loader[k] = M[GC_LOADER_HP];
    }
    fails += expect_eq("repeated session loader", session_loader[0] == session_loader[1] &&
                         session_loader[1] == session_loader[2], 1);

    printf("INFO managed heap high=%ld gc=%ld live=%ld\n",
           r0_s1_heap_high(), r0_s1_gc_count(), r0_s1_gc_live_cells());
    printf("self-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

static void build_projection_blocks(void) {
    reset_block = parse_source("[ doom/reset ]", "reset");
    floor_line_block = parse_source("[ doom/detail-line 768 256 768 384 0 5 ]", "floor wall base");
    ceil_line_block = parse_source("[ doom/detail-line 768 256 768 384 128 5 ]", "ceiling wall top");
    if (!gl_active) for (int b = 0; b < 2; b++) {
        size_t cap = 256 + (PROJ_COLS / 2) * 64;
        char *src = malloc(cap);
        if (!src) fail("out of memory building projection render block");
        size_t used = 0;
        used += (size_t)snprintf(src + used, cap - used, "[ ");
        for (int i = b * (PROJ_COLS / 2); i < (b + 1) * (PROJ_COLS / 2); i++) {
            int sx = i * PROJ_SCALE + PROJ_SCALE / 2;
            used += (size_t)snprintf(src + used, cap - used,
                                     "doom/sx: %d do %s ", sx,
                                     raw_wall_mode ? "doom/raw-column-block"
                                                   : "doom/render-column-block");
        }
        snprintf(src + used, cap - used, "]");
        render_blocks[b] = parse_source(src, "projection render");
        free(src);
    }
}

static int projection_test(void) {
    load_session();
    load_file("tests/detail.glon");
    detail_block = parse_source("[ do doom/detail-block ]", "detail");
    build_projection_blocks();
    int fails = 0;
    Spans sf;
    const int cams[][3] = {
        {500, 320, 0}, {550, 180, 32}, {700, 150, 64},
        {600, 150, 96}, {900, 320, 128}
    };
    Detail d1, d2, df;
    reset();
    if (get_detail(&d1) != 0) return 2;
    reset();
    if (get_detail(&d2) != 0) return 2;
    fails += expect_eq("detail line count", d1.n, 4);
    fails += expect_eq("identical frame detail hash", d1.hash == d2.hash, 1);
    reset();
    if (run_block(parse_source("[ doom/px: 330 doom/aim ]", "detail translate"), "detail translate") != 0 ||
        get_detail(&df) != 0) return 2;
    fails += expect_eq("floor detail moves on translation", df.hash != d1.hash, 1);
    reset();
    if (run_block(parse_source("[ doom/angle: 1 doom/aim ]", "detail turn"), "detail turn") != 0 ||
        get_detail(&df) != 0) return 2;
    fails += expect_eq("ceiling detail responds to turn", df.hash != d1.hash, 1);
    int visible_checks = 0, aligned = 0;
    int angle_seen[5] = {0,0,0,0,0};
    for (size_t ci = 0; ci < sizeof cams / sizeof cams[0]; ci++) {
        char src[160];
        snprintf(src, sizeof src, "[ doom/px: %d doom/py: %d doom/angle: %d doom/aim ]",
                 cams[ci][0], cams[ci][1], cams[ci][2]);
        cell cam = parse_source(src, "camera");
        if (run_block(cam, "camera") != 0 || render_spans(&sf, NULL) != 0) return 2;
        int lf[6], lc[6];
        if (run_capture(floor_line_block, lf, 6, "floor wall base") != 0 ||
            run_capture(ceil_line_block, lc, 6, "ceiling wall top") != 0) return 2;
        int ai = (cams[ci][2] + 16) / 32;
        if (ai > 4) ai = 4;
        for (int which = 0; which < 2; which++) {
            int *ln = which ? lc : lf;
            if (!ln[0]) continue;
            int xm = (ln[1] + ln[3]) / 2;
            int ym = (ln[2] + ln[4]) / 2;
            if (xm < 0 || xm >= FB_W || ym < 0 || ym >= FB_H) continue;
            int col = xm * PROJ_COLS / FB_W;
            if (col < 0) col = 0;
            if (col >= PROJ_COLS) col = PROJ_COLS - 1;
            if (sf.color[col] != 4) continue;
            int wall_y = which ? sf.top[col] : sf.bot[col];
            if (wall_y < 0 || wall_y >= FB_H) continue;
            visible_checks++;
            int d = ym > wall_y ? ym - wall_y : wall_y - ym;
            if (d <= 4) aligned++;
            else printf("FAIL %s alignment angle=%d pos=%d,%d line_y=%d wall_y=%d\n",
                        which ? "ceiling" : "floor", cams[ci][2],
                        cams[ci][0], cams[ci][1], ym, wall_y);
            angle_seen[ai] = 1;
        }
    }
    fails += expect_eq("floor/ceiling wall alignment samples", aligned, visible_checks);
    printf("INFO projection alignment: visible=%d aligned=%d angles=%d%d%d%d%d\n",
           visible_checks, aligned, angle_seen[0], angle_seen[1], angle_seen[2],
           angle_seen[3], angle_seen[4]);
    {
        cell forward_line = parse_source("[ doom/detail-line 100 320 1000 320 0 5 ]", "near clip");
        cell reverse_line = parse_source("[ doom/detail-line 1000 320 100 320 0 5 ]", "near clip reverse");
        int a6[6], b6[6];
        reset();
        if (run_capture(forward_line, a6, 6, "near clip") != 0 ||
            run_capture(reverse_line, b6, 6, "near clip reverse") != 0) return 2;
        fails += expect_eq("near-plane clipped line visible", a6[0], 1);
        fails += expect_eq("reversed line visible", b6[0], 1);
        fails += expect_eq("endpoint order independent", a6[0] == b6[0], 1);
    }
    printf("projection-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

/* ---- Scripted trace ---------------------------------------------------- */

static int scripted_trace(void) {
    load_session();
    build_blocks();
    Status st;
    reset();
    int x, y, a;
    get_state(&x, &y, &a);
    printf("trace start: x=%d y=%d angle=%d\n", x, y, a);
    struct { int mask, n; const char *name; } steps[] = {
        {1, 60, "approach-closed-door"}, {0, 12, "activate-door"},
        {0, 60, "observe-alien"}, {1, 20, "enter-corridor"},
        {16, 64, "turn-left-90"}, {4, 15, "strafe-left"}
    };
    for (size_t i = 0; i < sizeof steps / sizeof steps[0]; i++) {
        if (i == 1) {
            if (run_block(activate_block, "activate") != 0) return 2;
        }
        if (run_ticks(steps[i].mask, steps[i].n) != 0) return 2;
        get_state(&x, &y, &a);
        if (get_status(&st) != 0) return 2;
        printf("trace %-20s x=%d y=%d angle=%d door=%d/%d alien=%d,%d state=%d det=%d vis=%d\n",
               steps[i].name, x, y, a, st.door_open, st.door_blocked,
               st.ax, st.ay, st.state, st.detected, st.visible);
    }
    return 0;
}

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c);

/* ---- Deterministic screenshot walkthrough ------------------------------ */

static int save_ppm(const char *path, const uint32_t *fb) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", FB_W, FB_H);
    for (int i = 0; i < FB_W * FB_H; i++) {
        uint32_t c = fb[i];
        unsigned char rgb[3] = { (unsigned char)((c >> 16) & 255),
                                 (unsigned char)((c >> 8) & 255),
                                 (unsigned char)(c & 255) };
        if (fwrite(rgb, 1, 3, f) != 3) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

static int shot_walkthrough(const char *dir) {
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    char path[4096];
    const char *names[] = { "room", "near-door", "corridor", "alien", "inside", "turn" };
    int stage = 0;
    Spans s; Detail d; TexRows tr; Status st;
    for (;;) {
        if (render_spans(&s, NULL) != 0 || (need_texrows() && get_texrows(&tr) != 0) || get_status(&st) != 0) {
            free(fb); return 2;
        }
        draw_buffer(fb, &s, &d, &tr, &st, floor_c, ceil_c);
        snprintf(path, sizeof path, "%s/%s.ppm", dir, names[stage]);
        if (save_ppm(path, fb) != 0) { free(fb); return 2; }
        int px, py, pa;
        get_state(&px, &py, &pa);
        printf("shot %s: player=%d,%d door=%d/%d alien=%d,%d state=%d vis=%d\n",
               names[stage], px, py, st.door_open, st.door_blocked,
               st.ax, st.ay, st.state, st.visible);
        stage++;
        if (stage == 1) { if (run_ticks(1, 37) != 0) break; }
        else if (stage == 2) { if (run_block(activate_block, "activate") != 0) break; if (run_ticks(0, 12) != 0) break; }
        else if (stage == 3) { if (run_ticks(0, 60) != 0) break; }
        else if (stage == 4) { if (run_ticks(1, 48) != 0) break; }
        else if (stage == 5) { if (run_ticks(1, 25) != 0 || run_ticks(16, 64) != 0) break; }
        else break;
    }
    free(fb);
    return stage == 6 ? 0 : 2;
}

/* ---- SDL presentation -------------------------------------------------- */

static uint32_t palette(int id) {
    switch (id) {
    case 1: return 0xff4a5059u;  /* steel room wall */
    case 2: return 0xff66707au;  /* lighter corridor steel */
    case 3: return 0xff3b5570u;  /* blue turn wall */
    case 4: return 0xffa47b3bu;  /* amber door */
    case 5: return 0xff4d5963u;  /* floor/ceiling grid */
    case 6: return 0xff242a2fu;  /* floor base */
    case 7: return 0xff151d24u;  /* ceiling base */
    case 8: return 0xffcaa84fu;  /* floor centreline */
    case 9: return 0xffeaf4dcu;  /* overhead light */
    case 10: return 0xff3f8b90u; /* edge marking */
    default: return 0xff101010u;
    }
}

static uint32_t scale_color(uint32_t c, int pct) {
    int r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    r = r * pct / 100; g = g * pct / 100; b = b * pct / 100;
    return 0xff000000u | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}
static uint32_t shade_color(uint32_t c, int shade) {
    static const int pct[4] = { 48, 64, 82, 100 };
    if (shade < 0) shade = 0;
    if (shade > 3) shade = 3;
    return scale_color(c, pct[shade]);
}

static void plot(uint32_t *fb, int x, int y, uint32_t c) {
    if (x >= 0 && x < FB_W && y >= 0 && y < FB_H) fb[y * FB_W + x] = c;
}

static const uint32_t *wall_texture(int material) {
    if (new_textures) {
        if (material == 0 || material == 2 || material == 4) return tex_panel_v2;
        if (material == 1 || material == 3) return tex_service_v2;
    }
    switch (material) {
    case 0: return tex_wall_panel;
    case 1: return tex_wall_service;
    case 2: return tex_vent_grille;
    case 3: return tex_rib;
    case 4: return tex_pipes;
    case 5: return tex_door;
    default: return tex_wall_panel;
    }
}

static int wall_tex_v(int y, int top, int bot) {
    int den = bot - top;
    if (den <= 0) return 0;
    int v = (y - top) * 64 / den;
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    return v;
}

static int smooth_wall_pct(int top, int bot, int shade) {
    int h = bot - top + 1;
    int pct;
    if (h >= 123) pct = 100;
    else if (h >= 61) pct = 82 + (h - 61) * 18 / 62;
    else if (h >= 31) pct = 64 + (h - 31) * 18 / 30;
    else pct = 48 + h * 16 / 31;
    (void)shade;
    return pct;
}

static uint32_t checker_tex(int u, int v) {
    int cell = ((u >> 3) + (v >> 3)) & 1;
    int minor = ((u & 7) == 0 || (v & 7) == 0);
    int major = ((u & 31) == 0 || (v & 31) == 0);
    int grad = ((u + v) * 19) & 31;
    if (major) return 0xfffff0c0u;
    if (minor) return 0xffd8d0b0u;
    int r = 40 + grad, g = 58 + grad, b = 66 + grad;
    if (cell) { r = 150 - grad; g = 65 + grad / 2; b = 52; }
    else { r = 35; g = 74 + grad; b = 82 + grad; }
    return 0xff000000u | ((uint32_t)r<<16) | ((uint32_t)g<<8) | (uint32_t)b;
}

static uint32_t sample_wall_tex(int material, int u, int y, int top, int bot, int shade) {
    const uint32_t *tex = wall_texture(material);
    if (checker_walls) {
        int tu = (u >> 2) & 63;
        int tv = wall_tex_v(y, top, bot);
        return shade_color(checker_tex(tu, tv), shade);
    }
    int tu = (material == 5) ? (u - 256) * 64 / 128 : (u >> 2) & 63;
    if (tu < 0) tu = 0;
    if (tu > 63) tu = 63;
    int v = wall_tex_v(y, top, bot);
    uint32_t base = tex[v * 64 + tu];
    if (smooth_shading) return scale_color(base, smooth_wall_pct(top, bot, shade));
    return shade_color(base, shade);
}

static void draw_tex_planes(uint32_t *fb, const TexRows *r) {
    for (int y = 0; y < FB_H; y++) {
        if (!r->valid[y]) continue;
        const uint32_t *tex = (y > FB_H / 2) ? tex_floor : tex_ceiling_light;
        int shade = r->shade[y];
        for (int x = 0; x < FB_W; x++) {
            int64_t wx = (int64_t)r->bx[y] + (int64_t)r->sx[y] * x;
            int64_t wy = (int64_t)r->by[y] + (int64_t)r->sy[y] * x;
            int tx = (int)((wx >> 16) >> 2) & 63;
            int ty = (int)((wy >> 16) >> 2) & 63;
            uint32_t c = tex[ty * 64 + tx];
            fb[y * FB_W + x] = shade_color(c, shade);
        }
    }
}

static uint32_t shade_argb(uint32_t c, int pct) {
    int a = (int)(c >> 24), r = (int)((c >> 16) & 255), g = (int)((c >> 8) & 255), b = (int)(c & 255);
    r = r * pct / 100; g = g * pct / 100; b = b * pct / 100;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b;
}

typedef struct { int idx, flip; } AlienFrame;

static int alien_prev_ax = -999999, alien_prev_ay = -999999;
static double alien_mvx = 0.0, alien_mvy = 0.0;
static int alien_dir_kind = ALIEN_DIR_IDLE, alien_dir_flip = 0;
static int alien_dir_pending = -1, alien_dir_pending_n = 0;

static void alien_view_reset(void) {
    alien_prev_ax = alien_prev_ay = -999999;
    alien_mvx = alien_mvy = 0.0;
    alien_dir_kind = ALIEN_DIR_IDLE;
    alien_dir_flip = 0;
    alien_dir_pending = -1;
    alien_dir_pending_n = 0;
}

/* Select artwork from the Alien's smoothed world movement (approach/retreat)
 * plus its bearing relative to the camera (front/three-quarter/side). */
static AlienFrame alien_frame_select(const Status *st) {
    AlienFrame out = { ALIEN_DIR_IDLE, 0 };
    if (st->state == 2) {
        alien_dir_kind = ALIEN_DIR_ATTACK;
        alien_dir_pending = -1;
        alien_dir_pending_n = 0;
        out.idx = ALIEN_DIR_ATTACK;
        return out;
    }
    if (st->state == 0) {
        alien_dir_kind = ALIEN_DIR_IDLE;
        alien_dir_flip = 0;
        return out;
    }
    if (alien_prev_ax == -999999) {
        alien_prev_ax = st->ax;
        alien_prev_ay = st->ay;
        return out;
    }
    int dax = st->ax - alien_prev_ax;
    int day = st->ay - alien_prev_ay;
    alien_prev_ax = st->ax;
    alien_prev_ay = st->ay;
    if (abs(dax) > 24 || abs(day) > 24) {
        alien_mvx = alien_mvy = 0.0;
        dax = day = 0;
    }
    alien_mvx = alien_mvx * 0.78 + dax * 0.22;
    alien_mvy = alien_mvy * 0.78 + day * 0.22;

    double ang = st_current_angle * 2.0 * M_PI / 256.0;
    double fx = cos(ang), fy = sin(ang);
    double rx = sin(ang), ry = -cos(ang);
    double fdot = alien_mvx * fx + alien_mvy * fy;
    double rdot = alien_mvx * rx + alien_mvy * ry;
    double mag = sqrt(alien_mvx * alien_mvx + alien_mvy * alien_mvy);
    double sidemag = fabs((double)st->tside) / (fabs((double)st->tdepth) > 1.0 ? fabs((double)st->tdepth) : 1.0);
    int cand;
    if (fdot > 0.15) {
        cand = ALIEN_DIR_REAR;
    } else if (mag >= 0.35 && fabs(fdot) <= 0.15) {
        cand = ALIEN_DIR_SIDE;
    } else if (sidemag > 0.48) {
        cand = ALIEN_DIR_SIDE;
    } else if (sidemag > 0.28) {
        cand = ALIEN_DIR_THREEQ;
    } else {
        cand = ALIEN_DIR_FRONT1;
    }
    int flip = (rdot < -0.05) ? 1 : (rdot > 0.05 ? 0 : (st->tside > 0));
    if (cand != alien_dir_kind) {
        if (cand == alien_dir_pending) alien_dir_pending_n++;
        else { alien_dir_pending = cand; alien_dir_pending_n = 1; }
        if (alien_dir_pending_n >= 2) {
            alien_dir_kind = cand;
            alien_dir_flip = (cand == ALIEN_DIR_SIDE || cand == ALIEN_DIR_THREEQ) ? flip : 0;
            alien_dir_pending = -1;
            alien_dir_pending_n = 0;
        }
    } else {
        alien_dir_pending = -1;
        alien_dir_pending_n = 0;
        if (cand == ALIEN_DIR_SIDE || cand == ALIEN_DIR_THREEQ) alien_dir_flip = flip;
    }
    out.idx = alien_dir_kind;
    out.flip = alien_dir_flip;
    if (out.idx == ALIEN_DIR_FRONT1)
        out.idx = ((sim_tick / 8) & 1) ? ALIEN_DIR_FRONT2 : ALIEN_DIR_FRONT1;
    return out;
}

static int alien_frame_index(const Status *st) {
    return alien_frame_select(st).idx;
}

static void alien_sprite_box(const Status *st, AlienFrame af, int *l, int *r, int *t, int *b) {
    int sw = alien_dir_w[af.idx], sh = alien_dir_h[af.idx];
    int eh = alien_dir_eff_h[af.idx];
    int h = (st->bot - st->top + 1) * sh / eh;
    int w = h * sw / sh;
    int cx = (st->left + st->right) / 2;
    int px = alien_dir_pivot_x[af.idx] * h / sh;
    int anchor = alien_dir_anchor_y[af.idx];
    *l = cx - px;
    *r = *l + w - 1;
    *b = st->bot;
    *t = *b - (anchor - 1) * h / sh;
}

static const uint32_t *object_sprite(int id) {
    switch (id) { case 0: return obj_drum; case 1: return obj_crate;
                  case 2: return obj_cabinet; default: return obj_container; }
}

static void draw_sprite_rect(uint32_t *fb, const uint32_t *src, int sw, int sh,
                             int l, int r, int t, int b, int pct, int flip) {
    if (l < 0) l = 0;
    if (r >= FB_W) r = FB_W - 1;
    if (t < 0) t = 0;
    if (b >= FB_H) b = FB_H - 1;
    int dw = r - l + 1, dh = b - t + 1;
    if (dw <= 0 || dh <= 0) return;
    for (int y = t; y <= b; y++) {
        int sy = (y - t) * sh / dh;
        if (sy < 0) sy = 0;
        if (sy >= sh) sy = sh - 1;
        for (int x = l; x <= r; x++) {
            int sx = (x - l) * sw / dw;
            if (sx < 0) sx = 0;
            if (sx >= sw) sx = sw - 1;
            if (flip) sx = sw - 1 - sx;
            uint32_t c = src[sy * sw + sx];
            int a = (int)(c >> 24);
            if (!a) continue;
            c = shade_argb(c, pct);
            if (a == 255) { fb[y * FB_W + x] = c; continue; }
            int da = 255 - a;
            int dr = (int)((fb[y*FB_W+x] >> 16) & 255);
            int dg = (int)((fb[y*FB_W+x] >> 8) & 255);
            int db = (int)(fb[y*FB_W+x] & 255);
            int rr = ((int)((c >> 16) & 255) * a + dr * da) / 255;
            int gg = ((int)((c >> 8) & 255) * a + dg * da) / 255;
            int bb = ((int)(c & 255) * a + db * da) / 255;
            fb[y*FB_W+x] = 0xff000000u | ((uint32_t)rr << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
        }
    }
}

static void draw_alien_sprite(uint32_t *fb, const Status *st) {
    if (!st->visible) return;
    AlienFrame af = alien_frame_select(st);
    int l, r, t, b;
    alien_sprite_box(st, af, &l, &r, &t, &b);
    int pct = st->tdepth < 256 ? 112 : st->tdepth < 512 ? 100 : st->tdepth < 1024 ? 86 : 72;
    draw_sprite_rect(fb, alien_dir_tex[af.idx], alien_dir_w[af.idx], alien_dir_h[af.idx],
                     l, r, t, b, pct, af.flip);
}

static int tracker_blip(const Status *st, int *bx, int *by) {
    const int cx = 42, cy = 168, rad = 22;
    if (!st->detected) { *bx = cx; *by = cy; return 0; }
    int x = cx + st->tside * 18 / 700;
    int y = cy - st->tdepth * 18 / 700;
    if (x < cx - rad + 3) x = cx - rad + 3;
    if (x > cx + rad - 3) x = cx + rad - 3;
    if (y < cy - rad + 3) y = cy - rad + 3;
    if (y > cy + rad - 3) y = cy + rad - 3;
    *bx = x; *by = y; return 1;
}

static void draw_tracker(uint32_t *fb, const Status *st) {
    const int cx = 42, cy = 168, rad = 22;
    for (int y = 142; y < 196; y++)
        for (int x = 6; x < 78; x++) fb[y * FB_W + x] = 0xff0d1117u;
    uint32_t door_c = st->door_blocked ? 0xff9a6b35u : 0xff3eaa58u;
    for (int y = 146; y < 150; y++)
        for (int x = 10; x < 74; x++) fb[y * FB_W + x] = door_c;
    for (int y = -rad; y <= rad; y++)
        for (int x = -rad; x <= rad; x++) {
            int d2 = x * x + y * y;
            if (d2 >= (rad - 2) * (rad - 2) && d2 <= rad * rad)
                plot(fb, cx + x, cy + y, 0xff55635au);
        }
    for (int y = -rad + 3; y <= rad - 3; y++) plot(fb, cx, cy + y, 0xff27322bu);
    for (int x = -rad + 3; x <= rad - 3; x++) plot(fb, cx + x, cy, 0xff27322bu);
    plot(fb, cx, cy - rad + 2, 0xffb8e0a0u);
    int bx, by;
    if (tracker_blip(st, &bx, &by)) {
        uint32_t c = st->state == 2 ? 0xffe05050u : 0xff6fe08au;
        for (int y = -2; y <= 2; y++)
            for (int x = -2; x <= 2; x++) plot(fb, bx + x, by + y, c);
    } else {
        for (int y = -1; y <= 1; y++)
            for (int x = -1; x <= 1; x++) plot(fb, cx + x, cy + y, 0xff4d5c52u);
    }
}

static void draw_buffer(uint32_t *fb, const Spans *s, const Detail *d,
                        const TexRows *tr, const Status *st,
                        int floor_c, int ceil_c) {
    (void)d;
    if (flat_surfaces) {
        for (int y = 0; y < FB_H; y++) {
            int d = y < FB_H / 2 ? (FB_H / 2 - y) : (y - FB_H / 2);
            int pct = 58 + d * 48 / (FB_H / 2);
            uint32_t base = (y < FB_H / 2) ? 0xff26323au : 0xff3b474du;
            uint32_t c = scale_color(base, pct);
            for (int x = 0; x < FB_W; x++) fb[y * FB_W + x] = c;
        }
        for (int x = 0; x < FB_W; x++) fb[(FB_H / 2) * FB_W + x] = 0xff11171bu;
    } else {
        for (int y = 0; y < FB_H; y++)
            for (int x = 0; x < FB_W; x++)
                fb[y * FB_W + x] = (y < FB_H / 2) ? palette(ceil_c) : palette(floor_c);
    }
    if (!flat_surfaces && !simple_mode && tr) draw_tex_planes(fb, tr);
    for (int i = 0; i < PROJ_COLS; i++) {
        int top = s->top[i], bot = s->bot[i];
        if (top < 0) top = 0;
        if (bot >= FB_H) bot = FB_H - 1;
        if (top > bot) continue;
        int color = s->color[i], u = s->u[i], shade = s->shade[i];
        int material = s->material[i];
        uint32_t flat = palette(color);
        for (int y = top; y <= bot; y++) {
            uint32_t c = simple_mode ? shade_color(flat, shade)
                                     : sample_wall_tex(material, u, y, top, bot, shade);
            for (int x = i * PROJ_SCALE; x < (i + 1) * PROJ_SCALE; x++)
                fb[y * FB_W + x] = c;
        }
    }
    long sp0 = now_ns();
    if (objects_mode) {
        if (current_objs_valid) {
            SpriteDraw list[7]; int n = 0;
            for (int i = 0; i < 6; i++) if (current_objs[i].visible) {
                list[n].depth = current_objs[i].depth; list[n].l = current_objs[i].left; list[n].r = current_objs[i].right;
                list[n].t = current_objs[i].top; list[n].b = current_objs[i].bot; list[n].src = object_sprite(current_objs[i].id);
                list[n].sw = OBJ_W; list[n].sh = OBJ_H; list[n].pct = current_objs[i].depth < 256 ? 100 : 86;
                list[n].flip = 0; n++;
            }
            if (st->visible) {
                AlienFrame af = alien_frame_select(st);
                alien_sprite_box(st, af, &list[n].l, &list[n].r, &list[n].t, &list[n].b);
                list[n].depth = st->tdepth; list[n].src = alien_dir_tex[af.idx];
                list[n].sw = alien_dir_w[af.idx]; list[n].sh = alien_dir_h[af.idx];
                list[n].pct = st->tdepth < 256 ? 112 : st->tdepth < 512 ? 100 : 86;
                list[n].flip = af.flip; n++;
            }
            for (int i = 1; i < n; i++) { int j=i; while (j>0 && list[j-1].depth < list[j].depth) { SpriteDraw z=list[j]; list[j]=list[j-1]; list[j-1]=z; j--; } }
            for (int i = 0; i < n; i++) draw_sprite_rect(fb, list[i].src, list[i].sw, list[i].sh, list[i].l, list[i].r, list[i].t, list[i].b, list[i].pct, list[i].flip);
        } else draw_alien_sprite(fb, st);
    } else draw_alien_sprite(fb, st);
    long sp1 = now_ns();
    last_sprite_ms = (double)(sp1 - sp0) / 1e6;
    draw_tracker(fb, st);
}

static void apply_scale_hint(void) {
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, linear_filter ? "1" : "0");
}

static int sdl_present(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                       const Spans *s, const Detail *d, const TexRows *tr,
                       const Status *st, int floor_c, int ceil_c,
                       double *raster_ms, double *present_ms) {
    long r0 = now_ns();
    draw_buffer(fb, s, d, tr, st, floor_c, ceil_c);
    long r1 = now_ns();
    if (raster_ms) *raster_ms = (double)(r1 - r0) / 1e6;
    if (SDL_UpdateTexture(tex, NULL, fb, FB_W * (int)sizeof(uint32_t)) != 0) return -1;
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    long t1 = now_ns();
    if (present_ms) *present_ms = (double)(t1 - r1) / 1e6;
    return 0;
}


/* ---- G5g: controlled benchmarks, view cache and loader diagnostics ------ */

static int set_camera(int x, int y, int angle) {
    char src[160];
    snprintf(src, sizeof src,
             "[ doom/px: %d doom/py: %d doom/angle: %d doom/aim ]", x, y, angle);
    cell b = parse_source(src, "fixed camera");
    return run_block(b, "fixed camera");
}

static double median_of(double *v, int n) {
    for (int i = 1; i < n; i++) {
        double key = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
        v[j + 1] = key;
    }
    return n & 1 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

static void sample_summary(const char *name, double *v, int n, double *mean_out) {
    double sum = 0, min = v[0];
    for (int i = 0; i < n; i++) { sum += v[i]; if (v[i] < min) min = v[i]; }
    double mean = sum / n, var = 0;
    for (int i = 0; i < n; i++) { double d = v[i] - mean; var += d * d; }
    double med = median_of(v, n);
    printf("%s min=%.2f med=%.2f mean=%.2f sd=%.2f\n",
           name, min, med, mean, n > 1 ? sqrt(var / (n - 1)) : 0.0);
    if (mean_out) *mean_out = mean;
}

static int bench_fixed(int frames, int camx, int camy, int cama) {
    if (frames < 1) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5g - fixed benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_HIDDEN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    double *wall = malloc((size_t)frames * sizeof *wall);
    double *row = malloc((size_t)frames * sizeof *row);
    double *raster = malloc((size_t)frames * sizeof *raster);
    double *present = malloc((size_t)frames * sizeof *present);
    double *total = malloc((size_t)frames * sizeof *total);
    if (!fb || !wall || !row || !raster || !present || !total) fail("out of memory");

    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(camx, camy, cama) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s; Detail det; TexRows tr; Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;

    const int warm = 20;
    long w0 = now_ns();
    for (int i = 0; i < warm; i++) {
        long a = now_ns();
        if (render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) return 2;
        (void)a;
    }
    long w1 = now_ns();

    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (render_spans(&s, NULL) != 0) return 2;
        long t1 = now_ns();
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        long t2 = now_ns();
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) return 2;
        long t3 = now_ns();
        wall[i] = (t1 - t0) / 1e6;
        row[i] = (t2 - t1) / 1e6;
        raster[i] = rm; present[i] = pm;
        total[i] = (t3 - t0) / 1e6;
    }
    printf("fixed frames=%d warmup=%d camera=%d,%d,%d mode=%s\n",
           frames, warm, camx, camy, cama, simple_mode ? "simple" : "textured");
    printf("fixed warmup_wall=%.2fms\n", (double)(w1 - w0) / warm / 1e6);
    sample_summary("fixed wall", wall, frames, NULL);
    sample_summary("fixed texrow", row, frames, NULL);
    sample_summary("fixed raster", raster, frames, NULL);
    sample_summary("fixed present", present, frames, NULL);
    sample_summary("fixed total", total, frames, NULL);
    printf("fixed hash spans=%08x rows=%08x status=%08x\n", s.hash,
           simple_mode ? 0 : tr.hash, fnv1a(&st, sizeof st));
    printf("fixed loader=%ld used=%ld free=%ld code=%ld heap_high=%ld gc=%ld\n",
           (long)M[GC_LOADER_HP], (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size(),
           r0_s1_heap_high(), r0_s1_gc_count());

    free(wall); free(row); free(raster); free(present); free(total); free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int cache_frame(ViewCache *c, uint32_t *fb, int floor_c, int ceil_c, int force) {
    int px, py, pa;
    if (get_state(&px, &py, &pa) != 0) return -1;
    Status st;
    if (get_status(&st) != 0) return -1;
    ViewSig sig;
    sig.px = px; sig.py = py; sig.angle = pa; sig.simple = simple_mode;
    sig.floor_c = floor_c; sig.ceil_c = ceil_c;
    sig.st = st; sig.st.light = 0; /* stationary cache ignores the light pulse */
    if (!force && c->valid && memcmp(&sig, &c->sig, sizeof sig) == 0) return 0;
    if (render_spans(&c->s, NULL) != 0) return -1;
    if (need_texrows() && get_texrows(&c->tr) != 0) return -1;
    draw_buffer(fb, &c->s, NULL, simple_mode ? NULL : &c->tr, &st, floor_c, ceil_c);
    c->sig = sig;
    c->valid = 1;
    return 1;
}

static int stationary_test(void) {
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) return 2;
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    ViewCache cache; memset(&cache, 0, sizeof cache);
    int fails = 0;
    int miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h0 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("stationary first frame is a miss", miss, 1);
    int sx0, sy0, sa0; Status st0;
    get_state(&sx0, &sy0, &sa0); get_status(&st0);
    if (tick(0) != 0) return 2;
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    fails += expect_eq("stationary unchanged frame is a hit", miss, 0);
    fails += expect_eq("stationary cached pixels identical",
                       fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), h0);
    int sx1, sy1, sa1; Status st1;
    get_state(&sx1, &sy1, &sa1); get_status(&st1);
    fails += expect_eq("stationary cache does not alter player",
                       sx0 == sx1 && sy0 == sy1 && sa0 == sa1, 1);
    st0.light = st1.light = 0;  /* the light pulse advances independently */
    fails += expect_eq("stationary cache does not alter status",
                       memcmp(&st0, &st1, sizeof st0) == 0, 1);

    if (tick(32) != 0) return 2;  /* turn right */
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h1 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("camera turn invalidates cache", miss, 1);
    fails += expect_eq("camera turn changes pixels", h1 != h0, 1);

    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    memset(&cache, 0, sizeof cache);
    if (cache_frame(&cache, fb, floor_c, ceil_c, 0) < 0) return 2;
    uint32_t hd = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    cell door = parse_source("[ doom/door-target: 64 ]", "door target");
    if (run_block(door, "door target") != 0 || run_ticks(0, 12) != 0) return 2;
    miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
    if (miss < 0) return 2;
    uint32_t h2 = fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb);
    fails += expect_eq("door state invalidates cache", miss, 1);
    fails += expect_eq("door change alters pixels", h2 != hd, 1);

    ViewCache fresh; memset(&fresh, 0, sizeof fresh);
    if (cache_frame(&fresh, fb, floor_c, ceil_c, 1) < 0) return 2;
    fails += expect_eq("forced uncached frame matches cached path",
                       fnv1a(fb, (size_t)FB_W * FB_H * sizeof *fb), h2);

    free(fb);
    printf("stationary-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

static int sdl_present_cached(SDL_Renderer *ren, SDL_Texture *tex, uint32_t *fb,
                              int redraw) {
    if (redraw && SDL_UpdateTexture(tex, NULL, fb, FB_W * (int)sizeof(uint32_t)) != 0)
        return -1;
    SDL_RenderClear(ren);
    SDL_RenderCopy(ren, tex, NULL, NULL);
    SDL_RenderPresent(ren);
    return 0;
}

static int stationary_bench(int frames, int throttle) {
    if (frames < 4) return 2;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5g - stationary benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_HIDDEN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    ViewCache cache; memset(&cache, 0, sizeof cache);
    int hits = 0, misses = 0;
    double hit_ms = 0, miss_ms = 0, sim_ms = 0, present_ms = 0;
    double input_ms = -1;
    struct timespec cpu0, cpu1, wall0, wall1;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu0);
    clock_gettime(CLOCK_MONOTONIC, &wall0);
    for (int i = -5; i < frames; i++) {
        long t0 = now_ns();
        if (tick(0) != 0) return 2;
        int inject = (i == frames / 2);
        if (inject && tick(32) != 0) return 2;
        long t1 = now_ns();
        int miss = cache_frame(&cache, fb, floor_c, ceil_c, 0);
        if (miss < 0) return 2;
        long t2 = now_ns();
        if (sdl_present_cached(ren, tex, fb, miss) != 0) return 2;
        long t3 = now_ns();
        if (i >= 0) {
            if (miss) { misses++; miss_ms += (double)(t3 - t0) / 1e6; }
            else { hits++; hit_ms += (double)(t3 - t0) / 1e6; }
            sim_ms += (double)(t1 - t0) / 1e6;
            present_ms += (double)(t3 - t2) / 1e6;
            if (inject) input_ms = (double)(t3 - t0) / 1e6;
        }
        if (throttle) {
            long spent = t3 - t0;
            long target = 1000000000L / TICK_HZ;
            if (spent < target) SDL_Delay((Uint32)((target - spent + 999999) / 1000000));
        }
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &cpu1);
    clock_gettime(CLOCK_MONOTONIC, &wall1);
    double cpu_ms = ((double)(cpu1.tv_sec - cpu0.tv_sec) * 1e9 +
                     (double)(cpu1.tv_nsec - cpu0.tv_nsec)) / 1e6;
    double wall_ms = ((double)(wall1.tv_sec - wall0.tv_sec) * 1e9 +
                      (double)(wall1.tv_nsec - wall0.tv_nsec)) / 1e6;
    printf("stationary frames=%d throttle=%d hits=%d misses=%d hit_rate=%.1f%%\n",
           frames, throttle, hits, misses, 100.0 * hits / (hits + misses));
    printf("stationary full_frame=%.2fms cached_frame=%.2fms sim=%.2fms present=%.2fms input_to_update=%.2fms\n",
           misses ? miss_ms / misses : 0.0, hits ? hit_ms / hits : 0.0,
           sim_ms / frames, present_ms / frames, input_ms);
    printf("stationary cpu=%.2fms/frame wall=%.2fms cpu_pct=%.1f%%\n",
           cpu_ms / frames, wall_ms / frames, wall_ms > 0 ? 100.0 * cpu_ms / wall_ms : 0.0);
    printf("stationary loader=%ld heap_high=%ld gc=%ld\n",
           (long)M[GC_LOADER_HP], r0_s1_heap_high(), r0_s1_gc_count());
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

static int loader_report(void) {
    init_runtime();
    printf("loader report: base=%ld limit=%ld reserved=%ld\n",
           R0S1_HEAP_BASE, R0S1_HEAP_LIMIT, R0S1_HEAP_LIMIT - R0S1_HEAP_BASE);
    printf("loader after init used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    char path[4096];
    snprintf(path, sizeof path, "%s/jupyter/prelude.glon", root_dir);
    load_file(path);
    printf("loader after prelude used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    snprintf(path, sizeof path, "%s/experiments/glon-doom/doom.glon", root_dir);
    load_file(path);
    if (objects_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/objects.glon", root_dir);
        load_file(path);
    }
    printf("loader after game used=%ld free=%ld code=%ld\n",
           (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
           R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], r0_s1_code_size());
    if (raw_wall_mode) {
        snprintf(path, sizeof path, "%s/experiments/glon-doom/raw-wall.glon", root_dir);
        load_file(path);
        printf("loader after raw-wall used=%ld free=%ld asm_here=%ld\n",
               (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
               R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP], (long)asm_here());
    }
    build_blocks();
    long hp = (long)M[GC_LOADER_HP];
    cell gc, cap;
    r0_s1_global_usage(&gc, &cap);
    printf("loader after host blocks used=%ld free=%ld code=%ld asm_here=%ld globals=%ld/%ld\n",
           hp - R0S1_HEAP_BASE, R0S1_HEAP_LIMIT - hp, r0_s1_code_size(),
           (long)asm_here(), gc, cap);
    printf("code bytes? cells=%ld ip_start=%ld ip_end=%ld\n",
           r0_s1_code_size(), r0_s1_ip_start(), r0_s1_ip_end());
    int made = 0;
    const int probes[] = {128, 256, 320, 384, 448};
    for (size_t pi = 0; pi < sizeof probes / sizeof probes[0]; pi++) {
        int n = probes[pi];
        size_t capn = (size_t)n * 4 + 8;
        char *src = malloc(capn);
        if (!src) return 2;
        size_t w = 0;
        w += (size_t)snprintf(src + w, capn - w, "[");
        for (int i = 0; i < n; i++)
            w += (size_t)snprintf(src + w, capn - w, " %d", i & 255);
        snprintf(src + w, capn - w, " ]");
        long before = (long)M[GC_LOADER_HP];
        int err = 0;
        cell b = r0_s1_parse(src, &err);
        if (err) {
            printf("loader table probe n=%d refused kind=%d free_before=%ld\n",
                   n, r0_s1_parse_error_kind(), R0S1_HEAP_LIMIT - before);
            free(src);
            continue;
        }
        (void)b;
        made = n;
        printf("loader table probe n=%d delta=%ld used=%ld free=%ld\n",
               n, (long)M[GC_LOADER_HP] - before,
               (long)M[GC_LOADER_HP] - R0S1_HEAP_BASE,
               R0S1_HEAP_LIMIT - (long)M[GC_LOADER_HP]);
        free(src);
    }
    printf("loader table probe largest=%d; game has no masm (all loader data, runtime code resident)\n", made);
    return 0;
}

#ifdef DOOM_S1_PROFILE
static void print_prof(const char *name, const ProfAccum *a, int frames) {
    static const char *opname[8] = {"LIT","DUP","DROP","@","!","0BRANCH","HOST","HALT"};
    static const char *hname[16] = {"+","-","*","/","mod","neg","=","!=","<",">",
                                    "<=",">=","alloc","putchar","print","dump"};
    long total = 0, hosttotal = 0;
    for (int i = 0; i < 8; i++) total += a->op[i];
    for (int i = 0; i < 16; i++) hosttotal += a->host[i];
    printf("profile %s per_frame ops=%ld host=%ld branches=%ld taken=%ld cycles=%.0f\n",
           name, total / frames, hosttotal / frames, a->op[5] / frames,
           a->branches_taken / frames, (double)a->cycles / frames);
    printf("profile %s opcodes:", name);
    for (int i = 0; i < 8; i++) printf(" %s=%ld", opname[i], a->op[i] / frames);
    printf("\nprofile %s host:", name);
    for (int i = 0; i < 16; i++) if (a->host[i]) printf(" %s=%ld", hname[i], a->host[i] / frames);
    printf("\n");
}

static int opcode_bench(int frames) {
    load_session();
    build_blocks();
    if (reset() != 0 || set_camera(320, 320, 0) != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s; TexRows tr; Status st;
    memset(&prof_wall, 0, sizeof prof_wall);
    memset(&prof_tex, 0, sizeof prof_tex);
    memset(&prof_status, 0, sizeof prof_status);
    memset(&prof_tick, 0, sizeof prof_tick);
    prof_on = 1;
    for (int i = 0; i < 3; i++) {
        if (tick(0) != 0 || render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
    }
    memset(&prof_wall, 0, sizeof prof_wall);
    memset(&prof_tex, 0, sizeof prof_tex);
    memset(&prof_status, 0, sizeof prof_status);
    memset(&prof_tick, 0, sizeof prof_tick);
    for (int i = 0; i < frames; i++) {
        if (tick(0) != 0) return 2;
        if (render_spans(&s, NULL) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
    }
    prof_on = 0;
    printf("opcode frames=%d mode=%s loader=%ld heap_high=%ld gc=%ld\n",
           frames, simple_mode ? "simple" : "textured",
           (long)M[GC_LOADER_HP], r0_s1_heap_high(), r0_s1_gc_count());
    print_prof("wall", &prof_wall, frames);
    print_prof("texrow", &prof_tex, frames);
    print_prof("status", &prof_status, frames);
    print_prof("tick", &prof_tick, frames);
    /* top execution buckets in the wall stage */
    long *h = prof_wall.hist;
    for (int k = 0; k < 8; k++) {
        int best = -1;
        for (int i = 0; i < 256; i++) if (h[i] > 0 && (best < 0 || h[i] > h[best])) best = i;
        if (best < 0) break;
        printf("profile wall bucket ip=%d count_per_frame=%ld\n", best * 256, h[best] / frames);
        h[best] = 0;
    }
    return 0;
}
#endif

static void print_metrics(const Metrics *m) {
    printf("metrics: sim=%.2fms wall=%.2fms texrow=%.2fms raster=%.2fms "
           "sprite=%.2fms present=%.2fms frame=%.2fms fps=%.1f "
           "heap_high=%ld gc=%ld live=%ld\n",
           m->sim_ms, m->proj_ms, m->texrow_ms, m->raster_ms, m->sprite_ms, m->present_ms,
           m->frame_ms, m->frame_ms > 0.0 ? 1000.0 / m->frame_ms : 0.0,
           m->heap_high, m->gc_count, m->live_cells);
}

static int set_close_pose(void) {
    reset();
    const char *src;
    if (!alien_cover) {
        src = "[ doom/px: 700 doom/py: 320 doom/angle: 0 doom/aim "
              "doom/door-open: 64 doom/door-target: 64 doom/door-blocked: 0 "
              "doom/alien-x: 850 doom/alien-y: 320 doom/alien-state: 2 "
              "doom/alien-health: 100 doom/alien-detected: 1 ]";
    } else {
        int ay = cover_view == 1 ? 292 : cover_view == 2 ? 330 : 270;
        int py = cover_view == 3 ? 370 : 320;
        static char buf[320];
        snprintf(buf, sizeof buf,
                 "[ doom/px: 700 doom/py: %d doom/angle: 0 doom/aim "
                 "doom/door-open: 64 doom/door-target: 64 doom/door-blocked: 0 "
                 "doom/alien-x: 920 doom/alien-y: %d doom/alien-state: 1 "
                 "doom/alien-health: 100 doom/alien-detected: 1 ]", py, ay);
        src = buf;
    }
    cell pose = parse_source(src, "close pose");
    return run_block(pose, "close pose");
}

static int bench(int frames, int bench_alien) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5i - benchmark",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");

    load_session();
    build_blocks();
    if (bench_alien && set_close_pose() != 0) return 2;
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s;
    Detail det;
    TexRows tr;
    Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;
    Metrics sum = {0};
    long wall0 = now_ns();
    for (int i = 0; i < frames; i++) {
        long t0 = now_ns();
        if (tick(i & 63) != 0) return 2;
        long t1 = now_ns();
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) return 2;
        if (need_texrows() && get_texrows(&tr) != 0) return 2;
        if (get_status(&st) != 0) return 2;
        long t2 = now_ns();
        double raster_ms, present_ms;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &raster_ms, &present_ms) != 0) return 2;
        long t3 = now_ns();
        sum.sim_ms += (double)(t1 - t0) / 1e6;
        sum.proj_ms += (double)proj_ns / 1e6;
        sum.texrow_ms += (double)(t2 - t1 - proj_ns) / 1e6;
        sum.raster_ms += raster_ms;
        sum.sprite_ms += last_sprite_ms;
        sum.present_ms += present_ms;
        sum.frame_ms += (double)(t3 - t0) / 1e6;
        SDL_PumpEvents();
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT) { i = frames; break; }
    }
    long wall1 = now_ns();
    sum.sim_ms /= frames; sum.proj_ms /= frames; sum.texrow_ms /= frames;
    sum.raster_ms /= frames; sum.sprite_ms /= frames; sum.present_ms /= frames; sum.frame_ms /= frames;
    sum.heap_high = r0_s1_heap_high();
    sum.gc_count = r0_s1_gc_count();
    sum.live_cells = r0_s1_gc_live_cells();
    printf("bench frames=%d wall_fps=%.1f\n", frames,
           frames * 1e9 / (double)(wall1 - wall0));
    print_metrics(&sum);
    free(fb);
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}

typedef struct { int w,s,a,d,left,right; } HeldKeys;
static void held_keys_apply(HeldKeys *h, const SDL_Event *e) {
    if (e->type != SDL_KEYDOWN && e->type != SDL_KEYUP) return;
    int down = e->type == SDL_KEYDOWN;
    SDL_Scancode sc = e->key.keysym.scancode;
    SDL_Keycode sym = e->key.keysym.sym;
    if (sc == SDL_SCANCODE_W || sym == SDLK_w) h->w = down;
    else if (sc == SDL_SCANCODE_S || sym == SDLK_s) h->s = down;
    else if (sc == SDL_SCANCODE_A || sym == SDLK_a) h->a = down;
    else if (sc == SDL_SCANCODE_D || sym == SDLK_d) h->d = down;
    else if (sc == SDL_SCANCODE_LEFT || sym == SDLK_LEFT || sc == SDL_SCANCODE_KP_4) h->left = down;
    else if (sc == SDL_SCANCODE_RIGHT || sym == SDLK_RIGHT || sc == SDL_SCANCODE_KP_6) h->right = down;
}
static int held_keys_mask(const HeldKeys *h, const Uint8 *k) {
    return (h->w || k[SDL_SCANCODE_W] ? 1 : 0) | (h->s || k[SDL_SCANCODE_S] ? 2 : 0) |
           (h->a || k[SDL_SCANCODE_A] ? 4 : 0) | (h->d || k[SDL_SCANCODE_D] ? 8 : 0) |
           (h->left || k[SDL_SCANCODE_LEFT] ? 16 : 0) |
           (h->right || k[SDL_SCANCODE_RIGHT] ? 32 : 0);
}
static int input_test(void) {
    int fails = 0;
    HeldKeys h = {0};
    SDL_Event e; memset(&e,0,sizeof e);
    e.type=SDL_KEYDOWN; e.key.keysym.scancode=SDL_SCANCODE_RIGHT; e.key.keysym.sym=SDLK_RIGHT;
    held_keys_apply(&h,&e); Uint8 k[SDL_NUM_SCANCODES]; memset(k,0,sizeof k);
    fails += expect_eq("right keydown maps turn-right", held_keys_mask(&h,k), 32);
    e.type=SDL_KEYUP; held_keys_apply(&h,&e); fails += expect_eq("right keyup clears", held_keys_mask(&h,k), 0);
    e.type=SDL_KEYDOWN; e.key.keysym.scancode=SDL_SCANCODE_KP_6; e.key.keysym.sym=SDLK_KP_6;
    held_keys_apply(&h,&e); fails += expect_eq("keypad right fallback", held_keys_mask(&h,k), 32);
    e.type=SDL_KEYUP; held_keys_apply(&h,&e);
    e.type=SDL_KEYDOWN; e.key.keysym.scancode=SDL_SCANCODE_LEFT; e.key.keysym.sym=SDLK_LEFT;
    held_keys_apply(&h,&e); fails += expect_eq("left keydown maps turn-left", held_keys_mask(&h,k), 16);
    e.type=SDL_KEYUP; held_keys_apply(&h,&e);
    e.type=SDL_KEYDOWN; e.key.keysym.scancode=SDL_SCANCODE_W; e.key.keysym.sym=SDLK_w;
    held_keys_apply(&h,&e); fails += expect_eq("W keydown maps forward", held_keys_mask(&h,k), 1);
    e.type=SDL_KEYUP; held_keys_apply(&h,&e);
    printf("input-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}

static int interactive(void) {
    printf("G5p renderer=CPU framebuffer=320x200 interactive=on\n");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5p CPU | W/S/A/D move  arrows turn  E door  R textured/simple  Esc quit",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");

    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    Spans s;
    Detail det;
    TexRows tr;
    Status st;
    if (get_status(&st) != 0) return 2;
    if (need_texrows() && get_texrows(&tr) != 0) return 2;
    audio_start();
    int running = 1, frames = 0;
    double acc = 0.0, fps_start = (double)now_ns(), sim_ms = 0, proj_ms = 0;
    double texrow_ms = 0, raster_ms = 0, present_ms = 0;
    ViewCache live_cache; memset(&live_cache, 0, sizeof live_cache);
    HeldKeys held = {0};
    long last = now_ns();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            else if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                memset(&held, 0, sizeof held);
            else if (e.type == SDL_KEYDOWN || e.type == SDL_KEYUP) {
                held_keys_apply(&held, &e);
                int down = e.type == SDL_KEYDOWN;
                SDL_Scancode sc = e.key.keysym.scancode;
                SDL_Keycode sym = e.key.keysym.sym;
                if ((sc == SDL_SCANCODE_ESCAPE || sym == SDLK_ESCAPE) && down) running = 0;
                else if ((sc == SDL_SCANCODE_E || sym == SDLK_e) && down && !e.key.repeat)
                    run_block(activate_block, "activate");
                else if ((sc == SDL_SCANCODE_R || sym == SDLK_r) && down && !e.key.repeat) {
                    simple_mode = !simple_mode;
                    live_cache.valid = 0;
                    printf("render mode: %s\n", simple_mode ? "simple" : "detailed");
                    fflush(stdout);
                }
                else if ((sc == SDL_SCANCODE_M || sym == SDLK_m) && down && !e.key.repeat)
                    audio_toggle_mute();
                else if ((sc == SDL_SCANCODE_LEFTBRACKET || sym == SDLK_LEFTBRACKET) && down && !e.key.repeat)
                    audio_set_volume(SDL_AtomicGet(&audio_volume) - 5);
                else if ((sc == SDL_SCANCODE_RIGHTBRACKET || sym == SDLK_RIGHTBRACKET) && down && !e.key.repeat)
                    audio_set_volume(SDL_AtomicGet(&audio_volume) + 5);
            }
        }
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        int mask = held_keys_mask(&held, k);
        long tn = now_ns();
        double dt = (double)(tn - last) / 1e9;
        last = tn;
        if (dt > 0.10) dt = 0.10;
        acc += dt;
        long ts = now_ns();
        int ticks = 0;
        while (acc >= 1.0 / TICK_HZ && ticks < 10) {
            if (tick(mask) != 0) { running = 0; break; }
            acc -= 1.0 / TICK_HZ;
            ticks++;
        }
        long te = now_ns();
        sim_ms += (double)(te - ts) / 1e6;
        if (stationary_enabled) {
            long cf0 = now_ns();
            int miss = cache_frame(&live_cache, fb, floor_c, ceil_c, 0);
            if (miss < 0) break;
            if (sdl_present_cached(ren, tex, fb, miss) != 0) break;
            long cf1 = now_ns();
            proj_ms += (double)(cf1 - cf0) / 1e6;
            frames++;
            if (frames % 120 == 0) {
                double secs = ((double)now_ns() - fps_start) / 1e9;
                printf("frame %d stationary: frame=%.2fms fps=%.1f\n", frames,
                       proj_ms / 120, 120 / secs);
                fflush(stdout);
                sim_ms = proj_ms = texrow_ms = raster_ms = present_ms = 0;
                fps_start = (double)now_ns();
            }
            continue;
        }
        long proj_ns;
        if (render_spans(&s, &proj_ns) != 0) break;
        long st0 = now_ns();
        if (need_texrows() && get_texrows(&tr) != 0) break;
        if (get_status(&st) != 0) break;
        long st1 = now_ns();
        proj_ms += (double)proj_ns / 1e6;
        texrow_ms += (double)(st1 - st0 - proj_ns) / 1e6;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) break;
        raster_ms += rm;
        present_ms += pm;
        frames++;
        if (frames % 120 == 0) {
            double secs = ((double)now_ns() - fps_start) / 1e9;
            Metrics m = { sim_ms / 120, proj_ms / 120, texrow_ms / 120,
                          raster_ms / 120, present_ms / 120,
                          secs * 1000.0 / 120, 0.0, r0_s1_heap_high(),
                          r0_s1_gc_count(), r0_s1_gc_live_cells() };
            printf("frame %d: ", frames); print_metrics(&m);
            int sx, sy, sa; Status ds;
            if (get_state(&sx, &sy, &sa) == 0 && get_status(&ds) == 0)
                printf("state: player=%d,%d angle=%d door=%d/%d alien=%d,%d state=%d det=%d vis=%d\n",
                       sx, sy, sa, ds.door_open, ds.door_blocked, ds.ax, ds.ay,
                       ds.state, ds.detected, ds.visible);
            fflush(stdout);
            sim_ms = proj_ms = texrow_ms = raster_ms = present_ms = 0;
            fps_start = (double)now_ns();
        }
    }
    free(fb);
    audio_stop();
    SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}


static int alien_demo(int state) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return 2;
    SDL_Window *win = SDL_CreateWindow("Glon AVP G5i - close Alien",
                                       SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       960, 600, SDL_WINDOW_SHOWN);
    if (!win) fail(SDL_GetError());
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!ren) fail(SDL_GetError());
    apply_scale_hint();
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888,
                                         SDL_TEXTUREACCESS_STREAMING, FB_W, FB_H);
    if (!tex) fail(SDL_GetError());
    uint32_t *fb = malloc((size_t)FB_W * FB_H * sizeof *fb);
    if (!fb) fail("out of memory");
    load_session();
    build_blocks();
    int floor_c, ceil_c;
    if (get_sector(&floor_c, &ceil_c) != 0) return 2;
    if (set_close_pose() != 0) return 2;
    if (state == 1) {
        cell w = parse_source("[ doom/alien-state: 1 ]", "walk pose");
        if (run_block(w, "walk pose") != 0) return 2;
    }
    Spans s; Detail det; TexRows tr; Status st;
    if (get_status(&st) != 0 || (need_texrows() && get_texrows(&tr) != 0)) return 2;
    int running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) if (e.type == SDL_QUIT ||
            (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_ESCAPE)) running = 0;
        if (render_spans(&s, NULL) != 0 || get_status(&st) != 0) break;
        double rm, pm;
        if (sdl_present(ren, tex, fb, &s, &det, need_texrows() ? &tr : NULL, &st,
                        floor_c, ceil_c, &rm, &pm) != 0) break;
        sim_tick++;
        SDL_Delay(16);
    }
    free(fb); SDL_DestroyTexture(tex); SDL_DestroyRenderer(ren); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}


static int objects_test(void) {
    int fails = 0;
    load_session();
    build_blocks();
    reset();
    Obj a[6], b[6];
    if (get_objects(a) != 0 || get_objects(b) != 0) return 2;
    fails += expect_eq("object record count", 6, 6);
    int deterministic = 1;
    for (int i = 0; i < 6; i++)
        if (a[i].id != b[i].id || a[i].depth != b[i].depth ||
            a[i].left != b[i].left || a[i].visible != b[i].visible) deterministic = 0;
    fails += expect_eq("object records deterministic", deterministic, 1);
    cell hit = parse_source("[ doom/hit: 0 doom/check-all 900 272 100 print doom/hit ]", "object hit");
    cell clear = parse_source("[ doom/hit: 0 doom/check-all 700 320 100 print doom/hit ]", "object clear");
    int v;
    if (run_capture(hit, &v, 1, "object hit") != 0) return 2;
    fails += expect_eq("player object collision", v, 1);
    if (run_capture(clear, &v, 1, "object clear") != 0) return 2;
    fails += expect_eq("player clear of object", v, 0);
    cell alien_hit = parse_source("[ doom/hit: 0 doom/check-all 920 270 16 print doom/hit ]", "alien object hit");
    if (run_capture(alien_hit, &v, 1, "alien object hit") != 0) return 2;
    fails += expect_eq("alien object collision", v, 1);
    printf("objects-test: %s\n", fails ? "FAILED" : "PASSED");
    return fails ? 1 : 0;
}


/* ---- G5l OpenGL renderer -------------------------------------------------
 * Static level geometry is a renderer-side ABI copy, uploaded once. Glon
 * supplies camera, door, light phase, object records and Alien state every
 * frame; the GPU performs projection/clipping/raster/depth/texture sampling.
 */
typedef struct { int x1,y1,x2,y2,mat; } GlWall;
static const GlWall gl_walls[10] = {
    {128,128,768,128,1},{768,128,768,256,1},{768,256,1280,256,2},
    {1280,256,1280,1024,3},{1280,1024,1152,1024,3},{1152,1024,1152,384,3},
    {1152,384,768,384,2},{768,384,768,640,1},{768,640,128,640,1},
    {128,640,128,128,1}
};
enum { GT_PANEL, GT_SERVICE, GT_FLOOR, GT_CEIL, GT_CEIL_PANEL,
       GT_DOOR_LEFT, GT_DOOR_RIGHT,
       GT_DRUM, GT_CRATE, GT_CABINET, GT_CONTAINER, GT_ALIEN0,
       GT_ORIENT = GT_ALIEN0 + ALIEN_DIR_COUNT, GT_ART_WALL, GT_ART_PANEL, GT_ART_FLOOR,
       GT_WEAPON, GT_G5Z_ATLAS, GT_COUNT = GT_G5Z_ATLAS + 1 };
static GLuint gl_tex[GT_COUNT];
static SDL_Window *gl_win;
static SDL_GLContext gl_ctx;
static int gl_w, gl_h;
static int gl_door_closed = 0;
static int renderer_gl = 1; /* G5m default: GL when available */

#define G5Z_ATLAS_W 512
#define G5Z_ATLAS_H 256
static const uint8_t g5z_font[40][7] = {
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0F,0x10,0x10,0x10,0x10,0x10,0x0F}, {0x1E,0x11,0x11,0x11,0x11,0x11,0x1E},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0F,0x10,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x1F}, {0x07,0x02,0x02,0x02,0x12,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x19,0x15,0x13,0x11,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x0A,0x04,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1E,0x01,0x01,0x0E,0x01,0x01,0x1E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x10,0x1E,0x01,0x01,0x1E},
    {0x0E,0x10,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x01,0x0E},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x06,0x06},
    {0x01,0x02,0x02,0x04,0x08,0x08,0x10}, {0x00,0x06,0x06,0x00,0x06,0x06,0x00}
};
typedef struct { float u0,v0,u1,v1; } G5zUV;
static G5zUV g5z_uv_rect(int x0,int y0,int x1,int y1) {
    G5zUV r;
    r.u0=(float)x0/G5Z_ATLAS_W; r.u1=(float)x1/G5Z_ATLAS_W;
    r.v0=1.0f-(float)y1/G5Z_ATLAS_H; r.v1=1.0f-(float)y0/G5Z_ATLAS_H;
    return r;
}
static int g5z_glyph(char c) {
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='0'&&c<='9') return 26+c-'0';
    if(c=='-') return 36; if(c=='.') return 37; if(c=='/') return 38; if(c==':') return 39;
    return -1;
}
static unsigned g5z_hash(int x,int y) {
    unsigned h=(unsigned)(x*374761393u+y*668265263u);
    h=(h^(h>>13))*1274126177u; return h^(h>>16);
}
static void g5z_fill(uint32_t *a,int x0,int y0,int x1,int y1,uint32_t c) {
    if(x0<0)x0=0; if(y0<0)y0=0; if(x1>G5Z_ATLAS_W)x1=G5Z_ATLAS_W; if(y1>G5Z_ATLAS_H)y1=G5Z_ATLAS_H;
    for(int y=y0;y<y1;y++) for(int x=x0;x<x1;x++) a[y*G5Z_ATLAS_W+x]=c;
}
static void g5z_text(uint32_t *a,int x,int y,const char *s,int scale,uint32_t c,int worn) {
    for(;*s;s++){
        if(*s==' '){ x+=6*scale; continue; }
        int g=g5z_glyph(*s); if(g<0){ x+=6*scale; continue; }
        for(int row=0;row<7;row++) for(int col=0;col<5;col++){
            if(!(g5z_font[g][row]&(1u<<(4-col)))) continue;
            int px=x+col*scale, py=y+row*scale;
            if(worn && (g5z_hash(px,py)&63u)==0) continue;
            g5z_fill(a,px,py,px+scale,py+scale,c);
        }
        x+=6*scale;
    }
}
static void g5z_text_shadow(uint32_t *a,int x,int y,const char *s,int scale,uint32_t c) {
    g5z_text(a,x+1,y+1,s,scale,0xB0000000,0);
    g5z_text(a,x,y,s,scale,c,1);
}
static void g5z_text_bold(uint32_t *a,int x,int y,const char *s,int scale,uint32_t c) {
    for(;*s;s++){
        if(*s==' '){ x+=12*scale; continue; }
        int g=g5z_glyph(*s); if(g<0){ x+=12*scale; continue; }
        for(int row=0;row<7;row++) for(int col=0;col<5;col++){
            if(!(g5z_font[g][row]&(1u<<(4-col)))) continue;
            int px=x+col*scale*2, py=y+row*scale*2;
            g5z_fill(a,px,py,px+scale*2,py+scale*2,c);
        }
        x+=12*scale;
    }
}
static void g5z_hline(uint32_t *a,int x0,int x1,int y,uint32_t c,int t) {
    g5z_fill(a,x0,y,x1,y+t,c);
}
static void g5z_vline(uint32_t *a,int x,int y0,int y1,uint32_t c,int t) {
    g5z_fill(a,x,y0,x+t,y1,c);
}
static void g5z_line(uint32_t *a,int x0,int y0,int x1,int y1,uint32_t c,int t) {
    int dx=x1-x0, dy=y1-y0, ax=dx<0?-dx:dx, ay=dy<0?-dy:dy;
    int n=ax>ay?ax:ay; if(n<1)n=1;
    for(int i=0;i<=n;i++){
        int x=x0+dx*i/n, y=y0+dy*i/n;
        g5z_fill(a,x,y,x+t,y+t,c);
    }
}
static uint32_t *g5z_build_atlas(void) {
    uint32_t *a=calloc((size_t)G5Z_ATLAS_W*G5Z_ATLAS_H,sizeof(uint32_t));
    if(!a) fail("gl texture memory");
    const uint32_t white=0xFFE8E2D0, dim=0xFF9AA79C, cyan=0xFF58D6D0;
    const uint32_t green=0xFF5AD06A, amber=0xFFE0A030;
    const uint32_t bg=0xFF071012, border=0xFF263331, grid=0xFF12302E;
    /* Transparent-background stencil labels. */
    g5z_text_shadow(a,4,2,"GLON CORPORATION",3,white);
    g5z_text_shadow(a,4,32,"GLON CORPORATION",2,white);
    g5z_text_shadow(a,4,52,"ENGINEERING DIVISION",2,dim);
    g5z_text_shadow(a,4,74,"GC-204",2,white);
    g5z_text_shadow(a,88,74,"GC-205",2,white);
    g5z_text_shadow(a,172,74,"GC-206",2,white);
    g5z_text_shadow(a,4,94,"PROPERTY OF GLON CORPORATION",2,dim);
    g5z_text_shadow(a,4,112,"AUTHORIZED PERSONNEL ONLY",2,dim);
    g5z_text_bold(a,324,0,"GLON",2,white);
    g5z_text_shadow(a,324,30,"CORPORATION",1,dim);
    g5z_text_shadow(a,324,50,"COOLANT",2,white);
    g5z_text_shadow(a,324,68,"CAUTION",2,amber);
    g5z_text_shadow(a,430,50,"GC-207",2,white);
    g5z_text_shadow(a,430,70,"GC-208",2,white);
    g5z_text_shadow(a,430,90,"LIFT",2,dim);
    /* Instrument panel 0: flow waveform. */
    g5z_fill(a,0,128,128,192,bg); g5z_fill(a,0,128,128,130,border); g5z_fill(a,0,190,128,192,border);
    for(int x=16;x<128;x+=16) g5z_vline(a,x,132,188,grid,1);
    for(int y=140;y<188;y+=12) g5z_hline(a,2,126,y,grid,1);
    g5z_text(a,4,132,"FLOW A",1,cyan,0);
    g5z_text(a,78,130,"42.7",2,white,0);
    for(int x=4;x<124;x++){
        int y=166+(int)(7.0f*sinf(x*0.29f)+3.0f*sinf(x*0.73f+1.2f));
        g5z_fill(a,x,y,x+2,y+1,cyan);
    }
    g5z_text(a,4,176,"NOMINAL",1,green,0);
    g5z_fill(a,92,178,96,182,green);
    /* Instrument panel 1: power bus bars. */
    g5z_fill(a,128,128,256,192,bg); g5z_fill(a,128,128,256,130,border); g5z_fill(a,128,190,256,192,border);
    g5z_text(a,132,132,"PWR BUS",1,cyan,0);
    static const int heights[3]={22,30,26};
    for(int i=0;i<3;i++){
        int x=136+i*38, h=heights[i];
        g5z_fill(a,x,176-h,x+20,176,i==1?amber:green);
        g5z_fill(a,x,176,x+20,178,dim);
        char lab[2]={(char)('A'+i),0}; g5z_text(a,x+7,180,lab,1,white,0);
        g5z_text(a,x+5,168,i==0?"78":i==1?"64":"91",1,white,0);
    }
    /* Instrument panel 2: coolant schematic. */
    g5z_fill(a,256,128,384,192,bg); g5z_fill(a,256,128,384,130,border); g5z_fill(a,256,190,384,192,border);
    g5z_text(a,260,132,"COOLANT LOOP",1,cyan,0);
    g5z_hline(a,264,368,154,cyan,2);
    g5z_vline(a,300,154,178,cyan,2);
    g5z_hline(a,300,356,178,cyan,2);
    g5z_fill(a,292,146,308,162,border); g5z_fill(a,296,150,304,158,bg);
    g5z_text(a,294,164,"P1",1,white,0);
    g5z_line(a,340,146,350,154,amber,2); g5z_line(a,350,154,340,162,amber,2);
    g5z_line(a,340,162,330,154,amber,2); g5z_line(a,330,154,340,146,amber,2);
    g5z_text(a,332,166,"V2",1,white,0);
    g5z_text(a,346,174,"OK",2,green,0);
    /* Instrument panel 3: system status. */
    g5z_fill(a,384,128,512,192,bg); g5z_fill(a,384,128,512,130,border); g5z_fill(a,384,190,512,192,border);
    g5z_text(a,388,132,"SYSTEM STATUS",1,cyan,0);
    const char *names[4]={"DOOR","GRID","LIGHT","AUX"};
    const char *vals[4]={"SEALED","ONLINE","STANDBY","OK"};
    for(int i=0;i<4;i++){
        int y=144+i*10;
        g5z_fill(a,390,y,394,y+4,i==2?amber:green);
        g5z_text(a,398,y,names[i],1,white,0);
        g5z_text(a,448,y,vals[i],1,i==2?amber:green,0);
    }
    /* Auxiliary gauge, 64x32. */
    g5z_fill(a,0,192,64,224,bg); g5z_fill(a,0,192,64,194,border); g5z_fill(a,0,222,64,224,border);
    g5z_text(a,3,196,"AUX",1,cyan,0);
    for(int i=0;i<5;i++){
        int h=8+i*3, x=8+i*10;
        g5z_fill(a,x,216-h,x+6,216,i<3?green:amber);
    }
    g5z_fill(a,52,196,56,200,green);
    /* Wide strip and plate. */
    g5z_fill(a,64,192,384,224,bg); g5z_fill(a,64,192,384,194,border); g5z_fill(a,64,222,384,224,border);
    g5z_text(a,68,196,"GLON CORPORATION",2,white,0);
    g5z_text(a,68,214,"ENGINEERING DIVISION",1,dim,0);
    g5z_fill(a,344,198,348,202,green); g5z_fill(a,352,198,356,202,amber);
    g5z_fill(a,384,192,512,224,border); g5z_fill(a,386,194,510,222,bg);
    g5z_text(a,392,198,"GLON",3,white,0);
    g5z_text(a,392,218,"CORPORATION",1,dim,0);
    return a;
}

static void gl_upload(GLuint id, const uint32_t *src, int w, int h) {
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) fail("gl texture memory");
    for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
        uint32_t c = src[(h-1-y)*w + x];
        int i = y*w + x;
        rgba[i*4+0] = (uint8_t)(c >> 16); rgba[i*4+1] = (uint8_t)(c >> 8);
        rgba[i*4+2] = (uint8_t)c; rgba[i*4+3] = (uint8_t)(c >> 24);
    }
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    free(rgba);
}

static int gl_upload_rgb_file(GLuint id,const char *name,int w,int h) {
    char path[4096];
    snprintf(path,sizeof path,"%s/experiments/glon-doom/assets/doors/%s",root_dir,name);
    FILE *f=fopen(path,"rb");
    if(!f){ fprintf(stderr,"gl: missing door asset: %s\n",path); return -1; }
    size_t need=(size_t)w*h*3;
    uint8_t *rgb=malloc(need), *rgba=malloc((size_t)w*h*4);
    if(!rgb || !rgba){ free(rgb); free(rgba); fclose(f); fail("gl texture memory"); }
    if(fread(rgb,1,need,f)!=need){ fclose(f); free(rgb); free(rgba);
        fprintf(stderr,"gl: short door asset: %s\n",path); return -1; }
    fclose(f);
    for(int y=0;y<h;y++) for(int x=0;x<w;x++){
        uint8_t *s0=rgb+((size_t)(h-1-y)*w+x)*3, *d=rgba+((size_t)y*w+x)*4;
        d[0]=s0[0]; d[1]=s0[1]; d[2]=s0[2]; d[3]=255;
    }
    glBindTexture(GL_TEXTURE_2D,id);
    /* Native-resolution leaves use the renderer's established GL_LINEAR
     * sampling. Mipmapped sampling measurably raised per-frame driver cost on
     * the WSLg D3D12 path, so it is deliberately not used here. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    /* The leaf quads span exactly 0..1; clamp prevents the inner seam edge
     * from blending with the outer edge under GL_REPEAT linear filtering. */
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba);
    free(rgb); free(rgba); return 0;
}

static int gl_init(int w, int h, int shown) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    gl_win = SDL_CreateWindow("Glon AVP G5p OpenGL",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
        SDL_WINDOW_OPENGL | SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE |
        (shown ? 0 : SDL_WINDOW_HIDDEN));
    if (!gl_win) { fprintf(stderr, "gl: window: %s\n", SDL_GetError()); return -1; }
    gl_ctx = SDL_GL_CreateContext(gl_win);
    if (!gl_ctx) { fprintf(stderr, "gl: context: %s\n", SDL_GetError()); return -1; }
    SDL_GL_SetSwapInterval(0);
    printf("G5p renderer=OpenGL renderer=%s vendor=%s version=%s\n",
           glGetString(GL_RENDERER), glGetString(GL_VENDOR), glGetString(GL_VERSION));
    glGenTextures(GT_COUNT, gl_tex);
    gl_upload(gl_tex[GT_PANEL], tex_panel_v2, 64,64);
    gl_upload(gl_tex[GT_SERVICE], tex_service_v2,64,64);
    gl_upload(gl_tex[GT_FLOOR], tex_floor,64,64);
    gl_upload(gl_tex[GT_CEIL], tex_ceiling_light,64,64);
    gl_upload(gl_tex[GT_CEIL_PANEL], tex_ceiling_panel,64,64);
    if(gl_upload_rgb_file(gl_tex[GT_DOOR_LEFT],"g5x2-door-left.rgb",840,1470)!=0) return -1;
    if(gl_upload_rgb_file(gl_tex[GT_DOOR_RIGHT],"g5x2-door-right.rgb",840,1470)!=0) return -1;
    gl_upload(gl_tex[GT_DRUM], obj_drum,64,96);
    gl_upload(gl_tex[GT_CRATE], obj_crate,64,96);
    gl_upload(gl_tex[GT_CABINET], obj_cabinet,64,96);
    gl_upload(gl_tex[GT_CONTAINER], obj_container,64,96);
    for (int i = 0; i < ALIEN_DIR_COUNT; i++)
        gl_upload(gl_tex[GT_ALIEN0+i], alien_dir_tex[i], alien_dir_w[i], alien_dir_h[i]);
    gl_upload(gl_tex[GT_ORIENT], orient_test,128,192);
    gl_upload(gl_tex[GT_ART_WALL], tex_art_wall_bulkhead_steel,512,512);
    gl_upload(gl_tex[GT_ART_PANEL], tex_art_wall_recess_panel,512,512);
    gl_upload(gl_tex[GT_ART_FLOOR], tex_art_floor_plate,512,512);
    gl_upload(gl_tex[GT_WEAPON], weapon_pulse,WEAPON_W,WEAPON_H);
    uint32_t *g5z_atlas=g5z_build_atlas();
    gl_upload(gl_tex[GT_G5Z_ATLAS],g5z_atlas,G5Z_ATLAS_W,G5Z_ATLAS_H);
    free(g5z_atlas);
    glBindTexture(GL_TEXTURE_2D,gl_tex[GT_G5Z_ATLAS]);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
    gl_w=w; gl_h=h; SDL_GL_GetDrawableSize(gl_win,&gl_w,&gl_h);
    if (gl_w<=0 || gl_h<=0) { gl_w=w; gl_h=h; }
    glViewport(0,0,gl_w,gl_h); glClearColor(0.04,0.05,0.06,1); glEnable(GL_DEPTH_TEST);
    return 0;
}

static void gl_gv(double x,double y,double z) { glVertex3d(x/64.0, z/64.0, -y/64.0); }
static void gl_view(double px,double py,double angle) {
    /* map world (x,y,z) -> GL (x,z,-y), y up; yaw +90 - game angle */
    double ex=px/64.0, ey=1.0, ez=-py/64.0;
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glRotatef((float)(90.0 - angle*360.0/256.0), 0, 1, 0);
    if (gl_debug_pitch != 0.0f) glRotatef(gl_debug_pitch, 1, 0, 0);
    glTranslatef((float)-ex, (float)-ey, (float)-ez);
}
static void gl_update_viewport(void) {
    int dw=0, dh=0;
    SDL_GL_GetDrawableSize(gl_win, &dw, &dh);
    if (dw > 0 && dh > 0 && (dw != gl_w || dh != gl_h)) {
        gl_w = dw; gl_h = dh;
        glViewport(0, 0, gl_w, gl_h);
    }
}
static int gl_viewport_fill(int dw,int dh,int *x,int *y,int *w,int *h) {
    if (dw <= 0 || dh <= 0) return -1;
    *x=0; *y=0; *w=dw; *h=dh; return 0;
}
static float gl_hud_scale(int dh) { float s = dh > 0 ? dh / 200.0f : 1.0f; return s > 2.0f ? 2.0f : s; }
static void gl_projection(void) {
    double aspect=(double)gl_w/(double)gl_h;
    double fovy=2.0*atan(100.0/246.0), near=0.05, far=40.0;
    double top=near*tan(fovy/2.0), right=top*aspect;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glFrustum(-right,right,-top,top,near,far);
}
static void gl_wall(const GlWall *w, int door) {
    int mat=w->mat; GLuint t = door?GT_DOOR_LEFT:(mat==1||mat==3?GT_SERVICE:GT_PANEL);
    double dx=w->x2-w->x1, dy=w->y2-w->y1, len=sqrt(dx*dx+dy*dy);
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, gl_tex[t]);
    glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,0); gl_gv(w->x1,w->y1,0);
    glTexCoord2f(len/256.0,0); gl_gv(w->x2,w->y2,0);
    glTexCoord2f(len/256.0,1); gl_gv(w->x2,w->y2,128);
    glTexCoord2f(0,1); gl_gv(w->x1,w->y1,128);
    glEnd();
}
static void gl_plane(double z, GLuint tex, double off) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, gl_tex[tex]); glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(128/256.0,128/256.0); gl_gv(128,128,z);
    glTexCoord2f(1280/256.0,128/256.0); gl_gv(1280,128,z);
    glTexCoord2f(1280/256.0,1024/256.0); gl_gv(1280,1024,z);
    glTexCoord2f(128/256.0,1024/256.0); gl_gv(128,1024,z);
    glEnd();
}
static void gl_plane_tint(double z, GLuint tex, float tint) {
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, gl_tex[tex]); glColor3f(tint,tint,tint);
    glBegin(GL_QUADS);
    glTexCoord2f(128/256.0f,128/256.0f); gl_gv(128,128,z);
    glTexCoord2f(1280/256.0f,128/256.0f); gl_gv(1280,128,z);
    glTexCoord2f(1280/256.0f,1024/256.0f); gl_gv(1280,1024,z);
    glTexCoord2f(128/256.0f,1024/256.0f); gl_gv(128,1024,z);
    glEnd();
}
static void gl_billboard(GLuint tex, double x,double y,double wworld,double hworld,
                         double pivot_world, double below_ground,int flip) {
    /* camera-facing axes in world/game coordinates */
    double ca=cos(st_current_angle*2.0*M_PI/256.0), sa=sin(st_current_angle*2.0*M_PI/256.0);
    double rx=sa, ry=-ca; /* right */
    double cx = x + rx*pivot_world*64.0;
    double cy = y + ry*pivot_world*64.0;
    double zb = -below_ground*64.0;
    double zt = (hworld-below_ground)*64.0;
    float u0 = flip ? 1.0f : 0.0f, u1 = flip ? 0.0f : 1.0f;
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, tex); glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(u0,0); gl_gv(cx - rx*wworld*32, cy - ry*wworld*32, zb);
    glTexCoord2f(u1,0); gl_gv(cx + rx*wworld*32, cy + ry*wworld*32, zb);
    glTexCoord2f(u1,1); gl_gv(cx + rx*wworld*32, cy + ry*wworld*32, zt);
    glTexCoord2f(u0,1); gl_gv(cx - rx*wworld*32, cy - ry*wworld*32, zt);
    glEnd();
}
static void gl_ammo_seg(float x,float y,float w,float h) {
    glVertex2f(x,y); glVertex2f(x+w,y);
    glVertex2f(x+w,y+h); glVertex2f(x,y+h);
}
static void gl_ammo_digit(int d, float x, float y, float w, float h) {
    float t=h*0.14f, g=h*0.06f;
    float x0=x+g, y0=y+g, x1=x+w-g, y1=y+h-g, ym=y+h*0.5f;
    glBegin(GL_QUADS);
    if (d==0||d==2||d==3||d==5||d==6||d==7||d==8||d==9) gl_ammo_seg(x0,y0,x1-x0,t);
    if (d==0||d==1||d==2||d==3||d==4||d==7||d==8||d==9) gl_ammo_seg(x1-t,y0,t,ym-y0-t);
    if (d==0||d==1||d==2||d==3||d==4||d==5||d==6||d==7||d==8||d==9) gl_ammo_seg(x1-t,ym+t,t,y1-ym-t);
    if (d==0||d==2||d==3||d==5||d==6||d==8||d==9) gl_ammo_seg(x0,y1-t,x1-x0,t);
    if (d==0||d==2||d==6||d==8) gl_ammo_seg(x0,ym+t,t,y1-ym-t);
    if (d==0||d==4||d==5||d==6||d==8||d==9) gl_ammo_seg(x0,y0+t,t,ym-y0-t);
    if (d==2||d==3||d==4||d==5||d==6||d==8||d==9) gl_ammo_seg(x0,ym-t*0.5f,x1-x0,t);
    glEnd();
}
static void gl_draw_weapon(const Status *st) {
    if (st->ammo < 0) return;
    const float w=gl_w*0.60f, h=w*WEAPON_H/WEAPON_W;
    const float x1=gl_w*0.99f, y2=gl_h*1.02f;
    const float x0=x1-w, y0=y2-h;
    glDisable(GL_DEPTH_TEST); glDisable(GL_ALPHA_TEST);
    glEnable(GL_TEXTURE_2D); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,gl_w,gl_h,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glBindTexture(GL_TEXTURE_2D,gl_tex[GT_WEAPON]); glColor4f(1,1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(0,1); glVertex2f(x0,y0);
    glTexCoord2f(1,1); glVertex2f(x1,y0);
    glTexCoord2f(1,0); glVertex2f(x1,y2);
    glTexCoord2f(0,0); glVertex2f(x0,y2);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    /* Live count occupies the blanked display: output-space screen 444,194..519,251. */
    const float sx0=x0+(WEAPON_SCREEN_X0/(float)WEAPON_W)*w;
    const float sx1=x0+(WEAPON_SCREEN_X1/(float)WEAPON_W)*w;
    const float sy0=y0+(WEAPON_SCREEN_Y0/(float)WEAPON_H)*h;
    const float sy1=y0+(WEAPON_SCREEN_Y1/(float)WEAPON_H)*h;
    int value=st->ammo; char text[4]; int n=0;
    if (value > 999) value=999;
    do { text[n++]=(char)('0'+value%10); value/=10; } while (value && n<3);
    float padw=(sx1-sx0)*0.08f, pady=(sy1-sy0)*0.18f;
    float tx0=sx0+padw, tx1=sx1-padw, ty0=sy0+pady, ty1=sy1-pady;
    float cw=(tx1-tx0)/n;
    glColor4f(0.35f,1.0f,0.16f,1);
    for (int i=0;i<n;i++) {
        int d=text[n-1-i]-'0';
        gl_ammo_digit(d,tx0+i*cw,ty0,cw,ty1-ty0);
    }
    glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}
static void gl_draw_tracker(const Status *st) {
    const float s = gl_hud_scale(gl_h);
    glDisable(GL_DEPTH_TEST); glDisable(GL_TEXTURE_2D); glDisable(GL_ALPHA_TEST);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0,gl_w,gl_h,0,-1,1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glColor4f(0.05f,0.07f,0.09f,0.92f);
    glBegin(GL_QUADS); glVertex2f(6*s,142*s); glVertex2f(78*s,142*s);
    glVertex2f(78*s,196*s); glVertex2f(6*s,196*s); glEnd();
    if (st->door_blocked) glColor4f(0.60f,0.42f,0.21f,1);
    else glColor4f(0.24f,0.67f,0.35f,1);
    glBegin(GL_QUADS); glVertex2f(10*s,146*s); glVertex2f(74*s,146*s);
    glVertex2f(74*s,150*s); glVertex2f(10*s,150*s); glEnd();
    glColor4f(0.33f,0.39f,0.35f,1);
    glBegin(GL_LINE_LOOP);
    for (int i=0;i<48;i++) { float a=(float)i*6.2831853f/48.0f;
        glVertex2f((42+cosf(a)*20)*s,(168+sinf(a)*20)*s); }
    glEnd();
    glColor4f(0.15f,0.20f,0.17f,1);
    glBegin(GL_LINES);
    glVertex2f(42*s,147*s); glVertex2f(42*s,189*s);
    glVertex2f(23*s,168*s); glVertex2f(61*s,168*s); glEnd();
    glColor4f(0.72f,0.88f,0.63f,1);
    glBegin(GL_QUADS); glVertex2f(41*s,146*s); glVertex2f(43*s,146*s);
    glVertex2f(43*s,148*s); glVertex2f(41*s,148*s); glEnd();
    int bx,by;
    if (tracker_blip(st,&bx,&by)) {
        if (st->state==2) glColor4f(0.88f,0.31f,0.31f,1);
        else glColor4f(0.44f,0.88f,0.54f,1);
        glBegin(GL_QUADS); glVertex2f((bx-2)*s,(by-2)*s); glVertex2f((bx+3)*s,(by-2)*s);
        glVertex2f((bx+3)*s,(by+3)*s); glVertex2f((bx-2)*s,(by+3)*s); glEnd();
    } else {
        glColor4f(0.30f,0.36f,0.32f,1);
        glBegin(GL_QUADS); glVertex2f(41*s,167*s); glVertex2f(44*s,167*s);
        glVertex2f(44*s,170*s); glVertex2f(41*s,170*s); glEnd();
    }
    glColor4f(1,1,1,1); glDisable(GL_BLEND); glEnable(GL_DEPTH_TEST);
}


static void gl_rgb(float r,float g,float b) { glColor3f(r,g,b); }
static void gl_box(float x1,float y1,float z1,float x2,float y2,float z2,
                   float r,float g,float b) {
    glDisable(GL_TEXTURE_2D);
    /* shaded faces: top/side/front vary slightly for readable massing */
    gl_rgb(r*1.12f,g*1.12f,b*1.12f); glBegin(GL_QUADS);
    gl_gv(x1,y1,z2); gl_gv(x2,y1,z2); gl_gv(x2,y2,z2); gl_gv(x1,y2,z2); glEnd();
    gl_rgb(r*0.68f,g*0.68f,b*0.68f); glBegin(GL_QUADS);
    gl_gv(x1,y1,z1); gl_gv(x1,y2,z1); gl_gv(x1,y2,z2); gl_gv(x1,y1,z2); glEnd();
    gl_rgb(r*0.82f,g*0.82f,b*0.82f); glBegin(GL_QUADS);
    gl_gv(x2,y1,z1); gl_gv(x2,y2,z1); gl_gv(x2,y2,z2); gl_gv(x2,y1,z2); glEnd();
    gl_rgb(r*0.90f,g*0.90f,b*0.90f); glBegin(GL_QUADS);
    gl_gv(x1,y1,z1); gl_gv(x2,y1,z1); gl_gv(x2,y1,z2); gl_gv(x1,y1,z2); glEnd();
    gl_rgb(r*0.74f,g*0.74f,b*0.74f); glBegin(GL_QUADS);
    gl_gv(x1,y2,z1); gl_gv(x2,y2,z1); gl_gv(x2,y2,z2); gl_gv(x1,y2,z2); glEnd();
    gl_rgb(r*0.52f,g*0.52f,b*0.52f); glBegin(GL_QUADS);
    gl_gv(x1,y1,z1); gl_gv(x2,y1,z1); gl_gv(x2,y2,z1); gl_gv(x1,y2,z1); glEnd();
}
static void gl_cyl_x(float x1,float x2,float y,float z,float rad,int seg,
                     float r,float g,float b) {
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b);
    for(int i=0;i<seg;i++){ float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float y0=y+cosf(a0)*rad,z0=z+sinf(a0)*rad,y1=y+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS); gl_gv(x1,y0,z0); gl_gv(x2,y0,z0); gl_gv(x2,y1,z1); gl_gv(x1,y1,z1); glEnd(); }
}
static void gl_cyl_x_lit(float x1,float x2,float y,float z,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        gl_rgb(r*sh,g*sh,b*sh);
        float y0=y+cosf(a0)*rad,z0=z+sinf(a0)*rad,y1=y+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS); gl_gv(x1,y0,z0); gl_gv(x2,y0,z0); gl_gv(x2,y1,z1); gl_gv(x1,y1,z1); glEnd();
    }
}
static void gl_cyl_z(float x,float y,float z1,float z2,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){ float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        gl_gv(x0,y0,z1); gl_gv(x1,y1,z1); gl_gv(x1,y1,z2); gl_gv(x0,y0,z2); }
    glEnd();
}
static void gl_cyl_y(float y1,float y2,float x,float z,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b);
    for(int i=0;i<seg;i++){ float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float x0=x+cosf(a0)*rad,z0=z+sinf(a0)*rad,x1=x+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS); gl_gv(x0,y1,z0); gl_gv(x1,y1,z1); gl_gv(x1,y2,z1); gl_gv(x0,y2,z0); glEnd(); }
}
static void gl_disc_y(float y,float x,float z,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_TRIANGLE_FAN); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg; gl_gv(x+cosf(a)*rad,y,z+sinf(a)*rad); }
    glEnd();
}
static void gl_disc_x(float x,float y,float z,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_TRIANGLE_FAN); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg; gl_gv(x,y+cosf(a)*rad,z+sinf(a)*rad); }
    glEnd();
}
static void gl_disc_z(float z,float x,float y,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_TRIANGLE_FAN); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg; gl_gv(x+cosf(a)*rad,y+sinf(a)*rad,z); }
    glEnd();
}
static void gl_cyl_z_lit(float x,float y,float z1,float z2,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.60f+0.40f*(0.55f+0.45f*cosf(am));
        gl_rgb(r*sh,g*sh,b*sh);
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        glBegin(GL_QUADS); gl_gv(x0,y0,z1); gl_gv(x1,y1,z1); gl_gv(x1,y1,z2); gl_gv(x0,y0,z2); glEnd();
    }
}
static void gl_cyl_y_lit(float y1,float y2,float x,float z,float rad,int seg,float r,float g,float b){
    glDisable(GL_TEXTURE_2D);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.60f+0.40f*(0.55f+0.45f*cosf(am));
        gl_rgb(r*sh,g*sh,b*sh);
        float x0=x+cosf(a0)*rad,z0=z+sinf(a0)*rad,x1=x+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS); gl_gv(x0,y1,z0); gl_gv(x1,y1,z1); gl_gv(x1,y2,z1); gl_gv(x0,y2,z0); glEnd();
    }
}
static void gl_ring_y_tex(GLuint tex,float y,float x,float z,float ri,float ro,int seg,
                          float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint*0.72f,tint*0.86f,tint*1.05f);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float c0=cosf(a0),s0=sinf(a0),c1=cosf(a1),s1=sinf(a1);
        float ua=u0+(u1-u0)*((float)i/seg),ub=u0+(u1-u0)*((float)(i+1)/seg);
        glTexCoord2f(ua,v0); gl_gv(x+c0*ri,y,z+s0*ri);
        glTexCoord2f(ua,v1); gl_gv(x+c0*ro,y,z+s0*ro);
        glTexCoord2f(ub,v1); gl_gv(x+c1*ro,y,z+s1*ro);
        glTexCoord2f(ub,v0); gl_gv(x+c1*ri,y,z+s1*ri);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_turbine_face(float y,float x,float z,float rad,int seg,float r,float g,float b){
    gl_disc_y(y,x,z,rad,seg,0.055f,0.060f,0.060f);
    gl_ring_y_tex(gl_tex[GT_ART_FLOOR],y+0.22f,x,z,rad*0.53f,rad*0.995f,seg,0.82f,0.20f,0.20f,0.80f,0.80f);
    gl_disc_y(y+0.6f,x,z,rad*0.53f,seg,r,g,b);
    gl_disc_y(y+0.9f,x,z,rad*0.20f,seg,0.55f,0.15f,0.025f);
    for(int i=0;i<12;i++){
        float a=(float)i*6.2831853f/12.0f, a1=a+0.11f;
        float ir=rad*0.19f, orr=rad*0.74f;
        glDisable(GL_TEXTURE_2D); gl_rgb(0.035f,0.038f,0.036f); glBegin(GL_QUADS);
        gl_gv(x+cosf(a)*ir,y+1.1f,z+sinf(a)*ir);
        gl_gv(x+cosf(a)*orr,y+1.1f,z+sinf(a)*orr);
        gl_gv(x+cosf(a1)*orr,y+1.1f,z+sinf(a1)*orr);
        gl_gv(x+cosf(a1)*ir,y+1.1f,z+sinf(a1)*ir);
        glEnd();
    }
}
static void gl_solid_plane(float z,float r,float g,float b){
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_QUADS);
    gl_gv(128,128,z); gl_gv(1280,128,z); gl_gv(1280,1024,z); gl_gv(128,1024,z); glEnd();
}
static void gl_solid_wall(const GlWall *w,float r,float g,float b){
    double dx=w->x2-w->x1,dy=w->y2-w->y1;
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b); glBegin(GL_QUADS);
    gl_gv(w->x1,w->y1,0); gl_gv(w->x2,w->y2,0);
    gl_gv(w->x2,w->y2,128); gl_gv(w->x1,w->y1,128); glEnd();
}
static void gl_light_panel(float x1,float y1,float x2,float y2,float z,float heat){
    glDisable(GL_TEXTURE_2D); glEnable(GL_DEPTH_TEST); glDepthMask(GL_FALSE); glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE); gl_rgb(0.92f,0.84f*heat,0.66f*heat);
    glBegin(GL_QUADS); gl_gv(x1,y1,z); gl_gv(x2,y1,z); gl_gv(x2,y2,z); gl_gv(x1,y2,z); glEnd();
    glDisable(GL_BLEND); glDepthMask(GL_TRUE);
}
static void gl_tex_quad(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                        float u0,float v0,float u1,float v1);
static void gl_tex_quad_alpha(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                              float u0,float v0,float u1,float v1,float tint,float alpha);
static void gl_box_tex_uv_rgb(GLuint tex,float x1,float y1,float z1,float x2,float y2,float z2,
                              float r,float g,float b,float u0,float v0,float u1,float v1);
static void gl_cyl_z_tex_rgb(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                             float r,float g,float b,float urep,float vrep);
static void gl_cyl_z_band(float x,float y,float z1,float z2,float rad,
                          float a0,float a1,float r,float g,float b);

static void g5z_label_y(G5zUV uv,float xc,float yface,float zc,float w,float h,int flip) {
    float x0=xc-w*0.5f,x1=xc+w*0.5f;
    if(flip){ float t=x0; x0=x1; x1=t; }
    gl_tex_quad_alpha(gl_tex[GT_G5Z_ATLAS],x0,yface,x1,yface,zc-h*0.5f,zc+h*0.5f,
                      uv.u0,uv.v0,uv.u1,uv.v1,0.92f,0.94f);
}
static void g5z_label_x(G5zUV uv,float xface,float yc,float zc,float w,float h,int flip) {
    float y0=yc-w*0.5f,y1=yc+w*0.5f;
    if(flip){ float t=y0; y0=y1; y1=t; }
    gl_tex_quad_alpha(gl_tex[GT_G5Z_ATLAS],xface,y0,xface,y1,zc-h*0.5f,zc+h*0.5f,
                      uv.u0,uv.v0,uv.u1,uv.v1,0.92f,0.94f);
}
static void g5z_crate(const Obj *o,float br,float bg,float bb,int code,int secondary) {
    float x=o->x,y=o->y,hw=o->hw,hd=o->hd,h=o->h;
    float x0=x-hw,x1=x+hw,y0=y-hd,y1=y+hd;
    gl_box_tex_uv_rgb(gl_tex[GT_ART_FLOOR],x0,y0,0,x1,y1,h,br,bg,bb,0.02f,0.02f,0.34f,0.30f);
    gl_box(x0+1,y0+1,h-0.2f,x1-1,y0+3.2f,h+1.8f,br*0.70f,bg*0.70f,bb*0.70f);
    gl_box(x0+1,y1-3.2f,h-0.2f,x1-1,y1-1,h+1.8f,br*0.70f,bg*0.70f,bb*0.70f);
    gl_box(x0+1,y0+1,h-0.2f,x0+3.2f,y1-1,h+1.8f,br*0.70f,bg*0.70f,bb*0.70f);
    gl_box(x1-3.2f,y0+1,h-0.2f,x1-1,y1-1,h+1.8f,br*0.70f,bg*0.70f,bb*0.70f);
    gl_box(x0+5,y0+5,h+0.02f,x1-5,y1-5,h+0.16f,0.025f,0.03f,0.025f);
    gl_box_tex_uv_rgb(gl_tex[GT_ART_FLOOR],x0+6,y0+6,h+0.16f,x1-6,y1-6,h+0.42f,
                      br*0.64f,bg*0.64f,bb*0.64f,0.36f,0.36f,0.62f,0.62f);
    gl_box(x0+8,y-2,h+0.42f,x1-8,y-1,h+0.52f,0.12f,0.13f,0.11f);
    gl_box(x-4,y-2,h+0.52f,x+4,y+2,h+1.0f,0.30f,0.28f,0.21f);
    gl_box(x-2,y-1,h+1.0f,x+2,y+1,h+1.3f,0.13f,0.13f,0.11f);
    for(int sx=0;sx<2;sx++) for(int sy=0;sy<2;sy++){
        float cx=sx?x1-3.0f:x0, cy=sy?y1-3.0f:y0;
        gl_box(cx,cy,0,cx+3.0f,cy+3.0f,h,0.20f,0.21f,0.18f);
        gl_box(cx-0.25f,cy-0.25f,0,cx+3.25f,cy+3.25f,2.2f,0.34f,0.32f,0.25f);
        gl_box(cx-0.25f,cy-0.25f,h-2.2f,cx+3.25f,cy+3.25f,h,0.34f,0.32f,0.25f);
        gl_box(cx-0.4f,cy-0.4f,h*0.5f,cx+1.2f,cy+1.2f,h*0.5f+1.1f,0.46f,0.42f,0.32f);
    }
    for(int side=0;side<2;side++){
        float yP0=side?y1+0.04f:y0-0.14f, yP1=side?y1+0.14f:y0-0.04f;
        float yF0=side?y1+0.20f:y0-0.40f, yF1=side?y1+0.40f:y0-0.20f;
        float yB0=side?y1+0.16f:y0-0.56f, yB1=side?y1+0.56f:y0-0.16f;
        gl_box_tex_uv_rgb(gl_tex[GT_ART_FLOOR],x0+5,yP0,7,x1-5,yP1,h-7,
                          br*0.78f,bg*0.78f,bb*0.78f,0.36f,0.02f,0.66f,0.28f);
        gl_box(x0+4,yF0,7,x1-4,yF1,8.2f,0.18f,0.18f,0.15f);
        gl_box(x0+4,yF0,h-8.2f,x1-4,yF1,h-7,0.18f,0.18f,0.15f);
        gl_box(x0+4,yF0,7,x0+5.2f,yF1,h-7,0.18f,0.18f,0.15f);
        gl_box(x1-5.2f,yF0,7,x1-4,yF1,h-7,0.18f,0.18f,0.15f);
        for(int i=0;i<4;i++){
            float rx=(i&1)?x1-6.0f:x0+4.4f, rz=(i&2)?h-10.0f:7.8f;
            gl_box(rx,yF1-0.1f,rz,rx+1.4f,yF1+0.3f,rz+1.4f,0.46f,0.42f,0.32f);
        }
        gl_box(x0+4.2f,yB0,4,x0+7.2f,yB1,h-4,0.18f,0.19f,0.16f);
        gl_box(x1-7.2f,yB0,4,x1-4.2f,yB1,h-4,0.18f,0.19f,0.16f);
        gl_box(x0,yB0,4,x1,yB1,6.2f,0.20f,0.20f,0.16f);
        gl_box(x0,yB0,h-6.2f,x1,yB1,h-4,0.20f,0.20f,0.16f);
        float hy=side?y1+0.3f:y0-1.7f;
        gl_box(x0+7,hy,h-13,x0+9,hy+0.8f,h-6,0.23f,0.22f,0.17f);
        gl_box(x1-9,hy,h-13,x1-7,hy+0.8f,h-6,0.23f,0.22f,0.17f);
        gl_box(x0+7,hy+0.2f,h-8,x1-7,hy+0.9f,h-6,0.28f,0.26f,0.20f);
        gl_box(x0+5.5f,hy+0.3f,h*0.08f,x0+14.5f,hy+0.7f,h*0.17f,0.62f,0.44f,0.05f);
        for(int i=0;i<3;i++)
            gl_box(x0+6.4f+i*2.8f,hy+0.7f,h*0.09f,x0+7.4f+i*2.8f,hy+0.9f,h*0.16f,0.07f,0.06f,0.04f);
        gl_box(x0+3.2f,yB1+0.05f,10,x0+4.2f,yB1+0.25f,h*0.55f,0.20f,0.09f,0.035f);
        gl_box(x1-4.2f,yB1+0.05f,h*0.42f,x1-3.2f,yB1+0.25f,h-10,0.19f,0.08f,0.03f);
    }
    G5zUV corp=g5z_uv_rect(0,0,300,26);
    G5zUV eng=g5z_uv_rect(0,50,248,68);
    G5zUV prop=g5z_uv_rect(0,92,344,108);
    G5zUV auth=g5z_uv_rect(0,110,308,126);
    G5zUV gc=(code==204)?g5z_uv_rect(0,72,80,88):(code==205)?g5z_uv_rect(84,72,164,88):g5z_uv_rect(168,72,248,88);
    float lw=(x1-x0)*0.54f; if(lw>40.0f) lw=40.0f;
    float lh=lw*26.0f/300.0f;
    float zc=h*0.66f;
    g5z_label_y(corp,x,y1+0.62f,zc,lw,lh,1);
    g5z_label_y(corp,x,y0-0.62f,zc,lw,lh,0);
    float cw=14.0f,ch=cw*16.0f/80.0f,codz=h*0.40f;
    g5z_label_y(gc,x,y1+0.62f,codz,cw,ch,1);
    g5z_label_y(gc,x,y0-0.62f,codz,cw,ch,0);
    G5zUV sec=(secondary==0)?eng:(secondary==1)?auth:prop;
    float sw=(secondary==0)?30.0f:(secondary==1)?33.0f:37.0f;
    float sh=(secondary==0)?sw*18.0f/248.0f:(secondary==1)?sw*16.0f/308.0f:sw*16.0f/344.0f;
    float secz=h*0.26f;
    g5z_label_y(sec,x,y1+0.62f,secz,sw,sh,1);
    g5z_label_y(sec,x,y0-0.62f,secz,sw,sh,0);
    G5zUV corp_sm=g5z_uv_rect(0,28,200,48);
    g5z_label_x(corp_sm,x0-0.62f,y,h*0.64f,19.0f,19.0f*20.0f/200.0f,1);
    g5z_label_x(corp_sm,x1+0.62f,y,h*0.64f,19.0f,19.0f*20.0f/200.0f,0);
    g5z_label_x(gc,x0-0.62f,y,h*0.46f,11.0f,11.0f*16.0f/80.0f,1);
    g5z_label_x(gc,x1+0.62f,y,h*0.46f,11.0f,11.0f*16.0f/80.0f,0);
}
static void g5z_drum(const Obj *o) {
    float x=o->x,y=o->y,hw=o->hw,hd=o->hd,h=o->h;
    gl_cyl_z_tex_rgb(gl_tex[GT_ART_WALL],x,y,0,h,hw,20,0.74f,0.11f,0.08f,1.25f,0.40f);
    gl_disc_z(0,x,y,hw,20,0.14f,0.035f,0.03f);
    gl_disc_z(h,x,y,hw,20,0.48f,0.11f,0.08f);
    gl_cyl_z(x,y,h,h+1.2f,hw-2.0f,20,0.36f,0.09f,0.07f);
    gl_disc_z(h+1.2f,x,y,hw-2.0f,20,0.36f,0.09f,0.07f);
    gl_cyl_z(x,y,h+1.2f,h+2.2f,4.0f,12,0.20f,0.06f,0.045f);
    for(int i=0;i<4;i++){
        float z=(i==0)?2.0f:(i==1)?18.0f:(i==2)?42.0f:h-5.0f;
        gl_cyl_z(x,y,z,z+2.2f,hw+0.85f,20,0.24f,0.22f,0.18f);
        gl_cyl_z(x,y,z+2.2f,z+2.8f,hw+0.9f,20,0.40f,0.35f,0.26f);
    }
    glDisable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);
    for(int i=0;i<20;i++){
        float a0=(float)i*6.2831853f/20.0f,a1=(float)(i+1)*6.2831853f/20.0f;
        float ax0=x+cosf(a0)*(hw+0.15f),ay0=y+sinf(a0)*(hw+0.15f);
        float ax1=x+cosf(a1)*(hw+0.15f),ay1=y+sinf(a1)*(hw+0.15f);
        gl_rgb((i&1)?0.66f:0.05f,(i&1)?0.47f:0.045f,0.035f);
        gl_gv(ax0,ay0,47.0f); gl_gv(ax1,ay1,47.0f);
        gl_gv(ax1,ay1,52.0f); gl_gv(ax0,ay0,52.0f);
    }
    glEnd();
    for(int side=0;side<2;side++){
        float xx=side?x+hw-0.2f:x-hw-1.1f;
        gl_box(xx,y-7,25,xx+0.9f,y-5,39,0.21f,0.20f,0.17f);
        gl_box(xx,y+5,25,xx+0.9f,y+7,39,0.21f,0.20f,0.17f);
        gl_box(xx+0.2f,y-7,37,xx+0.9f,y+7,39,0.29f,0.27f,0.21f);
    }
    G5zUV logo=g5z_uv_rect(320,0,420,38);
    G5zUV gc=(o->x<920)?g5z_uv_rect(430,50,502,66):g5z_uv_rect(430,70,502,86);
    for(int side=0;side<2;side++){
        float yp=side?y+hd-0.1f:y-hd-1.1f;
        gl_box(x-10.0f,yp,20.5f,x+10.0f,yp+1.2f,36.0f,0.10f,0.11f,0.10f);
        gl_box(x-10.0f,yp+1.2f,20.5f,x+10.0f,yp+1.5f,21.7f,0.34f,0.31f,0.24f);
        gl_box(x-10.0f,yp+1.2f,34.8f,x+10.0f,yp+1.5f,36.0f,0.26f,0.24f,0.18f);
        gl_box(x-10.0f,yp+1.2f,21.7f,x-8.7f,yp+1.5f,34.8f,0.26f,0.24f,0.18f);
        gl_box(x+8.7f,yp+1.2f,21.7f,x+10.0f,yp+1.5f,34.8f,0.26f,0.24f,0.18f);
        for(int sx=0;sx<2;sx++) for(int sz=0;sz<2;sz++)
            gl_box(sx?x+9.0f:x-9.8f,yp+1.2f,sz?33.6f:22.2f,
                   sx?x+9.8f:x-9.0f,yp+1.7f,sz?34.6f:23.2f,0.46f,0.42f,0.32f);
        for(int sx=0;sx<2;sx++) for(int sz=0;sz<2;sz++)
            gl_box(sx?x+5.0f:x-6.0f,yp-1.0f,sz?31.0f:24.0f,
                   sx?x+6.0f:x-5.0f,yp+0.1f,sz?33.0f:26.0f,0.25f,0.23f,0.18f);
        g5z_label_y(logo,x,side?yp+1.55f:yp-0.05f,28.6f,17.5f,17.5f*38.0f/100.0f,side?1:0);
        g5z_label_y(gc,x,side?yp+1.55f:yp-0.05f,23.5f,9.5f,9.5f*16.0f/72.0f,side?1:0);
        gl_box(x-3.0f,yp+(side?0.0f:1.2f),9,x-2.2f,yp+(side?1.2f:0.0f),16,0.22f,0.10f,0.04f);
        gl_box(x+2.2f,yp+(side?0.0f:1.2f),33,x+3.0f,yp+(side?1.2f:0.0f),43,0.20f,0.09f,0.035f);
    }
}
static void gl_prop_3d(const Obj *o){
    float x=o->x, y=o->y, hw=o->hw, hd=o->hd, h=o->h;
    if(o->id==0){
        g5z_drum(o);
    } else if(o->id==1){
        g5z_crate(o,0.66f,0.68f,0.34f,o->x<950?204:205,0);
    } else if(o->id==2){
        gl_box(x-hw,y-hd,0,x+hw,y+hd,h,0.15f,0.17f,0.17f);
        for(int side=0;side<2;side++){
            float yy = side?y+hd-1:y-hd-3;
            gl_box(x-hw+2,yy,4,x+hw-2,yy+2,h-4,0.20f,0.22f,0.21f);
            gl_box(x-1,yy-1,4,x+1,yy+3,h-4,0.08f,0.09f,0.09f);
            for(int z=14;z<h-15;z+=5) gl_box(x-hw+6,yy-1,z,x+hw-6,yy+1,z+2,0.08f,0.09f,0.09f);
            gl_box(x-hw+4,yy, h-18,x-hw+6,yy+2,h-12,0.55f,0.42f,0.08f);
            gl_box(x+hw-6,yy, h-18,x+hw-4,yy+2,h-12,0.55f,0.42f,0.08f);
        }
    } else {
        g5z_crate(o,0.20f,0.44f,0.25f,208,1);
    }
}

static void gl_tex_quad(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                        float u0,float v0,float u1,float v1){
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(1,1,1);
    glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1);
    glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2);
    glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_tex_quad_tint(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                              float u0,float v0,float u1,float v1,float tint){
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint,tint,tint);
    glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1);
    glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2);
    glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glEnd(); glDisable(GL_TEXTURE_2D);
}

static void gl_tex_quad_tint_add(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                                  float u0,float v0,float u1,float v1,float add)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_ADD);
    glColor3f(add,add,add); glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1);
    glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2);
    glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glEnd(); glTexEnvi(GL_TEXTURE_ENV,GL_TEXTURE_ENV_MODE,GL_MODULATE);
    glDisable(GL_TEXTURE_2D);
}
static void gl_tex_plane(GLuint tex,float x1,float y1,float x2,float y2,float z,
                         float u0,float v0,float u1,float v1,float tint)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glColor3f(tint,tint,tint); glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z);
    glTexCoord2f(u1,v0); gl_gv(x2,y1,z);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z);
    glTexCoord2f(u0,v1); gl_gv(x1,y2,z);
    glEnd(); glDisable(GL_TEXTURE_2D);
}

static void gl_box_tex(GLuint tex,float x1,float y1,float z1,float x2,float y2,float z2,
                       float tint,float us,float vs)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    glColor3f(tint*1.08f,tint*1.06f,tint*1.02f);
    glTexCoord2f(0,0); gl_gv(x1,y1,z2); glTexCoord2f(us,0); gl_gv(x2,y1,z2);
    glTexCoord2f(us,vs); gl_gv(x2,y2,z2); glTexCoord2f(0,vs); gl_gv(x1,y2,z2);
    glColor3f(tint*0.88f,tint*0.88f,tint*0.86f);
    glTexCoord2f(0,0); gl_gv(x1,y1,z1); glTexCoord2f(us,0); gl_gv(x1,y2,z1);
    glTexCoord2f(us,vs); gl_gv(x1,y2,z2); glTexCoord2f(0,vs); gl_gv(x1,y1,z2);
    glColor3f(tint*0.78f,tint*0.78f,tint*0.76f);
    glTexCoord2f(0,0); gl_gv(x2,y1,z1); glTexCoord2f(us,0); gl_gv(x2,y2,z1);
    glTexCoord2f(us,vs); gl_gv(x2,y2,z2); glTexCoord2f(0,vs); gl_gv(x2,y1,z2);
    glColor3f(tint*0.96f,tint*0.94f,tint*0.90f);
    glTexCoord2f(0,0); gl_gv(x1,y1,z1); glTexCoord2f(us,0); gl_gv(x2,y1,z1);
    glTexCoord2f(us,vs); gl_gv(x2,y1,z2); glTexCoord2f(0,vs); gl_gv(x1,y1,z2);
    glColor3f(tint*0.70f,tint*0.70f,tint*0.68f);
    glTexCoord2f(0,0); gl_gv(x1,y2,z1); glTexCoord2f(us,0); gl_gv(x2,y2,z1);
    glTexCoord2f(us,vs); gl_gv(x2,y2,z2); glTexCoord2f(0,vs); gl_gv(x1,y2,z2);
    glColor3f(tint*0.50f,tint*0.50f,tint*0.48f);
    glTexCoord2f(0,0); gl_gv(x1,y1,z1); glTexCoord2f(us,0); gl_gv(x2,y1,z1);
    glTexCoord2f(us,vs); gl_gv(x2,y2,z1); glTexCoord2f(0,vs); gl_gv(x1,y2,z1);
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_x_tex(GLuint tex,float x1,float x2,float y,float z,float rad,int seg,
                         float tint,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float y0=y+cosf(a0)*rad,z0=z+sinf(a0)*rad,y1=y+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(0,0); gl_gv(x1,y0,z0);
        glTexCoord2f(0,vrep); gl_gv(x2,y0,z0);
        glTexCoord2f(urep,vrep); gl_gv(x2,y1,z1);
        glTexCoord2f(urep,0); gl_gv(x1,y1,z1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_y_tex(GLuint tex,float y1,float y2,float x,float z,float rad,int seg,
                         float tint,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float x0=x+cosf(a0)*rad,z0=z+sinf(a0)*rad,x1=x+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(0,0); gl_gv(x0,y1,z0);
        glTexCoord2f(0,vrep); gl_gv(x0,y2,z0);
        glTexCoord2f(urep,vrep); gl_gv(x1,y2,z1);
        glTexCoord2f(urep,0); gl_gv(x1,y1,z1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_box_tex_uv(GLuint tex,float x1,float y1,float z1,float x2,float y2,float z2,
                          float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    glColor3f(tint*1.08f,tint*1.06f,tint*1.02f);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z2); glTexCoord2f(u1,v0); gl_gv(x2,y1,z2);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2); glTexCoord2f(u0,v1); gl_gv(x1,y2,z2);
    glColor3f(tint*0.88f,tint*0.88f,tint*0.86f);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1); glTexCoord2f(u1,v0); gl_gv(x1,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x1,y2,z2); glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glColor3f(tint*0.78f,tint*0.78f,tint*0.76f);
    glTexCoord2f(u0,v0); gl_gv(x2,y1,z1); glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2); glTexCoord2f(u0,v1); gl_gv(x2,y1,z2);
    glColor3f(tint*0.96f,tint*0.94f,tint*0.90f);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1); glTexCoord2f(u1,v0); gl_gv(x2,y1,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y1,z2); glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glColor3f(tint*0.70f,tint*0.70f,tint*0.68f);
    glTexCoord2f(u0,v0); gl_gv(x1,y2,z1); glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2); glTexCoord2f(u0,v1); gl_gv(x1,y2,z2);
    glColor3f(tint*0.50f,tint*0.50f,tint*0.48f);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1); glTexCoord2f(u1,v0); gl_gv(x2,y1,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z1); glTexCoord2f(u0,v1); gl_gv(x1,y2,z1);
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_box_tex_uv_rgb(GLuint tex,float x1,float y1,float z1,float x2,float y2,float z2,
                              float r,float g,float b,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    const float sr[6]={1.08f,0.88f,0.78f,0.96f,0.70f,0.50f};
    const float sg[6]={1.06f,0.88f,0.78f,0.94f,0.70f,0.50f};
    const float sb[6]={1.02f,0.86f,0.76f,0.90f,0.68f,0.48f};
    glBegin(GL_QUADS);
    for(int f=0;f<6;f++){
        glColor3f(r*sr[f],g*sg[f],b*sb[f]);
        for(int v=0;v<4;v++){
            int ix,iy; float zz;
            if(f==0){ ix=v==0||v==3?0:1; iy=v<2?0:1; zz=z2; }
            else if(f==1){ ix=0; iy=v==0||v==3?0:1; zz=v<2?z1:z2; }
            else if(f==2){ ix=1; iy=v==0||v==3?0:1; zz=v<2?z1:z2; }
            else if(f==3){ ix=v==0||v==3?0:1; iy=0; zz=v<2?z1:z2; }
            else if(f==4){ ix=v==0||v==3?0:1; iy=1; zz=v<2?z1:z2; }
            else { ix=v==0||v==3?0:1; iy=v<2?0:1; zz=z1; }
            glTexCoord2f(ix?u1:u0, iy?v1:v0);
            gl_gv(ix?x2:x1, iy?y2:y1, zz);
        }
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_y_tex_flat(GLuint tex,float y1,float y2,float x,float z,float rad,int seg,
                             float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint,tint,tint);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float x0=x+cosf(a0)*rad,z0=z+sinf(a0)*rad,x1=x+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0); gl_gv(x0,y1,z0);
        glTexCoord2f(u0,v1); gl_gv(x0,y2,z0);
        glTexCoord2f(u1,v1); gl_gv(x1,y2,z1);
        glTexCoord2f(u1,v0); gl_gv(x1,y1,z1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
/* G5w: smooth, cylindrical UV mapping for real machinery surfaces. */
static void gl_cyl_x_tex_smooth(GLuint tex,float x1,float x2,float y,float z,float rad,int seg,
                                float tint,float u0,float v0,float u1,float v1,float urep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float c0=0.60f+0.40f*(0.55f+0.45f*cosf(a0));
        float c1=0.60f+0.40f*(0.55f+0.45f*cosf(a1));
        float y0=y+cosf(a0)*rad,z0=z+sinf(a0)*rad;
        float y1=y+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        float t0=(float)i/seg*urep,t1=(float)(i+1)/seg*urep;
        float ua=u0+(u1-u0)*(t0-floorf(t0));
        float ub=u0+(u1-u0)*(t1-floorf(t1));
        float va0=v0+(v1-v0)*((float)i/seg),va1=v0+(v1-v0)*((float)(i+1)/seg);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,va0); gl_gv(x1,y0,z0);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,va1); gl_gv(x2,y0,z0);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,va1); gl_gv(x2,y1,z1);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,va0); gl_gv(x1,y1,z1);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_y_tex_smooth(GLuint tex,float y1,float y2,float x,float z,float rad,int seg,
                                float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float c0=0.60f+0.40f*(0.55f+0.45f*cosf(a0));
        float c1=0.60f+0.40f*(0.55f+0.45f*cosf(a1));
        float x0=x+cosf(a0)*rad,z0=z+sinf(a0)*rad;
        float x1=x+cosf(a1)*rad,z1=z+sinf(a1)*rad;
        float ua=u0+(u1-u0)*((float)i/seg),ub=u0+(u1-u0)*((float)(i+1)/seg);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,v0); gl_gv(x0,y1,z0);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,v1); gl_gv(x0,y2,z0);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,v1); gl_gv(x1,y2,z1);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,v0); gl_gv(x1,y1,z1);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_tex_smooth(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                                float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float c0=0.60f+0.40f*(0.55f+0.45f*cosf(a0));
        float c1=0.60f+0.40f*(0.55f+0.45f*cosf(a1));
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad;
        float x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        float ua=u0+(u1-u0)*((float)i/seg),ub=u0+(u1-u0)*((float)(i+1)/seg);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,v0); gl_gv(x0,y0,z1);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ub,v0); gl_gv(x1,y1,z1);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,v1); gl_gv(x1,y1,z2);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ua,v1); gl_gv(x0,y0,z2);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_tex_rgb(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                             float r,float g,float b,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(r*sh,g*sh,b*sh);
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        glTexCoord2f(0,0); gl_gv(x0,y0,z1);
        glTexCoord2f(urep,0); gl_gv(x1,y1,z1);
        glTexCoord2f(urep,vrep); gl_gv(x1,y1,z2);
        glTexCoord2f(0,vrep); gl_gv(x0,y0,z2);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_tex_rot(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                             float tint,float u0,float v0,float u1,float v1,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glBegin(GL_QUADS);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float c0=0.60f+0.40f*(0.55f+0.45f*cosf(a0));
        float c1=0.60f+0.40f*(0.55f+0.45f*cosf(a1));
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad;
        float x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        float ua=u0,ub=u1;
        float t0=(float)i/seg*vrep,t1=(float)(i+1)/seg*vrep;
        float va0=v0+(v1-v0)*(t0-floorf(t0)),va1=v0+(v1-v0)*(t1-floorf(t1));
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,va0); gl_gv(x0,y0,z1);
        glColor3f(tint*c0,tint*c0,tint*c0); glTexCoord2f(ua,va1); gl_gv(x1,y1,z1);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,va1); gl_gv(x1,y1,z2);
        glColor3f(tint*c1,tint*c1,tint*c1); glTexCoord2f(ub,va0); gl_gv(x0,y0,z2);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_disc_y_tex_rgb(GLuint tex,float y,float x,float z,float rad,int seg,
                              float r,float g,float b,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(r,g,b);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f((u0+u1)*0.5f,(v0+v1)*0.5f); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg;
        glTexCoord2f(u0+(u1-u0)*(0.5f+0.5f*cosf(a)),v0+(v1-v0)*(0.5f+0.5f*sinf(a)));
        gl_gv(x+cosf(a)*rad,y,z+sinf(a)*rad);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_disc_x_tex_rgb(GLuint tex,float x,float y,float z,float rad,int seg,
                              float r,float g,float b,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(r,g,b);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f((u0+u1)*0.5f,(v0+v1)*0.5f); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg;
        glTexCoord2f(u0+(u1-u0)*(0.5f+0.5f*cosf(a)),v0+(v1-v0)*(0.5f+0.5f*sinf(a)));
        gl_gv(x,y+cosf(a)*rad,z+sinf(a)*rad);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_disc_y_tex(GLuint tex,float y,float x,float z,float rad,int seg,
                          float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint,tint,tint);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f((u0+u1)*0.5f,(v0+v1)*0.5f); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg;
        glTexCoord2f(u0+(u1-u0)*(0.5f+0.5f*cosf(a)),v0+(v1-v0)*(0.5f+0.5f*sinf(a)));
        gl_gv(x+cosf(a)*rad,y,z+sinf(a)*rad);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_disc_z_tex(GLuint tex,float z,float x,float y,float rad,int seg,
                          float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint,tint,tint);
    glBegin(GL_TRIANGLE_FAN);
    glTexCoord2f((u0+u1)*0.5f,(v0+v1)*0.5f); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float a=(float)i*6.2831853f/seg;
        glTexCoord2f(u0+(u1-u0)*(0.5f+0.5f*cosf(a)),v0+(v1-v0)*(0.5f+0.5f*sinf(a)));
        gl_gv(x+cosf(a)*rad,y+sinf(a)*rad,z);
    }
    glEnd(); glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_band(float x,float y,float z1,float z2,float rad,
                          float a0,float a1,float r,float g,float b)
{
    glDisable(GL_TEXTURE_2D); gl_rgb(r,g,b);
    float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
    glBegin(GL_QUADS); gl_gv(x0,y0,z1); gl_gv(x1,y1,z1); gl_gv(x1,y1,z2); gl_gv(x0,y0,z2); glEnd();
}
static void gl_cyl_z_tex_flat(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                             float tint,float u0,float v0,float u1,float v1)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex); glColor3f(tint,tint,tint);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg;
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0); gl_gv(x0,y0,z1);
        glTexCoord2f(u1,v0); gl_gv(x1,y1,z1);
        glTexCoord2f(u1,v1); gl_gv(x1,y1,z2);
        glTexCoord2f(u0,v1); gl_gv(x0,y0,z2);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_x_tex_uv(GLuint tex,float x1,float x2,float y,float z,float rad,int seg,
                            float tint,float u0,float v0,float u1,float v1,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float yy0=y+cosf(a0)*rad,zz0=z+sinf(a0)*rad,yy1=y+cosf(a1)*rad,zz1=z+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0); gl_gv(x1,yy0,zz0);
        glTexCoord2f(u0,v1); gl_gv(x2,yy0,zz0);
        glTexCoord2f(u1,v1); gl_gv(x2,yy1,zz1);
        glTexCoord2f(u1,v0); gl_gv(x1,yy1,zz1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_y_tex_uv(GLuint tex,float y1,float y2,float x,float z,float rad,int seg,
                            float tint,float u0,float v0,float u1,float v1,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float xx0=x+cosf(a0)*rad,zz0=z+sinf(a0)*rad,xx1=x+cosf(a1)*rad,zz1=z+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0); gl_gv(xx0,y1,zz0);
        glTexCoord2f(u0,v1); gl_gv(xx0,y2,zz0);
        glTexCoord2f(u1,v1); gl_gv(xx1,y2,zz1);
        glTexCoord2f(u1,v0); gl_gv(xx1,y1,zz1);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_tex_uv(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                            float tint,float u0,float v0,float u1,float v1,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float xx0=x+cosf(a0)*rad,yy0=y+sinf(a0)*rad,xx1=x+cosf(a1)*rad,yy1=y+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(u0,v0); gl_gv(xx0,yy0,z1);
        glTexCoord2f(u1,v0); gl_gv(xx1,yy1,z1);
        glTexCoord2f(u1,v1); gl_gv(xx1,yy1,z2);
        glTexCoord2f(u0,v1); gl_gv(xx0,yy0,z2);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_cyl_z_tex(GLuint tex,float x,float y,float z1,float z2,float rad,int seg,
                         float tint,float urep,float vrep)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    for(int i=0;i<seg;i++){
        float a0=(float)i*6.2831853f/seg,a1=(float)(i+1)*6.2831853f/seg,am=(a0+a1)*0.5f;
        float sh=0.52f+0.48f*(0.55f+0.45f*cosf(am));
        glColor3f(tint*sh,tint*sh,tint*sh);
        float x0=x+cosf(a0)*rad,y0=y+sinf(a0)*rad,x1=x+cosf(a1)*rad,y1=y+sinf(a1)*rad;
        glBegin(GL_QUADS);
        glTexCoord2f(0,0); gl_gv(x0,y0,z1);
        glTexCoord2f(urep,0); gl_gv(x1,y1,z1);
        glTexCoord2f(urep,vrep); gl_gv(x1,y1,z2);
        glTexCoord2f(0,vrep); gl_gv(x0,y0,z2);
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
}
static void gl_tex_quad_alpha(GLuint tex,float x1,float y1,float x2,float y2,float z1,float z2,
                              float u0,float v0,float u1,float v1,float tint,float alpha)
{
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,tex);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA); glDepthMask(GL_FALSE);
    glColor4f(tint,tint,tint,alpha); glBegin(GL_QUADS);
    glTexCoord2f(u0,v0); gl_gv(x1,y1,z1);
    glTexCoord2f(u1,v0); gl_gv(x2,y2,z1);
    glTexCoord2f(u1,v1); gl_gv(x2,y2,z2);
    glTexCoord2f(u0,v1); gl_gv(x1,y1,z2);
    glEnd(); glDepthMask(GL_TRUE); glDisable(GL_BLEND); glDisable(GL_TEXTURE_2D);
}
static void gl_glow_quad(float x1,float y1,float x2,float y2,float z,
                         float r,float g,float b,float a)
{
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE); glColor4f(r,g,b,a); glBegin(GL_QUADS);
    gl_gv(x1,y1,z); gl_gv(x2,y1,z); gl_gv(x2,y2,z); gl_gv(x1,y2,z); glEnd();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
static void gl_glow_wall_y(float y,float x1,float x2,float z1,float z2,
                           float r,float g,float b,float a)
{
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE); glColor4f(r,g,b,a); glBegin(GL_QUADS);
    gl_gv(x1,y,z1); gl_gv(x2,y,z1); gl_gv(x2,y,z2); gl_gv(x1,y,z2); glEnd();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
static void gl_glow_wall_x(float x,float y1,float y2,float z1,float z2,
                           float r,float g,float b,float a)
{
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE); glColor4f(r,g,b,a); glBegin(GL_QUADS);
    gl_gv(x,y1,z1); gl_gv(x,y2,z1); gl_gv(x,y2,z2); gl_gv(x,y1,z2); glEnd();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
static void gl_glow_disc_y(float y,float x,float z,float rad,int seg,float r,float g,float b,float a)
{
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE); glColor4f(r,g,b,a); glBegin(GL_TRIANGLE_FAN); gl_gv(x,y,z);
    for(int i=0;i<=seg;i++){ float aa=(float)i*6.2831853f/seg; gl_gv(x+cosf(aa)*rad,y,z+sinf(aa)*rad); }
    glEnd(); glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}
static void gl_light_panel_c(float x1,float y1,float x2,float y2,float z,float r,float g,float b)
{
    glDisable(GL_TEXTURE_2D); glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE);
    glDepthMask(GL_FALSE); glColor3f(r,g,b); glBegin(GL_QUADS);
    gl_gv(x1,y1,z); gl_gv(x2,y1,z); gl_gv(x2,y2,z); gl_gv(x1,y2,z); glEnd();
    glDepthMask(GL_TRUE); glDisable(GL_BLEND);
}

static void gl_corridor_static(void) {
    const float wallR=0.36f,wallG=0.33f,wallB=0.29f;
    const float darkR=0.14f,darkG=0.13f,darkB=0.12f;
    /* base floor uses the existing deck material; ceiling stays a deliberately dark plane */
    gl_plane(0, corridor_art?GT_ART_FLOOR:GT_FLOOR, 0);
    gl_plane_tint(128, GT_PANEL, 0.30f);
    for(int i=0;i<10;i++) gl_wall(&gl_walls[i],0);
    /* recessed side panels, ribs and lower service housings */
    for(int x=780;x<1260;x+=80){
        gl_box(x,258,18,x+64,262,108,darkR*1.3f,darkG*1.2f,darkB*1.1f);
        gl_box(x,378,18,x+64,382,108,darkR*1.3f,darkG*1.2f,darkB*1.1f);
        gl_box(x+3,257,22,x+61,258,104,0.16f,0.15f,0.13f);
        gl_box(x+3,382,22,x+61,383,104,0.16f,0.15f,0.13f);
        for(int z=28;z<100;z+=14){ gl_box(x+8,257,z,x+56,258,z+3,0.08f,0.08f,0.07f);
            gl_box(x+8,382,z,x+56,383,z+3,0.08f,0.08f,0.07f); }
        gl_box(x,258,4,x+72,266,36,0.28f,0.25f,0.20f);
        gl_box(x,374,4,x+72,382,36,0.28f,0.25f,0.20f);
        gl_tex_quad_tint(gl_tex[GT_SERVICE],x+2,266.1f,x+70,266.1f,5,35,0,0,2,1,0.55f);
        gl_tex_quad_tint(gl_tex[GT_SERVICE],x+2,373.9f,x+70,373.9f,5,35,0,0,2,1,0.55f);
        gl_box(x,260,14,x+72,264,18,1.0f,0.46f,0.08f); /* warm floor strip */
        gl_box(x,378,14,x+72,382,18,1.0f,0.46f,0.08f);
    }
    for(int x=768;x<=1280;x+=80){
        gl_box(x,250,0,x+8,262,128,darkR,darkG,darkB);
        gl_box(x,378,0,x+8,390,128,darkR,darkG,darkB);
        gl_box(x,256,120,x+8,384,128,darkR,darkG,darkB); /* ceiling rib */
        gl_box(x,256,0,x+8,384,4,darkR,darkG,darkB); /* floor rib */
    }
    /* longitudinal pipes and conduit runs */
    for(int y=264;y<=376;y+=112){
        gl_cyl_x(768,1280,y,112,5,10,0.20f,0.20f,0.19f);
        gl_cyl_x(768,1280,y+8,118,3,8,0.14f,0.10f,0.08f);
    }
    for(int x=840;x<1240;x+=160){
        gl_box(x,258,96,x+8,264,112,0.28f,0.12f,0.08f);
        gl_box(x,376,96,x+8,382,112,0.28f,0.12f,0.08f);
    }
    /* recessed grille light fixtures with real housing/frame depth */
    for(int x=820;x<1240;x+=160){
        /* dark recess, perimeter frame, lens and protective bars */
        gl_box(x+6,288,126.0f,x+74,352,128,0.055f,0.055f,0.05f);
        gl_box(x+2,284,123,x+10,356,128,0.12f,0.12f,0.11f);
        gl_box(x+70,284,123,x+78,356,128,0.12f,0.12f,0.11f);
        gl_box(x+2,284,123,x+78,292,128,0.12f,0.12f,0.11f);
        gl_box(x+2,348,123,x+78,356,128,0.12f,0.12f,0.11f);
        for(int y=306;y<342;y+=14) gl_box(x+13,y,125.35f,x+67,y+1,125.6f,0.09f,0.09f,0.08f);
        for(int z=123;z<126;z+=2) gl_box(x+82,292,z,x+88,346,z+1,0.08f,0.08f,0.07f);
    }
    /* deck: one world-scaled plate texture plus edge drainage and seams */
    if(corridor_art){
        gl_tex_quad_tint(gl_tex[GT_ART_FLOOR],768,260,1280,380,3.1f,3.1f,
                    0,(260-256)/128.0f,4.0f,(380-256)/128.0f,1.75f);
        for(int x=896;x<1260;x+=128) gl_box(x-1,270,3.1f,x+1,370,3.6f,0.08f,0.08f,0.07f);
        gl_box(768,318,3.1f,1280,321,3.5f,0.07f,0.08f,0.08f);
        for(int x=780;x<1260;x+=16) gl_box(x,318,3.5f,x+8,321,3.8f,0.18f,0.19f,0.18f);
        /* narrow side drainage grilles, not a black floor grid */
        for(int x=780;x<1260;x+=12){
            gl_box(x,260,2,x+8,268,3.5f,0.16f,0.17f,0.16f);
            gl_box(x,372,2,x+8,380,3.5f,0.16f,0.17f,0.16f);
        }
        for(int y=262;y<268;y+=3) gl_box(768,y,3,1280,y+1,3.4f,0.22f,0.23f,0.21f);
        for(int y=372;y<378;y+=3) gl_box(768,y,3,1280,y+1,3.4f,0.22f,0.23f,0.21f);
    } else {
        for(int x=780;x<1260;x+=40){
            gl_box(x,270,1,x+36,370,3,0.30f,0.29f,0.26f);
            for(int y=282;y<366;y+=18) gl_box(x, y,3, x+36, y+2, 4, 0.07f,0.08f,0.07f);
        }
        gl_box(768,260,0.5f,1280,270,2.5f,0.06f,0.07f,0.07f);
        gl_box(768,370,0.5f,1280,380,2.5f,0.06f,0.07f,0.07f);
    }
    if(corridor_art){
        /* prototype wall-panel material: one image per recessed panel */
        for(int x=780;x<1260;x+=80){
            gl_tex_quad(gl_tex[GT_ART_PANEL],x+3,262.1f,x+61,262.1f,22,104,0,0,1,1);
            gl_tex_quad(gl_tex[GT_ART_PANEL],x+3,377.9f,x+61,377.9f,22,104,0,0,1,1);
        }
        /* prototype bulkhead material on far door face */
        gl_tex_quad(gl_tex[GT_ART_WALL],1273.4f,256,1273.4f,384,0,128,1,0,0,1);
    }
    /* visual destination bulkhead at the far end (existing wall remains gameplay boundary) */
    gl_box(1272,244,0,1280,256,136,0.10f,0.10f,0.09f);
    gl_box(1272,384,0,1280,396,136,0.10f,0.10f,0.09f);
    gl_box(1272,244,128,1280,396,136,0.08f,0.08f,0.08f);
    gl_box(1274,256,0,1278,384,128,0.28f,0.25f,0.16f);
    for(int z=14;z<60;z+=12) gl_box(1274,264,z,1278,376,z+5,0.55f,0.40f,0.08f);
}

/* ===== G5x high-resolution sliding blast door ===== */
#define DOOR_X0 768.0f
#define DOOR_X1 776.0f
#define DOOR_Z0 8.0f
#define DOOR_Z1 120.0f

static void gl_door_leaf(GLuint tex,float y1,float y2,float seam,float tint,int flip_u)
{
    /* Body is 8 world units thick; the supplied leaf art covers both faces.
     * West (room) faces need U reversed because screen-right is decreasing Y. */
    float uw0 = flip_u ? 1.0f : 0.0f, uw1 = flip_u ? 0.0f : 1.0f;
    float ue0 = flip_u ? 0.0f : 1.0f, ue1 = flip_u ? 1.0f : 0.0f;
    gl_box(DOOR_X0,y1,DOOR_Z0,DOOR_X1,y2,DOOR_Z1,0.22f,0.17f,0.08f);
    if(last_px < DOOR_X0)
        gl_tex_quad_tint(tex,DOOR_X0-0.1f,y1,DOOR_X0-0.1f,y2,DOOR_Z0,DOOR_Z1,
                         uw0,0,uw1,1,tint);
    else if(last_px > DOOR_X1)
        gl_tex_quad_tint(tex,DOOR_X1+0.1f,y1,DOOR_X1+0.1f,y2,DOOR_Z0,DOOR_Z1,
                         ue0,0,ue1,1,tint*0.86f);
    /* Painted edge returns, inner interlock and outer hinge edges. */
    float out = (seam > (y1+y2)*0.5f) ? y1 : y2;
    gl_box(DOOR_X0-0.4f,y1,DOOR_Z0,DOOR_X1+0.4f,y1+1.0f,DOOR_Z1,0.16f,0.13f,0.075f);
    gl_box(DOOR_X0-0.4f,y2-1.0f,DOOR_Z0,DOOR_X1+0.4f,y2,DOOR_Z1,0.16f,0.13f,0.075f);
    if (seam > (y1+y2)*0.5f)
        gl_box(DOOR_X0-0.5f,y2-1.2f,DOOR_Z0,DOOR_X1+0.5f,y2,DOOR_Z1,0.075f,0.070f,0.060f);
    else
        gl_box(DOOR_X0-0.5f,y1,DOOR_Z0,DOOR_X1+0.5f,y1+1.2f,DOOR_Z1,0.075f,0.070f,0.060f);
    gl_box(DOOR_X0-0.7f,out-2.0f,10,DOOR_X1+0.7f,out+2.0f,DOOR_Z1-2,0.13f,0.11f,0.075f);
    gl_box(DOOR_X0-0.8f,out-4.0f,14,DOOR_X1+0.8f,out+4.0f,28,0.075f,0.070f,0.058f);
    gl_box(DOOR_X0-0.8f,out-4.0f,100,DOOR_X1+0.8f,out+4.0f,114,0.075f,0.070f,0.058f);
    gl_box(DOOR_X0-1.4f,out-3.0f,20,DOOR_X0-0.6f,out+3.0f,25,0.30f,0.25f,0.16f);
    gl_box(DOOR_X0-1.4f,out-3.0f,103,DOOR_X0-0.6f,out+3.0f,108,0.30f,0.25f,0.16f);
}

static void gl_blast_door(float light,int door_open)
{
    int raw = door_open < 0 ? 0 : (door_open > 64 ? 64 : door_open);
    float travel = (float)raw; /* two 64-wide leaves; 64 units parks each in its pocket */
    float ny1 = 256.0f - travel, ny2 = 320.0f - travel;
    float sy1 = 320.0f + travel, sy2 = 384.0f + travel;
    float tint = 0.74f + 0.08f*light;

    /* Deep casing: jambs, header, threshold and side receiving pockets. */
    gl_box(760,244,0,784,256,136,0.10f,0.10f,0.09f);
    gl_box(760,384,0,784,396,136,0.10f,0.10f,0.09f);
    gl_box(760,244,120,784,396,136,0.08f,0.08f,0.08f);
    gl_box(760,244,0,784,396,8,0.075f,0.070f,0.060f);
    gl_tex_quad_tint(gl_tex[GT_ART_WALL],759.9f,244,759.9f,256,0,136,
                     0.02f,0.02f,0.16f,0.98f,0.62f);
    gl_tex_quad_tint(gl_tex[GT_ART_WALL],759.9f,384,759.9f,396,0,136,
                     0.84f,0.02f,0.98f,0.98f,0.62f);
    gl_tex_quad_tint(gl_tex[GT_ART_WALL],759.9f,244,759.9f,396,136,120,
                     0.02f,0.02f,0.98f,0.20f,0.55f);
    for(int side=0;side<2;side++){
        float y0=side?384.0f:192.0f, y1=side?448.0f:256.0f;
        float yc=side?448.0f:192.0f;
        /* Receiving pocket: rails plus a front cover the leaf slides behind. */
        gl_box(760,y0,120,784,y1,136,0.075f,0.075f,0.070f);
        gl_box(760,y0,0,784,y1,8,0.075f,0.075f,0.070f);
        gl_box(760,side?y1:y0-6,0,784,side?y1+6:y0,136,0.085f,0.080f,0.070f);
        if(last_px < DOOR_X0)
            gl_tex_quad_tint(gl_tex[GT_ART_WALL],759.8f,y0,759.8f,y1,8,120,
                             0.02f,0.30f,0.20f,0.90f,0.44f);
        gl_box(760,yc-3,124,784,yc+3,132,0.45f,0.10f,0.035f);
    }
    /* Header warning lamps and frame fasteners. */
    for(int i=0;i<2;i++){
        float y=272.0f+i*80.0f;
        gl_box(758.8f,y-7,126,760.0f,y+7,134,0.065f,0.060f,0.050f);
        gl_glow_wall_x(758.7f,y-6,y+6,128,132,1.0f,0.30f,0.04f,0.58f);
    }
    /* Leaves move apart with Glon's authoritative door_open, 0..64.
     * The concept sheet's right/left leaves face the door from the room side. */
    gl_door_leaf(gl_tex[GT_DOOR_RIGHT],ny1,ny2,ny2,tint,1);
    gl_door_leaf(gl_tex[GT_DOOR_LEFT],sy1,sy2,sy1,tint,1);
    /* South leaf lock housing and status indicator. */
    gl_box(765.0f,sy1+2,34,DOOR_X0-0.2f,sy1+8,52,0.26f,0.24f,0.20f);
    gl_box(764.8f,sy1+2.5f,35,DOOR_X0-0.1f,sy1+7.5f,50,0.11f,0.11f,0.10f);
    gl_box(764.6f,sy1+3,37,DOOR_X0-0.1f,sy1+5,45,0.58f,0.50f,0.34f);
    gl_glow_wall_x(764.5f,sy1+4.0f,sy1+5.0f,39,42,0.20f,0.90f,0.25f,0.72f);
    gl_glow_wall_x(764.5f,sy1+4.0f,sy1+5.0f,45,48,0.95f,0.12f,0.04f,0.72f);
    (void)light;
}

/* ===== G5t engineering room reusable components ===== */
static void eng_hazard_x(float x1,float x2,float y,float z)
{
    int i=0;
    for(float x=x1;x<x2;x+=16.0f,i++){
        float xe=x+16.0f<x2?x+16.0f:x2;
        if(i&1) gl_box(x,y,z,xe,y+7.0f,z+1.2f,0.62f,0.44f,0.05f);
        else    gl_box(x,y,z,xe,y+7.0f,z+1.2f,0.035f,0.032f,0.028f);
    }
}
static void eng_rail_x(float x1,float x2,float y,float z0,float h)
{
    int i=0;
    for(float x=x1;x<=x2+1.0f;x+=40.0f,i++){
        float p=(i%4==0)?0.49f:(i%4==1)?0.59f:(i%4==2)?0.55f:0.63f;
        gl_box(x-2.0f,y-2.0f,z0,x+2.0f,y+2.0f,z0+h,p,p*0.72f,p*0.10f);
        gl_box(x-2.5f,y-2.5f,z0,x+2.5f,y+2.5f,z0+5.0f,0.075f,0.065f,0.05f);
        gl_box(x-2.0f,y-2.0f,z0+h-3.0f,x+2.0f,y+2.0f,z0+h,p*1.12f,p*0.84f,p*0.13f);
    }
    gl_box(x1,y-2.0f,z0+h*0.48f,x2,y+1.0f,z0+h*0.48f+3.0f,0.59f,0.43f,0.05f);
    gl_box(x1,y-2.0f,z0+h-3.0f,x2,y+1.0f,z0+h,0.64f,0.47f,0.06f);
    gl_box(x1,y-1.0f,z0+2.0f,x2,y+1.0f,z0+5.0f,0.065f,0.055f,0.045f);
}
static void eng_rail_y(float y1,float y2,float x,float z0,float h)
{
    int i=0;
    for(float y=y1;y<=y2+1.0f;y+=40.0f,i++){
        float p=(i%4==0)?0.51f:(i%4==1)?0.61f:(i%4==2)?0.56f:0.65f;
        gl_box(x-2.0f,y-2.0f,z0,x+2.0f,y+2.0f,z0+h,p,p*0.72f,p*0.10f);
        gl_box(x-2.5f,y-2.5f,z0,x+2.5f,y+2.5f,z0+5.0f,0.075f,0.065f,0.05f);
        gl_box(x-2.0f,y-2.0f,z0+h-3.0f,x+2.0f,y+2.0f,z0+h,p*1.12f,p*0.84f,p*0.13f);
    }
    gl_box(x-2.0f,y1,z0+h*0.48f,x+1.0f,y2,z0+h*0.48f+3.0f,0.59f,0.43f,0.05f);
    gl_box(x-2.0f,y1,z0+h-3.0f,x+1.0f,y2,z0+h,0.64f,0.47f,0.06f);
    gl_box(x-1.0f,y1,z0+2.0f,x+1.0f,y2,z0+5.0f,0.065f,0.055f,0.045f);
}
static void eng_pipe_x(float x1,float x2,float y,float z,float rad,float r,float g,float b);
static void eng_pipe_x_tex(float x1,float x2,float y,float z,float rad,float r,float g,float b)
{
    eng_pipe_x(x1,x2,y,z,rad,r,g,b);
}
static void eng_pipe_x(float x1,float x2,float y,float z,float rad,float r,float g,float b)
{
    int rust=(r>g*1.35f);
    GLuint tex=rust?gl_tex[GT_ART_WALL]:gl_tex[GT_ART_FLOOR];
    float u0=rust?0.02f:0.30f,v0=rust?0.02f:0.30f,u1=rust?0.14f:0.70f,v1=rust?0.98f:0.70f;
    gl_cyl_x_tex_smooth(tex,x1,x2,y,z,rad,12,0.68f,u0,v0,u1,v1,rust?3.0f:2.0f);
    for(float x=x1+24.0f;x<x2;x+=80.0f){
        gl_cyl_x(x-2.0f,x+2.0f,y,z,rad+2.0f,12,r*0.72f,g*0.72f,b*0.72f);
        gl_cyl_x(x-1.0f,x+1.0f,y,z,rad+3.2f,12,0.20f,0.18f,0.15f);
        gl_box(x-1.0f,y-1.0f,z+rad,x+1.0f,y+1.0f,z+rad+9.0f,0.09f,0.085f,0.075f);
    }
}
static void eng_pipe_z(float x,float y,float z1,float z2,float rad,float r,float g,float b)
{
    int rust=(r>g*1.35f);
    float u0=0.02f,u1=0.46f,v0=rust?0.64f:0.02f,v1=rust?0.98f:0.38f;
    gl_cyl_z_tex_rot(gl_tex[GT_ART_WALL],x,y,z1,z2,rad,12,0.68f,u0,v0,u1,v1,1.0f,2.0f);
    gl_cyl_z(x,y,z1,z1+3.0f,rad+3.0f,12,0.20f,0.18f,0.15f);
    gl_cyl_z(x,y,z2-3.0f,z2,rad+3.0f,12,0.20f,0.18f,0.15f);
}
static void eng_tray_x(float x1,float x2,float y,float z)
{
    gl_box(x1,y-6.0f,z,x2,y+6.0f,z+2.0f,0.11f,0.12f,0.11f);
    gl_box(x1,y-6.0f,z+2.0f,x2,y-4.5f,z+7.0f,0.16f,0.15f,0.13f);
    gl_box(x1,y+4.5f,z+2.0f,x2,y+6.0f,z+7.0f,0.16f,0.15f,0.13f);
    for(float x=x1+8.0f;x<x2;x+=32.0f)
        gl_box(x-1.0f,y-5.0f,z+1.0f,x+1.0f,y+5.0f,z+3.0f,0.30f,0.28f,0.22f);
    gl_box(x1+4.0f,y-3.5f,z+4.0f,x2-4.0f,y-2.4f,z+5.3f,0.32f,0.09f,0.04f);
    gl_box(x1+4.0f,y-1.0f,z+4.0f,x2-4.0f,y+0.1f,z+5.3f,0.09f,0.12f,0.19f);
    gl_box(x1+4.0f,y+1.5f,z+4.0f,x2-4.0f,y+2.6f,z+5.3f,0.56f,0.39f,0.05f);
}
static void eng_fixture(float x,float y,float light)
{
    gl_box(x-46.0f,y-32.0f,117.0f,x+46.0f,y+32.0f,121.0f,0.055f,0.055f,0.050f);
    gl_box(x-48.0f,y-34.0f,121.0f,x-38.0f,y+34.0f,127.0f,0.16f,0.16f,0.14f);
    gl_box(x+38.0f,y-34.0f,121.0f,x+48.0f,y+34.0f,127.0f,0.16f,0.16f,0.14f);
    gl_box(x-38.0f,y-34.0f,121.0f,x+38.0f,y-26.0f,127.0f,0.16f,0.16f,0.14f);
    gl_box(x-38.0f,y+26.0f,121.0f,x+38.0f,y+34.0f,127.0f,0.16f,0.16f,0.14f);
    gl_box(x-34.0f,y-22.0f,126.0f,x+34.0f,y+22.0f,126.35f,0.68f,0.78f,0.86f);
    gl_light_panel_c(x-34.0f,y-22.0f,x+34.0f,y+22.0f,126.45f,0.44f+0.32f*light,0.58f+0.28f*light,0.72f+0.24f*light);
    for(float yy=y-18.0f;yy<y+18.0f;yy+=9.0f)
        gl_box(x-35.0f,yy,126.2f,x+35.0f,yy+1.5f,126.7f,0.10f,0.10f,0.09f);
}
static void eng_wall_bay_y(float x1,float x2,float y,float z1,float z2,int face_south,float tint)
{
    float d=face_south?8.0f:-8.0f, yf=y+d;
    gl_box(x1,y,z1,x2,yf,z2,0.055f,0.060f,0.058f);
    float qy=yf+(face_south?0.1f:-0.1f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],x1+3.0f,qy,x2-3.0f,qy,z1+3.0f,z2-3.0f,0,0,1,1,0.18f);
    gl_glow_wall_y(qy+(face_south?0.05f:-0.05f),x1+3.0f,x2-3.0f,z1+3.0f,z2-3.0f,0.28f,0.40f,0.56f,0.16f);
    gl_box(x1,y,z1,x1+4.0f,yf,z2,0.18f,0.17f,0.15f);
    gl_box(x2-4.0f,y,z1,x2,yf,z2,0.18f,0.17f,0.15f);
    gl_box(x1,y,z1,x2,yf,z1+4.0f,0.18f,0.17f,0.15f);
    gl_box(x1,y,z2-4.0f,x2,yf,z2,0.18f,0.17f,0.15f);
}
static void eng_wall_bay_x(float y1,float y2,float x,float z1,float z2,int face_east,float tint)
{
    float xf=face_east?x+8.0f:x-8.0f;
    gl_box(x,y1,z1,x+(face_east?8.0f:-8.0f),y2,z2,0.055f,0.060f,0.058f);
    float qx=xf+(face_east?0.1f:-0.1f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],qx,y1+3.0f,qx,y2-3.0f,z1+3.0f,z2-3.0f,0,0,1,1,0.18f);
    gl_glow_wall_x(qx+(face_east?0.05f:-0.05f),y1+3.0f,y2-3.0f,z1+3.0f,z2-3.0f,0.28f,0.40f,0.56f,0.15f);
    gl_box(x,y1,z1,x+(face_east?8.0f:-8.0f),y1+4.0f,z2,0.18f,0.17f,0.15f);
    gl_box(x,y2-4.0f,z1,x+(face_east?8.0f:-8.0f),y2,z2,0.18f,0.17f,0.15f);
    gl_box(x,y1,z1,x+(face_east?8.0f:-8.0f),y2,z1+4.0f,0.18f,0.17f,0.15f);
    gl_box(x,y1,z2-4.0f,x+(face_east?8.0f:-8.0f),y2,z2,0.18f,0.17f,0.15f);
}
static void eng_grille_y(float x,float y,float z1,float z2,float w,int face_south)
{
    float yf=face_south?y+8.2f:y-8.2f;
    gl_box(x,yf-0.7f,z1,x+w,yf+0.7f,z2,0.08f,0.08f,0.07f);
    for(float z=z1+3.0f;z<z2-2.0f;z+=5.0f)
        gl_box(x+2.0f,yf-1.2f,z,x+w-2.0f,yf+1.2f,z+2.0f,0.19f,0.19f,0.17f);
}
static void eng_indicator(float x1,float y1,float z1,float size,float r,float g,float b)
{
    gl_box(x1,y1,z1,x1+size,y1+2.0f,z1+size*0.55f,r,g,b);
}
static void eng_generator(void)
{
    const float sr=0.19f,sg=0.20f,sb=0.20f;
    gl_box_tex_uv(gl_tex[GT_ART_WALL],150,145,0,500,245,10,1.30f,0.02f,0.02f,0.30f,0.30f);
    gl_box_tex_uv(gl_tex[GT_ART_WALL],160,151,10,490,242,18,1.22f,0.02f,0.02f,0.30f,0.30f);
    /* front-facing turbine drum: axis runs north-south so the room sees the rotor face */
    gl_cyl_y_tex_smooth(gl_tex[GT_ART_WALL],154,244,325,64,55,20,1.02f,0.02f,0.02f,0.98f,0.38f);
    gl_cyl_y(184,188,325,64,56,20,0.16f,0.11f,0.065f);
    gl_cyl_y(214,218,325,64,56,20,0.18f,0.13f,0.075f);
    for(float y=170;y<244;y+=22) gl_cyl_y(y,y+4,325,64,58,20,0.075f,0.08f,0.08f);
    gl_box_tex_uv(gl_tex[GT_ART_WALL],170,152,0,480,244,26,1.22f,0.70f,0.02f,0.98f,0.30f);
    gl_box_tex_uv(gl_tex[GT_ART_WALL],176,232,0,474,246,32,1.10f,0.70f,0.02f,0.98f,0.30f);
    gl_cyl_y(146,156,325,64,58,20,0.10f,0.11f,0.11f);
    gl_cyl_y(242,250,325,64,58,20,0.10f,0.11f,0.11f);
    gl_disc_y_tex(gl_tex[GT_ART_WALL],250.0f,325,64,60,20,0.92f,0.02f,0.02f,0.46f,0.38f);
    gl_turbine_face(250.5f,325,64,54,20,0.72f,0.24f,0.035f);
    for(int i=0;i<12;i++){
        float aa=(float)i*6.2831853f/12.0f;
        gl_disc_y(251.1f,325+cosf(aa)*56.5f,64+sinf(aa)*56.5f,1.9f,6,0.055f,0.052f,0.045f);
    }
    gl_glow_disc_y(251.2f,325,64,62,20,1.0f,0.24f,0.02f,0.11f);
    gl_glow_quad(225,195,435,285,2.25f,1.0f,0.18f,0.02f,0.10f);
    for(float x=236;x<410;x+=20) gl_box(x,249.5f,25,x+4,251.0f,44,0.035f,0.040f,0.038f);
    gl_box(168,238,0,492,247,8,0.08f,0.08f,0.07f);
    for(float x=175;x<490;x+=42.0f) gl_box(x,245,4,x+5,252,30,0.13f,0.13f,0.12f);
    eng_rail_x(176,486,254,0,30);
    eng_rail_y(160,248,172,0,30);
    eng_rail_y(160,248,490,0,30);
    eng_hazard_x(174,487,246,1.5f);
    gl_box(180,246,1.1f,486,260,2.0f,0.10f,0.11f,0.10f);
    for(float x=184;x<486;x+=12.0f) gl_box(x,248,2.0f,x+2,258,3.0f,0.25f,0.24f,0.21f);
    eng_pipe_z(430,190,79,124,13,0.18f,0.17f,0.16f);
    eng_pipe_z(405,210,81,121,7,0.20f,0.10f,0.06f);
    gl_tex_quad_alpha(gl_tex[GT_ART_WALL],400,136.2f,470,136.2f,78,116,0.02f,0.02f,0.28f,0.30f,0.18f,0.55f);
    gl_tex_quad_alpha(gl_tex[GT_ART_WALL],405,251.06f,452,251.06f,76,112,0.02f,0.02f,0.20f,0.30f,0.14f,0.42f);
    gl_tex_quad_alpha(gl_tex[GT_ART_FLOOR],190,252,475,305,2.22f,2.22f,0,0,1,1,0.16f,0.34f);
    eng_pipe_x(128,220,166,100,8,0.21f,0.19f,0.17f);
    eng_pipe_x(128,220,218,96,6,0.25f,0.13f,0.07f);
    for(float x=230;x<420;x+=60.0f) eng_indicator(x,226,66,5,0.70f,0.42f,0.06f);
    eng_indicator(442,205,36,6,0.95f,0.12f,0.05f);
    eng_indicator(442,218,36,6,0.22f,0.85f,0.15f);
}
static void eng_coolant(void)
{
    gl_box_tex_uv(gl_tex[GT_ART_WALL],142,330,0,236,522,5,1.14f,0.02f,0.02f,0.30f,0.30f);
    for(int i=0;i<2;i++){
        float y=386.0f+i*86.0f;
        if(i==0){
            gl_cyl_z_tex_rot(gl_tex[GT_ART_WALL],182,y,4,80,23,20,1.18f,0.02f,0.64f,0.46f,0.98f,1.0f,1.0f);
            gl_disc_z_tex(gl_tex[GT_ART_WALL],80.0f,182,y,23,20,1.10f,0.02f,0.64f,0.46f,0.98f);
            gl_disc_z_tex(gl_tex[GT_ART_WALL],4.0f,182,y,23,20,0.90f,0.02f,0.64f,0.46f,0.98f);
        } else {
            gl_cyl_z_tex_rot(gl_tex[GT_ART_WALL],182,y,4,80,23,20,1.02f,0.52f,0.64f,0.98f,0.98f,1.0f,1.0f);
            gl_disc_z_tex(gl_tex[GT_ART_WALL],80.0f,182,y,23,20,0.98f,0.52f,0.64f,0.98f,0.98f);
            gl_disc_z_tex(gl_tex[GT_ART_WALL],4.0f,182,y,23,20,0.82f,0.52f,0.64f,0.98f,0.98f);
        }
        gl_cyl_z(182,y,27,30,23.2f,20,0.21f,0.16f,0.10f);
        gl_cyl_z(182,y,64,67,23.2f,20,0.20f,0.17f,0.12f);
        gl_cyl_z_band(182,y,5,79,23.25f,-0.62f,-0.40f,0.39f,0.38f,0.35f);
        gl_cyl_z_band(182,y,5,79,23.25f,0.45f,0.72f,0.17f,0.10f,0.065f);
        gl_cyl_z_band(182,y,9,76,23.25f,2.45f,2.62f,0.19f,0.18f,0.17f);
        gl_cyl_z(182,y,72,82,25,16,0.12f,0.13f,0.14f);
        gl_cyl_z(182,y,20,24,26,16,0.10f,0.11f,0.12f);
        gl_cyl_z(182,y,48,52,26,16,0.10f,0.11f,0.12f);
        gl_box(174,y-5,79,190,y+5,87,0.26f,0.23f,0.17f);
        eng_pipe_z(182,y,82,107,6,0.22f,0.15f,0.08f);
        eng_pipe_x(128,182,y,100,4,0.25f,0.18f,0.09f);
        gl_disc_y(y+23.2f,182,55,10,16,0.06f,0.06f,0.055f);
        if(i==0) gl_disc_y_tex_rgb(gl_tex[GT_ART_FLOOR],y+23.6f,182,55,7.5f,16,0.95f,0.28f,0.08f,0.30f,0.30f,0.70f,0.70f);
        else gl_disc_y(y+23.6f,182,55,7.5f,16,0.55f,0.10f,0.035f);
        gl_disc_y(y+23.9f,182,55,2.2f,12,0.16f,0.15f,0.13f);
        gl_disc_y(y+23.2f,182,70,6,12,0.10f,0.11f,0.10f);
        gl_disc_y(y+23.8f,182,70,4.5f,12,0.78f,0.70f,0.42f);
        gl_disc_x(206.2f,y,55,10,16,0.06f,0.06f,0.055f);
        if(i==0) gl_disc_x_tex_rgb(gl_tex[GT_ART_FLOOR],206.6f,y,55,7.0f,16,0.95f,0.28f,0.08f,0.30f,0.30f,0.70f,0.70f);
        else gl_disc_x_tex_rgb(gl_tex[GT_ART_FLOOR],206.6f,y,55,7.0f,16,0.86f,0.22f,0.06f,0.30f,0.30f,0.70f,0.70f);
        gl_disc_x(206.9f,y,55,2.0f,12,0.16f,0.15f,0.13f);
        gl_disc_x(206.2f,y+12,55,5,12,0.80f,0.72f,0.44f);
        gl_box(205.9f,y-12,32,206.4f,y+6,40,0.48f,0.34f,0.04f);
        gl_box(205.8f,y-8,32,206.5f,y-4,40,0.045f,0.04f,0.03f);
        gl_box(205.8f,y+1,32,206.5f,y+5,40,0.045f,0.04f,0.03f);
        eng_indicator(206,y-20,56,5,0.95f,0.15f,0.04f);
        eng_indicator(206,y-20,47,5,0.18f,0.80f,0.20f);
    }
    gl_box(158,430,86,206,438,94,0.20f,0.18f,0.15f);
    gl_box(154,500,3,230,508,18,0.18f,0.17f,0.14f);
    gl_box(166,502,18,218,506,48,0.10f,0.10f,0.09f);
    for(int z=22;z<46;z+=8) gl_box(170,505,z,214,508,z+2,0.42f,0.12f,0.05f);
    eng_rail_y(342,510,232,0,28);
    eng_hazard_x(154,230,514,1.4f);
}
static void eng_cabinet_y(float x1,float x2,float y,float z1,float z2)
{
    gl_box_tex_uv(gl_tex[GT_ART_WALL],x1,y-26,z1,x2,y,z2,1.18f,0.02f,0.02f,0.30f,0.30f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],x1+3,y-26.1f,x2-3,y-26.1f,z1+4,z2-4,0,0,1,1,0.22f);
    gl_glow_wall_y(y-26.16f,x1+3,x2-3,z1+4,z2-4,0.38f,0.37f,0.34f,0.15f);
    /* G5y: the old near-black strip was a double-door centre stile, but it
     * crossed the reused panel's centred vent. Treat each cabinet as a single
     * door instead: steel jambs, top/bottom rails, hinges and an edge pull. */
    gl_box(x1,y-29,z1,x2,y-26,z1+4,0.22f,0.20f,0.17f);
    gl_box(x1,y-29,z2-4,x2,y-26,z2,0.22f,0.20f,0.17f);
    gl_box(x1,y-29,z1,x1+4,y-26,z2,0.21f,0.19f,0.16f);
    gl_box(x2-4,y-29,z1,x2,y-26,z2,0.21f,0.19f,0.16f);
    gl_box(x1+4,y-29.5f,z1+0.7f,x2-4,y-29,z1+1.9f,0.36f,0.32f,0.25f);
    gl_box(x1+4,y-29.5f,z2-1.9f,x2-4,y-29,z2-0.7f,0.32f,0.29f,0.23f);
    gl_box(x1+0.5f,y-29.5f,z1+5,x1+2.0f,y-29,z2-5,0.38f,0.33f,0.26f);
    gl_box(x2-2.0f,y-29.5f,z1+5,x2-0.5f,y-29,z2-5,0.33f,0.30f,0.24f);
    for(int k=0;k<3;k++){
        gl_box(x1+1,y-30,z1+12+k*30,x1+5,y-29,z1+19+k*30,0.24f,0.22f,0.18f);
        gl_box(x1+1.6f,y-30.4f,z1+13+k*30,x1+4.4f,y-30,z1+14.2f+k*30,0.42f,0.36f,0.27f);
        gl_box(x1+2.7f,y-30.5f,z1+15.5f+k*30,x1+3.3f,y-30.3f,z1+16.5f+k*30,0.10f,0.10f,0.09f);
    }
    float zm=0.5f*(z1+z2);
    gl_box(x2-11,y-30,zm-3,x2-7,y-29,zm+9,0.25f,0.23f,0.19f);
    gl_box(x2-10,y-30.6f,zm-2,x2-8,y-30,zm+8,0.08f,0.08f,0.07f);
    gl_box(x2-9.2f,y-30.8f,zm-1,x2-8.8f,y-30.6f,zm+7,0.44f,0.38f,0.28f);
    gl_box(x1+8,y-31,z1+7,x1+18,y-28,z1+11,0.45f,0.32f,0.04f);
    gl_box(x1+11,y-31,z1+7,x1+14,y-28,z1+11,0.045f,0.04f,0.03f);
    eng_indicator(x1+12,y-30,z2-16,5,0.20f,0.85f,0.16f);
    eng_indicator(x1+21,y-30,z2-16,5,0.95f,0.12f,0.04f);
}
static void eng_cabinet_x(float y1,float y2,float x,float z1,float z2)
{
    gl_box_tex_uv(gl_tex[GT_ART_WALL],x,y1,z1,x+24,y2,z2,1.18f,0.02f,0.02f,0.30f,0.30f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],x-0.1f,y1+3,x-0.1f,y2-3,z1+4,z2-4,0,0,1,1,0.22f);
    gl_glow_wall_x(x-0.04f,y1+3,y2-3,z1+4,z2-4,0.38f,0.37f,0.34f,0.14f);
    gl_box(x-1.0f,y1,z1,x+4,y2,z1+4,0.21f,0.19f,0.16f);
    gl_box(x-1.0f,y1,z2-4,x+4,y2,z2,0.21f,0.19f,0.16f);
    gl_box(x-1.0f,y1,z1,x+4,y1+4,z2,0.21f,0.19f,0.16f);
    gl_box(x-1.0f,y2-4,z1,x+4,y2,z2,0.21f,0.19f,0.16f);
    gl_box(x-1.4f,y1+4,z1+0.7f,x+4,y2-4,z1+1.9f,0.34f,0.31f,0.25f);
    gl_box(x-1.4f,y1+4,z2-1.9f,x+4,y2-4,z2-0.7f,0.31f,0.28f,0.23f);
    for(int k=0;k<3;k++){
        gl_box(x-1.2f,y1+7,z1+12+k*30,x+2.0f,y1+11,z1+19+k*30,0.24f,0.22f,0.18f);
        gl_box(x-1.5f,y1+7.6f,z1+13+k*30,x-1.2f,y1+10.4f,z1+14.2f+k*30,0.42f,0.36f,0.27f);
    }
    float zm=0.5f*(z1+z2);
    /* edge pull, kept clear of the centred vent */
    gl_box(x-2.0f,y2-13,zm-2,x+0.5f,y2-10,zm+9,0.25f,0.23f,0.19f);
    gl_box(x-2.6f,y2-12,zm-1,x-2.0f,y2-11,zm+8,0.12f,0.11f,0.10f);
    gl_box(x-2.8f,y2-11.6f,zm,x-2.6f,y2-11.4f,zm+7,0.44f,0.38f,0.28f);
    gl_box(x-1.0f,y1+7,z1+8,x+2.0f,y1+17,z1+12,0.45f,0.32f,0.04f);
    gl_box(x-1.0f,y1+10,z1+8,x+2.0f,y1+13,z1+12,0.045f,0.04f,0.03f);
    eng_indicator(x-0.3f,y1+20,z2-17,5,0.95f,0.12f,0.04f);
}
static void g5z_screw(float x,float y,float z) {
    gl_box(x,y,z,x+1.3f,y+0.30f,z+1.3f,0.44f,0.41f,0.32f);
    gl_box(x+0.35f,y+0.30f,z+0.35f,x+0.95f,y+0.42f,z+0.95f,0.10f,0.10f,0.09f);
}
static void g5z_led(float x,float y,float z,float s,float r,float g,float b) {
    gl_box(x-1.1f,y-0.2f,z-0.8f,x+s+1.1f,y+0.22f,z+s+0.8f,0.065f,0.075f,0.065f);
    gl_box(x,y+0.18f,z,x+s,y+0.60f,z+s,r,g,b);
    gl_box(x+s*0.25f,y+0.60f,z+s*0.25f,x+s*0.75f,y+0.76f,z+s*0.75f,
           r*1.15f,g*1.15f,b*1.15f);
}
static void g5z_display(float x1,float x2,float z1,float z2,G5zUV uv,int small) {
    float y=204.35f;
    gl_box(x1-1.1f,y-0.1f,z1-1.1f,x2+1.1f,y+0.08f,z2+1.1f,0.035f,0.040f,0.038f);
    gl_tex_quad(gl_tex[GT_G5Z_ATLAS],x2,y+0.16f,x1,y+0.16f,z1,z2,uv.u0,uv.v0,uv.u1,uv.v1);
    float b=small?1.7f:2.4f;
    gl_box(x1-b,y,z2,x2+b,y+0.55f,z2+b,0.14f,0.15f,0.14f);
    gl_box(x1-b,y,z1-b,x2+b,y+0.55f,z1,0.14f,0.15f,0.14f);
    gl_box(x1-b,y,z1,x1,y+0.55f,z2,0.14f,0.15f,0.14f);
    gl_box(x2,y,z1,x2+b,y+0.55f,z2,0.14f,0.15f,0.14f);
    gl_box(x1-b,y+0.50f,z2,x2+b,y+0.64f,z2+b,0.33f,0.31f,0.26f);
    gl_box(x1-b,y+0.50f,z1-b,x2+b,y+0.64f,z1,0.22f,0.22f,0.19f);
    gl_box(x1-b,y+0.50f,z1,x1-0.3f,y+0.64f,z2,0.31f,0.29f,0.24f);
    gl_box(x2+0.3f,y+0.50f,z1,x2+b,y+0.64f,z2,0.23f,0.23f,0.20f);
    g5z_screw(x1-b-1.1f,y+0.64f,z2+b-1.2f);
    g5z_screw(x2+b-0.2f,y+0.64f,z2+b-1.2f);
    g5z_screw(x1-b-1.1f,y+0.64f,z1-0.3f);
    g5z_screw(x2+b-0.2f,y+0.64f,z1-0.3f);
}
static void eng_walls(void)
{
    /* North wall: service bays, vent stack and control bank. */
    for(float x=145;x<530;x+=96) eng_wall_bay_y(x,x+78,128,58,112,1,1.78f);
    for(float x=145;x<530;x+=96){
        gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],x+3,136.2f,x+75,136.2f,8,52,0,0,1,1,0.16f);
        gl_box(x+1,136,6,x+4,138,54,0.16f,0.15f,0.13f);
        gl_box(x+74,136,6,x+77,138,54,0.16f,0.15f,0.13f);
        gl_box(x+4,136,50,x+74,138,54,0.15f,0.14f,0.12f);
    }
    eng_wall_bay_y(548,690,128,32,112,1,1.88f);
    eng_grille_y(560,128,84,116,108,1);
    gl_box_tex(gl_tex[GT_ART_FLOOR],550,136,0,736,204,84,0.76f,2.2f,1.5f);
    gl_box(550,136,84,736,204,88,0.17f,0.15f,0.12f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],560,203.9f,724,203.9f,16,78,0,0,1.4f,1.2f,0.22f);
    for(float x=560;x<728;x+=28) gl_box(x,135,92,x+20,137,112,0.07f,0.08f,0.08f);
    for(float z=16;z<76;z+=12) gl_box(562,203.9f,z,724,204.1f,z+4,0.08f,0.09f,0.08f);
    G5zUV wave=g5z_uv_rect(0,128,128,192);
    G5zUV bars=g5z_uv_rect(128,128,256,192);
    G5zUV gauge=g5z_uv_rect(0,192,64,224);
    g5z_display(572,632,54,76,wave,0);
    g5z_display(646,706,54,76,bars,0);
    g5z_display(660,700,26,44,gauge,1);
    /* status strip: the existing red/green indicator pairs, now housed and clear of the screens */
    gl_box(556,203.9f,78,740,204.68f,84,0.09f,0.10f,0.09f);
    gl_box(556,204.55f,78,740,204.72f,79,0.27f,0.26f,0.21f);
    for(float x=560;x<728;x+=28){
        gl_box(x-1,204.4f,79.3f,x+16,204.82f,83,0.065f,0.075f,0.065f);
        g5z_led(x+2.2f,204.78f,80.4f,2.4f,0.95f,0.20f,0.04f);
        g5z_led(x+7.2f,204.78f,80.4f,2.4f,0.20f,0.80f,0.16f);
    }
    /* GLON CORPORATION manufacturer/service plate */
    gl_box(556,204.1f,15,700,204.55f,25,0.09f,0.10f,0.09f);
    gl_box(556,204.50f,15,700,204.64f,25,0.24f,0.23f,0.19f);
    G5zUV corp=g5z_uv_rect(0,28,200,48);
    G5zUV eng=g5z_uv_rect(0,50,248,68);
    gl_tex_quad_alpha(gl_tex[GT_G5Z_ATLAS],696,204.68f,656,204.68f,18,23,
                      corp.u0,corp.v0,corp.u1,corp.v1,0.92f,0.95f);
    gl_tex_quad_alpha(gl_tex[GT_G5Z_ATLAS],652,204.68f,560,204.68f,18,22,
                      eng.u0,eng.v0,eng.u1,eng.v1,0.88f,0.95f);
    /* Cool work lamps between the upper bays; baked glow, no dynamic lights. */
    for(float x=158;x<720;x+=128){
        gl_box(x,136,96,x+20,138,101,0.46f,0.64f,0.82f);
        gl_box(x-2,136,94,x,138,103,0.12f,0.13f,0.12f);
        gl_box(x+20,136,94,x+22,138,103,0.12f,0.13f,0.12f);
        gl_glow_wall_y(136.35f,x,x+20,93,104,0.30f,0.50f,0.76f,0.24f);
        gl_glow_wall_y(136.5f,x+2,x+18,96,100,0.62f,0.82f,1.0f,0.42f);
    }
    /* West wall: conduit spine and two recessed maintenance bays. */
    eng_wall_bay_x(160,270,128,28,112,1,1.74f);
    eng_wall_bay_x(300,410,128,28,112,1,1.82f);
    eng_wall_bay_x(438,540,128,28,112,1,1.74f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],136.2f,166,136.2f,264,8,24,0,0,1,1,0.16f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],136.2f,306,136.2f,404,8,24,0,0,1,1,0.16f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],136.2f,444,136.2f,534,8,24,0,0,1,1,0.16f);
    eng_pipe_z(139,230,20,110,5,0.21f,0.18f,0.14f);
    eng_pipe_z(143,450,20,110,4,0.24f,0.13f,0.06f);
    for(float y=190;y<560;y+=180) {
        gl_box(136, y, 92, 140, y+34, 98, 0.20f,0.18f,0.13f);
        gl_box(140.2f, y+3, 93, 140.8f, y+31, 96.5f, 0.95f,0.70f,0.33f);
    }
    /* South wall: cabinets, louvres and cable drops. */
    for(float x=300;x<690;x+=112) eng_cabinet_y(x,x+92,640,18,106);
    eng_grille_y(606,640,82,116,68,0);
    eng_indicator(670,634,88,5,0.95f,0.12f,0.04f);
    /* East wall returns, clear of the door opening. */
    eng_cabinet_x(430,536,738,18,104);
    eng_cabinet_x(550,610,738,18,104);
    eng_wall_bay_x(410,620,768,28,112,0,1.72f);
    gl_tex_quad_tint_add(gl_tex[GT_ART_PANEL],759.8f,416,759.8f,614,8,24,0,0,1,1,0.16f);
    eng_pipe_z(744,460,20,112,4,0.24f,0.16f,0.07f);
}
static void eng_ceiling_vent(float x1,float y1,float x2,float y2)
{
    gl_box(x1,y1,126.85f,x2,y2,127.55f,0.045f,0.048f,0.045f);
    gl_box(x1-2,y1-2,126.7f,x2+2,y1,127.65f,0.16f,0.17f,0.17f);
    gl_box(x1-2,y2,126.7f,x2+2,y2+2,127.65f,0.16f,0.17f,0.17f);
    gl_box(x1-2,y1,126.7f,x1,y2,127.65f,0.16f,0.17f,0.17f);
    gl_box(x2,y1,126.7f,x2+2,y2,127.65f,0.16f,0.17f,0.17f);
    for(float yy=y1+4;yy<y2-3;yy+=5)
        gl_box(x1+3,yy,126.55f,x2-3,yy+2,126.75f,0.14f,0.15f,0.15f);
}

static void eng_ceiling(void)
{
    /* Room-only recessed panel layer below the world ceiling plane. */
    const float z=127.35f;
    const float x0=136.0f,x1=760.0f,y0=136.0f,y1=632.0f;
    const int cols=6,rows=4;
    const float pw=(x1-x0)/cols,pd=(y1-y0)/rows;
    for(int iy=0;iy<rows;iy++) for(int ix=0;ix<cols;ix++){
        float xa=x0+ix*pw+1.3f,xb=x0+(ix+1)*pw-1.3f;
        float ya=y0+iy*pd+1.3f,yb=y0+(iy+1)*pd-1.3f;
        float tint=0.60f+0.035f*((ix*3+iy*5)%3)-0.02f*(iy%2);
        gl_tex_plane(gl_tex[GT_CEIL_PANEL],xa,ya,xb,yb,z,0,0,1,1,tint);
    }
    /* perimeter support ledge */
    gl_box(x0-2,y0-2,z-0.5f,x1+2,y0+1,z+0.7f,0.13f,0.14f,0.14f);
    gl_box(x0-2,y1-1,z-0.5f,x1+2,y1+2,z+0.7f,0.13f,0.14f,0.14f);
    gl_box(x0-2,y0+1,z-0.5f,x0+1,y1-1,z+0.7f,0.13f,0.14f,0.14f);
    gl_box(x1-1,y0+1,z-0.5f,x1+2,y1-1,z+0.7f,0.13f,0.14f,0.14f);
    gl_glow_quad(x0+4,y0+4,x1-4,y1-4,z-0.15f,0.18f,0.26f,0.36f,0.05f);
    eng_ceiling_vent(360,220,470,268);
    eng_ceiling_vent(360,400,470,448);
}

static void eng_overhead(void)
{
    eng_ceiling();
    /* structural ceiling beams and corner posts */
    for(float y=190;y<620;y+=150) gl_box(128,y-4,118,768,y+4,128,0.075f,0.075f,0.070f);
    gl_box(136,136,0,148,148,128,0.10f,0.10f,0.095f);
    gl_box(748,620,0,760,632,128,0.10f,0.10f,0.095f);
    gl_box(136,620,0,148,632,128,0.10f,0.10f,0.095f);
    gl_box(748,136,0,760,148,128,0.10f,0.10f,0.095f);
    /* longitudinal feeds above the clear central lane */
    eng_pipe_x_tex(150,750,168,108,8,0.20f,0.19f,0.17f);
    eng_pipe_x(150,750,206,114,5,0.24f,0.13f,0.06f);
    eng_pipe_x(150,750,352,104,7,0.21f,0.18f,0.13f);
    eng_pipe_x(150,750,372,113,4,0.14f,0.18f,0.22f);
    /* cable trays, kept high and to the sides */
    eng_tray_x(154,742,560,112);
    eng_tray_x(154,742,148,115);
    eng_tray_x(154,742,588,108);
    eng_tray_x(154,742,320,120);
    /* recessed ceiling fixtures */
    eng_fixture(292,320,0.95f); eng_fixture(552,320,0.90f);
    eng_fixture(292,520,0.88f); eng_fixture(552,520,0.86f);
    gl_glow_quad(238,268,346,372,2.26f,0.36f,0.55f,0.76f,0.14f);
    gl_glow_quad(498,268,606,372,2.26f,0.34f,0.52f,0.72f,0.13f);
    gl_glow_quad(238,468,346,572,2.26f,0.30f,0.46f,0.66f,0.10f);
    gl_glow_quad(498,468,606,572,2.26f,0.28f,0.44f,0.64f,0.09f);
    for(float x=200;x<760;x+=180){
        gl_box(x-1.0f,164,122,x+1.0f,171,127,0.11f,0.10f,0.09f);
        gl_box(x-1.0f,203,122,x+1.0f,209,127,0.11f,0.10f,0.09f);
        gl_box(x-1.0f,349,122,x+1.0f,355,127,0.11f,0.10f,0.09f);
        gl_box(x-1.0f,369,122,x+1.0f,375,127,0.11f,0.10f,0.09f);
    }
    eng_fixture(292,520,0.90f); eng_fixture(552,520,0.86f);
    eng_fixture(632,440,0.88f);
    gl_box(166,238,124,730,242,126,0.34f,0.48f,0.64f);
    gl_glow_quad(166,234,730,246,125.6f,0.30f,0.46f,0.68f,0.16f);
    gl_box(166,498,124,730,502,126,0.30f,0.43f,0.58f);
    gl_glow_quad(166,494,730,506,125.6f,0.26f,0.40f,0.60f,0.13f);
}
static void gl_engineering_room(float light)
{
    if(!engineering_room_enabled) return;
    eng_generator();
    eng_coolant();
    eng_walls();
    eng_overhead();
    /* Room-only baked ambient: lift the very dark albedo without touching the corridor. */
    gl_glow_quad(130,130,766,638,2.12f,0.18f,0.26f,0.36f,0.085f);
    gl_glow_wall_y(136.05f,130,766,0,128,0.16f,0.22f,0.30f,0.050f);
    gl_glow_wall_y(632.0f,130,766,0,128,0.14f,0.20f,0.28f,0.045f);
    gl_glow_wall_x(136.05f,130,638,0,128,0.14f,0.20f,0.28f,0.040f);
    gl_glow_wall_x(760.0f,130,638,0,128,0.14f,0.20f,0.28f,0.040f);
}

static void gl_corridor(float light, int door_open) {
    gl_corridor_static();
    /* Dynamic lamp lenses (Glon light pulse). */
    for (int x=820;x<1240;x+=160)
        gl_light_panel(x+13,296,x+67,344,125.7f,light);
    gl_blast_door(light,door_open);
}
static void gl_scene(const Status *st, const Obj *objs, int nobj) {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    gl_projection(); gl_view(last_px, last_py, st_current_angle);
    glDisable(GL_TEXTURE_2D); glDisable(GL_BLEND); glDepthMask(GL_TRUE); glEnable(GL_DEPTH_TEST);
    float l=0.72f + 0.16f*(float)sin((double)st->light*0.09);
    gl_corridor(l, st->door_open);
    gl_engineering_room(l);
    /* depth-tested, alpha-tested billboards */
    glEnable(GL_TEXTURE_2D); glEnable(GL_ALPHA_TEST); glAlphaFunc(GL_GREATER,0.25f);
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    for (int i=0;i<nobj;i++) {
        if (!objs[i].visible) continue;
        glDepthMask(GL_TRUE);
        gl_prop_3d(&objs[i]);
    }
    if (st->visible) {
        AlienFrame af = alien_frame_select(st);
        int idx = af.idx;
        double px_scale = 1.5 / alien_dir_eff_h[idx];
        double wworld = alien_dir_w[idx] * px_scale;
        double hworld = alien_dir_h[idx] * px_scale;
        double pivot_world = (alien_dir_pivot_x[idx] - alien_dir_w[idx] * 0.5) * px_scale;
        double below_ground = (alien_dir_h[idx] - alien_dir_anchor_y[idx]) * px_scale;
        glDepthMask(GL_TRUE);
        gl_billboard(gl_tex[GT_ALIEN0+idx], st->ax, st->ay, wworld, hworld,
                     pivot_world, below_ground, af.flip);
    }
    glDisable(GL_ALPHA_TEST); glDisable(GL_BLEND);
    gl_draw_weapon(st);
    gl_draw_tracker(st);
    if(getenv("GLON_FINISH")){ long _pf=now_ns(); glFinish(); gl_prof_finish_ms=(double)(now_ns()-_pf)/1e6; } else gl_prof_finish_ms=0;
}

static int gl_bench(int frames, int w, int h) {
    if (gl_init(w,h,0) != 0) return 2;
    gl_active = 1;
    load_session(); build_blocks();
    Spans s; Detail det; TexRows tr; Status st; Obj objs[6];
    double sim=0, gpu=0, total=0; int n=0;
    for (int i=0;i<frames;i++) {
        long t0=now_ns();
        if (tick(i&63)!=0) return 2;
        int px,py,pa; if (get_state(&px,&py,&pa)!=0) return 2; st_current_angle=pa;
        if (get_status(&st)!=0) return 2;
        if (objects_mode) memcpy(objs,current_objs,sizeof current_objs);
        long t1=now_ns();
        int nobj=objects_mode?6:0;
        gl_scene(&st,objs,nobj);
        long t2=now_ns(); SDL_GL_SwapWindow(gl_win); long t3=now_ns();
        sim+=(t1-t0)/1e6; gpu+=(t2-t1)/1e6; total+=(t3-t0)/1e6; n++;
    }
    printf("gl bench renderer=%s resolution=%dx%d frames=%d sim=%.2fms submit=%.2fms total=%.2fms fps=%.1f loader=%ld heap=%ld\n",
           glGetString(GL_RENDERER), w,h,n,sim/n,gpu/n,total/n,1000.0/(total/n),(long)M[GC_LOADER_HP],r0_s1_heap_high());
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit();
    return 0;
}

static void gl_fixed_pose(void) {
    reset();
    if (alien_cover) { if (set_close_pose()!=0) fail("gl cover pose"); return; }
    cell p=parse_source("[ doom/px: 616 doom/py: 320 doom/angle: 0 doom/aim doom/door-open: 48 doom/door-target: 64 doom/door-blocked: 0 doom/alien-x: 990 doom/alien-y: 320 doom/alien-state: 1 doom/alien-health: 100 doom/alien-detected: 1 ]", "gl pose");
    if (run_block(p,"gl pose")!=0) fail("gl pose");
}
static int gl_demo(int w,int h) {
    if (gl_init(w,h,1)!=0) return 2;
    gl_active = 1;
    load_session(); build_blocks();
    if (gl_door_closed) reset(); else gl_fixed_pose();
    Spans s; Detail det; TexRows tr; Status st; Obj objs[6];
    int px,py,pa; get_state(&px,&py,&pa); st_current_angle=pa;
    int running=1;
    while(running){ SDL_Event e; while(SDL_PollEvent(&e)) if(e.type==SDL_QUIT||(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE))running=0;
        tick(0); get_state(&px,&py,&pa); st_current_angle=pa; get_status(&st); int nobj=0; if(objects_mode) {get_objects(objs);nobj=6;}
        gl_update_viewport(); gl_scene(&st,objs,nobj); SDL_GL_SwapWindow(gl_win); sim_tick++; SDL_Delay(16);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}


static int interactive_gl(int w, int h) {
    if (gl_init(w,h,1)!=0) { fprintf(stderr,"gl: falling back to CPU renderer\n"); return -1; }
    gl_active = 1;
    printf("G5p renderer=OpenGL framebuffer=%dx%d interactive=on\n",w,h);
    load_session(); build_blocks();
    if (run_block(parse_source("[ doom/angle: 240 doom/aim ]", "spawn orientation"), "spawn orientation") != 0) return 2;
    audio_start();
    Status st; Obj objs[6]; Spans spans; Detail det; TexRows tr;
    int px,py,pa; double acc=0.0,last=(double)now_ns(),fps_start=(double)now_ns(); int frames=0;
    double prof_tick=0,prof_state=0,prof_scene=0,prof_swap=0,prof_finish=0;
    int running=1;
    HeldKeys held={0};
    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT) running=0;
            else if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
                memset(&held,0,sizeof held);
            }
            else if(e.type==SDL_KEYDOWN || e.type==SDL_KEYUP) {
                held_keys_apply(&held,&e);
                int down=e.type==SDL_KEYDOWN;
                SDL_Scancode sc=e.key.keysym.scancode;
                SDL_Keycode sym=e.key.keysym.sym;
                if((sc==SDL_SCANCODE_ESCAPE||sym==SDLK_ESCAPE) && down) running=0;
                else if((sc==SDL_SCANCODE_E||sym==SDLK_e) && down && !e.key.repeat) run_block(activate_block,"activate");
                else if((sc==SDL_SCANCODE_R||sym==SDLK_r) && down && !e.key.repeat) printf("GL renderer: R debug toggle not used\n");
                else if((sc==SDL_SCANCODE_M||sym==SDLK_m) && down && !e.key.repeat) audio_toggle_mute();
                else if((sc==SDL_SCANCODE_LEFTBRACKET||sym==SDLK_LEFTBRACKET) && down && !e.key.repeat) audio_set_volume(SDL_AtomicGet(&audio_volume)-5);
                else if((sc==SDL_SCANCODE_RIGHTBRACKET||sym==SDLK_RIGHTBRACKET) && down && !e.key.repeat) audio_set_volume(SDL_AtomicGet(&audio_volume)+5);
            }
        }
        gl_update_viewport();
        const Uint8 *k=SDL_GetKeyboardState(NULL);
        int mask=held_keys_mask(&held,k);
        double tn=(double)now_ns(); double dt=(tn-last)/1e9; last=tn; if(dt>0.10)dt=0.10; acc+=dt;
        long pt_tick0=now_ns();
        int ticks=0; while(acc>=1.0/TICK_HZ && ticks<10){ if(tick(mask)!=0){running=0;break;} acc-=1.0/TICK_HZ; ticks++; }
        long pt_tick1=now_ns();
        if(!running) break;
        if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) break;
        long pt_scene0=now_ns();
        st_current_angle=pa;
        int nobj=0;
        if(objects_mode && current_objs_valid) { memcpy(objs,current_objs,sizeof objs); nobj=6; }
        long pt0=now_ns(); gl_scene(&st,objs,nobj); long pt1=now_ns();
        SDL_GL_SwapWindow(gl_win); long pt2=now_ns(); frames++;
        if(smoke_frames && frames>=smoke_frames) running=0;
        prof_tick += (double)(pt_tick1-pt_tick0)/1e6;
        prof_state += (double)(pt_scene0-pt_tick1)/1e6;
        prof_scene += (double)(pt1-pt_scene0)/1e6;
        prof_swap += (double)(pt2-pt1)/1e6;
        prof_finish += gl_prof_finish_ms;
        if(frames%120==0){ double secs=((double)now_ns()-fps_start)/1e9; printf("G5p GL frame=%d fps=%.1f tick=%.2f state=%.2f scene=%.2f finish=%.2f swap=%.2f player=%d,%d angle=%d door=%d/%d alien=%d,%d state=%d\n",frames,frames/secs,prof_tick/120,prof_state/120,prof_scene/120,prof_finish/120,prof_swap/120,px,py,pa,st.door_open,st.door_blocked,st.ax,st.ay,st.state); fflush(stdout); fps_start=(double)now_ns(); prof_tick=prof_state=prof_scene=prof_swap=prof_finish=0; }
        SDL_Delay(1);
    }
    audio_stop();
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}


static void gl_save_ppm(const char *path,int w,int h);
static int g5u_hit_at(int x,int y,int r) {
    char src[160];
    snprintf(src,sizeof src,"[ doom/hit: 0 doom/check-all %d %d %d print doom/hit ]",x,y,r);
    cell b=parse_source(src,"g5u collision probe");
    int v=-1;
    if(run_capture(b,&v,1,"g5u collision probe")!=0) return -1;
    return v;
}
static void g5u_capture_scene(const char *dir,const char *name,int w,int h) {
    int px,py,pa; Status st; Obj objs[6];
    if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return;
    st_current_angle=pa; int nobj=0;
    if(objects_mode){get_objects(objs);nobj=6;}
    gl_scene(&st,objs,nobj);
    char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,name); gl_save_ppm(path,w,h);
    printf("g5u capture %s player=%d,%d angle=%d alien=%d,%d state=%d door=%d/%d\n",
           name,px,py,pa,st.ax,st.ay,st.state,st.door_open,st.door_blocked);
}
static int g5u_nav(const char *dir,int w,int h,int legacy) {
    objects_mode=1;
    if(gl_init(w,h,0)!=0) return 2;
    gl_active=1; load_session(); build_blocks();
    reset();
    if(!legacy) {
        cell o=parse_source("[ doom/angle: 240 doom/aim ]","g5u spawn orientation");
        if(run_block(o,"g5u spawn orientation")!=0) return 2;
    }
    Status st; int px,py,pa;
    get_state(&px,&py,&pa); get_status(&st);
    printf("g5u start player=%d,%d angle=%d alien=%d,%d state=%d door=%d/%d\n",
           px,py,pa,st.ax,st.ay,st.state,st.door_open,st.door_blocked);
    printf("g5u map room=[128,128..768,640] corridor=[768,256..1280,384] spawn=(320,320,0)\n");
    Obj objs[6]; if(get_objects(objs)==0) for(int i=0;i<6;i++)
        printf("g5u object %d id=%d center=%d,%d hw=%d hd=%d h=%d\n",
               i,objs[i].id,objs[i].x,objs[i].y,objs[i].hw,objs[i].hd,objs[i].h);
    g5u_capture_scene(dir,"00-normal-start",w,h);
    if(!legacy){
        run_ticks(16,16); g5u_capture_scene(dir,"01-route-facing-door",w,h);
        run_ticks(1,60); g5u_capture_scene(dir,"02-route-at-closed-door",w,h);
        run_block(activate_block,"route activate");
        run_ticks(0,12); g5u_capture_scene(dir,"03-route-door-open",w,h);
        run_ticks(1,20); g5u_capture_scene(dir,"04-route-in-corridor",w,h);
        run_ticks(16,128); run_ticks(1,120); run_ticks(32,128); g5u_capture_scene(dir,"05-route-back-in-room",w,h);
    }
    /* Reproduce the reported barrel pocket: player just inside the room, Alien
       at the north side of the corridor behind the (940,370) barrel. */
    reset();
    cell b=parse_source("[ doom/px: 760 doom/py: 350 doom/angle: 0 doom/aim "
        "doom/door-open: 64 doom/door-target: 64 doom/door-blocked: 0 "
        "doom/alien-x: 970 doom/alien-y: 368 doom/alien-state: 1 "
        "doom/alien-health: 100 doom/alien-detected: 1 ]","g5u barrel pose");
    if(run_block(b,"g5u barrel pose")!=0) return 2;
    cell noavoid=parse_source("[ doom/alien-avoid: 0 ]","g5u legacy ai");
    int prevax=970,prevay=368;
    for(int t=0;t<=240;t++){
        if(t==0 || t==30 || t==60 || t==120 || t==240)
            g5u_capture_scene(dir,t==0?"06-barrel-start":t==30?"07-barrel-30":
                t==60?"08-barrel-60":t==120?"09-barrel-120":"10-barrel-240",w,h);
        if(t==240) break;
        get_state(&px,&py,&pa); get_status(&st);
        int ax=st.ax,ay=st.ay; int dx=px-ax,dy=py-ay;
        int ix=dx>2?2:dx<-2?-2:0, iy=dy>2?2:dy<-2?-2:0;
        if(t==0 || t==1 || t==2 || t==15 || t==30 || t==60 || t==120 || t==180 || t==239)
            printf("g5u tick=%03d player=%d,%d alien=%d,%d target=%d,%d intended=%d,%d "
                   "state=%d det=%d bars=%d\n",
                   t,px,py,ax,ay,dx,dy,ix,iy,st.state,st.detected,st.door_blocked);
        if(run_ticks(0,1)!=0) return 2;
        if(legacy && run_block(noavoid,"g5u legacy ai")!=0) return 2;
        get_status(&st);
        int movedx=st.ax-ax, movedy=st.ay-ay;
        if(t<=2 || t==15 || t==30 || t==60 || t==120 || t==180 || t==239)
            printf("g5u actual tick=%03d alien=%d,%d moved=%d,%d reject_x=%d reject_y=%d\n",
                   t+1,st.ax,st.ay,movedx,movedy,
                   ix!=0 && movedx==0, iy!=0 && movedy==0);
        prevax=st.ax; prevay=st.ay;
    }
    (void)prevax; (void)prevay;
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_orient_shot(const char *dir,int w,int h) {
    if (gl_init(w,h,0)!=0) return 2;
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT); glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D,gl_tex[GT_ORIENT]); glColor3f(1,1,1);
    glBegin(GL_QUADS); glTexCoord2f(0,0); glVertex2f(-1,-1); glTexCoord2f(1,0); glVertex2f(1,-1);
    glTexCoord2f(1,1); glVertex2f(1,1); glTexCoord2f(0,1); glVertex2f(-1,1); glEnd(); glFinish();
    uint8_t *buf=malloc((size_t)w*h*4); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf);
    char path[4096]; snprintf(path,sizeof path,"%s/orientation.ppm",dir); FILE*f=fopen(path,"wb");
    if(!f)return 2; fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=h-1;y>=0;y--)for(int x=0;x<w;x++)fwrite(buf+(y*w+x)*4,1,3,f); fclose(f); free(buf);
    printf("orientation shot %s\n",path);
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}


static void gl_save_ppm(const char *path,int w,int h) {
    uint8_t *buf=malloc((size_t)w*h*4); if(!buf) return;
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf);
    FILE *f=fopen(path,"wb"); if(!f){free(buf);return;}
    fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=h-1;y>=0;y--) for(int x=0;x<w;x++) fwrite(buf+(y*w+x)*4,1,3,f);
    fclose(f); free(buf);
}
static int gl_apply_pose(int px,int py,int angle,int door_open,int door_blocked,int ax,int ay,int astate){
    reset(); char src[320];
    snprintf(src,sizeof src,
      "[ doom/px: %d doom/py: %d doom/angle: %d doom/aim "
      "doom/door-open: %d doom/door-target: %d doom/door-blocked: %d "
      "doom/alien-x: %d doom/alien-y: %d doom/alien-state: %d "
      "doom/alien-health: 100 doom/alien-detected: 1 ]",
      px,py,angle,door_open,door_open,door_blocked,ax,ay,astate);
    cell b=parse_source(src,"gl tour pose"); return run_block(b,"gl tour pose");
}
static int gl_tour(const char *dir,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    struct P{const char*n;int px,py,a,open,block,ax,ay,state;float pitch;} poses[]={
      {"01-entrance",800,320,0,64,0,990,320,1},
      {"02-mid-corridor",1000,320,0,64,0,990,320,1},
      {"03-close-wall",1100,320,64,64,0,990,320,1},
      {"04-look-back",900,320,128,64,0,990,320,1},
      {"05-door-closed",620,320,0,0,1,1120,320,0},
      {"06-door-open",620,320,0,64,0,990,320,1},
      {"07-crates-tracker",850,330,0,64,0,990,320,1},
      {"08-far-bulkhead",1080,320,0,64,0,990,320,1},
      {"09-floor-detail",900,340,0,64,0,300,320,0,-24.0f},
      {"10-ceiling-lights",900,320,0,64,0,300,320,0,24.0f},
      {"11-alien-tracker",760,320,0,64,0,990,320,1},
      {"12-spawn-doorway",320,320,0,0,1,300,320,0,0},
      {"13-room-turnaround",320,320,128,0,1,1120,320,0,0},
      {"14-corridor-door-closed",900,320,128,0,1,1120,320,0,0},
      {"15-alien-attack",850,330,0,64,0,990,320,2,0}
    };
    for(size_t i=0;i<sizeof poses/sizeof poses[0];i++){
        gl_debug_pitch=poses[i].pitch;
        if(gl_apply_pose(poses[i].px,poses[i].py,poses[i].a,poses[i].open,poses[i].block,poses[i].ax,poses[i].ay,poses[i].state)!=0) return 2;
        int px,py,pa; get_state(&px,&py,&pa); st_current_angle=pa;
        Status st; Obj objs[6]; if(get_status(&st)!=0) return 2; int nobj=0;
        if(objects_mode){get_objects(objs);nobj=6;}
        gl_scene(&st,objs,nobj);
        char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,poses[i].n); gl_save_ppm(path,w,h);
        printf("tour %s %dx%d\n",poses[i].n,w,h);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_door_shots(const char *dir,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    struct DP { const char *name; int px,py,angle,open,blocked; float pitch; } poses[] = {
        {"01-door-closed",620,320,0,0,1,0},
        {"02-door-partial-24",620,320,0,24,1,0},
        {"03-door-opening-40",620,320,0,40,0,0},
        {"04-door-open-64",620,320,0,64,0,0},
        {"05-door-oblique-closed",700,252,24,0,1,0},
        {"06-door-oblique-partial",700,252,24,28,1,0}
    };
    for(size_t i=0;i<sizeof poses/sizeof poses[0];i++){
        gl_debug_pitch=poses[i].pitch;
        if(gl_apply_pose(poses[i].px,poses[i].py,poses[i].angle,
                         poses[i].open,poses[i].blocked,1120,320,0)!=0) return 2;
        int px,py,pa; Status st; Obj objs[6];
        if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0;
        gl_scene(&st,objs,nobj);
        char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,poses[i].name);
        gl_save_ppm(path,w,h);
        printf("door %s %dx%d open=%d blocked=%d\n",poses[i].name,w,h,st.door_open,st.door_blocked);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_capture(const char *dir,const char *name,int w,int h,
                      const Status *st,const Obj *objs,int nobj) {
    gl_scene(st,objs,nobj);
    char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,name); gl_save_ppm(path,w,h);
    printf("directional %s %dx%d\n",name,w,h);
    return 0;
}

static int gl_directional(const char *dir,int w,int h) {
    if (gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    Obj objs[6]; Status st;
    int px,py,pa;
    /* Head-on: player looks down +x while the Alien approaches along -x. */
    if (gl_apply_pose(320,320,0,64,0,1000,320,1)!=0) return 2;
    int got_long=0,got_mid=0,got_step2=0,got_close=0,got_attack=0;
    for (int i=0;i<900;i++) {
        if (tick(0)!=0 || get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0; if(objects_mode){get_objects(objs);nobj=6;}
        (void)alien_frame_index(&st);
        if (!got_long && st.tdepth <= 680) { gl_capture(dir,"01-headon-long",w,h,&st,objs,nobj); got_long=1; }
        if (!got_mid && st.tdepth <= 320) { gl_capture(dir,"02-headon-medium",w,h,&st,objs,nobj); got_mid=1; }
        if (got_mid && !got_step2 && st.tdepth <= 300) { gl_capture(dir,"02b-headon-medium-step2",w,h,&st,objs,nobj); got_step2=1; }
        if (!got_close && st.tdepth <= 150) { gl_capture(dir,"03-headon-close",w,h,&st,objs,nobj); got_close=1; }
        if (!got_attack && st.state==2) { gl_capture(dir,"07-headon-attack",w,h,&st,objs,nobj); got_attack=1; break; }
    }
    /* Three-quarter: the Alien is off-centre and approaches diagonally in view. */
    if (gl_apply_pose(500,320,0,64,0,720,430,1)!=0) return 2;
    for (int i=0;i<240;i++) {
        if (tick(0)!=0 || get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0; if(objects_mode){get_objects(objs);nobj=6;}
        int idx = alien_frame_index(&st);
        if (getenv("G5S_DEBUG") && i%20==0) fprintf(stderr,"diag i=%d ax=%d ay=%d tside=%d tdepth=%d state=%d idx=%d vis=%d\n",i,st.ax,st.ay,st.tside,st.tdepth,st.state,idx,st.visible);
        if (idx==ALIEN_DIR_THREEQ && st.visible) {
            gl_capture(dir,"04-diagonal",w,h,&st,objs,nobj); break;
        }
    }
    /* Side: the Alien crosses the view while closing on the player. */
    if (gl_apply_pose(500,320,0,64,0,700,160,1)!=0) return 2;
    for (int i=0;i<240;i++) {
        if (tick(0)!=0 || get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0; if(objects_mode){get_objects(objs);nobj=6;}
        AlienFrame af = alien_frame_select(&st);
        if (getenv("G5S_DEBUG") && (i<15 || i%20==0)) fprintf(stderr,"side i=%d ax=%d ay=%d tside=%d tdepth=%d state=%d idx=%d vis=%d\n",i,st.ax,st.ay,st.tside,st.tdepth,st.state,af.idx,st.visible);
        if (af.idx==ALIEN_DIR_SIDE && st.visible && st.tdepth < 500) {
            gl_capture(dir,"05-crossing",w,h,&st,objs,nobj); break;
        }
    }
    /* Diagnostic still only: the current AI always closes on the player and has
     * no retreat state, so rear artwork cannot occur in normal gameplay yet. */
    if (gl_apply_pose(500,320,0,64,0,900,320,1)!=0) return 2;
    if (get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
    st_current_angle=pa; st.visible=1;
    alien_view_reset(); (void)alien_frame_index(&st);
    for (int i=0;i<4;i++) { st.ax += 2; (void)alien_frame_index(&st); }
    { int nobj=0; if(objects_mode){get_objects(objs);nobj=6;}
      gl_capture(dir,"06-rear-diagnostic",w,h,&st,objs,nobj); }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}


static int gl_room_shots(const char *dir,int w,int h,int plain) {
    engineering_room_enabled = !plain;
    if (gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    struct RP { const char *name; int px,py,angle,ax,ay,astate; } poses[] = {
        {"01-inside-entrance",700,320,128,1120,320,0},
        {"02-centre-generator",450,560,176,1120,320,0},
        {"03-opposite-corner",650,560,160,1120,320,0},
        {"04-corridor-to-room",900,320,128,1120,320,0},
        {"05-room-to-corridor",700,320,0,1120,320,0},
        {"06-alien-near-generator",650,320,128,390,360,1},
        {"07-coolant-bay",520,420,128,1120,320,0}
    };
    Status st; Obj objs[6];
    for(size_t i=0;i<sizeof poses/sizeof poses[0];i++){
        if(gl_apply_pose(poses[i].px,poses[i].py,poses[i].angle,64,0,poses[i].ax,poses[i].ay,poses[i].astate)!=0) return 2;
        int px,py,pa; if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0; if(objects_mode){get_objects(objs);nobj=6;}
        gl_debug_pitch=0;
        gl_scene(&st,objs,nobj);
        char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,poses[i].name); gl_save_ppm(path,w,h);
        printf("room %s %dx%d%s\n",poses[i].name,w,h,plain?" plain":"");
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_detail_shots(const char *dir,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    struct DP { const char *name; int px,py,angle,ax,ay,astate; float pitch; } poses[] = {
        {"01-south-cabinets",450,430,64,1120,320,0,0},
        {"02-south-cabinet-closeup",450,555,64,1120,320,0,2},
        {"03-east-cabinets",620,520,0,1120,320,0,0},
        {"04-east-cabinet-closeup",670,520,0,1120,320,0,2},
        {"05-room-ceiling",450,380,64,1120,320,0,34},
        {"06-room-ceiling-wide",650,520,128,1120,320,0,28},
        {"07-room-wide",700,600,160,1120,320,0,8}
    };
    for(size_t i=0;i<sizeof poses/sizeof poses[0];i++){
        gl_debug_pitch=poses[i].pitch;
        if(gl_apply_pose(poses[i].px,poses[i].py,poses[i].angle,64,0,
                         poses[i].ax,poses[i].ay,poses[i].astate)!=0) return 2;
        int px,py,pa; Status st; Obj objs[6];
        if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0;
        if(objects_mode){get_objects(objs);nobj=6;}
        gl_scene(&st,objs,nobj);
        char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,poses[i].name);
        gl_save_ppm(path,w,h);
        printf("detail %s %dx%d\n",poses[i].name,w,h);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_g5z_shots(const char *dir,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    struct ZP { const char *name; int px,py,angle,ax,ay,astate; float pitch; } poses[] = {
        {"01-containers-normal",800,320,0,1120,320,0,0},
        {"02-container-crate-close",820,340,200,990,320,0,-22},
        {"03-container-drum-close",900,340,186,990,320,0,-25},
        {"04-control-bank",650,320,192,1120,320,0,0},
        {"05-control-bank-close",650,290,192,1120,320,0,0},
        {"06-room-wide",700,600,160,1120,320,0,8},
        {"07-corridor-wide",1200,320,128,990,320,0,0}
    };
    for(size_t i=0;i<sizeof poses/sizeof poses[0];i++){
        gl_debug_pitch=poses[i].pitch;
        if(gl_apply_pose(poses[i].px,poses[i].py,poses[i].angle,64,0,
                         poses[i].ax,poses[i].ay,poses[i].astate)!=0) return 2;
        int px,py,pa; Status st; Obj objs[6];
        if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
        st_current_angle=pa; int nobj=0;
        if(objects_mode){get_objects(objs);nobj=6;}
        gl_scene(&st,objs,nobj);
        char path[4096]; snprintf(path,sizeof path,"%s/%s.ppm",dir,poses[i].name);
        gl_save_ppm(path,w,h);
        printf("g5z %s %dx%d\n",poses[i].name,w,h);
    }
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_detail_bench(int frames,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    gl_apply_pose(650,520,128,64,0,1120,320,0); gl_debug_pitch=28;
    int px,py,pa; Status st; Obj objs[6];
    if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
    st_current_angle=pa; int nobj=0;
    if(objects_mode){get_objects(objs);nobj=6;}
    double total=0;
    for(int i=0;i<frames;i++){
        long t0=now_ns(); gl_scene(&st,objs,nobj); SDL_GL_SwapWindow(gl_win); long t1=now_ns();
        total+=(t1-t0)/1e6;
    }
    printf("gl detail bench renderer=%s resolution=%dx%d frames=%d total=%.2fms fps=%.1f"
           " loader=%ld heap=%ld\n",glGetString(GL_RENDERER),w,h,frames,total/frames,
           1000.0/(total/frames),(long)M[GC_LOADER_HP],r0_s1_heap_high());
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_g5z_bench(int pose,int frames,int w,int h) {
    if(gl_init(w,h,0)!=0) return 2; gl_active=1; load_session(); build_blocks();
    if(pose==1) { gl_apply_pose(650,320,192,64,0,1120,320,0); gl_debug_pitch=0; }
    else if(pose==2) { gl_apply_pose(700,600,160,64,0,1120,320,0); gl_debug_pitch=8; }
    else { gl_apply_pose(800,320,0,64,0,1120,320,0); gl_debug_pitch=0; }
    int px,py,pa; Status st; Obj objs[6];
    if(get_state(&px,&py,&pa)!=0 || get_status(&st)!=0) return 2;
    st_current_angle=pa; int nobj=0;
    if(objects_mode){get_objects(objs);nobj=6;}
    double total=0;
    for(int i=0;i<frames;i++){
        long t0=now_ns(); gl_scene(&st,objs,nobj); SDL_GL_SwapWindow(gl_win); long t1=now_ns();
        total+=(t1-t0)/1e6;
    }
    printf("gl g5z bench pose=%d renderer=%s resolution=%dx%d frames=%d total=%.2fms fps=%.1f"
           " loader=%ld heap=%ld\n",pose,glGetString(GL_RENDERER),w,h,frames,total/frames,
           1000.0/(total/frames),(long)M[GC_LOADER_HP],r0_s1_heap_high());
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}

static int gl_shots(const char *dir,int w,int h) {
    if (gl_init(w,h,0)!=0) return 2; gl_active = 1; load_session(); build_blocks(); gl_fixed_pose();
    Status st; Obj objs[6]; int px,py,pa; get_state(&px,&py,&pa); st_current_angle=pa; get_status(&st); int nobj=0;
    if(objects_mode){get_objects(objs);nobj=6;}
    gl_scene(&st,objs,nobj);
    uint8_t *buf=malloc((size_t)w*h*4); if(!buf)return 2;
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,buf);
    char path[4096]; snprintf(path,sizeof path,"%s/corridor.ppm",dir);
    FILE *f=fopen(path,"wb"); if(!f)return 2; fprintf(f,"P6\n%d %d\n255\n",w,h);
    for(int y=h-1;y>=0;y--) for(int x=0;x<w;x++){uint8_t*p=buf+(y*w+x)*4;fwrite(p,1,3,f);} fclose(f); free(buf);
    printf("gl shot %dx%d %s\n",w,h,path);
    SDL_GL_DeleteContext(gl_ctx); SDL_DestroyWindow(gl_win); SDL_Quit(); return 0;
}


static int tracker_test(void) {
    int fails=0, bx,by; Status s; memset(&s,0,sizeof s);
    s.detected=1; s.tside=0; s.tdepth=0;
    int vis=tracker_blip(&s,&bx,&by);
    fails+=expect_eq("tracker ahead blip visible",vis,1);
    fails+=expect_eq("tracker ahead x",bx,42); fails+=expect_eq("tracker ahead y",by,168);
    s.tside=700; s.tdepth=0; tracker_blip(&s,&bx,&by);
    fails+=expect_eq("tracker right x",bx,60);
    s.tside=-700; tracker_blip(&s,&bx,&by); fails+=expect_eq("tracker left x",bx,24);
    s.tside=0; s.tdepth=-700; tracker_blip(&s,&bx,&by); fails+=expect_eq("tracker behind",by,186);
    s.tside=0; s.tdepth=700; tracker_blip(&s,&bx,&by); fails+=expect_eq("tracker near y",by,150);
    s.detected=0; vis=tracker_blip(&s,&bx,&by);
    fails+=expect_eq("tracker out of range dark",vis,0);
    printf("tracker-test: %s\n",fails?"FAILED":"PASSED");
    return fails?1:0;
}


static int resize_test(void) {
    int fails=0;
    int sizes[][2]={{640,400},{960,600},{1280,800},{1600,900},{1200,900}};
    for (size_t i=0;i<sizeof sizes/sizeof sizes[0];i++) {
        int x,y,w,h; int rc=gl_viewport_fill(sizes[i][0],sizes[i][1],&x,&y,&w,&h);
        fails += expect_eq("resize viewport fills drawable",rc,0);
        fails += expect_eq("resize viewport origin",x==0&&y==0,1);
        fails += expect_eq("resize viewport dimensions",w==sizes[i][0]&&h==sizes[i][1],1);
        float s=gl_hud_scale(sizes[i][1]);
        float rx=20.0f*s, ry=20.0f*s;
        fails += expect_eq("HUD circular at resize",rx==ry,1);
    }
    fails += expect_eq("HUD cap at 1600 high", gl_hud_scale(1600)==2.0f, 1);
    fails += expect_eq("HUD base at 200 high", gl_hud_scale(200)==1.0f, 1);
    printf("resize-test: %s\n",fails?"FAILED":"PASSED");
    return fails?1:0;
}

int main(int argc, char **argv) {
    const char *env = getenv("DOOM_ROOT");
    if (env && *env) root_dir = env;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--raw-wall") == 0) raw_wall_mode = 1;
        if (strcmp(argv[i], "--checker") == 0) checker_walls = 1;
        if (strcmp(argv[i], "--new-textures") == 0) new_textures = 1;
        if (strcmp(argv[i], "--smooth-shading") == 0) smooth_shading = 1;
        if (strcmp(argv[i], "--flat-surfaces") == 0) flat_surfaces = 1;
        if (strcmp(argv[i], "--placeholder-corridor") == 0) corridor_art = 0;
        if (strcmp(argv[i], "--renderer") == 0 && i+1<argc) {
            if (strcmp(argv[i+1],"cpu") == 0) renderer_gl = 0;
            if (strcmp(argv[i+1],"gl") == 0) renderer_gl = 1;
        }
        if (strcmp(argv[i], "--ambient-volume") == 0 && i+1<argc) {
            ambient_volume_pct=atoi(argv[++i]);
            if(ambient_volume_pct<0)ambient_volume_pct=0;
            if(ambient_volume_pct>100)ambient_volume_pct=100;
        }
        if (strcmp(argv[i], "--no-audio") == 0) audio_disabled = 1;
        if (strcmp(argv[i], "--smoke-frames") == 0 && i+1<argc) smoke_frames=atoi(argv[++i]);
        if (strcmp(argv[i], "--objects") == 0) objects_mode = 1;
        if (strcmp(argv[i], "--alien-cover") == 0) { objects_mode = 1; alien_cover = 1; }
        if (strcmp(argv[i], "--demo-partial") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 1; }
        if (strcmp(argv[i], "--demo-emerge") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 2; }
        if (strcmp(argv[i], "--demo-side") == 0) { objects_mode = 1; alien_cover = 1; cover_view = 3; }
        if (strcmp(argv[i], "--gl-door") == 0) gl_door_closed = 1;
        if (strcmp(argv[i], "--filter") == 0 && i + 1 < argc &&
            strcmp(argv[i + 1], "linear") == 0) linear_filter = 1;
    }
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--objects-test") == 0) { objects_mode = 1; return objects_test(); }
    if (argc > 1 && strcmp(argv[1], "--demo-close") == 0) return alien_demo(2);
    if (argc > 1 && strcmp(argv[1], "--demo-walk") == 0) return alien_demo(1);
    if (argc > 2 && strcmp(argv[1], "--raw-wall") == 0) {
        if (strcmp(argv[2], "--self-test") == 0) return self_test();
        if (strcmp(argv[2], "--loader-report") == 0) return loader_report();
        if (strcmp(argv[2], "--bench") == 0) {
            int bench_alien = 0;
            for (int i = 4; i < argc; i++) {
                if (strcmp(argv[i], "simple") == 0) simple_mode = 1;
                if (strcmp(argv[i], "alien") == 0) bench_alien = 1;
            }
            return bench(atoi(argv[3]), bench_alien);
        }
    }
    if (argc > 2 && strcmp(argv[1], "--gl-bench") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_bench(atoi(argv[2]),w,h);
    }
    if (argc > 1 && strcmp(argv[1], "--gl-demo") == 0) {
        int w=argc>2?atoi(argv[2]):640, h=argc>3?atoi(argv[3]):400;
        for(int i=4;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_demo(w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--g5u-nav") == 0) {
        int w=argc>3?atoi(argv[3]):960, h=argc>4?atoi(argv[4]):600;
        int legacy=0; for(int i=5;i<argc;i++) if(strcmp(argv[i],"legacy")==0) legacy=1;
        return g5u_nav(argv[2],w,h,legacy);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-orient") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        return gl_orient_shot(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-door-shots") == 0) {
        int w=argc>3?atoi(argv[3]):960, h=argc>4?atoi(argv[4]):600;
        return gl_door_shots(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-tour") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_tour(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-directional") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_directional(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-rooms") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        int plain=0;
        for(int i=5;i<argc;i++){ if(strcmp(argv[i],"objects")==0) objects_mode=1;
            if(strcmp(argv[i],"plain")==0) plain=1; }
        return gl_room_shots(argv[2],w,h,plain);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-detail-shots") == 0) {
        int w=argc>3?atoi(argv[3]):960, h=argc>4?atoi(argv[4]):600;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_detail_shots(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-g5z-bench") == 0) {
        int pose=atoi(argv[2]), w=argc>4?atoi(argv[4]):960, h=argc>5?atoi(argv[5]):600;
        int frames=argc>3?atoi(argv[3]):120;
        for(int i=6;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_g5z_bench(pose,frames,w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-g5z-shots") == 0) {
        int w=argc>3?atoi(argv[3]):960, h=argc>4?atoi(argv[4]):600;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_g5z_shots(argv[2],w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-detail-bench") == 0) {
        int w=argc>3?atoi(argv[3]):960, h=argc>4?atoi(argv[4]):600;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_detail_bench(atoi(argv[2]),w,h);
    }
    if (argc > 2 && strcmp(argv[1], "--gl-shots") == 0) {
        int w=argc>3?atoi(argv[3]):640, h=argc>4?atoi(argv[4]):400;
        for(int i=5;i<argc;i++) if(strcmp(argv[i],"objects")==0) objects_mode=1;
        return gl_shots(argv[2],w,h);
    }
    if (argc > 1 && strcmp(argv[1], "--audio-test") == 0) return audio_selftest();
    if (argc > 1 && strcmp(argv[1], "--audio-device-test") == 0) return audio_device_test();
    if (argc > 1 && strcmp(argv[1], "--input-test") == 0) return input_test();
    if (argc > 1 && strcmp(argv[1], "--tracker-test") == 0) return tracker_test();
    if (argc > 1 && strcmp(argv[1], "--resize-test") == 0) return resize_test();
    if (argc > 1 && strcmp(argv[1], "--self-test") == 0) return self_test();
    if (argc > 1 && strcmp(argv[1], "--projection-test") == 0) {
        int rc = projection_test();
        fprintf(stderr, "glon-doom: projection-test %s\n", rc == 0 ? "passed" : "failed");
        return rc;
    }
    if (argc > 1 && strcmp(argv[1], "--trace") == 0) return scripted_trace();
    if (argc > 2 && strcmp(argv[1], "--shots") == 0) return shot_walkthrough(argv[2]);
    if (argc > 1 && strcmp(argv[1], "--stationary-test") == 0) return stationary_test();
    if (argc > 1 && strcmp(argv[1], "--loader-report") == 0) return loader_report();
    if (argc > 1 && strcmp(argv[1], "--simple") == 0) simple_mode = 1;
    if (argc > 1 && strcmp(argv[1], "--stationary") == 0) stationary_enabled = 1;
    if (argc > 2 && strcmp(argv[1], "--bench") == 0) {
        int bench_alien = 0;
        for (int i = 3; i < argc; i++) {
            if (strcmp(argv[i], "simple") == 0) simple_mode = 1;
            if (strcmp(argv[i], "alien") == 0) bench_alien = 1;
        }
        return bench(atoi(argv[2]), bench_alien);
    }
    if (argc > 2 && strcmp(argv[1], "--bench-fixed") == 0) {
        int frames = atoi(argv[2]);
        int i = 3;
        int x = 320, y = 320, a = 0;
        if (i < argc && strcmp(argv[i], "simple") == 0) { simple_mode = 1; i++; }
        if (i + 2 < argc) { x = atoi(argv[i]); y = atoi(argv[i + 1]); a = atoi(argv[i + 2]); }
        return bench_fixed(frames, x, y, a);
    }
    if (argc > 2 && strcmp(argv[1], "--stationary-bench") == 0) {
        int throttle = argc > 3 && strcmp(argv[3], "throttle") == 0;
        return stationary_bench(atoi(argv[2]), throttle);
    }
#ifdef DOOM_S1_PROFILE
    if (argc > 2 && strcmp(argv[1], "--opcode-bench") == 0)
        return opcode_bench(atoi(argv[2]));
#endif
#ifdef DOOM_S1_PROFILE
    if (argc > 2 && strcmp(argv[1], "--opcode-bench-simple") == 0) {
        simple_mode = 1;
        return opcode_bench(atoi(argv[2]));
    }
#endif
    if (renderer_gl) {
        objects_mode = 1;
        int rc=interactive_gl(640,400);
        if (rc==0) return 0;
        fprintf(stderr,"gl unavailable; using CPU renderer\n");
    }
    return interactive();
}
