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


void dwc_irq_stop(dwc_usb_t* dwc) {
    zx_interrupt_destroy(dwc->irq_handle);
    thrd_join(dwc->irq_thread, NULL);
    zx_handle_close(dwc->irq_handle);
    dwc->irq_handle = ZX_HANDLE_INVALID;
}

} // namespace dwc2
