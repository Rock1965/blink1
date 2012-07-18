/* 
 * blinkmusb-tool.c --
 *
 * 2012, Tod E. Kurt, http://todbot.com/blog/ , http://thingm.com/
 *
 *
 * Fade to RGB value #FFCC33 in 50 msec:
 * ./blink1-tool --hidwrite 0x63,0xff,0xcc,0x33,0x00,0x32
 * ./blink1-tool -m 50 -rgb 0xff,0xcc,0x33
 *
 * Read EEPROM position 1:
 * ./blink1-tool --hidwrite 0x65,0x01 && ./blink1-tool --hidread
 * ./blink1-tool --eeread 1
 *
 * Write color pattern entry {#EEDD44, 50} to position 2
 * ./blink1-tool --hidwrite 0x50,0xee,0xdd,0x44,0x00,0x32,0x2
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <getopt.h>    // for getopt_long()
#include <unistd.h>    // for usleep()

#include "blink1-lib.h"


int millis = 300;
int delayMillis = 1000;
int multiMillis = 1000;
int numDevicesToUse = 1;

usbDevice_t *dev;
char  deviceNums[blink1_max_devices];

char  cmdbuf[9];    // room at front for reportID
int verbose;


// local states for the "cmd" option variable
enum { 
    CMD_NONE = 0,
    CMD_LIST,
    CMD_HIDREAD,
    CMD_HIDWRITE,
    CMD_EEREAD,
    CMD_EEWRITE,
    CMD_RGB,
    CMD_OFF,
    CMD_ON,
    CMD_RANDOM,
    CMD_VERSION,
};
//---------------------------------------------------------------------------- 

// a simple logarithmic -> linear mapping as a sort of gamma correction
// maps from 0-255 to 0-255
static int log2lin( int n )  
{
  //return  (int)(1.0* (n * 0.707 ));  // 1/sqrt(2)
  return (((1<<(n/32))-1) + ((1<<(n/32))*((n%32)+1)+15)/32);
}

//
static void hexdump(char *buffer, int len)
{
int     i;
FILE    *fp = stdout;

    for(i = 0; i < len; i++){
        if(i != 0){
            if(i % 16 == 0){
                fprintf(fp, "\n");
            }else{
                fprintf(fp, " ");
            }
        }
        fprintf(fp, "0x%02x", buffer[i] & 0xff);
    }
    if(i != 0)
        fprintf(fp, "\n");
}

//
static int  hexread(char *buffer, char *string, int buflen)
{
char    *s;
int     pos = 0;

    while((s = strtok(string, ", ")) != NULL && pos < buflen){
        string = NULL;
        buffer[pos++] = (char)strtol(s, NULL, 0);
    }
    return pos;
}

// --------------------------------------------------------------------------- 

//
static void usage(char *myName)
{
    fprintf(stderr,
"Usage: \n"
"  %s <cmd> [options]\n"
"where <cmd> is one of:\n"
"  --hidread \n"
"  --hidwrite <listofbytes> \n"
"  --eeread <addr> \n"
"  --eewrite <addr>,<val> \n"
"  --blink <numtimes> \n"
"  --random <numtimes> \n"
"  --rgb <red>,<green>,<blue> \n"
"  --on \n"
"  --off \n"
"  --list \n"
"and [options] are: \n"
"  -d dNum  --device=deviceNum  blink(1) device number (from --list) \n"
"  -m ms,   --miilis=millis     Set millisecs for color fading (default 300)\n"
"  -t ms,   --delay=millis      Set millisecs between events (default 500)\n"
"  --vid=vid --pid=pid          Specifcy alternate USB VID & PID\n"
"  -v, --verbose                verbose debugging msgs\n"
            ,myName);
}

//
int main(int argc, char** argv)
{
    int openall = 0;
    //int blink1_count;
    int16_t arg=0;
    static int vid=0, pid=0;
    int  rc;
    static int cmd  = CMD_NONE;

    // parse options
    int option_index = 0, opt;
    char* opt_str = "avm:t:d:U:u:";
    static struct option loptions[] = {
        {"all",        no_argument,       0,      'a'},
        {"verbose",    optional_argument, 0,      'v'},
        {"millis",     required_argument, 0,      'm'},
        {"delay",      required_argument, 0,      't'},
        {"device",     required_argument, 0,      'd'},
        {"list",       no_argument,       &cmd,   CMD_LIST },
        {"hidread",    no_argument,       &cmd,   CMD_HIDREAD },
        {"hidwrite",   required_argument, &cmd,   CMD_HIDWRITE },
        {"eeread",     required_argument, &cmd,   CMD_EEREAD },
        {"eewrite",    required_argument, &cmd,   CMD_EEWRITE },
        {"rgb",        required_argument, &cmd,   CMD_RGB },
        {"off",        no_argument,       &cmd,   CMD_OFF },
        {"on",         no_argument,       &cmd,   CMD_ON },
        {"random",     required_argument, &cmd,   CMD_RANDOM },
        {"version",    no_argument,       &cmd,   CMD_VERSION },
        {"vid",        required_argument, 0,      'U'}, // FIXME: This sucks
        {"pid",        required_argument, 0,      'u'},
        {NULL,         0,                 0,      0}
    };
    while(1) {
        opt = getopt_long(argc, argv, opt_str, loptions, &option_index);
        if (opt==-1) break; // parsed all the args
        switch (opt) {
         case 0:             // deal with long opts that have no short opts
            switch(cmd) { 
            case CMD_HIDWRITE:
            case CMD_EEREAD:
            case CMD_EEWRITE:
            case CMD_RGB:
                hexread(cmdbuf, optarg, sizeof(cmdbuf));  // cmd w/ hexlist arg
                break;
            case CMD_RANDOM:
                  if( optarg ) 
                    arg = strtol(optarg,NULL,0);   // cmd w/ number arg
                break;
            }
            break;
        case 'a':
            openall = 1;
            break;
        case 'm':
            millis = strtol(optarg,NULL,10);
            break;
        case 't':
            delayMillis = strtol(optarg,NULL,10);
            break;
        case 'v':
            if( optarg==NULL ) verbose++;
            else verbose = strtol(optarg,NULL,0);
            break;
        case 'd':
            hexread(deviceNums, optarg, sizeof(deviceNums));
            break;
        case 'U': 
            vid = strtol(optarg,NULL,0);
            break;
        case 'u':
            pid = strtol(optarg,NULL,0);
            break;
        }
    }

    //printf("deviceNums: ");
    //hexdump(deviceNums, sizeof(deviceNums));

    if(argc < 2){
        usage( "blink1-tool" );
        exit(1);
    }

    if( vid!=0 && pid!=0 ) {
        printf("using alternate VID/PID: 0x%4.4x/0x%4.4x\n", vid,pid);
        rc = blink1_openall_byid( vid,pid );
    } else {
        rc = blink1_openall();
    }
    if( rc != 0 ) { 
        fprintf(stderr, "no devices found\n");
        //exit(1);
    }

    if(verbose) {
        for( int i=0; i<4; i++ ) {
            fprintf(stderr,"device:%p\n", (void*)blink1_getDevice(i));
        }
    }

    dev = blink1_getDevice( deviceNums[0] ); // FIXME:

    if( cmd == CMD_LIST ) { 
        printf("blink(1) list: \n");
        for( int i=0; i< blink1_max_devices; i++ ) { 
            usbDevice_t* d = blink1_getDevice(i);
            if( d!=NULL ) printf("%d: %x\n", i, (uint32_t)d);
        }
    }
    else if( cmd == CMD_HIDREAD ) { 
        printf("hidread:  ");
        int len = sizeof(cmdbuf);
        if((rc = usbhidGetReport(dev, 0, cmdbuf, &len)) != 0){
            fprintf(stderr,"error reading data: %s\n",blink1_error_msg(rc));
        } else {
            hexdump(cmdbuf + 1, sizeof(cmdbuf) - 1);
        }
    } 
    else if( cmd == CMD_HIDWRITE ) { 
        printf("hidwrite: "); hexdump(cmdbuf,sizeof(cmdbuf));
        memmove( cmdbuf+1, cmdbuf, sizeof(cmdbuf)-1 );
        cmdbuf[0] = 0; // reportid
        printf("hidwrite: "); hexdump(cmdbuf,sizeof(cmdbuf));
        if((rc = usbhidSetReport(dev, cmdbuf, sizeof(cmdbuf))) != 0) {
            fprintf(stderr,"error writing data: %s\n",blink1_error_msg(rc));
        }
    }
    else if( cmd == CMD_EEREAD ) {  // FIXME
        printf("eeread:  addr 0x%2.2x = ", cmdbuf[0]);
        uint8_t val = 0;
        rc = blink1_eeread(dev, cmdbuf[0], &val );
        if( rc ) { // on error
            printf("error\n");
        } else { 
            printf("%2.2x\n", val);
        }
    }
    else if( cmd == CMD_EEWRITE ) {  // FIXME
        printf("eewrite: \n");
        rc = blink1_eewrite(dev, cmdbuf[0], cmdbuf[1] );
        if( rc ) { // error
        }
    }
    else if( cmd == CMD_VERSION ) { 
        printf("version: ");
        rc = blink1_getVersion(dev);
        printf("%d\n", rc );
    }
    else if( cmd == CMD_RGB ) { 
        uint8_t r = cmdbuf[0];
        uint8_t g = cmdbuf[1];
        uint8_t b = cmdbuf[2];
        printf("setting rgb: 0x%2.2x,0x%2.2x,0x%2.2x\n", r,g,b );
        int rn = log2lin( r );
        int gn = log2lin( g );
        int bn = log2lin( b );

        rc = blink1_fadeToRGB(dev,millis, rn,gn,bn);
        if( rc ) { // on error, do something, anything. come on.
            printf("error on fadeToRGB\n");
        }
    }
    else if( cmd == CMD_ON ) {
        printf("turning on\n");
        rc = blink1_fadeToRGB(dev, millis, 255,255,255);
        if( rc ) // on error, do something, anything. come on.
            printf("error on ON fadeToRGB\n");
    }
    else if( cmd == CMD_OFF ) { 
        printf("turning off\n");
        rc = blink1_fadeToRGB(dev, millis, 0,0,0);
        if( rc ) // on error, do something, anything. come on.
            printf("error on OFF fadeToRGB\n");
    }
    else if( cmd == CMD_RANDOM ) { 
        printf("random %d times: \n", arg);
        for( int i=0; i<arg; i++ ) { 
            uint8_t r = log2lin( (rand()%255) );
            uint8_t g = log2lin( (rand()%255) );
            uint8_t b = log2lin( (rand()%255) );
            printf("%d : %2.2x,%2.2x,%2.2x \n", numDevicesToUse, r,g,b);
            
            for( int i=0; i< numDevicesToUse; i++ ) {
                usbDevice_t* mydev = blink1_getDevice(i);
                rc = blink1_fadeToRGB(mydev, millis,r,g,b);
                if( rc ) { // on error, do something, anything. come on.
                    break;
                }
            }
            usleep( delayMillis * 1000);
        }
    }   

    return 0;
}

