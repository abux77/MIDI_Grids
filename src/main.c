#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/adc.h"
#include "tusb.h"
#include "grids.h"
#include "transport.h"
#include "storage.h"
#include "audio.h"
bool usb_midi_enabled;
static grids_t engine={.x=128,.y=128,.density={128,128,128},.rng=1};
static transport_t transport={.division=1};
static unsigned bpm=120,held_notes;
static bool selecting, selection_used, selection_dirty, ignore_release;
static unsigned selection_anchor[3], density_anchor[3];
static bool density_pickup[3];
static unsigned attempted_sample[3];
static void save_selection(void) {
 if(!selection_dirty)return;
 audio_suspend();bool ok=selections_save();audio_resume();
 if(ok)selection_dirty=false;
 printf("Sample selections %s.\n",ok?"saved":"could not be saved");
}
static uint64_t led_until[4],test_note_until;
/* Bounded queues avoid blocking the audio service on UART/USB writes. */
static uint8_t din_queue[512],usb_queue[128][4];
static unsigned din_head,din_tail,usb_head,usb_tail,dropped;
static void send(const uint8_t *data,unsigned n){
 if((din_head-din_tail)+n<=512){for(unsigned i=0;i<n;i++)din_queue[din_head++&511]=data[i];}else dropped++;
 if(usb_midi_enabled&&tud_midi_mounted()){
  if(usb_head-usb_tail<128){uint8_t *p=usb_queue[usb_head++&127];p[0]=n==1?15:data[0]>>4;p[1]=data[0];p[2]=n>1?data[1]:0;p[3]=n>2?data[2]:0;}else dropped++;
 }
}
static void realtime(uint8_t b){send(&b,1);}
static void note(unsigned p,unsigned velocity){uint8_t b[3]={0x90|drum_channel[p],drum_note[p],velocity};send(b,3);}
static void output_service(void){
 while(din_tail!=din_head&&uart_is_writable(uart0))uart_putc_raw(uart0,din_queue[din_tail++&511]);
 if(!tud_midi_mounted()){usb_tail=usb_head;return;}
 while(usb_tail!=usb_head){if(!tud_midi_packet_write(usb_queue[usb_tail&127]))break;usb_tail++;}
}
static void release_notes(void){for(unsigned p=0;p<3;p++)if(held_notes&(1u<<p))note(p,0);held_notes=0;}
static void stop_sound(void){release_notes();audio_stop();for(unsigned p=0;p<4;p++){gpio_put(6+p,0);led_until[p]=0;}}
static void tick(uint64_t now){
 release_notes();
 if(!transport.count&&!transport.phase){gpio_put(9,1);led_until[3]=now+35000;}
 if(transport_step_due(&transport)){
  uint8_t accents,hits=grids_step(&engine,&accents);
  for(unsigned p=0;p<3;p++)if(hits&(1u<<p)){unsigned v=(accents&(1u<<p))?127:100;note(p,v);held_notes|=1u<<p;audio_trigger(p,v);gpio_put(6+p,1);led_until[p]=now+35000;}
 }
 
}
static void receive(unsigned source,uint8_t b,uint64_t now){
 if(storage_edit_active())return;
 b=transport_receive(&transport,source,b,now);if(!b)return;
 if(b==0xfa||b==0xfb){save_selection();if(selecting){ignore_release=true;for(unsigned p=0;p<3;p++)density_pickup[p]=true;}selecting=false;}
 if(b==0xfa){stop_sound();grids_reset(&engine);}
 if(b==0xfc)stop_sound();
 if(b!=0xf8||transport.running)realtime(b);
 if(b==0xf8&&transport.running)tick(now);
}
static unsigned filtered[7],raw_pots[7];static bool initialized[7];
static unsigned pot_u8(unsigned r){return r<=16?0:r>=4079?255:(r*255+2047)/4095;}
static void controls_service(uint64_t now){
 static uint64_t due;static unsigned p;
 if(now<due)return;
 due=now+1000;
 for(unsigned b=0;b<4;b++)gpio_put(16+b,(p>>b)&1);
 busy_wait_us_32(10);for(unsigned d=0;d<3;d++)adc_read();unsigned r=adc_read();
 if(!initialized[p]){filtered[p]=r<<2;raw_pots[p]=r;initialized[p]=true;}else{filtered[p]=filtered[p]-(filtered[p]>>2)+r;r=filtered[p]>>2;if(r>raw_pots[p]+2||raw_pots[p]>r+2)raw_pots[p]=r;}
 unsigned clockwise_raw=4095-raw_pots[p];
 r=pot_u8(clockwise_raw);
 if(p==0)engine.x=r;else if(p==1)engine.y=r;else if(p<5){
  unsigned part=p-2;
  if(selecting&&!transport.running){
   unsigned anchor=selection_anchor[part];
   if(r>anchor+5||anchor>r+5){
    selection_anchor[part]=r;
    unsigned choice=sample_choice(part,r);
    selection_used=true;
    if(choice&&choice!=attempted_sample[part]){
     attempted_sample[part]=choice;
     audio_suspend();bool ok=sample_select(part,choice);audio_resume();
     if(ok){selection_dirty=true;audio_trigger(part,100);gpio_put(6+part,1);led_until[part]=now+100000;
      printf("%s%02u.WAV preview\n",part==0?"BD":part==1?"SD":"HH",choice);}
    }
   }
  }else if(density_pickup[part]){
   unsigned target=density_anchor[part];
   if((r>=target? r-target:target-r)<=5 || (selection_anchor[part]<target&&r>=target) || (selection_anchor[part]>target&&r<=target)){
    density_pickup[part]=false;engine.density[part]=r;
   }
   selection_anchor[part]=r;
  }else engine.density[part]=r;
 }else if(p==5)engine.chaos=r;
 else{bpm=40+(clockwise_raw*200+2047)/4095;
  /* Hysteresis around the third boundaries prevents noisy division changes. */
  if(transport.division==1&&r<165)transport.division=r<80?4:2;
  else if(transport.division==2){if(r>175)transport.division=1;else if(r<80)transport.division=4;}
  else if(transport.division==4&&r>90)transport.division=r>175?1:2;
 }
 p=(p+1)%7;
}
static void button_service(uint64_t now){
 static bool previous=true,stable=true;static uint64_t changed;
 bool value=gpio_get(10);if(value!=previous){previous=value;changed=now;}
 if(value==stable||now-changed<30000)return;
 stable=value;
 if(!value){
  if(ignore_release||storage_edit_active())return;
  selection_used=false;
  selecting=!transport.running;
  if(selecting)for(unsigned p=0;p<3;p++){
   attempted_sample[p]=0;selection_anchor[p]=pot_u8(4095-raw_pots[p+2]);density_anchor[p]=engine.density[p];
  }
 }else{
  if(ignore_release){ignore_release=false;selecting=false;return;}
  selecting=false;
  if(storage_edit_active())return;
  if(selection_used){
   for(unsigned p=0;p<3;p++)density_pickup[p]=true;
   save_selection();return;
  }
  transport.running=!transport.running;
  if(transport.running){stop_sound();grids_reset(&engine);transport.phase=transport.count=0;transport.next_tick=now+60000000/(bpm*24);realtime(0xfa);}else{stop_sound();realtime(0xfc);}
 }
}
static void console_service(void) {
    static char command[32];
    static unsigned length;
    unsigned budget = 64;
    while (budget-- && tud_cdc_available()) {
        char c;
        if (tud_cdc_read(&c, 1) != 1) break;
        if (c == '\r' || c == '\n') {
            command[length] = 0;
            if (!strcmp(command, "status")) {
                printf("%s, %u BPM, clock source %u, division %u, dropped MIDI %u\n",
                       storage_edit_active() ? "Sample editing" : transport.running ? "Playing" : "Stopped",
                       bpm, transport.source, transport.division, dropped);
                printf("USB MIDI: %s; CONFIG.TXT: %s%s\n",usb_midi_enabled?"ON":"OFF",configured_usb_midi?"ON":"OFF",usb_midi_enabled!=configured_usb_midi?" (reboot to apply)":"");
                for(unsigned p=0;p<3;p++)printf("%s: CH%u N%u sample %02u\n",p==0?"BD":p==1?"SD":"HH",drum_channel[p]+1,drum_note[p],selected_sample[p]);
                printf("Samples: %s, frames kick=%u snare=%u hat=%u; audio %s\n",
                       storage_is_ready()?"flash files":"factory fallback",sample_frames[0],sample_frames[1],sample_frames[2],audio_is_active()?"running":"suspended");
                printf("ADC raw X/Y/K/S/H/Chaos/Tempo: %u %u %u %u %u %u %u\n",
                       raw_pots[0],raw_pots[1],raw_pots[2],raw_pots[3],raw_pots[4],raw_pots[5],raw_pots[6]);
            } else if (!strcmp(command,"samples")) {
                samples_report();
            } else if (!strcmp(command,"test")) {
                if(storage_edit_active()) printf("Eject the sample drive first.\n");
                else {note(0,100);held_notes|=1;test_note_until=time_us_64()+100000;audio_trigger(0,100);printf("Kick test on MIDI channel %u.\n",drum_channel[0]+1);}
            } else if (!strcmp(command, "format")) {
                if (storage_edit_active()) printf("Eject the sample drive before formatting.\n");
                else {
                    if (transport.running) realtime(0xfc);
                    transport.running = false;
                    stop_sound();
                    audio_suspend();
                    bool ok = storage_format_factory();
                    settings_load();samples_load();
                    audio_resume();
                    printf("Factory sample format: %s\n", ok ? "complete" : "failed");
                }
            } else if (length) printf("Commands: status; samples; test; format (ERASES sample partition and restores three factory WAVs).\n");
            length = 0;
        } else if ((c == 8 || c == 127) && length) --length;
        else if (c >= 32 && c < 127 && length + 1 < sizeof(command)) command[length++] = c;
    }
}

