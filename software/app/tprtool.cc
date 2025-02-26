
#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>

#include "tpr.hh"

#include <string>

using namespace Tpr;

enum TimingMode { LCLS1=0, LCLS2=1, UED=2, None=3 };

extern int optind;

static void usage(const char* p) {
    printf("Usage: %s [options]\n",p);
    printf("          -d <dev>  : <tpr a/b>\n");
    printf("          -1        : enable LCLS-I  timing\n");
    printf("          -2        : enable LCLS-II timing\n");
    printf("          -R        : reset Rx\n");
    printf("          -x <0|1>  : set EVR output loopback (1)\n");
    printf("          -X <0|1>  : set TPR output loopback (1)\n");
}

int main(int argc, char** argv) {

    extern char* optarg;
    char tprid='a';

    int c;
    bool lUsage = false;

    TimingMode tmode = None;
    bool lResetRx = false;
    int  lEvrLoop = -1;
    int  lTprLoop = -1;

    while ( (c=getopt( argc, argv, "d:12Rx:X:h?")) != EOF ) {
        switch(c) {
        case '1': tmode = LCLS1; break;
        case '2': tmode = LCLS2; break;
        case 'R': lResetRx = true; break;
        case 'd':
            tprid  = optarg[0];
            if (strlen(optarg) != 1) {
                printf("%s: option `-r' parsing error\n", argv[0]);
                lUsage = true;
            }
            break;
	case 'x': lEvrLoop = strtoul(optarg,NULL,0); break;
	case 'X': lTprLoop = strtoul(optarg,NULL,0); break;
        case 'h':
            usage(argv[0]);
            exit(0);
        case '?':
        default:
            lUsage = true;
            break;
        }
    }

    if (optind < argc) {
        printf("%s: invalid argument -- %s\n",argv[0], argv[optind]);
        lUsage = true;
    }

    if (lUsage) {
        usage(argv[0]);
        exit(1);
    }

    {
        TprReg* p = reinterpret_cast<TprReg*>(0);
        printf("version @%p\n",&p->version);
        printf("xbar    @%p\n",&p->xbar);
        printf("base    @%p\n",&p->base);
        printf("tpr     @%p\n",&p->tpr);
        printf("tpg     @%p\n",&p->tpg);
        printf("RxRecClks[%p]\n",&p->tpr.RxRecClks);
    }

    {
        char evrdev[16];
        sprintf(evrdev,"/dev/tpr%c",tprid);
        printf("Using tpr %s\n",evrdev);

        int fd = open(evrdev, O_RDWR);
        if (fd<0) {
            perror("Could not open");
            return -1;
        }

        void* ptr = mmap(0, sizeof(TprReg), PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
        if (ptr == MAP_FAILED) {
            perror("Failed to map");
            return -2;
        }

        TprReg& reg = *reinterpret_cast<TprReg*>(ptr);
        printf("FpgaVersion: %08X\n", reg.version.FpgaVersion);
        printf("BuildStamp: %s\n", reg.version.buildStamp().c_str());

        //reg.xbar.setEvr( XBar::StraightIn );
	if (lEvrLoop>=0)
	  reg.xbar.setEvr( lEvrLoop==0 ? XBar::StraightOut : XBar::LoopOut);
        //reg.xbar.setTpr( XBar::StraightIn );
	if (lTprLoop>=0)
	  reg.xbar.setTpr( lTprLoop==0 ? XBar::StraightOut : XBar::LoopOut);

	if (lResetRx)
	  reg.tpr.resetRx();

	if (tmode != None) {
	  reg.tpr.clkSel(tmode==LCLS2);
	  // modeSel chooses the protocol
	  reg.tpr.modeSel(tmode!=LCLS1);
	  reg.tpr.modeSelEn(true);
	}
    }
    return 0;
}

