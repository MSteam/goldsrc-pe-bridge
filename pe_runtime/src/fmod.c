/* Virtual "fmod.dll": the FMOD 3.x streaming subset used by Spirit of
 * Half-Life style MP3 players (Reprocessing: client message PlayMP3).
 *
 * The PE resolves stdcall exports such as "_FSOUND_Init@12" with
 * GetProcAddress. MP3 streams are decoded by the Steam Runtime's libmpg123,
 * converted by SDL_AudioStream and mixed into a separate SDL2 audio device.
 * Both libraries are loaded on demand; without them LoadLibrary fails and the
 * client runs without music, as it does on Windows without fmod.dll. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <unistd.h>
#include <dlfcn.h>
#include "fmod.h"

#define WINAPI __attribute__((stdcall,force_align_arg_pointer,noinline))
enum {FSOUND_FREE=-1,FSOUND_ALL=-3,FSOUND_LOOP_NORMAL=2,FSOUND_LOADMEMORY=0x8000,MAX_CHANNELS=8};
enum {MPG123_OK=0,MPG123_DONE=-12,MPG123_NEW_FORMAT=-11,MPG123_NEED_MORE=-10,
      MPG123_MONO=1,MPG123_STEREO=2,MPG123_ENC_SIGNED_16=0xd0};
enum {SDL_INIT_AUDIO=0x10,AUDIO_S16SYS=0x8010};

/* SDL_AudioSpec (SDL2, i386) */
struct audio_spec {int freq;uint16_t format;uint8_t channels,silence;uint16_t samples,padding;uint32_t size;
    void(*callback)(void*,uint8_t*,int);void *userdata;};
static struct {
    void *(*new_handle)(const char*,int*);int (*init)(void);int (*param)(void*,int,long,double);int (*format_none)(void*);
    int (*format)(void*,long,int,int);int (*open)(void*,const char*);int (*getformat)(void*,long*,int*,int*);
    int (*read)(void*,void*,size_t,size_t*);int64_t (*seek)(void*,int64_t,int);int64_t (*tell)(void*);
    int (*close)(void*);void (*destroy)(void*);const char *(*error)(void*);
} mpg;
static struct {
    uint32_t (*was_init)(uint32_t);int (*init_subsystem)(uint32_t);
    uint32_t (*open_device)(const char*,int,const struct audio_spec*,struct audio_spec*,int);
    void (*pause_device)(uint32_t,int);void (*lock)(uint32_t);void (*unlock)(uint32_t);void (*close_device)(uint32_t);
    void *(*new_stream)(uint16_t,uint8_t,int,uint16_t,uint8_t,int);int (*stream_put)(void*,const void*,int);
    int (*stream_get)(void*,void*,int);int (*stream_available)(void*);void (*stream_clear)(void*);void (*free_stream)(void*);
    const char *(*get_error)(void);
} sdl;
static bool resolve(void *library,void **slot,const char *name) {
    *slot=library?dlsym(library,name):NULL;
    if(!*slot)fprintf(stderr,"[bridge] fmod.dll: %s unavailable\n",name);
    return *slot!=NULL;
}
static bool libraries_ready(void) {
    static int state; /* 0 unknown, 1 ready, -1 missing */
    if(state)return state>0;
    void *m=dlopen("libmpg123.so.0",RTLD_NOW|RTLD_LOCAL);
    void *s=dlopen("libSDL2-2.0.so.0",RTLD_NOW|RTLD_GLOBAL); /* the engine's copy when loaded */
    if(!m)fprintf(stderr,"[bridge] fmod.dll: libmpg123.so.0 unavailable: %s\n",dlerror());
    bool ok=m&&s;
#define M(field,name) ok&=resolve(m,(void**)&mpg.field,name)
#define S(field,name) ok&=resolve(s,(void**)&sdl.field,name)
    if(ok) {
        M(new_handle,"mpg123_new");M(param,"mpg123_param");M(init,"mpg123_init");M(format_none,"mpg123_format_none");M(format,"mpg123_format");
        M(open,"mpg123_open_64");M(getformat,"mpg123_getformat");M(read,"mpg123_read");M(seek,"mpg123_seek_64");
        M(tell,"mpg123_tell_64");M(close,"mpg123_close");M(destroy,"mpg123_delete");M(error,"mpg123_strerror");
        S(was_init,"SDL_WasInit");S(init_subsystem,"SDL_InitSubSystem");S(open_device,"SDL_OpenAudioDevice");
        S(pause_device,"SDL_PauseAudioDevice");S(lock,"SDL_LockAudioDevice");S(unlock,"SDL_UnlockAudioDevice");
        S(close_device,"SDL_CloseAudioDevice");S(new_stream,"SDL_NewAudioStream");S(stream_put,"SDL_AudioStreamPut");
        S(stream_get,"SDL_AudioStreamGet");S(stream_available,"SDL_AudioStreamAvailable");S(stream_clear,"SDL_AudioStreamClear");
        S(free_stream,"SDL_FreeAudioStream");S(get_error,"SDL_GetError");
    }
#undef M
#undef S
    if(ok&&mpg.init()!=MPG123_OK){fprintf(stderr,"[bridge] fmod.dll: mpg123_init failed\n");ok=false;}
    state=ok?1:-1;return ok;
}

