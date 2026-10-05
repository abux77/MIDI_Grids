#include "storage.h"
#include "flash_disk.h"
#include "ff.h"
#include "diskio.h"
#include "factory.h"
#include <string.h>
#include <stdio.h>
static FATFS filesystem;
static bool disk_available,mounted,editing,host_medium,remount_requested,reloaded;
DSTATUS disk_initialize(BYTE drive) {
    return drive || !disk_available ? STA_NOINIT : 0;
}
DSTATUS disk_status(BYTE drive) {
    return disk_initialize(drive);
}
DRESULT disk_read(BYTE drive, BYTE *buffer, LBA_t sector, UINT count) {
    if (drive || !count || sector >= PVS_DISK_SECTOR_COUNT ||
        count > PVS_DISK_SECTOR_COUNT - sector) return RES_PARERR;
    return flash_disk_read((uint32_t)sector * PVS_DISK_SECTOR_SIZE, buffer,
                           (size_t)count * PVS_DISK_SECTOR_SIZE) ? RES_OK : RES_ERROR;
}
DRESULT disk_write(BYTE drive, const BYTE *buffer, LBA_t sector, UINT count) {
    if (drive || !count || sector >= PVS_DISK_SECTOR_COUNT ||
        count > PVS_DISK_SECTOR_COUNT - sector) return RES_PARERR;
    return flash_disk_write((uint32_t)sector * PVS_DISK_SECTOR_SIZE, buffer,
                            (size_t)count * PVS_DISK_SECTOR_SIZE) ? RES_OK : RES_ERROR;
}
DRESULT disk_ioctl(BYTE drive, BYTE command, void *buffer) {
    if (drive || !disk_available) return RES_NOTRDY;
    switch (command) {
    case CTRL_SYNC: return flash_disk_sync() ? RES_OK : RES_ERROR;
    case GET_SECTOR_COUNT:
        if (!buffer) return RES_PARERR;
        *(LBA_t *)buffer = PVS_DISK_SECTOR_COUNT;
        return RES_OK;
    case GET_SECTOR_SIZE:
        if (!buffer) return RES_PARERR;
        *(WORD *)buffer = PVS_DISK_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        if (!buffer) return RES_PARERR;
        *(DWORD *)buffer = 4096 / PVS_DISK_SECTOR_SIZE;
        return RES_OK;
    default: return RES_PARERR;
    }
}


bool configured_usb_midi = true;
static const char default_config[] = "USB_MIDI,ON\nBD,CH1,N36\nSD,CH1,N38\nHH,CH1,N42\n";
static const char *drum_names[3] = {"BD", "SD", "HH"};
unsigned drum_channel[3], drum_note[3] = {36,38,42}, selected_sample[3] = {1,1,1};
static unsigned available[3];
static void sample_name(char name[13], unsigned part, unsigned number) {
    snprintf(name, 13, "%s%02u.WAV", drum_names[part], number);
}
static FRESULT write_text(const char *name, const char *text) {
    FIL file; UINT written;
    FRESULT result = f_open(&file, name, FA_WRITE | FA_CREATE_ALWAYS);
    if (result != FR_OK) return result;
    unsigned size = strlen(text);
    result = f_write(&file, text, size, &written);
    FRESULT closed = f_close(&file);
    return result == FR_OK && written == size ? closed : FR_DISK_ERR;
}

static FRESULT provision_factory(void) {
    uint8_t work[512];
    MKFS_PARM options = {.fmt=FM_FAT|FM_SFD, .n_fat=2, .align=8,
                         .n_root=128, .au_size=512};
    FRESULT result = f_mkfs("", &options, work, sizeof(work));
    if (result == FR_OK) result = f_mount(&filesystem, "", 1);
    if (result == FR_OK) result = f_setlabel("MIDI GRIDS");
    for (unsigned i=0; i<3 && result==FR_OK; ++i) {
        FIL file;
        UINT written;
        result = f_open(&file, factory[i].name, FA_WRITE|FA_CREATE_ALWAYS);
        if (result == FR_OK) {
            result = f_write(&file, factory[i].data, factory[i].size, &written);
            FRESULT closed = f_close(&file);
            if (result == FR_OK && (written != factory[i].size || closed != FR_OK))
                result = FR_DISK_ERR;
        }
    }
    if (result == FR_OK) result = write_text("CONFIG.TXT", default_config);
    if (!flash_disk_sync()) result = FR_DISK_ERR;
    return result;
}

