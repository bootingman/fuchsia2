// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "dwc2.h"

namespace dwc2 {

static void dwc_ep_read_packet(volatile void* regs, void* buffer, uint32_t length, uint8_t ep_num) {
    uint32_t count = (length + 3) >> 2;
    uint32_t* dest = (uint32_t*)buffer;
    volatile uint32_t* fifo = DWC_REG_DATA_FIFO(regs, ep_num);

    for (uint32_t i = 0; i < count; i++) {
        *dest++ = *fifo;
zxlogf(LSPEW, "read %08x\n", dest[-1]);
    }
}

static void dwc_set_address(dwc_usb_t* dwc, uint8_t address) {
    auto* mmio = dwc->mmio();

zxlogf(LINFO, "dwc_set_address %u\n", address);
    DCFG::Get().ReadFrom(mmio).set_devaddr(address).WriteTo(mmio);
}

static void dwc2_ep0_out_start(dwc_usb_t* dwc)  {
}

static void do_setup_status_phase(dwc_usb_t* dwc, bool is_in) {
//zxlogf(LINFO, "do_setup_status_phase is_in: %d\n", is_in);
//     dwc_endpoint_t* ep = &dwc->eps[0];

    dwc->ep0_state = EP0_STATE_STATUS;

    dwc_ep_start_transfer(dwc, (is_in ? DWC_EP0_IN : DWC_EP0_OUT), 0);

    /* Prepare for more SETUP Packets */
    dwc2_ep0_out_start(dwc);
}

static void dwc_ep0_complete_request(dwc_usb_t* dwc) {
     dwc_endpoint_t* ep = &dwc->eps[0];

    if (dwc->ep0_state == EP0_STATE_STATUS) {
//zxlogf(LINFO, "dwc_ep0_complete_request EP0_STATE_STATUS\n");
        ep->req_offset = 0;
        ep->req_length = 0;
// this interferes with zero length OUT
//    } else if ( ep->req_length == 0) {
//zxlogf(LINFO, "dwc_ep0_complete_request ep->req_length == 0\n");
//      dwc_otg_ep_start_transfer(ep);
    } else if (dwc->ep0_state == EP0_STATE_DATA_IN) {
//zxlogf(LINFO, "dwc_ep0_complete_request EP0_STATE_DATA_IN\n");
       if (ep->req_offset >= ep->req_length) {
            do_setup_status_phase(dwc, false);
       }
    } else {
//zxlogf(LINFO, "dwc_ep0_complete_request ep0-OUT\n");
        do_setup_status_phase(dwc, true);
    }

#if 0
    deptsiz0_data_t deptsiz;
    dwc_ep_t* ep = &pcd->dwc_eps[0].dwc_ep;
    int ret = 0;

    if (EP0_STATUS == pcd->ep0state) {
        ep->start_xfer_buff = 0;
        ep->xfer_buff = 0;
        ep->xfer_len = 0;
        ep->num = 0;
        ret = 1;
    } else if (0 == ep->xfer_len) {
        ep->xfer_len = 0;
        ep->xfer_count = 0;
        ep->sent_zlp = 1;
        ep->num = 0;
        dwc_otg_ep_start_transfer(ep);
        ret = 1;
    } else if (ep->is_in) {
        deptsiz.d32 = dwc_read_reg32(DWC_REG_IN_EP_TSIZE(0));
        if (0 == deptsiz.b.xfersize) {
            /* Is a Zero Len Packet needed? */
            do_setup_status_phase(pcd, 0);
        }
    } else {
        /* ep0-OUT */
        do_setup_status_phase(pcd, 1);
    }

#endif
}

static zx_status_t dwc_handle_setup(dwc_usb_t* dwc, usb_setup_t* setup, void* buffer, size_t length,
                                     size_t* out_actual) {
//zxlogf(LINFO, "dwc_handle_setup\n");
    zx_status_t status;
    dwc_endpoint_t* ep = &dwc->eps[0];

    if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) {
        // handle some special setup requests in this driver
        switch (setup->bRequest) {
        case USB_REQ_SET_ADDRESS:
            zxlogf(INFO, "SET_ADDRESS %d\n", setup->wValue);
            dwc_set_address(dwc, static_cast<uint8_t>(setup->wValue));
            *out_actual = 0;
            return ZX_OK;
        case USB_REQ_SET_CONFIGURATION:
            zxlogf(INFO, "SET_CONFIGURATION %d\n", setup->wValue);
            dwc_reset_configuration(dwc);
                dwc->configured = true;
            status = usb_dci_interface_control(&dwc->dci_intf, setup, NULL, 0, buffer, length, out_actual);
            if (status == ZX_OK && setup->wValue) {
                dwc_start_eps(dwc);
            } else {
                dwc->configured = false;
            }
            return status;
        default:
            // fall through to usb_dci_interface_control()
            break;
        }
    } else if (setup->bmRequestType == (USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) &&
               setup->bRequest == USB_REQ_SET_INTERFACE) {
        zxlogf(INFO, "SET_INTERFACE %d\n", setup->wValue);
        dwc_reset_configuration(dwc);
        dwc->configured = true;
        status = usb_dci_interface_control(&dwc->dci_intf, setup, NULL, 0, buffer, length, out_actual);
        if (status == ZX_OK) {
            dwc_start_eps(dwc);
        } else {
            dwc->configured = false;
        }
        return status;
    }