struct stream {void *decoder;void *converter;long rate;int channels;bool loop,ended;int64_t start_samples;};
struct channel {struct stream *stream;int volume;bool paused,active;};
static struct channel channels[MAX_CHANNELS];
static uint32_t device;
static int device_rate;
static void lock(void) {if(device)sdl.lock(device);}
static void unlock(void) {if(device)sdl.unlock(device);}

/* Produce up to `bytes` of device-format audio (S16 stereo) from a stream. */
static int pull(struct stream *s,int16_t *out,int bytes) {
    int filled=0;
    while(filled<bytes) {
        int got=sdl.stream_get(s->converter,(char*)out+filled,bytes-filled);
        if(got<0)return filled;
        filled+=got;
        if(filled>=bytes||s->ended)break;
        unsigned char decoded[8192];size_t done=0;
        int result=mpg.read(s->decoder,decoded,sizeof(decoded),&done);
        if(done)sdl.stream_put(s->converter,decoded,(int)done);
        if(result==MPG123_DONE) {
            if(s->loop&&mpg.seek(s->decoder,0,SEEK_SET)>=0)continue;
            s->ended=true;
        } else if(result!=MPG123_OK&&result!=MPG123_NEW_FORMAT&&result!=MPG123_NEED_MORE)s->ended=true;
    }
    return filled;
}
__attribute__((force_align_arg_pointer)) static void mix(void *unused,uint8_t *bytes,int length) {
    (void)unused;
    int samples=length/2;int32_t sum[4096];int16_t part[4096];
    memset(bytes,0,length);
    for(int offset=0;offset<samples;offset+=4096) {
        int count=samples-offset<4096?samples-offset:4096;
        memset(sum,0,count*sizeof(*sum));
        for(int c=0;c<MAX_CHANNELS;c++) {
            struct channel *ch=&channels[c];
            if(!ch->active||ch->paused||!ch->stream)continue;
            int got=pull(ch->stream,part,count*2)/2;
            for(int i=0;i<got;i++)sum[i]+=part[i]*ch->volume/255;
            if(got<count&&ch->stream->ended)ch->active=false;
        }
        int16_t *out=(int16_t*)bytes+offset;
        for(int i=0;i<count;i++)out[i]=sum[i]>32767?32767:sum[i]<-32768?-32768:sum[i];
    }
}

/* Mod paths are Windows-style and may differ in case from the files. */
static bool find_case_insensitive(const char *name,char *out,size_t size) {
    char path[4096];snprintf(path,sizeof(path),"%s",name);
    for(char *p=path;*p;p++)if(*p=='\\')*p='/';
    if(!access(path,R_OK)){snprintf(out,size,"%s",path);return true;}
    char built[4096]=".";if(path[0]=='/')built[0]=0;
    for(char *part=strtok(path,"/");part;part=strtok(NULL,"/")) {
        DIR *dir=opendir(built[0]?built:"/");if(!dir)return false;
        struct dirent *entry;bool found=false;
        while((entry=readdir(dir))) if(!strcasecmp(entry->d_name,part)) {
            size_t n=strlen(built);snprintf(built+n,sizeof(built)-n,"/%s",entry->d_name);found=true;break;
        }
        closedir(dir);if(!found)return false;
    }
    snprintf(out,size,"%s",built);return true;
}

