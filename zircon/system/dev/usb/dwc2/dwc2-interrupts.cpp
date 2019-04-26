// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <ddk/debug.h>

#include "dwc2.h"

namespace dwc2 {

static void dwc_ep_read_packet(volatile void* regs, void* buffer, uint32_t length, uint8_t ep_num) {
}

static void dwc_set_address(dwc_usb_t* dwc, uint8_t address) {
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
return 0;
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



void dwc_irq_stop(dwc_usb_t* dwc) {
    zx_interrupt_destroy(dwc->irq_handle);
    thrd_join(dwc->irq_thread, NULL);
    zx_handle_close(dwc->irq_handle);
    dwc->irq_handle = ZX_HANDLE_INVALID;
}

} // namespace dwc2