int main(void){
 for(unsigned p=6;p<=9;p++){gpio_init(p);gpio_set_dir(p,true);}
 gpio_init(10);gpio_pull_up(10);sleep_ms(2);bool boot_storage=!gpio_get(10);ignore_release=boot_storage;
 for(unsigned p=16;p<=19;p++){gpio_init(p);gpio_set_dir(p,true);}
 adc_init();adc_gpio_init(26);adc_select_input(0);
 uart_init(uart0,31250);gpio_set_function(0,GPIO_FUNC_UART);gpio_set_function(1,GPIO_FUNC_UART);uart_set_format(uart0,8,1,UART_PARITY_NONE);uart_set_fifo_enabled(uart0,true);
 /* Choose descriptors before USB initialization; keep that choice until reboot. */
 storage_init();settings_load();usb_midi_enabled=configured_usb_midi;
 tusb_init();stdio_init_all();samples_load();audio_init();
 if(boot_storage){audio_suspend();if(storage_begin_edit())printf("Sample drive active. Safely eject to reload.\n");else audio_resume();}
 printf("Pico MIDI Grids ready, stopped. DIN RX GP1. MIDI channel %u.\n",drum_channel[0]+1);
 while(true){
  uint64_t now=time_us_64();audio_service();tud_task();
  while(uart_is_readable(uart0))receive(1,uart_getc(uart0),time_us_64());
  uint8_t packet[4];unsigned budget=32;
  while(usb_midi_enabled&&budget--&&tud_midi_packet_read(packet)){
   /* Only realtime CIN; ignore notes, SysEx and other channel messages. */
   if((packet[0]&15)==15)receive(2,packet[1],time_us_64());
  }
  output_service();console_service();storage_service_remount();
  if(test_note_until && time_us_64()>=test_note_until){release_notes();test_note_until=0;}
  if(storage_reload_pending()){release_notes();settings_load();samples_load();transport.source=0;transport.clock_seen=false;audio_resume();printf("Samples reloaded. Press Start to play.\n");if(configured_usb_midi!=usb_midi_enabled)printf("USB MIDI config changed; reboot to apply.\n");}
  button_service(now);
  if(storage_edit_active()){sleep_us(100);continue;}
  controls_service(now);
  unsigned period=60000000/(bpm*24);
  if(transport_timeout(&transport,now,period))printf("Clock lost; internal tempo %u BPM\n",bpm);
  if(transport.running&&!transport.source&&now>=transport.next_tick){realtime(0xf8);tick(now);transport.next_tick+=period;if(now>=transport.next_tick)transport.next_tick=now+period;}
  for(unsigned p=0;p<4;p++)if(led_until[p]&&now>=led_until[p]){gpio_put(6+p,0);led_until[p]=0;}
  audio_service();
 }
}