static signed char WINAPI fs_SetOutput(int type) {(void)type;return 1;}
static signed char WINAPI fs_SetDriver(int driver) {(void)driver;return 1;}
static signed char WINAPI fs_SetBufferSize(int milliseconds) {(void)milliseconds;return 1;}
static float WINAPI fs_GetVersion(void) {return 3.75f;}
static signed char WINAPI fs_Init(int rate,int max_channels,unsigned flags) {
    (void)max_channels;(void)flags;
    if(device)return 1;
    if(!sdl.was_init(SDL_INIT_AUDIO)&&sdl.init_subsystem(SDL_INIT_AUDIO)) {
        fprintf(stderr,"[bridge] fmod.dll: SDL audio: %s\n",sdl.get_error());return 0;
    }
    struct audio_spec want={rate>0?rate:44100,AUDIO_S16SYS,2,0,2048,0,0,mix,NULL},have;
    device=sdl.open_device(NULL,0,&want,&have,0);
    if(!device){fprintf(stderr,"[bridge] fmod.dll: SDL_OpenAudioDevice: %s\n",sdl.get_error());return 0;}
    device_rate=want.freq;memset(channels,0,sizeof(channels));
    sdl.pause_device(device,0);
    fprintf(stderr,"[bridge] fmod.dll: native MP3 output %d Hz\n",device_rate);
    return 1;
}
static void WINAPI fs_Close(void) {
    if(!device)return;
    sdl.close_device(device);device=0;memset(channels,0,sizeof(channels));
}
static void free_stream(struct stream *s) {
    if(s->converter)sdl.free_stream(s->converter);
    if(s->decoder){mpg.close(s->decoder);mpg.destroy(s->decoder);}
    free(s);
}
static struct stream *WINAPI fs_Stream_Open(const char *name,unsigned mode,int offset,int length) {
    if(!device||!name)return NULL;
    if(mode&FSOUND_LOADMEMORY||offset||length) {
        fprintf(stderr,"[bridge] fmod.dll: memory/offset streams are not provided (mode=%#x)\n",mode);return NULL;
    }
    char path[4096];
    if(!find_case_insensitive(name,path,sizeof(path))){fprintf(stderr,"[bridge] fmod.dll: %s not found\n",name);return NULL;}
    struct stream *s=calloc(1,sizeof(*s));if(!s)return NULL;
    int error=0;s->decoder=mpg.new_handle(NULL,&error);
    if(!s->decoder){free(s);return NULL;}
    mpg.param(s->decoder,2 /* MPG123_ADD_FLAGS */,0x20 /* MPG123_QUIET: no tag diagnostics on stderr */,0);
    static const long rates[]={8000,11025,12000,16000,22050,24000,32000,44100,48000};
    mpg.format_none(s->decoder);
    for(size_t i=0;i<sizeof(rates)/sizeof(*rates);i++)mpg.format(s->decoder,rates[i],MPG123_MONO|MPG123_STEREO,MPG123_ENC_SIGNED_16);
    int encoding=0;
    if(mpg.open(s->decoder,path)!=MPG123_OK||mpg.getformat(s->decoder,&s->rate,&s->channels,&encoding)!=MPG123_OK) {
        fprintf(stderr,"[bridge] fmod.dll: cannot decode %s: %s\n",path,mpg.error(s->decoder));free_stream(s);return NULL;
    }
    s->converter=sdl.new_stream(AUDIO_S16SYS,(uint8_t)s->channels,(int)s->rate,AUDIO_S16SYS,2,device_rate);
    if(!s->converter){fprintf(stderr,"[bridge] fmod.dll: SDL_NewAudioStream: %s\n",sdl.get_error());free_stream(s);return NULL;}
    s->loop=(mode&FSOUND_LOOP_NORMAL)!=0;
    fprintf(stderr,"[bridge] fmod.dll: stream %s (%ld Hz, %d ch%s)\n",path,s->rate,s->channels,s->loop?", loop":"");
    return s;
}
/* FMOD 3.6x name; memory length only applies to FSOUND_LOADMEMORY. */
static struct stream *WINAPI fs_Stream_OpenFile(const char *name,unsigned mode,int memory_length) {
    return fs_Stream_Open(name,mode,0,(mode&FSOUND_LOADMEMORY)?memory_length:0);
}
static int WINAPI fs_Stream_Play(int channel,struct stream *s) {
    if(!device||!s)return -1;
    if(channel==FSOUND_FREE) {
        for(channel=0;channel<MAX_CHANNELS&&channels[channel].active;channel++){}
    }
    if(channel<0||channel>=MAX_CHANNELS)return -1;
    lock();
    channels[channel]=(struct channel){s,255,false,true};s->ended=false;
    unlock();
    return channel;
}
static signed char WINAPI fs_Stream_Close(struct stream *s) {
    if(!s)return 0;
    lock();for(int c=0;c<MAX_CHANNELS;c++)if(channels[c].stream==s)channels[c]=(struct channel){0};unlock();
    free_stream(s);return 1;
}
static int WINAPI fs_Stream_GetTime(struct stream *s) {
    if(!s)return 0;
    lock();
    /* Decoder position minus what is still buffered for conversion. */
    int64_t decoded=mpg.tell(s->decoder);int buffered=sdl.stream_available(s->converter)/4;
    unlock();
    int64_t played=decoded-(int64_t)buffered*s->rate/device_rate;
    return played>0?(int)(played*1000/s->rate):0;
}
static signed char WINAPI fs_Stream_SetTime(struct stream *s,int milliseconds) {
    if(!s||milliseconds<0)return 0;
    lock();
    int64_t result=mpg.seek(s->decoder,(int64_t)milliseconds*s->rate/1000,SEEK_SET);
    sdl.stream_clear(s->converter);s->ended=false;
    unlock();
    return result>=0;
}
static signed char apply(int channel,void(*change)(struct channel*,int),int value) {
    if(channel!=FSOUND_ALL&&(channel<0||channel>=MAX_CHANNELS))return 0;
    lock();
    for(int c=0;c<MAX_CHANNELS;c++)if(channel==FSOUND_ALL||c==channel)change(&channels[c],value);
    unlock();return 1;
}
static void set_volume(struct channel *c,int value) {c->volume=value<0?0:value>255?255:value;}
static void set_paused(struct channel *c,int value) {c->paused=value!=0;}
static signed char WINAPI fs_SetVolume(int channel,int volume) {return apply(channel,set_volume,volume);}
static signed char WINAPI fs_SetPaused(int channel,signed char paused) {return apply(channel,set_paused,paused);}

static const struct {const char *name;void *function;} exports[]={
#define EXPORT(name,bytes) {"_FSOUND_"#name"@"#bytes,(void*)fs_##name}
    EXPORT(GetVersion,0),EXPORT(SetOutput,4),EXPORT(SetDriver,4),EXPORT(SetBufferSize,4),EXPORT(Init,12),
    EXPORT(Close,0),EXPORT(SetVolume,8),EXPORT(SetPaused,8),EXPORT(Stream_Open,16),EXPORT(Stream_OpenFile,12),
    EXPORT(Stream_Play,8),EXPORT(Stream_Close,4),EXPORT(Stream_GetTime,4),EXPORT(Stream_SetTime,8)
#undef EXPORT
};
static char module_tag;
void *cb_fmod_module(void) {return libraries_ready()?&module_tag:NULL;}
int cb_is_fmod_module(void *handle) {return handle&&handle==&module_tag;}
void *cb_fmod_symbol(const char *name) {
    for(size_t i=0;i<sizeof(exports)/sizeof(*exports);i++)if(!strcmp(exports[i].name,name))return exports[i].function;
    return NULL;
}
void cb_fmod_unload(void) {
    fs_Close();
}
