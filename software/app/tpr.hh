#ifndef TPR_HH
#define TPR_HH

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <math.h>

namespace Tpr {
  //
  //  Interface to DMA engine
  //
  class RxDesc {
  public:
    RxDesc(uint32_t* d, unsigned sz) : maxSize(sz), data(d) {}
  public:
    uint32_t  maxSize;
    uint32_t* data;
  };

  //
  //  Firmware registers
  //
  class AxiVersion {
  public:
    std::string buildStamp() const;
  public:
    volatile uint32_t FpgaVersion;
    volatile uint32_t ScratchPad;
    volatile uint32_t DeviceDnaHigh;
    volatile uint32_t DeviceDnaLow;
    volatile uint32_t FdSerialHigh;
    volatile uint32_t FdSerialLow;
    volatile uint32_t MasterReset;
    volatile uint32_t FpgaReload;
    volatile uint32_t FpgaReloadAddress;
    volatile uint32_t Counter;
    volatile uint32_t FpgaReloadHalt;
    volatile uint32_t reserved_11[0x100-11];
    volatile uint32_t UserConstants[64];
    volatile uint32_t reserved_0x140[0x200-0x140];
    volatile uint32_t BuildStamp[64];
    volatile uint32_t reserved_0x240[0x4000-0x240];
  };

  class DebugBridge {
  public:
    volatile uint32_t length;
    volatile uint32_t tms_vector;
    volatile uint32_t tdi_vector;
    volatile uint32_t tdo_vector;
    volatile uint32_t ctrl;
  };

  class XBar {
  public:
    enum InMode  { StraightIn , LoopIn };
    enum OutMode { StraightOut, LoopOut };
    void setEvr( InMode  m );
    void setEvr( OutMode m );
    void setTpr( InMode  m );
    void setTpr( OutMode m );
    void dump() const;
  public:
    volatile uint32_t outMap[4];
  };

  class TprCsr {
  public:
    void setupDma    (unsigned fullThr=0x3f2);
    void enableRefClk(bool);
    void dump        () const;
  public:
    volatile uint32_t irqEnable;
    volatile uint32_t irqStatus;
    volatile uint32_t partitionAddr;
    volatile uint32_t dmaCount;
    volatile uint32_t countReset;
    volatile uint32_t trigMaster;
    volatile uint32_t dmaFullThr;
    volatile uint32_t dmaDrops;
  };

  class ClockManager {
  public:
    void clkSel     (bool lcls2);
    void dump       () const;
  public:
    volatile uint32_t reg[256];
  private:
    class ClkReg1 {
    public:
      ClkReg1(double div) : phase_mux(0) {
        low_time  = int(div/2);
        high_time = int(div-low_time);
      }
      void write(uint32_t* p) {
        rsvd = (*p >>12)&1;
        printf("Writing %04x to %p\n",*reinterpret_cast<uint32_t*>(this),p);
        //*p = *reinterpret_cast<uint32_t*>(this);
      }
    public:
      unsigned low_time:6;
      unsigned high_time:6;
      unsigned rsvd:1;
      unsigned phase_mux:3;
    };
    class ClkReg2 {
    public:
      ClkReg2(double div) : delay_time(0), mx(0) {
        no_count  = div < 3;
        edge      = int(div)%2;
        frac      = int(drem(div,1.0)*8.+0.5);
        frac_en   = (frac != 0);
        frac_wf_r = frac_en;
      }
      void write(uint32_t* p) {
        rsvd = (*p >>15)&1;
        printf("Writing %04x to %p\n",*reinterpret_cast<uint32_t*>(this),p);
        //*p = *reinterpret_cast<uint32_t*>(this);
      }
    public:
      unsigned delay_time:6;
      unsigned no_count:1;
      unsigned edge:1;
      unsigned mx:2;
      unsigned frac_wf_r:1;
      unsigned frac_en:1;
      unsigned frac:3;
      unsigned rsvd:1;
    };
  };

  class TrgMon {
  public:
    enum { NTRIGGERS=12 };
  public:
    void dump() const;
  public:
    volatile uint32_t reset;
    volatile uint32_t reserved;
    struct {
      volatile uint32_t periodMin;
      volatile uint32_t periodMax;
    } trigger[NTRIGGERS];
  };
  
  class TprBase {
  public:
    enum { NCHANNELS=14 };
    enum { NTRIGGERS=12 };
    enum Destination { Any };
    enum FixedRate { _1M, _71K, _10K, _1K, _100H, _10H, _1H };
    enum ACRate    { _60HA, _30HA, _10HA, _5HA, _1HA, _0_5HA };
    enum EventCode { _0, _1 };
  public:
    void dump() const;
    void setupDma    (unsigned fullThr=0x3f2);
    void setupDaq    (unsigned i,
                      unsigned partition);
    void setupChannel(unsigned i,
                      Destination d,
                      FixedRate   r,
                      unsigned    bsaPresample,
                      unsigned    bsaDelay,
                      unsigned    bsaWidth);
    void setupChannel(unsigned i,
                      Destination d,
                      ACRate      r,
                      unsigned    timeSlotMask,
                      unsigned    bsaPresample,
                      unsigned    bsaDelay,
                      unsigned    bsaWidth);
    void setupChannel(unsigned i,
                      EventCode   r,
                      unsigned    bsaPresample,
                      unsigned    bsaDelay,
                      unsigned    bsaWidth);
    void setupTrigger(unsigned i,
                      unsigned source,
                      unsigned polarity,
                      unsigned delay,
                      unsigned width,
                      unsigned delayTap=0);
  public:
    struct {
      volatile uint32_t control;
      volatile uint32_t evtSel;
      volatile uint32_t evtCount;
      volatile uint32_t bsaDelay;
      volatile uint32_t bsaWidth;
      volatile uint32_t bsaCount; // not implemented
      volatile uint32_t bsaData;  // not implemented
      volatile uint32_t reserved[0x3f9];
    } channel[NCHANNELS];
    volatile uint32_t reserved_20[2];
    volatile uint32_t frameCount;
    volatile uint32_t reserved_2C[2];
    volatile uint32_t bsaCntlCount; // not implemented
    volatile uint32_t bsaCntlData;  // not implemented
    volatile uint32_t reserved_b[0x3f9+0x400*(31-NCHANNELS)];
    struct {
      volatile uint32_t control; // input, polarity, enabled
      volatile uint32_t delay;
      volatile uint32_t width;
      volatile uint32_t delayTap;
      volatile uint32_t reserved[0x3fc];
    } trigger[NTRIGGERS];
  };

