#include <platform/native_music_selection.h>
#include <NativeSession.h>
#include <platform/native_assets.h>
#include <platform/native_custom_tracks.h>
#include <SDL3/SDL.h>
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define NMS_MAX_PACKS 64
#define NMS_MAX_SONGS 256
#define NMS_FILE "ctr_music.cfg"

/* The music-selection layer only resolves paths. It never opens the audio
 * device, mutates live CSEQ/OGG voices or decodes samples in menu callbacks.
 * Existing MUSIC_CUSTOM and mods/music compatibility is retained by source
 * EXISTING (the default for old installations). */
struct NMS_Pack
{
    char slug[64];
    char title[96];
    char dir[512];
    int source;
    int samples;
    int target;
    int songCount;
};
struct NMS_Song
{
    char label[160];
    char path[1024];
    /* Optional per-song ownership. Normal packs inherit directory/samples
     * from NMS_Pack; virtual custom-track songs keep their own bank here. */
    char directory[1024];
    int customSamples;
    int packIndex;
    int source;
    int target;
};
struct NMS_Level
{
    int choice;             /* -1 DEFAULT, 0 EXISTING, 1 ORIGINAL, 2/3 PACK */
    char pack[64];
    char path[1024];
};

static struct NMS_Pack nmsPacks[NMS_MAX_PACKS];
static struct NMS_Song nmsSongs[NMS_MAX_SONGS];
static struct NMS_Level nmsLevels[NATIVE_MUSIC_MAX_LEVELS];

/* An OGG pack can assign the same named file to multiple levels without
 * renaming or duplicating the song. Legacy one-song CSEQ manifests continue
 * to work and retain their existing target=level_XX behavior. */
#define NMS_MAX_MAPPINGS 512
struct NMS_Mapping
{
    int packIndex;
    int levelID;                /* >=0 for a vanilla/special CTR music cue */
    char trackID[80];           /* nonempty for a native custom track */
    char fileName[128];
};
struct NMS_CustomChoice
{
    char trackID[80];
    struct NMS_Level selected;
};
static struct NMS_CustomChoice nmsCustomChoices[64];
static int nmsCustomChoiceCount;
static struct NMS_Mapping nmsMappings[NMS_MAX_MAPPINGS];
static int nmsMappingCount;
static int nmsPackCount, nmsSongCount, nmsReady, nmsDefaultSource;
static char nmsDefaultOgg[64], nmsDefaultCseq[64];

