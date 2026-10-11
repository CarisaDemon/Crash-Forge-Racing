/* Crash Forge native LEV/VRM track discovery.
   This module does not modify original tracks or the rendering pipeline. */
#include <platform/native_custom_tracks.h>
#include <platform/native_assets.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#define NCT_PATH_MAX 1024
#define NCT_MAX_LEV (20u * 1024u * 1024u)
#define NCT_MAX_VRM (12u * 1024u * 1024u)

struct NCT_Record
{
    char slug[64];
    char name[80];
    char lev[NCT_PATH_MAX];
    char vrm[NCT_PATH_MAX];
    char directory[192];
    char music[192];
    char finalMusic[192];
    int musicKind;
    int musicSamples;
};
static struct NCT_Record s_nctRecords[NATIVE_CUSTOM_TRACK_LIMIT];
static int s_nctCount;
static int s_nctSelected = -1;

static char *NCT_Trim(char *p)
{
    char *last;
    while (*p && isspace((unsigned char)*p)) ++p;
    last = p + strlen(p);
    while (last > p && isspace((unsigned char)last[-1])) --last;
    *last = 0;
    return p;
}

static int NCT_SafeSlug(const char *s)
{
    int n=0;
    if (!s || !*s) return 0;
    while (*s)
    {
        char c=*s++;
        if (!((c>='a' && c<='z') || (c>='0' && c<='9') ||
              c=='_' || c=='-') || ++n>=64) return 0;
    }
    return 1;
}

static int NCT_FileName(const char *s, const char *extension)
{
    size_t n = s ? strlen(s) : 0;
    size_t extLen = strlen(extension);
    size_t i;
    if (n <= extLen || n >= 90 || SDL_strcasecmp(s + n - extLen, extension))
        return 0;
    for (i = 0; i < n; ++i)
    {
        unsigned char c=(unsigned char)s[i];
        if (!(isalnum(c) || c=='_' || c=='-' || c=='.'))
            return 0;
    }
    return 1;
}

static int NCT_Read32(const unsigned char *p, size_t end, size_t at, uint32_t *out)
{
    if (at > end || end-at < 4) return 0;
    *out = (uint32_t)p[at] | ((uint32_t)p[at+1]<<8) |
           ((uint32_t)p[at+2]<<16) | ((uint32_t)p[at+3]<<24);
    return 1;
}

static int NCT_ValidateBytes(const unsigned char *p, size_t n, int kind)
{
    uint32_t value, mapOffset, ptrBytes;
    size_t cursor, i;
    if (kind == 1)
    {
        if (n < 32 || !NCT_Read32(p,n,0,&mapOffset) ||
            mapOffset < 32 || mapOffset > n-12 ||
            !NCT_Read32(p,n,(size_t)mapOffset+4,&ptrBytes) ||
            ptrBytes < 4 || (ptrBytes & 3) ||
            ptrBytes > n-(size_t)mapOffset-8)
            return 0;
        /* Inspect every relocation *before* allowing LOAD_RunPtrMap, which
           dereferences these offsets without its own bounds checks. */
        for (i=0; i<(size_t)ptrBytes; i+=4)
        {
            size_t off;
            if (!NCT_Read32(p,n,(size_t)mapOffset+8+i,&value)) return 0;
            off=(size_t)(value & ~3u);
            if (off > mapOffset-4 || !NCT_Read32(p,n,4+off,&value) ||
                (value != 0 && value > mapOffset))
                return 0;
        }
        return 1;
    }
    if (kind != 2 || n < 24 || !NCT_Read32(p,n,0,&value) ||
        value != 0x20u)
        return 0;
    cursor=4;
    for (i=0; i<2048; ++i)
    {
        if (!NCT_Read32(p,n,cursor,&value)) return 0;
        if (!value) return cursor+4 == n && i>0;
        if (value < 12 || value > n-(cursor+4)) return 0;
        cursor+=(size_t)value+4;
    }
    return 0;
}

static int NCT_ValidateFile(const char *relative, int kind)
{
    char path[NCT_PATH_MAX];
    FILE *f;
    long size;
    unsigned char *buf;
    int valid=0;
    size_t maxSize = kind==1 ? NCT_MAX_LEV : NCT_MAX_VRM;
    if (!NativeAssets_BuildPath(relative, path, sizeof(path)))
        return 0;
#if defined(_WIN32)
    {
        DWORD attr=GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES ||
            (attr & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)))
            return 0;
    }
