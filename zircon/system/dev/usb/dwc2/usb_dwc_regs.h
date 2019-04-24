// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hwreg/bitfields.h>
#include <zircon/hw/usb.h>

#define MAX_EPS_CHANNELS 16

#define DWC_EP_IN_SHIFT  0
#define DWC_EP_OUT_SHIFT 16

#define DWC_EP_IN_MASK   0x0000ffff
#define DWC_EP_OUT_MASK  0xffff0000

#define DWC_EP_IS_IN(ep)    ((ep) < 16)
#define DWC_EP_IS_OUT(ep)   ((ep) >= 16)

#define DWC_MAX_EPS    32

// converts a USB endpoint address to 0 - 31 index
// in endpoints -> 0 - 15
// out endpoints -> 17 - 31 (16 is unused)
#define DWC_ADDR_TO_INDEX(addr) (uint8_t)(((addr) & 0xF) + (16 * !((addr) & USB_DIR_IN)))

#define DWC_REG_DATA_FIFO_START 0x1000
#define DWC_REG_DATA_FIFO(regs, ep)	((volatile uint32_t*)((uint8_t*)regs + (ep + 1) * 0x1000))

class GAHBCFG : public hwreg::RegisterBase<GAHBCFG, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, glblintrmsk);
    DEF_FIELD(4, 1, hburstlen);
    DEF_BIT(5, dmaenable);
    DEF_BIT(7, nptxfemplvl_txfemplvl);
    DEF_BIT(8, ptxfemplvl);
    DEF_BIT(21, remmemsupp);
    DEF_BIT(22, notialldmawrit);
    DEF_BIT(23, AHBSingle);
    static auto Get() { return hwreg::RegisterAddr<GAHBCFG>(0x8); }
};

class GUSBCFG : public hwreg::RegisterBase<GUSBCFG, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(2, 0, toutcal);
    DEF_BIT(3, phyif);
    DEF_BIT(4, ulpi_utmi_sel);
    DEF_BIT(5, fsintf);
    DEF_BIT(6, physel);
    DEF_BIT(7, ddrsel);
    DEF_BIT(8, srpcap);
    DEF_BIT(9, hnpcap);
    DEF_FIELD(13, 10, usbtrdtim);
    DEF_BIT(15, phylpwrclksel);
    DEF_BIT(16, otgutmifssel);
    DEF_BIT(17, ulpi_fsls);
    DEF_BIT(18, ulpi_auto_res);
    DEF_BIT(19, ulpi_clk_sus_m);
    DEF_BIT(20, ulpi_ext_vbus_drv);
    DEF_BIT(21, ulpi_int_vbus_indicator);
    DEF_BIT(22, term_sel_dl_pulse);
    DEF_BIT(23, indicator_complement);
    DEF_BIT(24, indicator_pass_through);
    DEF_BIT(25, ulpi_int_prot_dis);
    DEF_BIT(26, ic_usb_cap);
    DEF_BIT(27, ic_traffic_pull_remove);
    DEF_BIT(28, tx_end_delay);
    DEF_BIT(29, force_host_mode);
    DEF_BIT(30, force_dev_mode);
    static auto Get() { return hwreg::RegisterAddr<GUSBCFG>(0xC); }
};

class GRSTCTL : public hwreg::RegisterBase<GRSTCTL, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, csftrst);
    DEF_BIT(1, hsftrst);
    DEF_BIT(2, hstfrm);
    DEF_BIT(3, intknqflsh);
    DEF_BIT(4, rxfflsh);
    DEF_BIT(5, txfflsh);
    DEF_FIELD(10, 6, txfnum);
    DEF_BIT(30, dmareq);
    DEF_BIT(31, ahbidle);
    static auto Get() { return hwreg::RegisterAddr<GRSTCTL>(0x10); }
};