    if ((setup->bmRequestType & USB_DIR_MASK) == USB_DIR_OUT) {
        status = usb_dci_interface_control(&dwc->dci_intf, setup, NULL, 0, buffer, length, out_actual);
    } else {
        status = usb_dci_interface_control(&dwc->dci_intf, setup, buffer, length, NULL, 0, out_actual);
    }
    if (status == ZX_OK) {
        ep->req_offset = 0;
        ep->req_length = static_cast<uint32_t>(*out_actual);
    }
    return status;
}

static void pcd_setup(dwc_usb_t* dwc) {
    usb_setup_t* setup = &dwc->cur_setup;

    if (!dwc->got_setup) {
//zxlogf(LINFO, "no setup\n");
        return;
    }
    dwc->got_setup = false;
//    _pcd->status = 0;


    if (setup->bmRequestType & USB_DIR_IN) {
//zxlogf(LINFO, "pcd_setup set EP0_STATE_DATA_IN\n");
        dwc->ep0_state = EP0_STATE_DATA_IN;
    } else {
//zxlogf(LINFO, "pcd_setup set EP0_STATE_DATA_OUT\n");
        dwc->ep0_state = EP0_STATE_DATA_OUT;
    }

    if (setup->wLength > 0 && dwc->ep0_state == EP0_STATE_DATA_OUT) {
//zxlogf(LINFO, "queue read\n");
        // queue a read for the data phase
        dwc->ep0_state = EP0_STATE_DATA_OUT;
        dwc_ep_start_transfer(dwc, DWC_EP0_OUT, setup->wLength);
    } else {
        size_t actual = 0;
        __UNUSED zx_status_t status = dwc_handle_setup(dwc, setup, dwc->ep0_buffer,
                                              sizeof(dwc->ep0_buffer), &actual);
        //zxlogf(INFO, "dwc_handle_setup returned %d actual %zu\n", status, actual);
//            if (status != ZX_OK) {
//                dwc3_cmd_ep_set_stall(dwc, EP0_OUT);
//                dwc3_queue_setup_locked(dwc);
//                break;
//            }

        if (dwc->ep0_state == EP0_STATE_DATA_IN && setup->wLength > 0) {
//            zxlogf(LINFO, "queue a write for the data phase\n");
            dwc->ep0_state = EP0_STATE_DATA_IN;
            dwc_ep_start_transfer(dwc, DWC_EP0_IN, static_cast<uint32_t>(actual));
        } else {
            dwc_ep0_complete_request(dwc);
        }
    }
}


static void dwc_handle_ep0(dwc_usb_t* dwc) {
//    zxlogf(LINFO, "dwc_handle_ep0\n");

    switch (dwc->ep0_state) {
    case EP0_STATE_IDLE: {
//zxlogf(LINFO, "dwc_handle_ep0 EP0_STATE_IDLE\n");
//        req_flag->request_config = 0;
        pcd_setup(dwc);
        break;
    }
    case EP0_STATE_DATA_IN:
//    zxlogf(LINFO, "dwc_handle_ep0 EP0_STATE_DATA_IN\n");
//        if (ep0->xfer_count < ep0->total_len)
//            zxlogf(LINFO, "FIX ME!! dwc_otg_ep0_continue_transfer!\n");
//        else
            dwc_ep0_complete_request(dwc);
        break;
    case EP0_STATE_DATA_OUT:
//    zxlogf(LINFO, "dwc_handle_ep0 EP0_STATE_DATA_OUT\n");
        dwc_ep0_complete_request(dwc);
        break;
    case EP0_STATE_STATUS:
//    zxlogf(LINFO, "dwc_handle_ep0 EP0_STATE_STATUS\n");
        dwc_ep0_complete_request(dwc);
        /* OUT for next SETUP */
        dwc->ep0_state = EP0_STATE_IDLE;
//        ep0->stopped = 1;
//        ep0->is_in = 0;
        break;

    case EP0_STATE_STALL:
    default:
        zxlogf(LINFO, "EP0 state is %d, should not get here pcd_setup()\n", dwc->ep0_state);
        break;
    }
}