#endif
    f=fopen(path,"rb");
    if (!f) return 0;
    if (fseek(f,0,SEEK_END) || (size=ftell(f))<0 ||
        (size_t)size > maxSize || (size_t)size < 24 ||
        fseek(f,0,SEEK_SET))
    {
        fclose(f);
        return 0;
    }
    buf=(unsigned char *)malloc((size_t)size);
    if (buf && fread(buf,1,(size_t)size,f)==(size_t)size)
        valid=NCT_ValidateBytes(buf,(size_t)size,kind);
    free(buf);
    fclose(f);
    return valid;
}

/* Only safe, regular mod-local files can act as community music. */
static int NCT_MusicFileExists(const char *relative)
{
    char path[NCT_PATH_MAX];
    FILE *f;
    if (!NativeAssets_BuildPath(relative,path,sizeof(path))) return 0;
#if defined(_WIN32)
    {
        DWORD attr=GetFileAttributesA(path);
        if(attr==INVALID_FILE_ATTRIBUTES ||
           (attr&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)))
            return 0;
    }
#endif
    f=fopen(path,"rb");
    if(!f)return 0;
    fclose(f);
    return 1;
}
static int NCT_MusicCandidate(struct NCT_Record *record,
                             const char *filename, int kind)
{
    char relative[sizeof(record->music)];
    const char *ext=kind==NATIVE_CUSTOM_TRACK_MUSIC_CSEQ?".cseq":".ogg";
    if (!NCT_FileName(filename,ext) ||
        snprintf(relative,sizeof(relative),"%s/%s",record->directory,filename)
             >= (int)sizeof(relative) ||
        !NCT_MusicFileExists(relative))
        return 0;
    record->musicKind=kind;
    snprintf(record->music,sizeof(record->music),"%s",relative);
    return 1;
}