  class DmaControl {
  public:
    void dump() const;
    void test();
    void setEmptyThr(unsigned);
  public:
    volatile uint32_t rxFree;
    volatile uint32_t reserved_4[15];
    volatile uint32_t rxFreeStat;
    volatile uint32_t reserved_14[47];
    volatile uint32_t rxMaxFrame;
    volatile uint32_t rxFifoSize;
    volatile uint32_t rxCount;
    volatile uint32_t lastDesc;
  };

  class TprCore {
  public:
    bool clkSel     () const;
    void clkSel     (bool lcls2);
    bool modeSelEn  () const;
    void modeSelEn  (bool);
    bool modeSel    () const;
    void modeSel    (bool lcls2);
    bool rxPolarity () const;
    void rxPolarity (bool p);
    void resetRx    ();
    void resetRxPll ();
    void resetCounts();
    bool vsnErr     () const;
    void dump() const;
  public:
    volatile uint32_t SOFcounts;
    volatile uint32_t EOFcounts;
    volatile uint32_t Msgcounts;
    volatile uint32_t CRCerrors;
    volatile uint32_t RxRecClks;
    volatile uint32_t RxRstDone;
    volatile uint32_t RxDecErrs;
    volatile uint32_t RxDspErrs;
    volatile uint32_t CSR;
    uint32_t          reserved;
    volatile uint32_t TxRefClks;
    volatile uint32_t BypassCnts;
    volatile uint32_t FrameVersion;
  };

  class RingB {
  public:
    void enable(bool l);
    void clear ();
    void dump(const char* fmt="%05x") const;
    void dumpFrames() const;
  public:
    volatile uint32_t csr;
    volatile uint32_t data[0x1fff];
  };

  class TpgMini {
  public:
    void setBsa(unsigned rate,
                unsigned ntoavg, unsigned navg);
    void dump() const;
  public:
    volatile uint32_t ClkSel;
    volatile uint32_t BaseCntl;
    volatile uint32_t PulseIdU;
    volatile uint32_t PulseIdL;
    volatile uint32_t TStampU;
    volatile uint32_t TStampL;
    volatile uint32_t FixedRate[10];
    volatile uint32_t RateReload;
    volatile uint32_t HistoryCntl;
    volatile uint32_t FwVersion;
    volatile uint32_t Resources;
    volatile uint32_t BsaCompleteU;
    volatile uint32_t BsaCompleteL;
    volatile uint32_t reserved_22[128-22];
    struct {
      volatile uint32_t l;
      volatile uint32_t h;
    } BsaDef[64];
    volatile uint32_t reserved_256[320-256];
    volatile uint32_t CntPLL;
    volatile uint32_t Cnt186M;
    volatile uint32_t reserved_322;
    volatile uint32_t CntIntvl;
    volatile uint32_t CntBRT;
  };

  //
  // Memory map of TPR registers (EvrCardG2 BAR 1)
  //
  class TprReg {
  public:
    uint32_t     reserved_0    [(0x10000)>>2];
    AxiVersion   version;  // 0x00010000
    uint32_t     reserved_10000[(0x30000-0x20000)>>2];  // boot_mem is here
    DebugBridge  debug;    // 0x00030000
    uint32_t     reserved_30000[(0x10000-sizeof(debug))>>2];
    XBar         xbar;     // 0x00040000
    uint32_t     reserved_30010[(0x60000-0x40010)>>2];
    TprCsr       csr;      // 0x00060000
    uint32_t     reserved_60400[(0x400-sizeof(TprCsr))/4];
    DmaControl   dma;      // 0x00060400
    uint32_t     reserved_78000[(0x17C00-sizeof(DmaControl))/4];
    ClockManager refclk;     // 0x00078000
    uint32_t     reserved_7E000[(0x06000-sizeof(ClockManager))/4];
    TrgMon       trgmon;     // 0x0007E000
    uint32_t     reserved_80000[(0x02000-sizeof(TrgMon))/4];
    TprBase      base;     // 0x00080000
    uint32_t     reserved_C0000[(0x40000-sizeof(TprBase))/4];
    TprCore      tpr;      // 0x000C0000
    uint32_t     reserved_tpr  [(0x10000-sizeof(TprCore))/4];
    RingB        ring0;    // 0x000D0000
    uint32_t     reserved_ring0[(0x10000-sizeof(RingB))/4];
    RingB        ring1;    // 0x000E0000
    uint32_t     reserved_ring1[(0x10000-sizeof(RingB))/4];
    TpgMini      tpg;      // 0x000F0000
  };
};

#endif