class GINTSTS : public hwreg::RegisterBase<GINTSTS, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, curmode);
    DEF_BIT(1, modemismatch);
    DEF_BIT(2, otgintr);
    DEF_BIT(3, sof_intr);
    DEF_BIT(4, rxstsqlvl);
    DEF_BIT(5, nptxfempty);
    DEF_BIT(6, ginnakeff);
    DEF_BIT(7, goutnakeff);
    DEF_BIT(8, ulpickint);
    DEF_BIT(9, i2cintr);
    DEF_BIT(10, erlysuspend);
    DEF_BIT(11, usbsuspend);
    DEF_BIT(12, usbreset);
    DEF_BIT(13, enumdone);
    DEF_BIT(14, isooutdrop);
    DEF_BIT(15, eopframe);
    DEF_BIT(16, restoredone);
    DEF_BIT(17, epmismatch);
    DEF_BIT(18, inepintr);
    DEF_BIT(19, outepintr);
    DEF_BIT(20, incomplisoin);
    DEF_BIT(21, incomplisoout);
    DEF_BIT(22, fetsusp);
    DEF_BIT(23, resetdet);
    DEF_BIT(24, port_intr);
    DEF_BIT(25, host_channel_intr);
    DEF_BIT(26, ptxfempty);
    DEF_BIT(27, lpmtranrcvd);
    DEF_BIT(28, conidstschng);
    DEF_BIT(29, disconnect);
    DEF_BIT(30, sessreqintr);
    DEF_BIT(31, wkupintr);
    static auto Get() { return hwreg::RegisterAddr<GINTSTS>(0x14); }
};

class GINTMSK : public hwreg::RegisterBase<GINTMSK, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, curmode);
    DEF_BIT(1, modemismatch);
    DEF_BIT(2, otgintr);
    DEF_BIT(3, sof_intr);
    DEF_BIT(4, rxstsqlvl);
    DEF_BIT(5, nptxfempty);
    DEF_BIT(6, ginnakeff);
    DEF_BIT(7, goutnakeff);
    DEF_BIT(8, ulpickint);
    DEF_BIT(9, i2cintr);
    DEF_BIT(10, erlysuspend);
    DEF_BIT(11, usbsuspend);
    DEF_BIT(12, usbreset);
    DEF_BIT(13, enumdone);
    DEF_BIT(14, isooutdrop);
    DEF_BIT(15, eopframe);
    DEF_BIT(16, restoredone);
    DEF_BIT(17, epmismatch);
    DEF_BIT(18, inepintr);
    DEF_BIT(19, outepintr);
    DEF_BIT(20, incomplisoin);
    DEF_BIT(21, incomplisoout);
    DEF_BIT(22, fetsusp);
    DEF_BIT(23, resetdet);
    DEF_BIT(24, port_intr);
    DEF_BIT(25, host_channel_intr);
    DEF_BIT(26, ptxfempty);
    DEF_BIT(27, lpmtranrcvd);
    DEF_BIT(28, conidstschng);
    DEF_BIT(29, disconnect);
    DEF_BIT(30, sessreqintr);
    DEF_BIT(31, wkupintr);
    static auto Get() { return hwreg::RegisterAddr<GINTMSK>(0x18); }
};

class GRXSTSP : public hwreg::RegisterBase<GRXSTSP, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(3, 0, epnum);
    DEF_FIELD(14, 4, bcnt);
    DEF_FIELD(16, 15, dpid);
    DEF_FIELD(20, 17, pktsts);
    DEF_FIELD(24, 21, fn);
    static auto Get() { return hwreg::RegisterAddr<GRXSTSP>(0x20); }
};

union dwc_grxstsp_t {
    uint32_t val;
    struct {
    	uint32_t epnum      : 4;
    	uint32_t bcnt       : 11;
    	uint32_t dpid       : 2;
#define DWC_STS_DATA_UPDT		0x2	// OUT Data Packet
#define DWC_STS_XFER_COMP		0x3	// OUT Data Transfer Complete
#define DWC_DSTS_GOUT_NAK		0x1	// Global OUT NAK
#define DWC_DSTS_SETUP_COMP		0x4	// Setup Phase Complete
#define DWC_DSTS_SETUP_UPDT 0x6	// SETUP Packet
    	uint32_t pktsts     : 4;
    	uint32_t fn         : 4;
    	uint32_t reserved   : 7;
    };
    dwc_grxstsp_t(volatile dwc_grxstsp_t& r) { val = r.val; }
};

class DEPCTL : public hwreg::RegisterBase<DEPCTL, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(10, 0, mps);
    DEF_FIELD(14, 11, nextep);
    DEF_BIT(15, usbactep);
    DEF_BIT(16, dpid);
    DEF_BIT(17, naksts);
    DEF_FIELD(19, 18, eptype);
    DEF_BIT(20, snp);
    DEF_BIT(21, stall);
    DEF_FIELD(25, 22, txfnum);
    DEF_BIT(26, cnak);
    DEF_BIT(27, snak);
    DEF_BIT(28, setd0pid);
    DEF_BIT(29, setd1pid);
    DEF_BIT(30, epdis);
    DEF_BIT(31, epena);
};