void storage_init(void) {
    disk_available = flash_disk_init();
    if (!disk_available) return;
    FRESULT result = f_mount(&filesystem, "", 1);
    if (result != FR_OK && flash_disk_is_blank()) result = provision_factory();
    mounted = result == FR_OK;
    if (!mounted) f_mount(NULL, "", 0);
    printf("Sample filesystem: %u (0=ready, 13=no FAT; CDC format restores defaults)\n", result);
}

bool storage_format_factory(void) {
    if (editing || !disk_available || !flash_disk_sync()) return false;
    if (f_mount(NULL, "", 0) != FR_OK) return false;
    mounted = provision_factory() == FR_OK;
    if (!mounted) f_mount(NULL, "", 0);
    return mounted;
}
bool storage_edit_active(void){return editing;}
bool storage_host_ready(void){return editing&&host_medium;}
bool storage_begin_edit(void){if(!disk_available||!flash_disk_sync()||f_mount(NULL,"",0)!=FR_OK)return false;mounted=false;editing=host_medium=true;return true;}
bool storage_host_eject(void){if(!storage_host_ready()||!flash_disk_sync())return false;host_medium=false;remount_requested=true;return true;}
void storage_service_remount(void){if(!remount_requested)return;remount_requested=false;mounted=f_mount(&filesystem,"",1)==FR_OK;editing=false;reloaded=true;}
bool storage_reload_pending(void){bool r=reloaded;reloaded=false;return r;}
/* Shared bounded arena: all three PCM payloads together may use 128 KiB. */
_Static_assert(sizeof(short)==2, "PCM requires 16-bit shorts");
static short arena[65536];
const short *sample_data[3];unsigned sample_frames[3];
static unsigned u16(const unsigned char *p){return p[0]|p[1]<<8;}
static unsigned u32(const unsigned char *p){return u16(p)|u16(p+2)<<16;}
static bool read_exact(FIL *f,void *p,unsigned n){UINT got;return f_read(f,p,n,&got)==FR_OK&&got==n;}
static bool load_one(unsigned i, unsigned *used) {
    char name[13]; sample_name(name, i, selected_sample[i]);
    FIL file; FRESULT result = f_open(&file, name, FA_READ);
    if (result != FR_OK) {
        printf("Sample %s: open failed, FatFs error %u\n", name, result);
        return false;
    }
    unsigned char h[16]; bool fmt = false, ok = false;
    const char *reason = "missing fmt or data chunk";
    unsigned size = (unsigned)f_size(&file);
    printf("Loading %s: file %u bytes\n", name, size);
    if (size < 12 || !read_exact(&file, h, 12)) {
        reason = "short/unreadable WAV header"; goto done;
    }
    if (memcmp(h, "RIFF", 4) || memcmp(h+8, "WAVE", 4)) {
        reason = "not a RIFF/WAVE file"; goto done;
    }
    if (u32(h+4) > size-8) {
        printf("Sample %s: RIFF declares %u bytes after header, file has %u\n", name, u32(h+4), size-8);
        reason = "truncated RIFF file"; goto done;
    }
    while (f_tell(&file)+8 <= f_size(&file)) {
        if (!read_exact(&file, h, 8)) {reason = "chunk header read failed"; break;}
        unsigned n = u32(h+4), pos = f_tell(&file);
        if (n > f_size(&file)-pos) {reason = "chunk extends beyond end of file"; break;}
        if (!memcmp(h, "fmt ", 4)) {
            if (n < 16 || !read_exact(&file, h, 16)) {reason = "short/unreadable fmt chunk"; break;}
            unsigned encoding = u16(h), channels = u16(h+2), rate = u32(h+4);
            unsigned align = u16(h+12), bits = u16(h+14);
            printf("Sample %s: encoding=%u channels=%u rate=%u Hz bits=%u block=%u\n",
                   name, encoding, channels, rate, bits, align);
            if (encoding != 1) {reason = "unsupported encoding; requires PCM encoding 1"; break;}
            if (channels != 1) {reason = "unsupported channels; requires mono"; break;}
            if (rate != 44100) {reason = "unsupported sample rate; requires 44100 Hz"; break;}
            if (bits != 16) {reason = "unsupported bit depth; requires 16 bits"; break;}
            if (align != 2) {reason = "invalid block alignment; requires 2 bytes"; break;}
            fmt = true;
        } else if (!memcmp(h, "data", 4)) {
            if (!fmt) {reason = "data chunk precedes fmt chunk"; break;}
            if (!n || (n & 1)) {reason = "empty or odd-length PCM data"; break;}
            if (n/2 > 65536-*used) {
                printf("Sample %s: PCM needs %u bytes, RAM remaining %u of 131072 bytes\n",
                       name, n, (65536-*used)*2);
                reason = "selected samples exceed shared 128 KiB RAM"; break;
            }
            if (!read_exact(&file, arena+*used, n)) {reason = "PCM data read failed"; break;}
            sample_data[i] = arena+*used; sample_frames[i] = n/2; *used += n/2;
            printf("Loaded %s: %u PCM bytes, %u frames, %u ms; RAM %u/131072 bytes\n",
                   name, n, n/2, (n/2)*1000/44100, *used*2);
            ok = true; break;
        }
        if (f_lseek(&file, pos+n+(n&1)) != FR_OK) {reason = "chunk seek failed"; break;}
    }
 done:
    f_close(&file);
    if (!ok) printf("Sample %s rejected: %s\n", name, reason);
    return ok;
}
static bool load_factory(unsigned i, unsigned *used) {
    const uint8_t *wav = factory[i].data;
    unsigned size = factory[i].size;
    if (size < 12 || memcmp(wav, "RIFF", 4) || memcmp(wav + 8, "WAVE", 4)) return false;
    bool format_ok = false;
    for (unsigned pos = 12; pos + 8 <= size;) {
        const uint8_t *chunk = wav + pos;
        unsigned length = u32(chunk + 4);
        pos += 8;
        if (length > size - pos) return false;
        if (!memcmp(chunk, "fmt ", 4)) {
            const uint8_t *fmt = wav + pos;
            format_ok = length >= 16 && u16(fmt) == 1 && u16(fmt + 2) == 1 &&
                        u32(fmt + 4) == 44100 && u16(fmt + 14) == 16;
        } else if (!memcmp(chunk, "data", 4)) {
            if (!format_ok || !length || (length & 1) || length / 2 > 65536 - *used) return false;
            memcpy(arena + *used, wav + pos, length);
            sample_data[i] = arena + *used;
            sample_frames[i] = length / 2;
            *used += length / 2;
            return true;
        }
        pos += length + (length & 1);
    }
    return false;
}