static void NCT_Add(const char *prefix, const char *slug, int legacy)
{
    struct NCT_Record newTrack;
    char iniRelative[192], iniPath[NCT_PATH_MAX];
    FILE *f;
    char line[512], section[32]="";
    char levFile[96]="data.lev",vrmFile[96]="data.vrm";
    char declaredID[80]="",declaredType[40]="";
    char musicSource[16]="auto",musicDeclaredFile[96]="";
    int musicEnabled=1,musicSamples=0;
    int declaredMeta=0, enabled=0, seenTrack=0, formatOK=0;
    int i;
    if (!NCT_SafeSlug(slug) || s_nctCount>=NATIVE_CUSTOM_TRACK_LIMIT) return;
    for(i=0;i<s_nctCount;i++)
        if (!strcmp(s_nctRecords[i].slug,slug)) return;
    if (snprintf(iniRelative,sizeof(iniRelative),"mods/%s%s/track.ini",
                 prefix,slug) >= (int)sizeof(iniRelative) ||
        !NativeAssets_BuildPath(iniRelative,iniPath,sizeof(iniPath)))
        return;
    f=fopen(iniPath,"rb");
    if(!f)return;
    memset(&newTrack,0,sizeof(newTrack));
    snprintf(newTrack.slug,sizeof(newTrack.slug),"%s",slug);
    snprintf(newTrack.name,sizeof(newTrack.name),"%s",slug);
    while(fgets(line,sizeof(line),f))
    {
        char *str=NCT_Trim(line);
        char *eq;
        if(!*str||*str=='#'||*str==';') continue;
        if(*str=='[')
        {
            char *end=strchr(str,']');
            if(!end)continue;
            *end=0;
            snprintf(section,sizeof(section),"%s",NCT_Trim(str+1));
            if(!SDL_strcasecmp(section,"forge_mod"))declaredMeta=1;
            if(!SDL_strcasecmp(section,"track"))seenTrack=1;
            continue;
        }
        eq=strchr(str,'=');
        if(!eq)continue;
        *eq=0;
        {
            char *key=NCT_Trim(str),*val=NCT_Trim(eq+1);
            if(!SDL_strcasecmp(section,"track"))
            {
                if(!SDL_strcasecmp(key,"enabled"))
                    enabled=(!SDL_strcasecmp(val,"true")||!strcmp(val,"1"));
                if(!SDL_strcasecmp(key,"format"))
                    formatOK=!SDL_strcasecmp(val,"native_lev_vrm");
                if(!SDL_strcasecmp(key,"name")&&*val)
                    snprintf(newTrack.name,sizeof(newTrack.name),"%s",val);
                if(!SDL_strcasecmp(key,"lev"))
                    snprintf(levFile,sizeof(levFile),"%s",val);
                if(!SDL_strcasecmp(key,"vrm"))
                    snprintf(vrmFile,sizeof(vrmFile),"%s",val);
            }
            if(!SDL_strcasecmp(section,"music"))
            {
                if(!SDL_strcasecmp(key,"enabled"))
                    musicEnabled=(!SDL_strcasecmp(val,"true")||!strcmp(val,"1"));
                if(!SDL_strcasecmp(key,"source"))
                    snprintf(musicSource,sizeof(musicSource),"%s",val);
                if(!SDL_strcasecmp(key,"file"))
                    snprintf(musicDeclaredFile,sizeof(musicDeclaredFile),"%s",val);
                if(!SDL_strcasecmp(key,"custom_samples"))
                {
                    char *end;
                    long n=strtol(val,&end,10);
                    if(end!=val && !*end && n>=0 && n<=256)
                        musicSamples=(int)n;
                }
            }
            if(!SDL_strcasecmp(section,"forge_mod"))
            {
                if(!SDL_strcasecmp(key,"id"))
                    snprintf(declaredID,sizeof(declaredID),"%s",val);
                if(!SDL_strcasecmp(key,"type"))
                    snprintf(declaredType,sizeof(declaredType),"%s",val);
            }
        }
    }
    fclose(f);
    if(!seenTrack||!enabled||!formatOK ||
       !NCT_FileName(levFile,".lev")||!NCT_FileName(vrmFile,".vrm"))
        return;
    /* New module folder contains the readable name. Its stable web ID is
       an independent, validated [forge_mod] value inside track.ini. */
    if(!legacy && (!declaredMeta||!NCT_SafeSlug(declaredID) ||
                   SDL_strcasecmp(declaredType,"Track")))
        return;
    if(legacy && declaredMeta &&
       (!NCT_SafeSlug(declaredID)||SDL_strcasecmp(declaredType,"Track")))
        return;
    if(snprintf(newTrack.lev,sizeof(newTrack.lev),"mods/%s%s/%s",
                prefix,slug,levFile)>=(int)sizeof(newTrack.lev)||
       snprintf(newTrack.vrm,sizeof(newTrack.vrm),"mods/%s%s/%s",
                prefix,slug,vrmFile)>=(int)sizeof(newTrack.vrm))
        return;
    if(!NCT_ValidateFile(newTrack.lev,1) ||
       !NCT_ValidateFile(newTrack.vrm,2))
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "[CustomTracks] Rejected invalid LEV/VRM: %s",slug);
        return;
    }
    if(snprintf(newTrack.directory,sizeof(newTrack.directory),"mods/%s%s",
                prefix,slug)>=(int)sizeof(newTrack.directory))
        return;
    /* A track contains no soundtrack unless it actually ships one.
       Allow a [music] declaration, or automatically detect common toolkit
       output names. OGG has priority when both formats exist. */
    if(musicEnabled && SDL_strcasecmp(musicSource,"none") &&
       SDL_strcasecmp(musicSource,"off"))
    {
        int allowOgg=(!SDL_strcasecmp(musicSource,"auto") ||
                      !SDL_strcasecmp(musicSource,"ogg"));
        int allowCseq=(!SDL_strcasecmp(musicSource,"auto") ||
                       !SDL_strcasecmp(musicSource,"cseq"));
        if(musicDeclaredFile[0])
        {
            if(allowOgg)NCT_MusicCandidate(&newTrack,musicDeclaredFile,
                                          NATIVE_CUSTOM_TRACK_MUSIC_OGG);
            if(!newTrack.musicKind && allowCseq)
                NCT_MusicCandidate(&newTrack,musicDeclaredFile,
                                   NATIVE_CUSTOM_TRACK_MUSIC_CSEQ);
        }
        else
        {
            if(allowOgg &&
               !NCT_MusicCandidate(&newTrack,"music.ogg",NATIVE_CUSTOM_TRACK_MUSIC_OGG))
                NCT_MusicCandidate(&newTrack,"track.ogg",NATIVE_CUSTOM_TRACK_MUSIC_OGG);
            if(allowCseq && !newTrack.musicKind &&
               !NCT_MusicCandidate(&newTrack,"track.cseq",NATIVE_CUSTOM_TRACK_MUSIC_CSEQ))
                NCT_MusicCandidate(&newTrack,"music.cseq",NATIVE_CUSTOM_TRACK_MUSIC_CSEQ);
        }
        if(newTrack.musicKind==NATIVE_CUSTOM_TRACK_MUSIC_OGG)
        {
            const char *finalNames[]={"music_final.ogg","track_final.ogg"};
            int j;
            for(j=0;j<2;j++)
            {
                char relative[sizeof(newTrack.finalMusic)];
                if(snprintf(relative,sizeof(relative),"%s/%s",
                            newTrack.directory,finalNames[j])
                   >=(int)sizeof(relative))continue;
                if(NCT_MusicFileExists(relative))
                {
                    snprintf(newTrack.finalMusic,sizeof(newTrack.finalMusic),
                             "%s",relative);
                    break;
                }
            }
        }
    }
    newTrack.musicSamples=musicSamples;
    s_nctRecords[s_nctCount++]=newTrack;
    SDL_Log("[CustomTracks] Ready: %s (%s) music=%s",
            newTrack.name,newTrack.slug,
            newTrack.musicKind==NATIVE_CUSTOM_TRACK_MUSIC_OGG?"OGG":
            newTrack.musicKind==NATIVE_CUSTOM_TRACK_MUSIC_CSEQ?"CSEQ":"none");
}