union dwc_depctl_t {
	uint32_t val;
	struct {
		uint32_t mps        : 11;
#define DWC_DEP0CTL_MPS_64	 0
#define DWC_DEP0CTL_MPS_32	 1
#define DWC_DEP0CTL_MPS_16	 2
#define DWC_DEP0CTL_MPS_8	 3
		uint32_t nextep     : 4;
		uint32_t usbactep   : 1;
		uint32_t dpid       : 1;
		uint32_t naksts     : 1;
		uint32_t eptype     : 2;
		uint32_t snp        : 1;
		uint32_t stall      : 1;
		uint32_t txfnum     : 4;
		uint32_t cnak       : 1;
		uint32_t snak       : 1;
		uint32_t setd0pid   : 1;
		uint32_t setd1pid   : 1;
		uint32_t epdis      : 1;
		uint32_t epena      : 1;
	};
    dwc_depctl_t(volatile dwc_depctl_t& r) { val = r.val; }
};

class DEPTSIZ : public hwreg::RegisterBase<DEPTSIZ, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(18, 0, xfersize);
    DEF_FIELD(28, 19, pktcnt);
    DEF_FIELD(30, 29, mc);
};

union dwc_deptsiz_t {
	uint32_t val;
	struct {
		/* Transfer size */
		uint32_t xfersize   : 19;
		/* Packet Count */
		uint32_t pktcnt     : 10;
		/* Multi Count */
		uint32_t mc         : 2;
		uint32_t reserved   : 1;
	};
    dwc_deptsiz_t(volatile dwc_deptsiz_t& r) { val = r.val; }
};

class DEPTSIZ0 : public hwreg::RegisterBase<DEPTSIZ0, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(6, 0, xfersize);
    DEF_FIELD(20, 19, pktcnt);
    DEF_FIELD(30, 29, supcnt);
};

union dwc_deptsiz0_t {
	uint32_t val;
	struct {
		/* Transfer size */
		uint32_t xfersize   : 7;
		uint32_t reserved   : 12;
		/* Packet Count */
		uint32_t pktcnt     : 2;
		uint32_t reserved2  : 8;
		/* Setup Packet Count */
		uint32_t supcnt     : 2;
		uint32_t reserved3  : 1;
	};
};

class DIEPINT : public hwreg::RegisterBase<DIEPINT, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, xfercompl);
    DEF_BIT(1, epdisabled);
    DEF_BIT(2, ahberr);
    DEF_BIT(3, timeout);
    DEF_BIT(4, intktxfemp);
    DEF_BIT(5, intknepmis);
    DEF_BIT(6, inepnakeff);
    DEF_BIT(8, txfifoundrn);
    DEF_BIT(9, bna);
    DEF_BIT(13, nak);
};

union dwc_diepint_t {
	uint32_t val;
	struct {
		/* Transfer complete mask */
		uint32_t xfercompl      : 1;
		/* Endpoint disable mask */
		uint32_t epdisabled     : 1;
		/* AHB Error mask */
		uint32_t ahberr         : 1;
		/* TimeOUT Handshake mask (non-ISOC EPs) */
		uint32_t timeout        : 1;
		/* IN Token received with TxF Empty mask */
		uint32_t intktxfemp     : 1;
		/* IN Token Received with EP mismatch mask */
		uint32_t intknepmis     : 1;
		/* IN Endpoint NAK Effective mask */
		uint32_t inepnakeff     : 1;
		uint32_t reserved       : 1;
		uint32_t txfifoundrn    : 1;
		/* BNA Interrupt mask */
		uint32_t bna            : 1;
		uint32_t reserved2      : 3;
		/* BNA Interrupt mask */
		uint32_t nak:1;
		uint32_t reserved3      : 18;
	};
    dwc_diepint_t(volatile dwc_diepint_t& r) { val = r.val; }
    dwc_diepint_t() { val = 0; }
};

class DOEPINT : public hwreg::RegisterBase<DOEPINT, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, xfercompl);
    DEF_BIT(1, epdisabled);
    DEF_BIT(2, ahberr);
    DEF_BIT(3, setup);
    DEF_BIT(4, outtknepdis);
    DEF_BIT(5, stsphsercvd);
    DEF_BIT(6, back2backsetup);
    DEF_BIT(8, outpkterr);
    DEF_BIT(9, bna);
    DEF_BIT(11, pktdrpsts);
    DEF_BIT(12, babble);
    DEF_BIT(13, nak);
    DEF_BIT(14, nyet);
    DEF_BIT(15, sr);
};