void dwc_flush_fifo(dwc_usb_t* dwc, const int num) {}

static void dwc_handle_reset_irq(dwc_usb_t* dwc) {
}

static void dwc_handle_enumdone_irq(dwc_usb_t* dwc) {}

static void dwc_handle_rxstsqlvl_irq(dwc_usb_t* dwc) {
    auto* regs = dwc->regs;
    auto* mmio = dwc->mmio();

//why?    regs->gintmsk.rxstsqlvl = 0;

    /* Get the Status from the top of the FIFO */
    auto grxstsp = GRXSTSP::Get().ReadFrom(mmio);
zxlogf(LINFO, "dwc_handle_rxstsqlvl_irq epnum: %u bcnt: %u pktsts: %u\n", grxstsp.epnum(), grxstsp.bcnt(), grxstsp.pktsts());

    auto ep_num = grxstsp.epnum();
    if (ep_num > 0) {
        ep_num += 16;
    }
    dwc_endpoint_t* ep = &dwc->eps[ep_num];

    switch (grxstsp.pktsts()) {
    case DWC_STS_DATA_UPDT: {
        uint32_t fifo_count = grxstsp.bcnt();
zxlogf(LINFO, "DWC_STS_DATA_UPDT grxstsp.bcnt: %u\n", grxstsp.bcnt());
        if (fifo_count > ep->req_length - ep->req_offset) {
zxlogf(LINFO, "fifo_count %u > %u\n", fifo_count, ep->req_length - ep->req_offset);
            fifo_count = ep->req_length - ep->req_offset;
        }
        if (fifo_count > 0) {
            dwc_ep_read_packet(regs, ep->req_buffer + ep->req_offset, fifo_count, static_cast<uint8_t>(ep_num));
            ep->req_offset += fifo_count;
        }
        break;
    }

    case DWC_DSTS_SETUP_UPDT: {
//zxlogf(LINFO, "DWC_DSTS_SETUP_UPDT\n"); 
    volatile uint32_t* fifo = (uint32_t *)((uint8_t *)regs + 0x1000);
    uint32_t* dest = (uint32_t*)&dwc->cur_setup;
    dest[0] = *fifo;
    dest[1] = *fifo;
zxlogf(LINFO, "SETUP bmRequestType: 0x%02x bRequest: %u wValue: %u wIndex: %u wLength: %u\n",
       dwc->cur_setup.bmRequestType, dwc->cur_setup.bRequest, dwc->cur_setup.wValue,
       dwc->cur_setup.wIndex, dwc->cur_setup.wLength); 
       dwc->got_setup = true;
        break;
    }

    case DWC_DSTS_GOUT_NAK:
zxlogf(LINFO, "DWC_DSTS_GOUT_NAK\n");
break;
    case DWC_STS_XFER_COMP:
//zxlogf(LINFO, "DWC_STS_XFER_COMP\n");
break;
    case DWC_DSTS_SETUP_COMP:
//zxlogf(LINFO, "DWC_DSTS_SETUP_COMP\n");
break;
    default:
        break;
    }
}

static void dwc_handle_inepintr_irq(dwc_usb_t* dwc) {
    auto* mmio = dwc->mmio();

printf("dwc_handle_inepintr_irq\n");
    for (uint8_t ep_num = 0; ep_num < MAX_EPS_CHANNELS; ep_num++) {
        uint32_t bit = 1 << ep_num;
        auto daint = DAINT::Get().ReadFrom(mmio);
        if ((daint.enable() & bit) == 0) {
            continue;
        }
        daint.set_enable(daint.enable() | bit).WriteTo(mmio);        

        auto diepint = DIEPINT::Get(ep_num).ReadFrom(mmio);

        /* Transfer complete */
        if (diepint.xfercompl()) {
if (ep_num > 0) zxlogf(LINFO, "dwc_handle_inepintr_irq xfercompl ep_num %u\n", ep_num);
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_xfercompl(1).WriteTo(mmio);
//                regs->depin[ep_num].diepint.xfercompl = 1;
            /* Complete the transfer */
            if (0 == ep_num) {
                dwc_handle_ep0(dwc);
            } else {
                dwc_complete_ep(dwc, ep_num);
                if (diepint.nak()) {
printf("diepint.nak ep_num %u\n", ep_num);
                    DIEPINT::Get(ep_num).ReadFrom(mmio).set_nak(1).WriteTo(mmio);
                }
            }
        }
        /* Endpoint disable  */
        if (diepint.epdisabled()) {
            /* Clear the bit in DIEPINTn for this interrupt */
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_epdisabled(1).WriteTo(mmio);
        }
        /* AHB Error */
        if (diepint.ahberr()) {
            /* Clear the bit in DIEPINTn for this interrupt */
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_ahberr(1).WriteTo(mmio);
        }
        /* TimeOUT Handshake (non-ISOC IN EPs) */
        if (diepint.timeout()) {
//                handle_in_ep_timeout_intr(ep_num);
zxlogf(LINFO, "TODO handle_in_ep_timeout_intr\n");
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_timeout(1).WriteTo(mmio);
        }
        /** IN Token received with TxF Empty */
        if (diepint.intktxfemp()) {
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_intktxfemp(1).WriteTo(mmio);
        }
        /** IN Token Received with EP mismatch */
        if (diepint.intknepmis()) {
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_intknepmis(1).WriteTo(mmio);
        }
        /** IN Endpoint NAK Effective */
        if (diepint.inepnakeff()) {
printf("diepint.inepnakeff ep_num %u\n", ep_num);
            DIEPINT::Get(ep_num).ReadFrom(mmio).set_inepnakeff(1).WriteTo(mmio);
        }
    }
}