static void NCT_ScanDir(const char *prefix,int legacy)
{
    char root[NCT_PATH_MAX];
    char rel[96], pattern[NCT_PATH_MAX];
    if(snprintf(rel,sizeof(rel),"mods/%s",legacy?"tracks":"")>=(int)sizeof(rel) ||
       !NativeAssets_BuildPath(rel,root,sizeof(root)))
        return;
#if defined(_WIN32)
    WIN32_FIND_DATAA data;
    HANDLE h;
    if(snprintf(pattern,sizeof(pattern),"%s\\*",root)>=(int)sizeof(pattern))
        return;
    h=FindFirstFileA(pattern,&data);
    if(h==INVALID_HANDLE_VALUE)return;
    do {
        if((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
           !(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            NCT_Add(prefix,data.cFileName,legacy);
    }while(FindNextFileA(h,&data));
    FindClose(h);
#else
    DIR *dir=opendir(root);
    struct dirent *ent;
    (void)pattern;
    if(!dir)return;
    while((ent=readdir(dir))!=NULL)NCT_Add(prefix,ent->d_name,legacy);
    closedir(dir);
#endif
}

static int NCT_Compare(const void *a,const void *b)
{
    const struct NCT_Record *left=(const struct NCT_Record *)a;
    const struct NCT_Record *right=(const struct NCT_Record *)b;
    return SDL_strcasecmp(left->name,right->name);
}

void NativeCustomTracks_ClearSelection(void){s_nctSelected=-1;}
int NativeCustomTracks_IsSelected(void)
{
    return s_nctSelected>=0 && s_nctSelected<s_nctCount;
}
void NativeCustomTracks_Scan(void)
{
    s_nctCount=0;
    s_nctSelected=-1;
    NCT_ScanDir("",0);
    NCT_ScanDir("tracks/",1);
    qsort(s_nctRecords,(size_t)s_nctCount,sizeof(s_nctRecords[0]),NCT_Compare);
    SDL_Log("[CustomTracks] %d valid native LEV/VRM track(s) found",s_nctCount);
}
int NativeCustomTracks_Count(void){return s_nctCount;}
const char *NativeCustomTracks_Name(int index)
{
    return index>=0&&index<s_nctCount ? s_nctRecords[index].name : NULL;
}
int NativeCustomTracks_Select(int index)
{
    if(index<0||index>=s_nctCount){s_nctSelected=-1;return 0;}
    s_nctSelected=index;
    SDL_Log("[CustomTracks] Selected %s",s_nctRecords[index].slug);
    return 1;
}
int NativeCustomTracks_IsActiveRace(void)
{
    struct GameTracker *gt;
    return NativeCustomTracks_IsSelected()&&sdata&&(gt=sdata->gGT) &&
           gt->levelID==CRASH_COVE &&
           !(gt->gameMode1&(MAIN_MENU|ADVENTURE_ARENA));
}
int NativeCustomTracks_MusicKindAt(int index)
{
    return index>=0&&index<s_nctCount?s_nctRecords[index].musicKind:0;
}
const char *NativeCustomTracks_MusicPathAt(int index)
{
    return index>=0&&index<s_nctCount?s_nctRecords[index].music:NULL;
}
int NativeCustomTracks_SelectedMusicKind(void)
{
    return NativeCustomTracks_IsSelected()?s_nctRecords[s_nctSelected].musicKind:0;
}
int NativeCustomTracks_SelectedMusicSamples(void)
{
    return NativeCustomTracks_IsSelected()?s_nctRecords[s_nctSelected].musicSamples:0;
}
int NativeCustomTracks_SelectedMusicKey(void)
{
    /* Keep custom track music outside the retail 0..99 OGG PCM cache. */
    return NativeCustomTracks_IsSelected()?100+s_nctSelected:-1;
}
const char *NativeCustomTracks_SelectedMusicPath(void)
{
    return NativeCustomTracks_IsSelected()?s_nctRecords[s_nctSelected].music:NULL;
}
const char *NativeCustomTracks_SelectedFinalMusicPath(void)
{
    return NativeCustomTracks_IsSelected()?s_nctRecords[s_nctSelected].finalMusic:NULL;
}
const char *NativeCustomTracks_SelectedDirectory(void)
{
    return NativeCustomTracks_IsSelected()?s_nctRecords[s_nctSelected].directory:NULL;
}
int NativeCustomTracks_OverrideKind(int loadType,int subfileIndex)
{
    struct GameTracker *gt;
    if(!NativeCustomTracks_IsSelected()||!sdata||!(gt=sdata->gGT)||
       gt->levelID!=CRASH_COVE || (gt->gameMode1 & (MAIN_MENU|ADVENTURE_ARENA)))
        return 0;
    if(loadType==LT_VRAM &&
       subfileIndex==LOAD_GetBigfileIndex(CRASH_COVE,sdata->levelLOD,LVI_VRAM))
        return 2;
    if(loadType==LT_GETADDR &&
       subfileIndex==LOAD_GetBigfileIndex(CRASH_COVE,sdata->levelLOD,LVI_LEV))
        return 1;
    return 0;
}
void *NativeCustomTracks_ReadOverride(int kind,void *destination,
                                      unsigned int *outSize)
{
    const char *relative;
    char host[NCT_PATH_MAX];
    FILE *f;
    long length;
    unsigned int allocSize;
    void *mem;
    if(!NativeCustomTracks_IsSelected()||destination||!outSize||
       (kind!=1&&kind!=2))return NULL;
    relative=kind==1?s_nctRecords[s_nctSelected].lev :
                      s_nctRecords[s_nctSelected].vrm;
    if(!NativeAssets_BuildPath(relative,host,sizeof(host)))return NULL;
    f=fopen(host,"rb");
    if(!f)return NULL;
    if(fseek(f,0,SEEK_END)|| (length=ftell(f))<24 ||
       (unsigned long)length>(kind==1?NCT_MAX_LEV:NCT_MAX_VRM) ||
       fseek(f,0,SEEK_SET))
    {
        fclose(f);
        return NULL;
    }
    allocSize=(((unsigned int)length)+2047u)&~2047u;
    mem=MEMPACK_AllocMem(allocSize);
    if(!mem){fclose(f);return NULL;}
    if(fread(mem,1,(size_t)length,f)!=(size_t)length ||
       !NCT_ValidateBytes((const unsigned char *)mem,(size_t)length,kind))
    {
        fclose(f);
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "[CustomTracks] File changed/invalid during load");
        return NULL;
    }
    fclose(f);
    *outSize=(unsigned int)length;
    SDL_Log("[CustomTracks] Loaded %s (%ld bytes)",relative,length);
    return mem;
}