union dwc_doepint_t {
	uint32_t val;
	struct {
		/* Transfer complete */
		uint32_t xfercompl      : 1;
		/* Endpoint disable  */
		uint32_t epdisabled     : 1;
		/* AHB Error */
		uint32_t ahberr         : 1;
		/* Setup Phase Done (contorl EPs) */
		uint32_t setup          : 1;
		/* OUT Token Received when Endpoint Disabled */
		uint32_t outtknepdis    : 1;
		uint32_t stsphsercvd    : 1;
		/* Back-to-Back SETUP Packets Received */
		uint32_t back2backsetup : 1;
		uint32_t reserved       : 1;
		/* OUT packet Error */
		uint32_t outpkterr      : 1;
		/* BNA Interrupt */
		uint32_t bna            : 1;

		uint32_t reserved2      : 1;
		/* Packet Drop Status */
		uint32_t pktdrpsts      : 1;
		/* Babble Interrupt */
		uint32_t babble         : 1;
		/* NAK Interrupt */
		uint32_t nak            : 1;
		/* NYET Interrupt */
		uint32_t nyet           : 1;
        /* Bit indicating setup packet received */
        uint32_t sr             : 1;
		uint32_t reserved3      : 16;
	};
    dwc_doepint_t(volatile dwc_doepint_t& r) { val = r.val; }
    dwc_doepint_t() { val = 0; }
};

class DCFG : public hwreg::RegisterBase<DCFG, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(1, 0, devspd);
    DEF_BIT(2, nzstsouthshk);
    DEF_BIT(3, ena32khzs);
    DEF_FIELD(10, 4, devaddr);
    DEF_FIELD(12, 11, perfrint);
    DEF_BIT(13, endevoutnak);
    DEF_FIELD(22, 18, epmscnt);
    DEF_BIT(23, descdma);
    DEF_FIELD(25, 24, perschintvl);
    DEF_FIELD(31, 26, resvalid);
};

typedef union {
    uint32_t val;
    struct {
        uint32_t devspd         : 2;
        uint32_t nzstsouthshk   : 1;
        uint32_t ena32khzs      : 1;
        uint32_t devaddr        : 7;
        uint32_t perfrint       : 2;
        uint32_t endevoutnak    : 1;
        uint32_t reserved       : 4;
        uint32_t epmscnt        : 5;
        uint32_t descdma        : 1;
        uint32_t perschintvl    : 2;
        uint32_t resvalid       : 6;
    };
} dwc_dcfg_t;

class DCTL : public hwreg::RegisterBase<DCTL, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, rmtwkupsig);
    DEF_BIT(1, sftdiscon);
    DEF_BIT(2, gnpinnaksts);
    DEF_BIT(3, goutnaksts);
    DEF_FIELD(6, 4, tstctl);
    DEF_BIT(7, sgnpinnak);
    DEF_BIT(8, cgnpinnak);
    DEF_BIT(9, sgoutnak);
    DEF_BIT(10, cgoutnak);
    DEF_BIT(11, pwronprgdone);
    DEF_FIELD(14, 13, gmc);
    DEF_BIT(15, ifrmnum);
    DEF_BIT(16, nakonbble);
    DEF_BIT(17, encontonbna);
    DEF_BIT(18, besl_reject);
};

typedef union {
    uint32_t val;
    struct {
        /* Remote Wakeup */
        uint32_t rmtwkupsig     : 1;
        /* Soft Disconnect */
        uint32_t sftdiscon      : 1;
        /* Global Non-Periodic IN NAK Status */
        uint32_t gnpinnaksts    : 1;
        /* Global OUT NAK Status */
        uint32_t goutnaksts     : 1;
        /* Test Control */
        uint32_t tstctl         : 3;
        /* Set Global Non-Periodic IN NAK */
        uint32_t sgnpinnak      : 1;
        /* Clear Global Non-Periodic IN NAK */
        uint32_t cgnpinnak      : 1;
        /* Set Global OUT NAK */
        uint32_t sgoutnak       : 1;
        /* Clear Global OUT NAK */
        uint32_t cgoutnak       : 1;
        /* Power-On Programming Done */
        uint32_t pwronprgdone   : 1;
        /* Reserved */
        uint32_t reserved       : 1;
        /* Global Multi Count */
        uint32_t gmc            : 2;
        /* Ignore Frame Number for ISOC EPs */
        uint32_t ifrmnum        : 1;
        /* NAK on Babble */
        uint32_t nakonbble      : 1;
        /* Enable Continue on BNA */
        uint32_t encontonbna    : 1;
        /* Enable deep sleep besl reject feature*/
        uint32_t besl_reject    : 1;
        uint32_t reserved2      : 13;
    };
} dwc_dctl_t;

