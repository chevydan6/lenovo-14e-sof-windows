/* SPDX-License-Identifier: MIT */
#include "chromebook_dsp.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#define FILE_OPEN _open
#define FILE_STREAM _fdopen
#define FILE_CLOSE _close
#define FILE_MODE (_O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY)
#define FILE_PERMISSIONS (_S_IREAD | _S_IWRITE)
#include <sys/stat.h>
#else
#include <unistd.h>
#define FILE_OPEN open
#define FILE_STREAM fdopen
#define FILE_CLOSE close
#define FILE_MODE (O_WRONLY | O_CREAT | O_EXCL)
#define FILE_PERMISSIONS 0600
#endif

static unsigned u16(const unsigned char *b) { return b[0] | ((unsigned)b[1]<<8); }
static uint32_t u32(const unsigned char *b) { return b[0] | ((uint32_t)b[1]<<8) | ((uint32_t)b[2]<<16) | ((uint32_t)b[3]<<24); }
static void put32(unsigned char *b, uint32_t x) { unsigned i;for(i=0;i<4;++i)b[i]=(unsigned char)(x>>(8*i)); }
static int exact(FILE *f, void *b, size_t n) { return fread(b,1,n,f)==n; }
static int skip(FILE *f, uint32_t n) {
    unsigned char buffer[4096];
    while(n) { size_t part=n>sizeof(buffer)?sizeof(buffer):n;if(!exact(f,buffer,part))return 0;n-=(uint32_t)part; }
    return 1;
}
typedef struct { unsigned tag,bits,align;uint32_t bytes; } wav_info;
static int header(FILE *f, wav_info *info) {
    unsigned char b[64];uint32_t remain;int fmt=0;
    if(!exact(f,b,12) || memcmp(b,"RIFF",4) || memcmp(b+8,"WAVE",4) || u32(b+4)<4)return 0;
    remain=u32(b+4)-4;
    while(remain>=8 && exact(f,b,8)) {
        uint32_t n=u32(b+4),pad=n&1;remain-=8;
        if(n>remain || pad>remain-n)return 0;
        remain-=n+pad;
        if(!memcmp(b,"fmt ",4)) {
            if(fmt || n<16 || n>sizeof(b) || !exact(f,b,n))return 0;
            info->tag=u16(b); info->bits=u16(b+14);info->align=u16(b+12);
            if(u16(b+2)!=2 || u32(b+4)!=CB_SAMPLE_RATE || info->align!=2*info->bits/8 ||
               u32(b+8)!=CB_SAMPLE_RATE*info->align)return 0;
            if(info->tag==0xfffe) {
                static const unsigned char guid_tail[12]={0,0,0x10,0,0x80,0,0,0xaa,0,0x38,0x9b,0x71};
                if(n<40 || u16(b+16)<22 || u16(b+18)!=info->bits ||
                   (u32(b+20)!=0 && u32(b+20)!=3) || memcmp(b+28,guid_tail,12))return 0;
                info->tag=u32(b+24);
            }
            if(!((info->tag==1 && (info->bits==16 || info->bits==24 || info->bits==32)) ||
                 (info->tag==3 && info->bits==32)))return 0;
            fmt=1;if(pad && !skip(f,1))return 0;
        } else if(!memcmp(b,"data",4)) {
            if(!fmt || n%info->align)return 0;
            info->bytes=n;return 1;
        } else if(!skip(f,n+pad))return 0;
    }
    return 0;
}
static float sample(const unsigned char *b, const wav_info *w) {
    if(w->tag==3) { uint32_t u=u32(b);float x;memcpy(&x,&u,4);return x; }
    if(w->bits==16) { int v=(int)u16(b);if(v>=32768)v-=65536;return v/32768.0f; }
    if(w->bits==24) { int v=b[0]|((int)b[1]<<8)|((int)b[2]<<16);if(v>=8388608)v-=16777216;return (float)(v/8388608.0); }
    {uint32_t u=u32(b);int32_t v;memcpy(&v,&u,4);return (float)(v/2147483648.0);}
}
static int write_float(FILE *f, const float *data, size_t n) {
    unsigned char b[4];size_t i;
    for(i=0;i<n;++i) { uint32_t u;memcpy(&u,data+i,4);put32(b,u);if(fwrite(b,1,4,f)!=4)return 0; }
    return 1;
}
static int output_header(FILE *f, uint32_t frames) {
    /* IEEE float WAV with a fact chunk for non-PCM interoperability. */
    unsigned char b[56]={0};
    memcpy(b,"RIFF",4);put32(b+4,48+frames*8);memcpy(b+8,"WAVEfmt ",8);
    put32(b+16,16);b[20]=3;b[22]=2;put32(b+24,48000);put32(b+28,384000);b[32]=8;b[34]=32;
    memcpy(b+36,"fact",4);put32(b+40,4);put32(b+44,frames);memcpy(b+48,"data",4);put32(b+52,frames*8);
    return fwrite(b,1,sizeof(b),f)==sizeof(b);
}
static void usage(void) {
    puts("Usage: chromebook-dsp input.wav output.wav [--linear] [--compress] [--measured-bass] [--chromeos-volume N] [--gain-db N] [--no-limiter] [--no-eq]\n"
         "48 kHz stereo PCM16/24/32 or float32 input; float32 output.\n"
         "Default: recovered speaker filters, -9 dB output gain, independent -1 dBFS sample limiter.\n"
         "--linear: filters only, unity output gain, no compression/limiter latency.\n"
         "--compress: enable independently designed three-band dynamics (not recovered Waves behavior).\n"
         "Existing output files are never overwritten. No playback or driver installation.");
}
int main(int argc, char **argv) {
    FILE *in=NULL,*out=NULL; cb_config config;cb_state state;wav_info w={0};
    unsigned char raw[8192];float x[2048],y[2048];uint32_t remaining,frames;unsigned latency;int i,ok=0;
    if(argc==2 && (!strcmp(argv[1],"--help") || !strcmp(argv[1],"-h"))) {usage();return 0;}
    if(argc<3) {usage();return 2;}
    cb_default_config(&config);
    for(i=3;i<argc;++i) {
        if(!strcmp(argv[i],"--linear")) {config.compressor_enabled=0;config.harmonics_enabled=0;config.limiter_enabled=0;config.output_gain_db=0;}
        else if(!strcmp(argv[i],"--chromeos-volume")) {
            char *end; double volume;
            if (++i>=argc) {fprintf(stderr,"Missing ChromeOS volume\n");return 1;}
            volume=strtod(argv[i],&end);
            if (!argv[i][0] || *end || cb_measured_config(&config,volume)) {fprintf(stderr,"ChromeOS volume must be 25..100\n");return 1;}
        }
        else if(!strcmp(argv[i],"--measured-bass"))config.measured_bass_enabled=1;
        else if(!strcmp(argv[i],"--compress"))config.compressor_enabled=1;
        else if(!strcmp(argv[i],"--no-limiter"))config.limiter_enabled=0;
        else if(!strcmp(argv[i],"--no-harmonics"))config.harmonics_enabled=0;
        else if(!strcmp(argv[i],"--no-eq"))config.equalizer_enabled=0;
        else if(!strcmp(argv[i],"--gain-db") && i+1<argc) {
            char *end;config.output_gain_db=strtod(argv[++i],&end);if(!*argv[i] || *end) {fputs("Invalid gain\n",stderr);return 2;}
        } else {fputs("Unknown or incomplete option\n",stderr);return 2;}
    }
    if(cb_init(&state,&config)!=0) {fputs("Invalid DSP configuration\n",stderr);return 2;}
    in=fopen(argv[1],"rb");if(!in) {perror("Input");return 1;}
    if(!header(in,&w)) {fputs("Unsupported, invalid, or truncated WAV; expected 48 kHz stereo\n",stderr);goto done;}
    latency=cb_latency_frames(&state);frames=w.bytes/w.align;
    if((uint64_t)frames+latency>(UINT32_MAX-48u)/8u) {fputs("Output exceeds RIFF size limit\n",stderr);goto done;}
    {
        int fd=FILE_OPEN(argv[2],FILE_MODE,FILE_PERMISSIONS);
        if(fd<0) {perror("Cannot create output (existing files are protected)");goto done;}
        out=FILE_STREAM(fd,"wb");
        if(!out) {FILE_CLOSE(fd);remove(argv[2]);perror("Output stream");goto done;}
    }
    if(!output_header(out,frames+latency))goto done;
    remaining=w.bytes;
    while(remaining) {
        size_t capacity=1024u*w.align;
        size_t bytes=remaining>capacity?capacity:remaining, n, j;
        bytes-=bytes%w.align;
        if(!exact(in,raw,bytes)) {fputs("Truncated audio data\n",stderr);goto done;}
        n=bytes/w.align;
        for(j=0;j<n*2;++j)x[j]=sample(raw+j*(w.bits/8),&w);
        cb_process_f32(&state,x,y,n);if(!write_float(out,y,2*n))goto done;
        remaining-=(uint32_t)bytes;
    }
    if(latency) {memset(x,0,sizeof(x));cb_process_f32(&state,x,y,latency);if(!write_float(out,y,latency*2))goto done;}
    if(ferror(out))goto done;
    fprintf(stderr,"Processed %u frames; latency %u frames; peak %.6f; invalid samples %llu\n",
            frames,latency,state.meter.peak_output,(unsigned long long)state.meter.invalid_samples);
    ok=1;
done:
    if(in)fclose(in);
    if(out && fclose(out)!=0)ok=0;
    if(!ok && out)remove(argv[2]);
    return ok?0:1;
}