static char *NMS_Trim(char *s)
{
    char *e;
    if ((unsigned char)s[0]==0xef && (unsigned char)s[1]==0xbb &&
        (unsigned char)s[2]==0xbf) s+=3;
    while (*s && isspace((unsigned char)*s)) ++s;
    e=s+strlen(s);
    while (e>s && isspace((unsigned char)e[-1])) --e;
    *e=0;
    return s;
}
static int NMS_SafeLeaf(const char *s)
{
    const unsigned char *p=(const unsigned char *)s;
    size_t n=s ? strlen(s) : 0;
    if (!n || n>=128 || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for (; *p; ++p)
        if (*p=='/' || *p=='\\' || *p=='|' || *p==':' ||
            *p=='\n' || *p=='\r' || *p<32)
            return 0;
    return 1;
}
static int NMS_ExistsFile(const char *relative)
{
    char full[1536];
    struct stat st;
    return NativeAssets_BuildPath(relative,full,sizeof(full)) &&
           stat(full,&st)==0 && S_ISREG(st.st_mode);
}
static int NMS_IsDir(const char *full)
{
    struct stat st;
    return stat(full,&st)==0 && S_ISDIR(st.st_mode);
}
static int NMS_ParseBool(const char *s, int fallback)
{
    if (!SDL_strcasecmp(s,"true") || !SDL_strcasecmp(s,"yes") ||
        !strcmp(s,"1")) return 1;
    if (!SDL_strcasecmp(s,"false") || !SDL_strcasecmp(s,"no") ||
        !strcmp(s,"0")) return 0;
    return fallback;
}
static int NMS_Kind(const char *s)
{
    if (!SDL_strcasecmp(s,"ogg")) return NATIVE_MUSIC_SOURCE_OGG;
    if (!SDL_strcasecmp(s,"cseq")) return NATIVE_MUSIC_SOURCE_CSEQ;
    return -1;
}
static int NMS_Target(const char *text)
{
    char *end;
    long n;
    /* Music-song overlays, separate from SFX mask pickup IDs 0x53/0x54.
     * The CSEQ song slots inside each retail level pack are 1 and 2. */
    if (!SDL_strcasecmp(text,"mask_aku") ||
        !SDL_strcasecmp(text,"aku_aku") ||
        !SDL_strcasecmp(text,"aku_aku_theme"))
        return NATIVE_MUSIC_MASK_AKU_SLOT;
    if (!SDL_strcasecmp(text,"mask_uka") ||
        !SDL_strcasecmp(text,"uka_uka") ||
        !SDL_strcasecmp(text,"uka_uka_theme"))
        return NATIVE_MUSIC_MASK_UKA_SLOT;
    /* Human-readable aliases keep soundtracks editable without numbers. */
    if (!SDL_strcasecmp(text,"boss") || !SDL_strcasecmp(text,"boss_races") ||
        !SDL_strcasecmp(text,"boss_music")) return NATIVE_MUSIC_BOSS_SLOT;
    if (!SDL_strcasecmp(text,"hangout") || !SDL_strcasecmp(text,"hangout_hub"))
        return NATIVE_MUSIC_HANGOUT_SLOT;
    if (!SDL_strcasecmp(text,"main_menu") || !SDL_strcasecmp(text,"menu"))
        return 39;
    if (!SDL_strcasecmp(text,"credits") || !SDL_strcasecmp(text,"end_credits"))
        return 45; /* CREDITS_CRASH: actual HOWL credits song, not LEV 44 */
    if (!SDL_strcasecmp(text,"intro") || !SDL_strcasecmp(text,"race_intro"))
        return 30;
    if (!SDL_strcasecmp(text,"crate_intro")) return 41;
    if (!SDL_strcasecmp(text,"oxide_ending")) return 42;
    if (!SDL_strcasecmp(text,"oxide_true_ending")) return 43;
    if (!SDL_strncasecmp(text,"level_",6)) text+=6;
    n=strtol(text,&end,10);
    if (end==text || *end!=0 || n<0 || n>=NATIVE_MUSIC_MAX_LEVELS)
        return -1;
    return (int)n;
}
static void NMS_ClearLevel(struct NMS_Level *v)
{
    v->choice=NATIVE_MUSIC_SOURCE_DEFAULT;
    v->pack[0]=v->path[0]=0;
}
static struct NMS_Level *NMS_TrackChoice(const char *id,int create)
{
    if(!id || !NMS_SafeLeaf(id)) return NULL;
    for(int i=0;i<nmsCustomChoiceCount;i++)
        if(!SDL_strcasecmp(id,nmsCustomChoices[i].trackID))
            return &nmsCustomChoices[i].selected;
    if(!create || nmsCustomChoiceCount >= (int)(
       sizeof(nmsCustomChoices)/sizeof(nmsCustomChoices[0])))
        return NULL;
    struct NMS_CustomChoice *c=&nmsCustomChoices[nmsCustomChoiceCount++];
    memset(c,0,sizeof(*c));
    snprintf(c->trackID,sizeof(c->trackID),"%s",id);
    NMS_ClearLevel(&c->selected);
    return &c->selected;
}
static const char *NMS_SlotTrackID(int slot)
{
    if(slot<NATIVE_MUSIC_CUSTOM_BASE ||
       slot>=NATIVE_MUSIC_CUSTOM_BASE+NATIVE_CUSTOM_TRACK_LIMIT) return NULL;
    return NativeCustomTracks_IdAt(slot-NATIVE_MUSIC_CUSTOM_BASE);
}
static struct NMS_Level *NMS_Choice(int slot,int create)
{
    if(slot>=NATIVE_MUSIC_CUSTOM_BASE)
        return NMS_TrackChoice(NMS_SlotTrackID(slot),create);
    return slot>=0 && slot<NATIVE_MUSIC_CUSTOM_BASE ? &nmsLevels[slot] : NULL;
}
static int NMS_ParseChoiceValue(char *value,struct NMS_Level *v)
{
    char *p1,*p2;
    int mode;
    if(!v || !value) return 0;
    mode=atoi(value);
    if(mode<0 || mode>NATIVE_MUSIC_SOURCE_CSEQ) return 0;
    p1=strchr(value,'|');if(!p1) return 0;
    *p1++=0;
    p2=strchr(p1,'|');if(!p2) return 0;
    *p2++=0;
    if((*p1 && !NMS_SafeLeaf(p1)) || strstr(p2,"..") ||
       strchr(p2,'|') || strchr(p2,'\\') ||
       strchr(p2,'\r') || strchr(p2,'\n')) return 0;
    NMS_ClearLevel(v);
    v->choice=mode;
    snprintf(v->pack,sizeof(v->pack),"%s",p1);
    snprintf(v->path,sizeof(v->path),"%s",p2);
    return 1;
}
static void NMS_Save(void)
{
    FILE *f=fopen(NMS_FILE,"wb");
    if (!f) { Platform_LogWarn("[Music] Failed to save %s\n",NMS_FILE); return; }
    fprintf(f,"version=1\n");
    fprintf(f,"default_source=%d\n",nmsDefaultSource);
    fprintf(f,"default_ogg=%s\n",nmsDefaultOgg);
    fprintf(f,"default_cseq=%s\n",nmsDefaultCseq);
    for(int i=0;i<NATIVE_MUSIC_CUSTOM_BASE;i++)
    {
        const struct NMS_Level *v=&nmsLevels[i];
        if(v->choice!=NATIVE_MUSIC_SOURCE_DEFAULT)
            fprintf(f,"level_%02d=%d|%s|%s\n",i,v->choice,v->pack,v->path);
    }
    /* Keep saved custom choices by stable track ID even if a track is
     * temporarily uninstalled. Its entry stays dormant, never misassigned. */
    for(int i=0;i<nmsCustomChoiceCount;i++)
    {
        const struct NMS_CustomChoice *c=&nmsCustomChoices[i];
        const struct NMS_Level *v=&c->selected;
        if(v->choice!=NATIVE_MUSIC_SOURCE_DEFAULT)
            fprintf(f,"custom_%s=%d|%s|%s\n",
                    c->trackID,v->choice,v->pack,v->path);
    }
    fclose(f);
}
static void NMS_Load(void)
{
    FILE *f;
    char line[1600];
    nmsDefaultSource=NATIVE_MUSIC_SOURCE_EXISTING;
    nmsDefaultOgg[0]=nmsDefaultCseq[0]=0;
    nmsCustomChoiceCount=0;
    for(int i=0;i<NATIVE_MUSIC_MAX_LEVELS;i++)
        NMS_ClearLevel(&nmsLevels[i]);
    f=fopen(NMS_FILE,"rb");
    if(!f)return;
    while(fgets(line,sizeof(line),f))
    {
        char *eq,*key,*val;
        int id;
        key=NMS_Trim(line);
        eq=strchr(key,'=');
        if(!eq)continue;
        *eq++=0;key=NMS_Trim(key);val=NMS_Trim(eq);
        if(!strcmp(key,"default_source"))
        {
            int n=atoi(val);
            if(n>=0 && n<=NATIVE_MUSIC_SOURCE_CSEQ)
                nmsDefaultSource=n;
        }
        else if(!strcmp(key,"default_ogg") && NMS_SafeLeaf(val))
            snprintf(nmsDefaultOgg,sizeof(nmsDefaultOgg),"%s",val);
        else if(!strcmp(key,"default_cseq") && NMS_SafeLeaf(val))
            snprintf(nmsDefaultCseq,sizeof(nmsDefaultCseq),"%s",val);
        else if(!strncmp(key,"custom_",7))
            NMS_ParseChoiceValue(val,NMS_TrackChoice(key+7,1));
        else if(!strncmp(key,"level_",6))
        {
            id=NMS_Target(key);
            if(id>=0 && id<NATIVE_MUSIC_CUSTOM_BASE)
                NMS_ParseChoiceValue(val,&nmsLevels[id]);
        }
    }
    fclose(f);
}

static void NMS_AddSong(struct NMS_Pack *pack, const char *rel,
                        const char *name)
{
    const char *dot=strrchr(name,'.');
    int kind;
    char label[96];
    struct NMS_Song *song;

    if (!dot) return;
    kind=NMS_Kind(dot+1);
    if (kind<0 || (pack->source>0 && pack->source!=kind) ||
        nmsSongCount>=NMS_MAX_SONGS || !NMS_SafeLeaf(name))
        return;
    if (!NMS_ExistsFile(rel)) return;

    song=&nmsSongs[nmsSongCount];
    memset(song,0,sizeof(*song));
    song->source=kind;
    song->packIndex=(int)(pack-nmsPacks);
    snprintf(song->path,sizeof(song->path),"%s",rel);
    snprintf(label,sizeof(label),"%.*s",(int)(dot-name),name);
    /* OGG song names should be immediately recognizable in Music settings;
     * CSEQ entries are often only track.cseq, so retain their pack label. */
    if (kind==NATIVE_MUSIC_SOURCE_OGG)
        snprintf(song->label,sizeof(song->label),"%s",label);
    else
        snprintf(song->label,sizeof(song->label),"%s / %s",pack->title,label);
    song->target=-1;
    if (!SDL_strncasecmp(label,"level_",6))
        song->target=NMS_Target(label);
    ++pack->songCount;
    ++nmsSongCount;
}
static void NMS_ScanSongFolder(struct NMS_Pack *pack, const char *sub)
{
    char rel[1024],full[1536];
    DIR *dir;
    struct dirent *entry;
    snprintf(rel,sizeof(rel),"%s%s%s",
             pack->dir,sub[0]?"/":"",sub);
    if (!NativeAssets_BuildPath(rel,full,sizeof(full)) ||
        !NMS_IsDir(full) || !(dir=opendir(full))) return;
    while ((entry=readdir(dir))!=NULL)
    {
        char path[1024];
        if (!NMS_SafeLeaf(entry->d_name) ||
            snprintf(path,sizeof(path),"%s/%s",rel,entry->d_name)
            >=(int)sizeof(path)) continue;
        NMS_AddSong(pack,path,entry->d_name);
    }
    closedir(dir);
}
static void NMS_ScanPack(const char *parent, const char *slug)
{
    char rel[1024],full[1536],ini[1536],line[512];
    char section[48]="",specified[128]="";
    char declaredCustom[80]="";
    struct NMS_Pack *pack;
    int enabled=1;
    int mappingStart=nmsMappingCount;
    FILE *f;

    if (!NMS_SafeLeaf(slug) || nmsPackCount>=NMS_MAX_PACKS) return;
    snprintf(rel,sizeof(rel),"%s/%s",parent,slug);
    snprintf(ini,sizeof(ini),"%s/music.ini",rel);
    if (!NativeAssets_BuildPath(ini,full,sizeof(full)) ||
        !(f=fopen(full,"rb"))) return;

    pack=&nmsPacks[nmsPackCount];
    memset(pack,0,sizeof(*pack));
    pack->source=-1;
    pack->target=-1;
    snprintf(pack->slug,sizeof(pack->slug),"%s",slug);
    snprintf(pack->dir,sizeof(pack->dir),"%s",rel);
    snprintf(pack->title,sizeof(pack->title),"%s",slug);

    while (fgets(line,sizeof(line),f))
    {
        char *s=NMS_Trim(line),*eq;
        if (!*s || *s=='#' || *s==';') continue;
        if (*s=='[')
        {
            char *e=strchr(s,']');
            if (!e) continue;
            *e=0;
            snprintf(section,sizeof(section),"%s",NMS_Trim(s+1));
            continue;
        }
        if (SDL_strcasecmp(section,"music") &&
            SDL_strcasecmp(section,"levels") &&
            SDL_strcasecmp(section,"custom_tracks")) continue;
        eq=strchr(s,'=');
        if (!eq) continue;
        *eq++=0;s=NMS_Trim(s);eq=NMS_Trim(eq);

        if(!SDL_strcasecmp(section,"levels") ||
           !SDL_strcasecmp(section,"custom_tracks"))
        {
            int custom=!SDL_strcasecmp(section,"custom_tracks");
            const char *track=s;
            const char *dot=strrchr(eq,'.');
            int id=-1;
            if(!custom && !SDL_strncasecmp(s,"custom:",7))
            { custom=1; track=s+7; }
            if(!custom) id=NMS_Target(s);
            if((custom ? NMS_SafeLeaf(track) : (id>=0)) &&
               NMS_SafeLeaf(eq) && dot && NMS_Kind(dot+1)>=0 &&
               nmsMappingCount<NMS_MAX_MAPPINGS)
            {
                struct NMS_Mapping *m=&nmsMappings[nmsMappingCount++];
                memset(m,0,sizeof(*m));
                m->packIndex=nmsPackCount;
                m->levelID=custom ? -1 : id;
                if(custom)snprintf(m->trackID,sizeof(m->trackID),"%s",track);
                snprintf(m->fileName,sizeof(m->fileName),"%s",eq);
            }
            continue;
        }

        if (!SDL_strcasecmp(s,"name") ||
            !SDL_strcasecmp(s,"display_name"))
            snprintf(pack->title,sizeof(pack->title),"%s",eq);
        else if (!SDL_strcasecmp(s,"source") ||
                 !SDL_strcasecmp(s,"format"))
            pack->source=NMS_Kind(eq);
        else if (!SDL_strcasecmp(s,"file"))
            snprintf(specified,sizeof(specified),"%s",eq);
        else if (!SDL_strcasecmp(s,"target"))
        {
            if(!SDL_strncasecmp(eq,"custom:",7) &&
               NMS_SafeLeaf(eq+7))
                snprintf(declaredCustom,sizeof(declaredCustom),"%s",eq+7);
            else
                pack->target=NMS_Target(eq);
        }
        else if (!SDL_strcasecmp(s,"custom_samples"))
        {
            int num=atoi(eq);
            if (num>=0 && num<=128) pack->samples=num;
        }
        else if (!SDL_strcasecmp(s,"enabled"))
            enabled=NMS_ParseBool(eq,enabled);
    }
    fclose(f);
    if (!enabled)
    {
        nmsMappingCount=mappingStart;
        return;
    }

    ++nmsPackCount;
    NMS_ScanSongFolder(pack,"");
    NMS_ScanSongFolder(pack,"music");
    NMS_ScanSongFolder(pack,"tracks");
    NMS_ScanSongFolder(pack,"songs");

    /* A legacy one-song pack has a target=level_XX. An explicit file in
     * the manifest is authoritative rather than dirent ordering. */
    if(declaredCustom[0] && NMS_SafeLeaf(specified) &&
       nmsMappingCount<NMS_MAX_MAPPINGS)
    {
        struct NMS_Mapping *m=&nmsMappings[nmsMappingCount++];
        memset(m,0,sizeof(*m));
        m->packIndex=(int)(pack-nmsPacks);
        m->levelID=-1;
        snprintf(m->trackID,sizeof(m->trackID),"%s",declaredCustom);
        snprintf(m->fileName,sizeof(m->fileName),"%s",specified);
    }

    if (pack->target>=0)
    {
        for (int j=0;j<nmsSongCount;j++)
        {
            struct NMS_Song *song=&nmsSongs[j];
            if (song->packIndex!=(int)(pack-nmsPacks)) continue;
            if ((specified[0] &&
                 !SDL_strcasecmp(strrchr(song->path,'/')+1,specified)) ||
                pack->songCount==1)
                song->target=pack->target;
        }
    }
    if (!pack->songCount)
    {
        /* Do not expose empty/broken packages as selectable. */
        --nmsPackCount;
        nmsMappingCount=mappingStart;
    }
}
static void NMS_ScanRoot(const char *rel)
{
    char full[1536];
    DIR *dir;
    struct dirent *entry;
    if (!NativeAssets_BuildPath(rel,full,sizeof(full)) ||
        !NMS_IsDir(full) || !(dir=opendir(full))) return;
    while ((entry=readdir(dir))!=NULL)
    {
        char path[1536];
        if (!NMS_SafeLeaf(entry->d_name) ||
            snprintf(path,sizeof(path),"%s/%s",full,entry->d_name)
            >=(int)sizeof(path) || !NMS_IsDir(path))
            continue;
        NMS_ScanPack(rel,entry->d_name);
    }
    closedir(dir);
}

/* Expose the soundtrack already shipped inside the LRG2Pancho track mods as
 * one selectable browser pack. Nothing is copied: each song keeps the track
 * folder and its own custom-sample bank. The track itself still owns its
 * default music through NativeCustomTracks, regardless of pack selection. */
static void NMS_AddLRG2PanchoTrackPack(void)
{
    struct NMS_Pack *pack;
    int packIndex;
    int added=0;

    NativeCustomTracks_EnsureScanned();
    if(nmsPackCount>=NMS_MAX_PACKS) return;

    packIndex=nmsPackCount;
    pack=&nmsPacks[packIndex];
    memset(pack,0,sizeof(*pack));
    pack->source=NATIVE_MUSIC_SOURCE_CSEQ;
    pack->target=-1;
    snprintf(pack->slug,sizeof(pack->slug),"lrg2pancho_tracks");
    snprintf(pack->title,sizeof(pack->title),"LRG2Pancho - Soundtrack");

    for(int i=0;i<NativeCustomTracks_Count() && nmsSongCount<NMS_MAX_SONGS;i++)
    {
        const char *id=NativeCustomTracks_IdAt(i);
        const char *name=NativeCustomTracks_Name(i);
        const char *path=NativeCustomTracks_MusicPathAt(i);
        const char *dir=NativeCustomTracks_DirectoryAt(i);
        struct NMS_Song *song;

        if(!id || SDL_strncasecmp(id,"lrg2_",5) ||
           NativeCustomTracks_MusicKindAt(i)!=NATIVE_CUSTOM_TRACK_MUSIC_CSEQ ||
           !name || !path || !dir || !NMS_ExistsFile(path))
            continue;

        song=&nmsSongs[nmsSongCount++];
        memset(song,0,sizeof(*song));
        song->source=NATIVE_MUSIC_SOURCE_CSEQ;
        song->packIndex=packIndex;
        song->target=-1;
        song->customSamples=NativeCustomTracks_MusicSamplesAt(i);
        snprintf(song->label,sizeof(song->label),"%s",name);
        snprintf(song->path,sizeof(song->path),"%s",path);
        snprintf(song->directory,sizeof(song->directory),"%s",dir);
        ++pack->songCount;
        ++added;
    }

    if(added)
    {
        ++nmsPackCount;
        Platform_Log("[Music] virtual LRG2Pancho soundtrack: %d track songs\n",added);
    }
    else memset(pack,0,sizeof(*pack));
}

void NativeMusicSelection_Reload(void)
{
    if (NativeSession_NoModsActive())
    {
        nmsPackCount=nmsSongCount=nmsMappingCount=0;
        nmsDefaultSource=NATIVE_MUSIC_SOURCE_RETAIL;
        nmsReady=1;
        return;
    }
    nmsPackCount=nmsSongCount=nmsMappingCount=0;
    memset(nmsPacks,0,sizeof(nmsPacks));
    memset(nmsSongs,0,sizeof(nmsSongs));
    NMS_ScanRoot("mods/music");
    NMS_ScanRoot("mods");
    NMS_AddLRG2PanchoTrackPack();
    /* Support existing Toolkit imports without moving their files.
     * Older CSEQ bundles live in MUSIC_CUSTOM/<pack>/music.ini. */
    NMS_ScanRoot("MUSIC_CUSTOM");
    /* Legacy MUSIC_CUSTOM/level_XX.ogg files are a virtual OGG pack. */
    if (nmsPackCount<NMS_MAX_PACKS)
    {
        struct NMS_Pack *legacy=&nmsPacks[nmsPackCount];
        memset(legacy,0,sizeof(*legacy));
        legacy->source=NATIVE_MUSIC_SOURCE_OGG;
        legacy->target=-1;
        snprintf(legacy->slug,sizeof(legacy->slug),"imported_ogg");
        snprintf(legacy->title,sizeof(legacy->title),"IMPORTED OGG");
        snprintf(legacy->dir,sizeof(legacy->dir),"MUSIC_CUSTOM");
        NMS_ScanSongFolder(legacy,"");
        if(legacy->songCount) ++nmsPackCount;
    }
    if (!nmsReady)
        NMS_Load();
    nmsReady=1;
    Platform_Log("[Music] %d active packs, %d songs indexed\n",
                 nmsPackCount,nmsSongCount);
}
void NativeMusicSelection_EnsureReady(void)
{
    if (!nmsReady) NativeMusicSelection_Reload();
}
const char *NativeMusicSelection_SourceName(int src)
{
    switch(src)
    {
    case NATIVE_MUSIC_SOURCE_EXISTING: return "CURRENT IMPORTS";
    case NATIVE_MUSIC_SOURCE_RETAIL: return "ORIGINAL CTR";
    case NATIVE_MUSIC_SOURCE_OGG: return "OGG MUSIC PACK";
    case NATIVE_MUSIC_SOURCE_CSEQ: return "CSEQ MUSIC PACK";
    default:return "DEFAULT";
    }
}
int NativeMusicSelection_GetDefaultSource(void)
{
    NativeMusicSelection_EnsureReady();
    return nmsDefaultSource;
}
void NativeMusicSelection_SetDefaultSource(int source)
{
    NativeMusicSelection_EnsureReady();
    if (source<0 || source>3) return;
    nmsDefaultSource=source;
    NMS_Save();
}
/* A conditional song linked ONLY to custom tracks must not leak
 * into the vanilla song list, nor show up if its track is absent. */
static int NMS_SongLinkMatch(const struct NMS_Song *song,
                             const struct NMS_Mapping *m)
{
    const char *leaf=strrchr(song->path,'/');
    leaf=leaf ? leaf+1 : song->path;
    return m->packIndex==song->packIndex &&
           !SDL_strcasecmp(leaf,m->fileName);
}
static int NMS_SongVisibleAt(const struct NMS_Song *song,int slot)
{
    int custom=0,vanilla=0,match=0;
    const char *current=NMS_SlotTrackID(slot);
    /* CSEQ mask music uses a dedicated per-level song pool and is not
     * interchangeable with a standalone CSEQ file yet. Until that native
     * hook is implemented, only advertise mask songs that really play:
     * independent OGG overrides. */
    if((slot==NATIVE_MUSIC_MASK_AKU_SLOT ||
        slot==NATIVE_MUSIC_MASK_UKA_SLOT) &&
       song->source!=NATIVE_MUSIC_SOURCE_OGG) return 0;
    if(slot>=NATIVE_MUSIC_CUSTOM_BASE && !current) return 0;
    for(int i=0;i<nmsMappingCount;i++)
    {
        const struct NMS_Mapping *m=&nmsMappings[i];
        if(!NMS_SongLinkMatch(song,m)) continue;
        if(m->trackID[0])
        {
            custom=1;
            if(current && !SDL_strcasecmp(current,m->trackID))
                match=1;
        }
        else vanilla=1;
    }
    if(slot>=NATIVE_MUSIC_CUSTOM_BASE)
        return !custom || match;
    return !custom || vanilla;
}
static int NMS_SongVisibleAnywhere(const struct NMS_Song *song)
{
    if(NMS_SongVisibleAt(song,0)) return 1;
    for(int i=0;i<NativeCustomTracks_Count();i++)
        if(NMS_SongVisibleAt(song,NATIVE_MUSIC_CUSTOM_BASE+i))
            return 1;
    return 0;
}
static int NMS_ActualPack(int kind,int nth)
{
    for(int i=0;i<nmsPackCount;i++)
    {
        int packKind=nmsPacks[i].source;
        /* Pack format comes from music.ini, or its first valid song. */
        if (packKind<0)
        {
            for(int j=0;j<nmsSongCount;j++)
                if(nmsSongs[j].packIndex==i)
                { packKind=nmsSongs[j].source; break; }
        }
        if(packKind!=kind) continue;
        int visible=0;
        for(int j=0;j<nmsSongCount;j++)
            if(nmsSongs[j].packIndex==i &&
               NMS_SongVisibleAnywhere(&nmsSongs[j]))
            { visible=1; break; }
        if(visible && nth--==0) return i;
    }
    return -1;
}
int NativeMusicSelection_GetPackCount(int source)
{
    int count=0;
    NativeMusicSelection_EnsureReady();
    for(int i=0;i<nmsPackCount;i++)
        if(NMS_ActualPack(source,count)>=0) ++count;
        else break;
    return count;
}
const char *NativeMusicSelection_GetPackName(int source,int index)
{
    int n;
    NativeMusicSelection_EnsureReady();
    n=NMS_ActualPack(source,index);
    return n>=0 ? nmsPacks[n].title : "NO PACKS";
}
static const char *NMS_DefaultSlug(int source)
{
    return source==NATIVE_MUSIC_SOURCE_OGG ?
           nmsDefaultOgg : nmsDefaultCseq;
}
int NativeMusicSelection_GetDefaultPackIndex(int source)
{
    const char *slug=NMS_DefaultSlug(source);
    NativeMusicSelection_EnsureReady();
    for(int n=0;;n++)
    {
        int i=NMS_ActualPack(source,n);
        if(i<0) break;
        if(!strcmp(slug,nmsPacks[i].slug)) return n;
    }
    return 0;
}
void NativeMusicSelection_CycleDefaultPack(int source,int direction)
{
    int count=NativeMusicSelection_GetPackCount(source);
    int n;
    char *dst;
    if(count<=0) return;
    n=NativeMusicSelection_GetDefaultPackIndex(source);
    n=(n+(direction<0?-1:1)+count)%count;
    n=NMS_ActualPack(source,n);
    if(n<0) return;
    dst=source==NATIVE_MUSIC_SOURCE_OGG ?
        nmsDefaultOgg : nmsDefaultCseq;
    snprintf(dst,64,"%s",nmsPacks[n].slug);
    NMS_Save();
}
int NativeMusicSelection_GetSongCount(void)
{
    int count=0;
    NativeMusicSelection_EnsureReady();
    for(int i=0;i<nmsSongCount;i++)
        if(NMS_SongVisibleAnywhere(&nmsSongs[i]))count++;
    return count;
}
const char *NativeMusicSelection_GetSongName(int index)
{
    NativeMusicSelection_EnsureReady();
    return index>=0 && index<nmsSongCount ? nmsSongs[index].label : "";
}
int NativeMusicSelection_GetSongSource(int index)
{
    NativeMusicSelection_EnsureReady();
    return index>=0 && index<nmsSongCount ? nmsSongs[index].source : -1;
}
int NativeMusicSelection_GetAvailableChoiceCount(int slot)
{
    int count=3;
    NativeMusicSelection_EnsureReady();
    for(int i=0;i<nmsSongCount;i++)
        if(NMS_SongVisibleAt(&nmsSongs[i],slot)) count++;
    return count;
}
int NativeMusicSelection_GetAvailableChoiceValue(int slot,int position)
{
    NativeMusicSelection_EnsureReady();
    if(position<3) return position>=0 ? position : 0;
    position-=3;
    for(int i=0;i<nmsSongCount;i++)
    {
        if(!NMS_SongVisibleAt(&nmsSongs[i],slot)) continue;
        if(position--==0)return 3+i;
    }
    return 0;
}
int NativeMusicSelection_GetAvailableChoicePosition(int slot,int choice)
{
    int count=NativeMusicSelection_GetAvailableChoiceCount(slot);
    for(int i=0;i<count;i++)
        if(NativeMusicSelection_GetAvailableChoiceValue(slot,i)==choice)
            return i;
    return 0;
}
/* Music browser: one pack per entry, with a nested list of that pack's
 * real OGG/CSEQ songs. Keep the existing per-song choice numbers untouched:
 * they are the keys saved in ctr_music.cfg and used by the audio resolver.
 * Never expose an empty pack or a song hidden for this specific destination. */
static int NMS_AvailablePackAt(int slot,int position)
{
    if(position<0) return -1;
    for(int p=0;p<nmsPackCount;p++)
    {
        for(int i=0;i<nmsSongCount;i++)
        {
            if(nmsSongs[i].packIndex!=p ||
               !NMS_SongVisibleAt(&nmsSongs[i],slot)) continue;
            if(position--==0) return p;
            break;
        }
    }
    return -1;
}
int NativeMusicSelection_GetAvailablePackCount(int slot)
{
    int count=0;
    NativeMusicSelection_EnsureReady();
    /* One traversal rather than recalculating the entire song list. */
    for(int p=0;p<nmsPackCount;p++)
    {
        for(int i=0;i<nmsSongCount;i++)
        {
            if(nmsSongs[i].packIndex==p &&
               NMS_SongVisibleAt(&nmsSongs[i],slot))
            {
                ++count;
                break;
            }
        }
    }
    return count;
}
const char *NativeMusicSelection_GetAvailablePackName(int slot,int position)
{
    int p;
    NativeMusicSelection_EnsureReady();
    p=NMS_AvailablePackAt(slot,position);
    return p>=0 ? nmsPacks[p].title : "NO PACK";
}
int NativeMusicSelection_GetAvailablePackPositionForChoice(int slot,int choice)
{
    int owner,pos=0;
    NativeMusicSelection_EnsureReady();
    if(choice<3 || choice>=3+nmsSongCount ||
       !NMS_SongVisibleAt(&nmsSongs[choice-3],slot)) return -1;
    owner=nmsSongs[choice-3].packIndex;
    for(int p=0;p<nmsPackCount;p++)
    {
        int visible=0;
        for(int i=0;i<nmsSongCount;i++)
        {
            if(nmsSongs[i].packIndex==p &&
               NMS_SongVisibleAt(&nmsSongs[i],slot))
            {
                visible=1; break;
            }
        }
        if(!visible) continue;
        if(p==owner) return pos;
        ++pos;
    }
    return -1;
}
int NativeMusicSelection_GetAvailablePackSongCount(int slot,int packPosition)
{
    int count=0;
    int p;
    NativeMusicSelection_EnsureReady();
    p=NMS_AvailablePackAt(slot,packPosition);
    if(p<0) return 0;
    for(int i=0;i<nmsSongCount;i++)
        if(nmsSongs[i].packIndex==p &&
           NMS_SongVisibleAt(&nmsSongs[i],slot))
            ++count;
    return count;
}
int NativeMusicSelection_GetAvailablePackSongChoice(int slot,int packPosition,int position)
{
    int p;
    NativeMusicSelection_EnsureReady();
    p=NMS_AvailablePackAt(slot,packPosition);
    if(p<0 || position<0) return 0;
    for(int i=0;i<nmsSongCount;i++)
    {
        if(nmsSongs[i].packIndex!=p ||
           !NMS_SongVisibleAt(&nmsSongs[i],slot)) continue;
        if(position--==0) return i+3;
    }
    return 0;
}
int NativeMusicSelection_GetAvailablePackSongPosition(
    int slot,int packPosition,int choice)
{
    int count=NativeMusicSelection_GetAvailablePackSongCount(slot,packPosition);
    for(int i=0;i<count;i++)
        if(NativeMusicSelection_GetAvailablePackSongChoice(
            slot,packPosition,i)==choice) return i;
    return 0;
}
const char *NativeMusicSelection_GetAvailablePackSongName(int choice)
{
    static char label[128];
    const char *leaf,*ext;
    const struct NMS_Song *song;
    NativeMusicSelection_EnsureReady();
    if(choice<3 || choice>=3+nmsSongCount) return "";
    song=&nmsSongs[choice-3];

    /* Virtual custom-track songs carry a real catalog label and their own
     * directory/sample bank. Keep normal packs' existing filename display. */
    if(song->directory[0])
        return song->label;

    leaf=strrchr(song->path,'/');
    leaf=leaf ? leaf+1 : song->path;
    ext=strrchr(leaf,'.');
    if(ext && ext>leaf)
        snprintf(label,sizeof(label),"%.*s",(int)(ext-leaf),leaf);
    else
        snprintf(label,sizeof(label),"%s",leaf);
    return label;
}
int NativeMusicSelection_GetLevelChoiceCount(void)
{
    return 3+NativeMusicSelection_GetSongCount();
}
int NativeMusicSelection_GetLevelChoice(int levelID)
{
    struct NMS_Level *v;
    NativeMusicSelection_EnsureReady();
    if(levelID<0 || levelID>=NATIVE_MUSIC_MAX_LEVELS) return 0;
    v=NMS_Choice(levelID,0);
    if(!v || v->choice<0) return 0;
    if(v->choice==NATIVE_MUSIC_SOURCE_RETAIL) return 1;
    if(v->choice==NATIVE_MUSIC_SOURCE_EXISTING) return 2;
    for(int i=0;i<nmsSongCount;i++)
        if(v->choice==nmsSongs[i].source &&
           !strcmp(v->path,nmsSongs[i].path) &&
           NMS_SongVisibleAt(&nmsSongs[i],levelID) &&
           !strcmp(v->pack,nmsPacks[nmsSongs[i].packIndex].slug))
            return i+3;
    return 0;
}
const char *NativeMusicSelection_GetLevelChoiceName(int choice)
{
    switch(choice)
    {
    case 0:return "USE DEFAULT SETTING";
    case 1:return "ORIGINAL CTR";
    case 2:return "CURRENT IMPORTS";
    default:return NativeMusicSelection_GetSongName(choice-3);
    }
}
void NativeMusicSelection_SetLevelChoice(int levelID,int choice)
{
    struct NMS_Level *v;
    NativeMusicSelection_EnsureReady();
    if(levelID<0 || levelID>=NATIVE_MUSIC_MAX_LEVELS ||
       choice<0 || choice>=3+nmsSongCount) return;
    if(choice>=3 && !NMS_SongVisibleAt(&nmsSongs[choice-3],levelID))
        return;
    v=NMS_Choice(levelID,1);
    if(!v) return;
    NMS_ClearLevel(v);
    if(choice==1) v->choice=NATIVE_MUSIC_SOURCE_RETAIL;
    else if(choice==2) v->choice=NATIVE_MUSIC_SOURCE_EXISTING;
    else if(choice>=3)
    {
        struct NMS_Song *song=&nmsSongs[choice-3];
        v->choice=song->source;
        snprintf(v->path,sizeof(v->path),"%s",song->path);
        snprintf(v->pack,sizeof(v->pack),"%s",
                 nmsPacks[song->packIndex].slug);
    }
    NMS_Save();
}
const char *NativeMusicSelection_GetLevelSelectionName(int levelID)
{
    return NativeMusicSelection_GetLevelChoiceName(
        NativeMusicSelection_GetLevelChoice(levelID));
}
static const struct NMS_Song *NMS_FindDefaultSong(int kind,
                                                  int levelID)
{
    const char *slug=NMS_DefaultSlug(kind);
    int p;
    if(!slug[0])
    {
        p=NMS_ActualPack(kind,0);
        if(p<0) return NULL;
        slug=nmsPacks[p].slug;
    }
    /* Explicit [levels] map file names directly, allowing multi-song
     * soundtrack packs to play in every assigned level. */
    for(int m=0;m<nmsMappingCount;m++)
    {
        const struct NMS_Mapping *map=&nmsMappings[m];
        if(map->packIndex<0 || map->packIndex>=nmsPackCount ||
           strcmp(nmsPacks[map->packIndex].slug,slug)) continue;
        if(levelID>=NATIVE_MUSIC_CUSTOM_BASE)
        {
            const char *current=NMS_SlotTrackID(levelID);
            if(!current || !map->trackID[0] ||
               SDL_strcasecmp(current,map->trackID)) continue;
        }
        else if(map->trackID[0] || map->levelID!=levelID)
            continue;

        for(int j=0;j<nmsSongCount;j++)
        {
            const struct NMS_Song *song=&nmsSongs[j];
            const char *file=strrchr(song->path,'/');
            file=file ? file+1 : song->path;
            if(song->packIndex==map->packIndex &&
               song->source==kind &&
               !SDL_strcasecmp(file,map->fileName))
                return song;
        }
    }

    /* A community track never receives another unrelated track's music. */
    if(levelID>=NATIVE_MUSIC_CUSTOM_BASE) return NULL;
    /* Backwards compatibility: target=level_XX or level_XX.ogg. */
    for(int i=0;i<nmsSongCount;i++)
    {
        const struct NMS_Song *song=&nmsSongs[i];
        if(song->source==kind &&
           NMS_SongVisibleAt(song,levelID) &&
           !strcmp(nmsPacks[song->packIndex].slug,slug) &&
           song->target==levelID)
            return song;
    }
    for(int i=0;i<nmsSongCount;i++)
    {
        const struct NMS_Song *song=&nmsSongs[i];
        const struct NMS_Pack *pack=&nmsPacks[song->packIndex];
        if(song->source==kind && NMS_SongVisibleAt(song,levelID) &&
           !strcmp(pack->slug,slug) &&
           pack->songCount==1) return song;
    }
    return NULL;
}
int NativeMusicSelection_Resolve(int levelID,
                                 struct NativeMusicSelectionResolved *out)
{
    const struct NMS_Level *selected=NULL;
    const struct NMS_Song *song=NULL;
    int source;
    if(!out) return 0;
    memset(out,0,sizeof(*out));
    NativeMusicSelection_EnsureReady();
    if(levelID>=0 && levelID<NATIVE_MUSIC_MAX_LEVELS)
        selected=NMS_Choice(levelID,0);
    source=(selected && selected->choice>=0) ?
           selected->choice : nmsDefaultSource;
    /* A custom level is a genuine mod, not retail Crash Cove. Choosing
     * "original" for it means its own shipped track soundtrack or silence. */
    if(levelID>=NATIVE_MUSIC_CUSTOM_BASE &&
       source==NATIVE_MUSIC_SOURCE_RETAIL)
        source=NATIVE_MUSIC_SOURCE_EXISTING;
    out->source=source;
    if(source==NATIVE_MUSIC_SOURCE_EXISTING ||
       source==NATIVE_MUSIC_SOURCE_RETAIL) return source;

    if(selected && selected->choice>=NATIVE_MUSIC_SOURCE_OGG)
    {
        for(int i=0;i<nmsSongCount;i++)
            if(nmsSongs[i].source==source &&
               !strcmp(nmsSongs[i].path,selected->path) &&
               NMS_SongVisibleAt(&nmsSongs[i],levelID) &&
               !strcmp(nmsPacks[nmsSongs[i].packIndex].slug,
                       selected->pack))
            { song=&nmsSongs[i];break; }
    }
    else song=NMS_FindDefaultSong(source,levelID);

    if(!song || !NMS_ExistsFile(song->path))
    {
        /* A custom track with no matching music keeps its own soundtrack
         * (or silence); it must never inherit Crash Cove's retail theme. */
        out->source=levelID>=NATIVE_MUSIC_CUSTOM_BASE ?
                    NATIVE_MUSIC_SOURCE_EXISTING :
                    NATIVE_MUSIC_SOURCE_RETAIL;
        return out->source;
    }
    {
        const struct NMS_Pack *pack=&nmsPacks[song->packIndex];
        snprintf(out->path,sizeof(out->path),"%s",song->path);
        snprintf(out->directory,sizeof(out->directory),"%s",
                 song->directory[0] ? song->directory : pack->dir);
        snprintf(out->pack,sizeof(out->pack),"%s",pack->slug);
        out->customSamples=song->directory[0] ?
                           song->customSamples : pack->samples;
    }
    return out->source;
}