class DSTS : public hwreg::RegisterBase<DSTS, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_BIT(0, suspsts);
    DEF_FIELD(2, 1, enumspd);
    DEF_BIT(3, errticerr);
    DEF_FIELD(21, 8, soffn);
};

typedef union {
    uint32_t val;
    struct {
        /* Suspend Status */
        uint32_t suspsts    : 1;
        /* Enumerated Speed */
        uint32_t enumspd    : 2;
        /* Erratic Error */
        uint32_t errticerr  : 1;
        uint32_t reserved   : 4;
        /* Frame or Microframe Number of the received SOF */
        uint32_t soffn      : 14;
        uint32_t reserved2  : 10;
    };
} dwc_dsts_t;


typedef struct {
	/* Device IN Endpoint Control Register */
	dwc_depctl_t diepctl;

	uint32_t reserved;
	/* Device IN Endpoint Interrupt Register */
	dwc_diepint_t diepint;
	uint32_t reserved2;
	/* Device IN Endpoint Transfer Size */
	dwc_deptsiz_t dieptsiz;
	/* Device IN Endpoint DMA Address Register */
	uint32_t diepdma;
	/* Device IN Endpoint Transmit FIFO Status Register */
	uint32_t dtxfsts;
	/* Device IN Endpoint DMA Buffer Register */
	uint32_t diepdmab;
} dwc_depin_t;

typedef struct {
	/* Device OUT Endpoint Control Register */
	dwc_depctl_t doepctl;

	uint32_t reserved;
	/* Device OUT Endpoint Interrupt Register */
	dwc_doepint_t doepint;
	uint32_t reserved2;
	/* Device OUT Endpoint Transfer Size Register */
	dwc_deptsiz_t doeptsiz;
	/* Device OUT Endpoint DMA Address Register */
	uint32_t doepdma;
	uint32_t reserved3;
	/* Device OUT Endpoint DMA Buffer Register */
	uint32_t doepdmab;
} dwc_depout_t;

typedef union {
    uint32_t val;
    struct {
		/** Stop Pclk */
		uint32_t stoppclk:1;
		/** Gate Hclk */
		uint32_t gatehclk:1;
		/** Power Clamp */
		uint32_t pwrclmp:1;
		/** Reset Power Down Modules */
		uint32_t rstpdwnmodule:1;
		/** Reserved */
		uint32_t reserved:1;
		/** Enable Sleep Clock Gating (Enbl_L1Gating) */
		uint32_t enbl_sleep_gating:1;
		/** PHY In Sleep (PhySleep) */
		uint32_t phy_in_sleep:1;
		/** Deep Sleep*/
		uint32_t deep_sleep:1;
		uint32_t resetaftsusp:1;
		uint32_t restoremode:1;
		uint32_t reserved10_12:3;
		uint32_t ess_reg_restored:1;
		uint32_t prt_clk_sel:2;
		uint32_t port_power:1;
		uint32_t max_xcvrselect:2;
		uint32_t max_termsel:1;
		uint32_t mac_dev_addr:7;
		uint32_t p2hd_dev_enum_spd:2;
		uint32_t p2hd_prt_spd:2;
		uint32_t if_dev_mode:1;
	};
} dwc_pcgcctl_t;

class GNPTXSTS : public hwreg::RegisterBase<GNPTXSTS, uint32_t, hwreg::EnablePrinter> {
public:
    DEF_FIELD(15, 0, nptxfspcavail);
    DEF_FIELD(23, 16, nptxqspcavail);
    DEF_BIT(24, nptxqtop_terminate);
    DEF_FIELD(26, 25, nptxqtop_token);
    DEF_FIELD(30, 27, nptxqtop_chnep);
};