static void dwc_handle_outepintr_irq(dwc_usb_t* dwc) {
    auto* mmio = dwc->mmio();

//zxlogf(LINFO, "dwc_handle_outepintr_irq\n");

    uint8_t ep_num = 0;

    /* Read in the device interrupt bits */
    uint32_t ep_intr = DAINT::Get().ReadFrom(mmio).enable() & DWC_EP_OUT_MASK;
    ep_intr >>= DWC_EP_OUT_SHIFT;

    /* Clear the interrupt */
    DAINT::Get().FromValue(DWC_EP_OUT_MASK).WriteTo(mmio);

    while (ep_intr) {
        if (ep_intr & 1) {
            auto doepint = DOEPINT::Get(ep_num).ReadFrom(mmio);
            doepint.set_reg_value(doepint.reg_value() & DOEPMSK::Get().ReadFrom(mmio).reg_value());
if (ep_num > 0) zxlogf(LINFO, "dwc_handle_outepintr_irq doepint.val %08x\n", doepint.reg_value());

            /* Transfer complete */
            if (doepint.xfercompl()) {
if (ep_num > 0) zxlogf(LINFO, "dwc_handle_outepintr_irq xfercompl\n");
                /* Clear the bit in DOEPINTn for this interrupt */
                DOEPINT::Get(ep_num).ReadFrom(mmio).set_xfercompl(1).WriteTo(mmio);

                if (ep_num == 0) {
                    if (doepint.setup()) { // astro
                        DOEPINT::Get(ep_num).ReadFrom(mmio).set_setup(1).WriteTo(mmio);
                    }
                    dwc_handle_ep0(dwc);
                } else {
                    dwc_complete_ep(dwc, ep_num);
                }
            }
            /* Endpoint disable  */
            if (doepint.epdisabled()) {
zxlogf(LINFO, "dwc_handle_outepintr_irq epdisabled\n");
                /* Clear the bit in DOEPINTn for this interrupt */
                DOEPINT::Get(ep_num).ReadFrom(mmio).set_epdisabled(1).WriteTo(mmio);
            }
            /* AHB Error */
            if (doepint.ahberr()) {
zxlogf(LINFO, "dwc_handle_outepintr_irq ahberr\n");
                DOEPINT::Get(ep_num).ReadFrom(mmio).set_ahberr(1).WriteTo(mmio);
            }
            /* Setup Phase Done (contr0l EPs) */
            if (doepint.setup()) {
                if (1) { // astro
                    dwc_handle_ep0(dwc);
                }
                DOEPINT::Get(ep_num).ReadFrom(mmio).set_setup(1).WriteTo(mmio);
            }
        }
        ep_num++;
        ep_intr >>= 1;
    }
}

static void dwc_handle_nptxfempty_irq(dwc_usb_t* dwc) {
}

static void dwc_handle_usbsuspend_irq(dwc_usb_t* dwc) {
    zxlogf(LINFO, "dwc_handle_usbsuspend_irq\n");
}


void dwc_irq_stop(dwc_usb_t* dwc) {
    zx_interrupt_destroy(dwc->irq_handle);
    thrd_join(dwc->irq_thread, NULL);
    zx_handle_close(dwc->irq_handle);
    dwc->irq_handle = ZX_HANDLE_INVALID;
}

} // namespace dwc2