bool storage_is_ready(void) { return mounted && !editing; }

bool samples_load(void){unsigned used=0;bool all=true;
 for(unsigned i=0;i<3;i++){sample_data[i]=NULL;sample_frames[i]=0;if(editing || !(mounted ? load_one(i,&used) : load_factory(i,&used))){char name[13];sample_name(name,i,selected_sample[i]);printf("Sample %s: voice unavailable\n",name);all=false;}}
 if (!mounted && !editing) printf("Sample filesystem unavailable; using embedded factory WAVs (disk preserved).\n");
 return all;
}

/* Bounded line reader: discard oversized lines rather than parse fragments. */
static bool read_line(FIL *file, char line[80]) {
    unsigned length = 0; bool overflow = false; UINT got = 0; char c;
    while (f_read(file, &c, 1, &got) == FR_OK && got) {
        if (c == '\n') break;
        if (c == '\r') continue;
        if (length < 79) line[length++] = c; else overflow = true;
    }
    line[length] = 0;
    if (overflow) line[0] = 0;
    return got || length || overflow;
}
void settings_load(void) {
    configured_usb_midi = true;
    const unsigned defaults[3] = {36,38,42};
    for (unsigned p=0;p<3;p++) {drum_channel[p]=0;drum_note[p]=defaults[p];selected_sample[p]=1;available[p]=0;}
    if (!storage_is_ready()) return;
    FIL file; char line[80], name[3], extra; unsigned channel, note;
    if (f_open(&file,"CONFIG.TXT",FA_READ)==FR_OK) {
        while (read_line(&file,line)) {
            if (!strcmp(line,"USB_MIDI,ON")) {configured_usb_midi=true;continue;}
            if (!strcmp(line,"USB_MIDI,OFF")) {configured_usb_midi=false;continue;}
            if (sscanf(line,"%2[^,],CH%u,N%u %c",name,&channel,&note,&extra)!=3 || channel<1 || channel>16 || note>127) continue;
            for (unsigned p=0;p<3;p++) if (!strcmp(name,drum_names[p])) {drum_channel[p]=channel-1;drum_note[p]=note;}
        }
        f_close(&file);
    }
    if (f_open(&file,"SELECT.TXT",FA_READ)==FR_OK) {
        while (read_line(&file,line)) {
            unsigned number;
            if (sscanf(line,"%2[^,],%u %c",name,&number,&extra)!=2 || number<1 || number>SAMPLE_SLOTS) continue;
            for(unsigned p=0;p<3;p++) if(!strcmp(name,drum_names[p])) selected_sample[p]=number;
        }
        f_close(&file);
    }
    for(unsigned p=0;p<3;p++) {
        for(unsigned n=1;n<=SAMPLE_SLOTS;n++) {
            char path[13];FILINFO info;sample_name(path,p,n);
            if(f_stat(path,&info)==FR_OK && !(info.fattrib & AM_DIR)) available[p]|=1u<<(n-1);
        }
        if(!(available[p] & (1u<<(selected_sample[p]-1)))) {
            selected_sample[p]=1;
            for(unsigned n=1;n<=SAMPLE_SLOTS;n++) if(available[p]&(1u<<(n-1))) {selected_sample[p]=n;break;}
        }
    }
}
unsigned sample_choice(unsigned part,unsigned knob) {
    if(part>=3) return 0;
    unsigned count=0;
    for(unsigned n=0;n<SAMPLE_SLOTS;n++) if(available[part]&(1u<<n)) count++;
    if(!count) return 0;
    unsigned index=(knob>255?255:knob)*count/256;
    for(unsigned n=0;n<SAMPLE_SLOTS;n++) if(available[part]&(1u<<n)) {if(!index--) return n+1;}
    return 0;
}
bool sample_select(unsigned part,unsigned number) {
    if(!storage_is_ready() || part>=3 || number<1 || number>SAMPLE_SLOTS || !(available[part]&(1u<<(number-1)))) return false;
    unsigned previous=selected_sample[part];
    char name[13];sample_name(name,part,number);
    printf("Selecting %s (previous %s%02u.WAV)\n",name,drum_names[part],previous);
    unsigned previous_frames[3];memcpy(previous_frames,sample_frames,sizeof(previous_frames));
    selected_sample[part]=number;
    samples_load();
    bool rejected = !sample_frames[part];
    for(unsigned p=0;p<3;p++) if(previous_frames[p] && !sample_frames[p]) rejected=true;
    if(rejected) {
        printf("Selection %s rejected; restoring %s%02u.WAV (a selected voice failed to load)\n",name,drum_names[part],previous);
        selected_sample[part]=previous;samples_load();return false;
    }
    return true;
}
bool selections_save(void) {
    if(!storage_is_ready()) return false;
    char text[48];snprintf(text,sizeof(text),"BD,%u\nSD,%u\nHH,%u\n",selected_sample[0],selected_sample[1],selected_sample[2]);
    return write_text("SELECT.TXT",text)==FR_OK && flash_disk_sync();
}

void samples_report(void) {
    if(!storage_is_ready()) {
        printf("Sample listing unavailable: %s\n",editing?"USB host owns drive; eject first":"filesystem not mounted");
        return;
    }
    for(unsigned p=0;p<3;p++) {
        printf("%s discovered:",drum_names[p]);
        for(unsigned n=1;n<=SAMPLE_SLOTS;n++) if(available[p]&(1u<<(n-1))) printf(" %02u",n);
        printf("; selected %02u; loaded %u frames\n",selected_sample[p],sample_frames[p]);
    }
    DIR dir; FILINFO info; FRESULT result=f_opendir(&dir,"/");
    if(result!=FR_OK){printf("Root directory error %u\n",result);return;}
    printf("Drive root (samples must be named BD01.WAV..BD08.WAV, SD01.WAV..SD08.WAV, HH01.WAV..HH08.WAV):\n");
    while((result=f_readdir(&dir,&info))==FR_OK && info.fname[0])
        printf("  %s%s %u bytes\n",info.fname,(info.fattrib&AM_DIR)?"/":"",(unsigned)info.fsize);
    if(result!=FR_OK)printf("Directory read error %u\n",result);
    f_closedir(&dir);
}