union dwc_gnptxsts_t {
    uint32_t val;
    struct {
		uint32_t nptxfspcavail:16;
		uint32_t nptxqspcavail:8;
		/** Top of the Non-Periodic Transmit Request Queue
		 *	- bit 24 - Terminate (Last entry for the selected
		 *	  channel/EP)
		 *	- bits 26:25 - Token Type
		 *	  - 2'b00 - IN/OUT
		 *	  - 2'b01 - Zero Length OUT
		 *	  - 2'b10 - PING/Complete Split
		 *	  - 2'b11 - Channel Halt
		 *	- bits 30:27 - Channel/EP Number
		 */
		uint32_t nptxqtop_terminate:1;
		uint32_t nptxqtop_token:2;
		uint32_t nptxqtop_chnep:4;
		uint32_t reserved:1;
	};
    dwc_gnptxsts_t(volatile dwc_gnptxsts_t& r) { val = r.val; }
};

typedef union  {
    uint32_t val;
    struct {
		uint32_t startaddr  : 16;
		uint32_t depth      : 16;
	};
} dwc_fifosiz_t;

typedef volatile struct {
    // OTG Control and Status Register
    uint32_t gotgctl;
    // OTG Interrupt Register
    uint32_t gotgint;
    // Core AHB Configuration Register
    uint32_t gahbcfg;
    // Core USB Configuration Register
    uint32_t gusbcfg;
    // Core Reset Register
    uint32_t grstctl;
    // Core Interrupt Register
    uint32_t gintsts;
    // Core Interrupt Mask Register
    uint32_t gintmsk;
	// Receive Status Queue Read Register
    uint32_t grxstsr;
	// Receive Status Queue Read & POP Register
    dwc_grxstsp_t grxstsp;
	// Receive FIFO Size Register
    uint32_t grxfsiz;
	// Non Periodic Transmit FIFO Size Register
    dwc_fifosiz_t gnptxfsiz;
	// Non Periodic Transmit FIFO/Queue Status Register
    dwc_gnptxsts_t gnptxsts;
    // I2C Access Register
    uint32_t gi2cctl;
	// PHY Vendor Control Register
	uint32_t gpvndctl;
	// General Purpose Input/Output Register
	uint32_t ggpio;
	// User ID Register
	uint32_t guid;
	// Synopsys ID Register (Read Only)
	uint32_t gsnpsid;
	// User HW Config1 Register (Read Only)
	uint32_t ghwcfg1;
	// User HW Config2 Register (Read Only)
	uint32_t ghwcfg2;
	// User HW Config3 Register (Read Only)
	uint32_t ghwcfg3;
	// User HW Config4 Register (Read Only)
	uint32_t ghwcfg4;

    uint32_t reserved_030[(0x800 - 0x054) / sizeof(uint32_t)];

    // Device Configuration Register
    dwc_dcfg_t dcfg;
    // Device Control Register
    dwc_dctl_t dctl;
    // Device Status Register
    dwc_dsts_t dsts;
    uint32_t unused;
    // Device IN Endpoint Common Interrupt Mask Register
    dwc_diepint_t diepmsk;
    // Device OUT Endpoint Common Interrupt Mask Register
    dwc_doepint_t doepmsk;
    // Device All Endpoints Interrupt Register
    uint32_t daint;
    //Device All Endpoints Interrupt Mask Register
    uint32_t daintmsk;
    // Device IN Token Queue Read Register-1
    uint32_t dtknqr1;
    // Device IN Token Queue Read Register-2
    uint32_t dtknqr2;
    // Device VBUS discharge Register
    uint32_t dvbusdis;
    // Device VBUS pulse Register
    uint32_t dvbuspulse;
    // Device IN Token Queue Read Register-3 / Device Thresholding control register
    uint32_t dtknqr3_dthrctl;
    // Device IN Token Queue Read Register-4 / Device IN EPs empty Inr. Mask Register
    uint32_t dtknqr4_fifoemptymsk;
    // Device Each Endpoint Interrupt Register
    uint32_t deachint;
    //: Device Each Endpoint Interrupt Mask Register
    uint32_t deachintmsk;
    // Device Each In Endpoint Interrupt Mask Register
    uint32_t diepeachintmsk[MAX_EPS_CHANNELS];
    // Device Each Out Endpoint Interrupt Mask Register
    uint32_t doepeachintmsk[MAX_EPS_CHANNELS];

    uint32_t reserved_0x8C0[(0x900 - 0x8C0) / sizeof(uint32_t)];

    dwc_depin_t depin[MAX_EPS_CHANNELS];

    dwc_depout_t depout[MAX_EPS_CHANNELS];

    uint32_t reserved_0xD00[(0xE00 - 0xD00) / sizeof(uint32_t)];

    dwc_pcgcctl_t pcgcctl;

} dwc_regs_t;
