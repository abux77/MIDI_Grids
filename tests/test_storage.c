#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "storage.h"
#include "flash_disk.h"
#include "ff.h"
static unsigned char disk[PVS_STORAGE_BYTES];
bool flash_disk_init(void){return true;}
bool flash_disk_is_blank(void){for(unsigned i=0;i<sizeof(disk);i++)if(disk[i]!=255)return false;return true;}
bool flash_disk_sync(void){return true;}
bool flash_disk_read(uint32_t o,void *b,size_t n){if(o>sizeof(disk)||n>sizeof(disk)-o)return false;memcpy(b,disk+o,n);return true;}
bool flash_disk_write(uint32_t o,const void *b,size_t n){if(o>sizeof(disk)||n>sizeof(disk)-o)return false;memcpy(disk+o,b,n);return true;}
int main(void){
 memset(disk,255,sizeof(disk));storage_init();assert(samples_load());assert(sample_frames[0]&&sample_frames[1]&&sample_frames[2]);
 settings_load();assert(drum_channel[0]==0&&drum_note[0]==36&&selected_sample[0]==1&&configured_usb_midi);
 assert(sample_choice(0,0)==1&&sample_choice(0,255)==1);
 FIL config;UINT written;
 const char *text="# USB_MIDI,ON\nUSB_MIDI,OFF\nUSB_MIDI,INVALID\nBD,CH10,N60\nSD,CH16,N0\nHH,CH0,N200\nBD,CH17,N128\n";
 assert(f_open(&config,"CONFIG.TXT",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 assert(f_write(&config,text,strlen(text),&written)==FR_OK);assert(f_close(&config)==FR_OK);
 settings_load();assert(!configured_usb_midi);assert(drum_channel[0]==9&&drum_note[0]==60&&drum_channel[1]==15&&drum_note[1]==0&&drum_channel[2]==0&&drum_note[2]==42);
 /* Full-line comments are ignored, absent/invalid USB values default to ON. */
 const char *usb_cases[]={"USB_MIDI,ON\n", "USB_MIDI,OFF\n", "# USB_MIDI,OFF\n", "USB_MIDI,MAYBE\n", ""};
 const bool usb_expected[]={true,false,true,true,true};
 for(unsigned c=0;c<5;c++){
  assert(f_open(&config,"CONFIG.TXT",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
  assert(f_write(&config,usb_cases[c],strlen(usb_cases[c]),&written)==FR_OK);assert(f_close(&config)==FR_OK);
  settings_load();assert(configured_usb_midi==usb_expected[c]);
 }
 assert(f_open(&config,"CONFIG.TXT",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 assert(f_write(&config,text,strlen(text),&written)==FR_OK);assert(f_close(&config)==FR_OK);
 assert(f_open(&config,"SELECT.TXT",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 const char *old_selection="BD,16\nSD,9\nHH,14\n";
 assert(f_write(&config,old_selection,strlen(old_selection),&written)==FR_OK);assert(f_close(&config)==FR_OK);
 assert(f_open(&config,"BD09.WAV",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);assert(f_close(&config)==FR_OK);
 settings_load();assert(selected_sample[0]==1&&selected_sample[1]==1&&selected_sample[2]==1);
 assert(sample_choice(0,255)==1&&!sample_select(0,9)&&!sample_select(0,16));
 /* Copy a real WAV to a sparse numbered slot, select, save and reload. */
 FIL source,dest;unsigned char block[512];UINT got;
 assert(f_open(&source,"BD01.WAV",FA_READ)==FR_OK);assert(f_open(&dest,"BD08.WAV",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 do {assert(f_read(&source,block,sizeof(block),&got)==FR_OK);assert(f_write(&dest,block,got,&written)==FR_OK&&written==got);}while(got);
 assert(f_close(&source)==FR_OK&&f_close(&dest)==FR_OK);
 settings_load();assert(sample_choice(0,0)==1&&sample_choice(0,255)==8);
 assert(sample_select(0,8)&&selections_save());settings_load();assert(selected_sample[0]==8&&drum_channel[0]==9);
 assert(!sample_select(0,2));
 assert(f_open(&dest,"BD02.WAV",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 const char bad_wav[]="invalid";assert(f_write(&dest,bad_wav,sizeof(bad_wav),&written)==FR_OK);assert(f_close(&dest)==FR_OK);
 settings_load();assert(!sample_select(0,2)&&selected_sample[0]==8&&sample_frames[0]>0);
 /* A valid candidate must not silently mute another active voice when RAM fills. */
 unsigned char large_header[44]={'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',0,0,0,0};
 unsigned pcm_bytes=120000,riff_bytes=pcm_bytes+36;
 for(unsigned b=0;b<4;b++){large_header[4+b]=(riff_bytes>>(8*b))&255;large_header[40+b]=(pcm_bytes>>(8*b))&255;}
 assert(f_open(&dest,"BD02.WAV",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 assert(f_write(&dest,large_header,sizeof(large_header),&written)==FR_OK);memset(block,0,sizeof(block));
 for(unsigned left=pcm_bytes;left;){unsigned count=left>sizeof(block)?sizeof(block):left;assert(f_write(&dest,block,count,&written)==FR_OK&&written==count);left-=count;}
 assert(f_close(&dest)==FR_OK);settings_load();
 assert(!sample_select(0,2)&&selected_sample[0]==8&&sample_frames[0]>0&&sample_frames[1]>0&&sample_frames[2]>0);
 assert(f_unlink("BD02.WAV")==FR_OK);assert(f_unlink("BD08.WAV")==FR_OK);settings_load();assert(selected_sample[0]==1);
 unsigned original=sample_frames[0];storage_init();assert(samples_load()&&sample_frames[0]==original);
 assert(storage_begin_edit()&&storage_edit_active()&&storage_host_ready());assert(!samples_load());assert(!storage_format_factory());
 assert(storage_host_eject()&&!storage_host_ready());storage_service_remount();assert(!storage_edit_active()&&storage_reload_pending());assert(samples_load());
 FIL f;UINT n;assert(f_open(&f,"BD01.WAV",FA_WRITE|FA_OPEN_EXISTING)==FR_OK);unsigned char bad[12]={0};assert(f_write(&f,bad,12,&n)==FR_OK&&n==12);assert(f_close(&f)==FR_OK);assert(!samples_load()&&sample_frames[0]==0&&sample_frames[1]>0);
 /* Nonblank damaged media must be preserved, never autoformatted. */
 memset(disk,0,sizeof(disk));storage_init();assert(samples_load()&&sample_frames[0]==original);for(unsigned i=0;i<sizeof(disk);i++)assert(disk[i]==0);
 assert(storage_format_factory()&&samples_load());
 assert(f_open(&f,"BD01.WAV",FA_WRITE|FA_OPEN_EXISTING)==FR_OK);
 assert(f_lseek(&f,22)==FR_OK);unsigned char stereo[2]={2,0};assert(f_write(&f,stereo,2,&n)==FR_OK);assert(f_close(&f)==FR_OK);
 assert(!samples_load()&&sample_frames[0]==0&&sample_frames[1]>0);
 assert(storage_format_factory()&&samples_load());
 /* Valid-looking but oversized mono PCM must not overrun the RAM arena. */
 unsigned char header[44]={'R','I','F','F',0x26,0,2,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,1,0,0x44,0xac,0,0,0x88,0x58,1,0,2,0,16,0,'d','a','t','a',2,0,2,0};
 assert(f_open(&f,"BD01.WAV",FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
 assert(f_write(&f,header,sizeof(header),&n)==FR_OK);
 unsigned char zeros[512]={0};unsigned remaining=131074;
 while(remaining){unsigned count=remaining>512?512:remaining;assert(f_write(&f,zeros,count,&n)==FR_OK&&n==count);remaining-=count;}
 assert(f_close(&f)==FR_OK);assert(!samples_load()&&sample_frames[0]==0&&sample_frames[1]>0);
 assert(f_unlink("SD01.WAV")==FR_OK);assert(!samples_load()&&sample_frames[1]==0&&sample_frames[2]>0);
 assert(storage_format_factory()&&samples_load());
 puts("Storage provisioning, ownership, reload, recovery and WAV rejection tests passed");
}
