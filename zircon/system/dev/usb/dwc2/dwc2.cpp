// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc2.h"

#include <ddk/binding.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddktl/pdev.h>
#include <ddktl/protocol/platform/device.h>
#include <usb/usb-request.h>

namespace dwc2 {

void Dwc2::HandleReset() {
    auto* mmio = get_mmio();

    zxlogf(LINFO, "\nRESET\n");

    ep0_state = EP0_STATE_DISCONNECTED;

    DCTL::Get().ReadFrom(mmio).set_rmtwkupsig(1).WriteTo(mmio);

    for (int i = 0; i < MAX_EPS_CHANNELS; i++) {
        auto diepctl = DEPCTL::Get(i).ReadFrom(mmio);

        if (diepctl.epena()) {
            // disable all active IN EPs
            diepctl.set_snak(1);
            diepctl.set_epdis(1);
            diepctl.WriteTo(mmio);
        }

        DEPCTL::Get(i + 16).ReadFrom(mmio).set_snak(1).WriteTo(mmio);
    }

    /* Flush the NP Tx FIFO */
    FlushFifo(0);

    /* Flush the Learning Queue */
    GRSTCTL::Get().ReadFrom(mmio).set_intknqflsh(1).WriteTo(mmio);

    // EPO IN and OUT
    DAINT::Get().FromValue((1 < DWC_EP_IN_SHIFT) | (1 < DWC_EP_OUT_SHIFT)).WriteTo(mmio);

    DOEPMSK::Get().FromValue(0).set_setup(1).set_xfercompl(1).set_ahberr(1).set_epdisabled(1).WriteTo(mmio);
    DIEPMSK::Get().FromValue(0).set_xfercompl(1).set_timeout(1).set_ahberr(1).set_epdisabled(1).WriteTo(mmio);

    /* Reset Device Address */
    DCFG::Get().ReadFrom(mmio).set_devaddr(0).WriteTo(mmio);

    StartEp0();

    // TODO how to detect disconnect?
    dci_intf_->SetConnected(true);
}

void Dwc2::HandleSuspend() {
    zxlogf(INFO, "Dwc2::HandleSuspend\n");
}

void Dwc2::HandleEnumDone() {
    auto* mmio = get_mmio();

    zxlogf(INFO, "dwc_handle_enumdone_irq\n");

/*
    if (dwc->astro_usb.ops) {
        astro_usb_do_usb_tuning(&dwc->astro_usb, false, false);
    }
*/
    ep0_state = EP0_STATE_IDLE;

    eps[0].max_packet_size = 64;

    DEPCTL::Get(0).ReadFrom(mmio).set_mps(DWC_DEP0CTL_MPS_64).WriteTo(mmio);
    DEPCTL::Get(16).ReadFrom(mmio).set_epena(1).WriteTo(mmio);

#if 0 // astro future use
    depctl.d32 = dwc_read_reg32(DWC_REG_IN_EP_REG(1));
    if (!depctl.b.usbactep) {
        depctl.b.mps = BULK_EP_MPS;
        depctl.b.eptype = 2;//BULK_STYLE
        depctl.b.setd0pid = 1;
        depctl.b.txfnum = 0;   //Non-Periodic TxFIFO
        depctl.b.usbactep = 1;
        dwc_write_reg32(DWC_REG_IN_EP_REG(1), depctl.d32);
    }

    depctl.d32 = dwc_read_reg32(DWC_REG_OUT_EP_REG(2));
    if (!depctl.b.usbactep) {
        depctl.b.mps = BULK_EP_MPS;
        depctl.b.eptype = 2;//BULK_STYLE
        depctl.b.setd0pid = 1;
        depctl.b.txfnum = 0;   //Non-Periodic TxFIFO
        depctl.b.usbactep = 1;
        dwc_write_reg32(DWC_REG_OUT_EP_REG(2), depctl.d32);
    }
#endif

    DCTL::Get().ReadFrom(mmio).set_cgnpinnak(1).WriteTo(mmio);

    /* high speed */
#if 0 // astro
    GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(9).WriteTo(mmio);
    regs->gusbcfg.usbtrdtim = 9;
#else
    GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(5).WriteTo(mmio);
#endif

    dci_intf_->SetSpeed(USB_SPEED_HIGH);
}

void Dwc2::HandleRxStatusQueueLevel() {
    bool need_more = false;
    auto* mmio = get_mmio();

    for (uint8_t ep_num = 0; ep_num < MAX_EPS_CHANNELS; ep_num++) {
        if (DAINTMSK::Get().ReadFrom(mmio).mask() & (1 << ep_num)) {
            if (WritePacket(ep_num)) {
                need_more = true;
            }
        }
    }
    if (!need_more) {
        zxlogf(LINFO, "turn off nptxfempty\n");
        GINTMSK::Get().ReadFrom(mmio).set_nptxfempty(0).WriteTo(mmio);
    }
}

void Dwc2::HandleInEpInterrupt() {
}

void Dwc2::HandleOutEpInterrupt() {
}

void Dwc2::HandleTxFifoEmpty() {
}

void Dwc2::StartEp0() {
    auto* mmio = get_mmio();

    auto doeptsize0 = DEPTSIZ0::Get().FromValue(0);

    doeptsize0.set_supcnt(3);
    doeptsize0.set_pktcnt(1);
    doeptsize0.set_xfersize(8 * 3);
    doeptsize0.WriteTo(mmio);

//??    ep0_state = EP0_STATE_IDLE;

    DEPCTL::Get(16).ReadFrom(mmio).set_epena(1).WriteTo(mmio);
}

bool Dwc2::WritePacket(uint8_t ep_num) {
    dwc_endpoint_t* ep = &eps[ep_num];
    auto* mmio = get_mmio();

    uint32_t len = ep->req_length - ep->req_offset;
    if (len > ep->max_packet_size)
        len = ep->max_packet_size;

    uint32_t dwords = (len + 3) >> 2;
    uint8_t *req_buffer = &ep->req_buffer[ep->req_offset];

    auto txstatus = GNPTXSTS::Get().ReadFrom(mmio);

    while  (ep->req_offset < ep->req_length && txstatus.nptxqspcavail() > 0 && txstatus.nptxfspcavail() > dwords) {
zxlogf(LINFO, "ep_num %d nptxqspcavail %u nptxfspcavail %u dwords %u\n", ep->ep_num, txstatus.nptxqspcavail(), txstatus.nptxfspcavail(), dwords);

        volatile uint32_t* fifo = DWC_REG_DATA_FIFO(mmio_->get(), ep_num);
    
        for (uint32_t i = 0; i < dwords; i++) {
            uint32_t temp = *((uint32_t*)req_buffer);
//zxlogf(LINFO, "write %08x\n", temp);
            *fifo = temp;
            req_buffer += 4;
        }
    
        ep->req_offset += len;

        len = ep->req_length - ep->req_offset;
        if (len > ep->max_packet_size)
            len = ep->max_packet_size;

        dwords = (len + 3) >> 2;
        txstatus.ReadFrom(mmio);
    }

    if (ep->req_offset < ep->req_length) {
        // enable txempty
        zxlogf(LINFO, "turn on nptxfempty\n");
        GINTMSK::Get().ReadFrom(mmio).set_nptxfempty(1).WriteTo(mmio);
        return true;
    } else {
        return false;
    }
}

void Dwc2::FlushFifo(uint32_t fifo_num) {
    auto* mmio = get_mmio();
    auto grstctl = GRSTCTL::Get().ReadFrom(mmio);

    grstctl.set_txfflsh(1);
    grstctl.set_txfnum(fifo_num);
    grstctl.WriteTo(mmio);
    
    uint32_t count = 0;
    do {
        grstctl.ReadFrom(mmio);
        if (++count > 10000)
            break;
    } while (grstctl.txfflsh() == 1);

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));

    if (fifo_num == 0) {
        return;
    }

    grstctl.set_reg_value(0).set_rxfflsh(1).WriteTo(mmio);

    count = 0;
    do {
        grstctl.ReadFrom(mmio);
        if (++count > 10000)
            break;
    } while (grstctl.txfflsh() == 1);

    zx_nanosleep(zx_deadline_after(ZX_USEC(1)));
}

zx_status_t Dwc2::InitController() {
    auto* mmio = get_mmio();

    // Is this necessary?
    auto grstctl = GRSTCTL::Get();
    while (grstctl.ReadFrom(mmio).ahbidle() == 0) {
        usleep(1000);
    }

    GRSTCTL::Get().ReadFrom(mmio).set_csftrst(1).WriteTo(mmio);

    bool done = false;
    for (int i = 0; i < 1000; i++) {
        if (grstctl.ReadFrom(mmio).csftrst() == 0) {
            usleep(10 * 1000);
            done = true;
            break;
        }
        usleep(1000);
    }
    if (!done) {
        return ZX_ERR_TIMED_OUT;
    }

    GUSBCFG::Get().ReadFrom(mmio).set_force_dev_mode(1).WriteTo(mmio);
    GAHBCFG::Get().ReadFrom(mmio).set_dmaenable(0).WriteTo(mmio);
printf("did regs->gahbcfg.dmaenable\n");

#if 0 // astro
    GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(9).WriteTo(mmio);
#else
    GUSBCFG::Get().ReadFrom(mmio).set_usbtrdtim(5).WriteTo(mmio);
#endif

    // ???
    DCTL::Get().ReadFrom(mmio).set_sftdiscon(1).WriteTo(mmio);
    DCTL::Get().ReadFrom(mmio).set_sftdiscon(0).WriteTo(mmio);

    // reset phy clock
// needed?    regs->pcgcctl = 0;

    // RX fifo size
    GRXFSIZ::Get().FromValue(0).set_size(256).WriteTo(mmio);

    // TX fifo size
    GNPTXFSIZ::Get().FromValue(0).set_depth(256).set_startaddr(256).WriteTo(mmio);

    FlushFifo(0x10);

    GRSTCTL::Get().ReadFrom(mmio).set_intknqflsh(1).WriteTo(mmio);

    /* Clear all pending Device Interrupts */
    DIEPMSK::Get().FromValue(0).WriteTo(mmio);
    DOEPMSK::Get().FromValue(0).WriteTo(mmio);
    DAINT::Get().FromValue(0xffffffff).WriteTo(mmio);
    DAINTMSK::Get().FromValue(0).WriteTo(mmio);

    for (int i = 0; i < DWC_MAX_EPS; i++) {
        DEPCTL::Get(i).FromValue(0).WriteTo(mmio);
        DEPTSIZ::Get(i).FromValue(0).WriteTo(mmio);
    }

    auto gintmsk = GINTMSK::Get().FromValue(0);

    gintmsk.set_rxstsqlvl(1);
    gintmsk.set_usbreset(1);
    gintmsk.set_enumdone(1);
    gintmsk.set_inepintr(1);
    gintmsk.set_outepintr(1);
//    gintmsk.set_sof_intr(1);
    gintmsk.set_usbsuspend(1);


    gintmsk.set_ginnakeff(1);
    gintmsk.set_goutnakeff(1);


/*
    gintmsk.set_modemismatch(1);
    gintmsk.set_otgintr(1);
    gintmsk.set_conidstschng(1);
    gintmsk.set_wkupintr(1);
    gintmsk.set_disconnect(0);
    gintmsk.set_sessreqintr(1);
*/

//printf("ghwcfg1 %08x ghwcfg2 %08x ghwcfg3 %08x\n", regs->ghwcfg1, regs->ghwcfg2, regs->ghwcfg3);

// do we need this? regs->gotgint = 0xFFFFFFF;
    GINTSTS::Get().FromValue(0xFFFFFFF).WriteTo(mmio);

zxlogf(LINFO, "enabling interrupts %08x\n", gintmsk.reg_value());

    gintmsk.WriteTo(mmio);

    GAHBCFG::Get().ReadFrom(mmio).set_glblintrmsk(1).WriteTo(mmio);

    return ZX_OK;
}

static void dwc_request_queue(void* ctx, usb_request_t* req, const usb_request_complete_t* cb) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);

    zxlogf(INFO, "XXXXXXX dwc_request_queue ep: 0x%02x length %zu\n", req->header.ep_address, req->header.length);
    uint8_t ep_num = DWC_ADDR_TO_INDEX(req->header.ep_address);
    if (ep_num == 0 || ep_num >= countof(dwc->eps)) {
        zxlogf(ERROR, "dwc_request_queue: bad ep address 0x%02X\n", req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
        return;
    }

    dwc_ep_queue(dwc, ep_num, req);
}

static zx_status_t dwc_set_interface(void* ctx, const usb_dci_interface_protocol_t* dci_intf) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    memcpy(&dwc->dci_intf, dci_intf, sizeof(dwc->dci_intf));
    return ZX_OK;
}

static zx_status_t dwc_config_ep(void* ctx, const usb_endpoint_descriptor_t* ep_desc,
                                 const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    return dwc_ep_config(dwc, ep_desc, ss_comp_desc);
}

static zx_status_t dwc_disable_ep(void* ctx, uint8_t ep_addr) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    return dwc_ep_disable(dwc, ep_addr);
}

static zx_status_t dwc_set_stall(void* ctx, uint8_t ep_address) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    return dwc_ep_set_stall(dwc, DWC_ADDR_TO_INDEX(ep_address), true);
}

static zx_status_t dwc_clear_stall(void* ctx, uint8_t ep_address) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    return dwc_ep_set_stall(dwc, DWC_ADDR_TO_INDEX(ep_address), false);
}

static size_t dwc_get_request_size(void* ctx) {
    return sizeof(usb_request_t) + sizeof(dwc_usb_req_internal_t);
}

static zx_status_t dwc_cancel_all(void* ctx, uint8_t ep) {
    return ZX_ERR_NOT_SUPPORTED;
}

static usb_dci_protocol_ops_t dwc_dci_protocol = {
    .request_queue = dwc_request_queue,
    .set_interface = dwc_set_interface,
    .config_ep = dwc_config_ep,
    .disable_ep = dwc_disable_ep,
    .ep_set_stall = dwc_set_stall,
    .ep_clear_stall = dwc_clear_stall,
    .get_request_size = dwc_get_request_size,
    .cancel_all = dwc_cancel_all
};

#if 0
static zx_status_t dwc_set_mode(void* ctx, usb_mode_t mode) {
    auto* dwc = static_cast<dwc_usb_t*>(ctx);
    zx_status_t status = ZX_OK;

    if (mode != USB_MODE_PERIPHERAL && mode != USB_MODE_NONE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (dwc->usb_mode == mode) {
        return ZX_OK;
    }

    // Shutdown if we are in device mode
    if (dwc->usb_mode == USB_MODE_PERIPHERAL) {
        dwc_irq_stop(dwc);
    }

/* may be unsupported
    status = usb_mode_switch_set_mode(&dwc->ums, mode);
    if (status != ZX_OK) {
        goto fail;
    }
*/

    if (mode == USB_MODE_PERIPHERAL) {
        status = dwc_irq_start(dwc);
        if (status != ZX_OK) {
            zxlogf(ERROR, "dwc3_set_mode: pdev_map_interrupt failed\n");
            goto fail;
        }
    }

    dwc->usb_mode = mode;
    return ZX_OK;

fail:
//    usb_mode_switch_set_mode(&dwc->ums, USB_MODE_NONE);
    dwc->usb_mode = USB_MODE_NONE;

    return status;
}

usb_mode_switch_protocol_ops_t dwc_ums_protocol = {
    .set_mode = dwc_set_mode,
};
#endif

/*
static zx_status_t dwc_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_USB_DCI: {
        usb_dci_protocol_t* proto = out;
        proto->ops = &dwc_dci_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = out;
        proto->ops = &dwc_ums_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

*/

static void dwc_unbind(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_unbind not implemented\n");
}

static void dwc_release(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_release not implemented\n");
}

static zx_protocol_device_t dwc_device_proto = [](){
    zx_protocol_device_t ops;
    ops.version = DEVICE_OPS_VERSION;
//    ops.get_protocol = dwc_get_protocol;
    ops.unbind = dwc_unbind;
    ops.release = dwc_release;
    return ops;
}();

// Bind is the entry point for this driver.
static zx_status_t dwc_bind(void* ctx, zx_device_t* dev) {
    zxlogf(TRACE, "dwc_bind: dev = %p\n", dev);

    // Allocate a new device object for the bus.
    auto* dwc = static_cast<dwc_usb_t*>(calloc(1, sizeof(dwc_usb_t)));
    if (!dwc) {
        zxlogf(ERROR, "dwc_bind: bind failed to allocate usb_dwc struct\n");
        return ZX_ERR_NO_MEMORY;
    }

#if SINGLE_EP_IN_QUEUE
    list_initialize(&dwc->queued_in_reqs);
#endif

    zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_PDEV, &dwc->pdev);
    if (status != ZX_OK) {
        free(dwc);
        return status;
    }
/*
    status = device_get_protocol(dev, ZX_PROTOCOL_USB_MODE_SWITCH, &dwc->ums);
    if (status != ZX_OK) {
        free(dwc);
        return status;
    }
*/

    // hack for astro USB tuning (also optional)
//    device_get_protocol(dev, ZX_PROTOCOL_ASTRO_USB, &dwc->astro_usb);

    for (uint8_t i = 0; i < countof(dwc->eps); i++) {
        dwc_endpoint_t* ep = &dwc->eps[i];
        ep->ep_num = i;
        list_initialize(&ep->queued_reqs);
    }
    dwc->eps[0].req_buffer = dwc->ep0_buffer;

   device_add_args_t args = {};
    args.version = DEVICE_ADD_ARGS_VERSION;
    args.name = "dwc2";
    args.ctx = dwc;
    args.ops = &dwc_device_proto;
    args.proto_id = ZX_PROTOCOL_USB_DCI;
    args.proto_ops = &dwc_dci_protocol;

    ddk::PDev pdev_(&dwc->pdev);

    status = pdev_.MapMmio(0, &dwc->mmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: pdev_map_mmio_buffer failed\n");
        goto error_return;
    }
    dwc->regs = static_cast<volatile void*>(dwc->mmio_->get());

    status = pdev_get_bti(&dwc->pdev, 0, &dwc->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: bind failed to get bti handle.\n");
        goto error_return;
    }

    dwc->parent = dev;
//    dwc->usb_mode = USB_MODE_NONE;

//    if (dwc->astro_usb.ops) {
//        astro_usb_do_usb_tuning(&dwc->astro_usb, false, true);
//    }

    if ((status = dwc_irq_start(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: dwc_irq_start failed\n");
        goto error_return;
    }

    if ((status = device_add(dev, &args, &dwc->zxdev)) != ZX_OK) {
        free(dwc);
        return status;
    }

    zxlogf(TRACE, "usb_dwc: bind success!\n");
    return ZX_OK;

error_return:
    if (dwc) {
//        if (dwc->regs) {
//            zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)dwc->regs, mmio_size);
//        }
//        zx_handle_close(mmio_handle);
        zx_handle_close(dwc->irq_handle);
        zx_handle_close(dwc->bti_handle);
        free(dwc);
    }

    return status;
}

zx_status_t Dwc2::Create(void* ctx, zx_device_t* parent) {
    pdev_protocol_t pdev;

    auto status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &pdev);
    if (status != ZX_OK) {
        return status;
    }

    fbl::AllocChecker ac;
    auto mt_usb = fbl::make_unique_checked<Dwc2>(&ac, parent, &pdev);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    status = mt_usb->Init();
    if (status != ZX_OK) {
        return status;
    }

    // devmgr is now in charge of the device.
    __UNUSED auto* dummy = mt_usb.release();
    return ZX_OK;
}

zx_status_t Dwc2::Init() {
#if SINGLE_EP_IN_QUEUE
    list_initialize(&queued_in_reqs);
#endif

    for (uint8_t i = 0; i < countof(eps); i++) {
        dwc_endpoint_t* ep = &eps[i];
        ep->ep_num = i;
        list_initialize(&ep->queued_reqs);
    }
    eps[0].req_buffer = ep0_buffer;

    auto status = pdev_.MapMmio(0, &mmio_);
    if (status != ZX_OK) {
        return status;
    }

    status = pdev_.GetInterrupt(0, &irq_);
    if (status != ZX_OK) {
        return status;
    }

    if ((status = InitController()) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed to init controller.\n");
        return status;
    }

    status = DdkAdd("dwc2");
    if (status != ZX_OK) {
        return status;
    }

    int rc = thrd_create_with_name(&irq_thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Dwc2*>(arg)->IrqThread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "dwc2-interrupt-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

    return ZX_OK;
}

void Dwc2::DdkUnbind() {
    irq_.destroy();
    thrd_join(irq_thread_, nullptr);
}

void Dwc2::DdkRelease() {
    delete this;
}

int Dwc2::IrqThread() {
    auto* mmio = get_mmio();

    while (1) {
        auto wait_res = irq_.wait(nullptr);
        if (wait_res != ZX_OK) {
            zxlogf(ERROR, "dwc_usb: irq wait failed, retcode = %d\n", wait_res);
        }

        //?? is while loop necessary?
        while (1) {
            auto gintsts = GINTSTS::Get().ReadFrom(mmio);
            auto gintmsk = GINTMSK::Get().ReadFrom(mmio);
            gintsts.set_reg_value(gintsts.reg_value() & gintmsk.reg_value());

            if (gintsts.reg_value() == 0) {
                break;
            }

            // acknowledge
            gintsts.WriteTo(mmio);

            zxlogf(LINFO, "dwc_handle_irq:");
            if (gintsts.modemismatch()) zxlogf(LINFO, " modemismatch");
            if (gintsts.otgintr()) zxlogf(LINFO, " otgintr");
            if (gintsts.sof_intr()) zxlogf(LINFO, " sof_intr");
            if (gintsts.rxstsqlvl()) zxlogf(LINFO, " rxstsqlvl");
            if (gintsts.nptxfempty()) zxlogf(LINFO, " nptxfempty");
            if (gintsts.ginnakeff()) zxlogf(LINFO, " ginnakeff");
            if (gintsts.goutnakeff()) zxlogf(LINFO, " goutnakeff");
            if (gintsts.ulpickint()) zxlogf(LINFO, " ulpickint");
            if (gintsts.i2cintr()) zxlogf(LINFO, " i2cintr");
            if (gintsts.erlysuspend()) zxlogf(LINFO, " erlysuspend");
            if (gintsts.usbsuspend()) zxlogf(LINFO, " usbsuspend");
            if (gintsts.usbreset()) zxlogf(LINFO, " usbreset");
            if (gintsts.enumdone()) zxlogf(LINFO, " enumdone");
            if (gintsts.isooutdrop()) zxlogf(LINFO, " isooutdrop");
            if (gintsts.eopframe()) zxlogf(LINFO, " eopframe");
            if (gintsts.restoredone()) zxlogf(LINFO, " restoredone");
            if (gintsts.epmismatch()) zxlogf(LINFO, " epmismatch");
            if (gintsts.inepintr()) zxlogf(LINFO, " inepintr");
            if (gintsts.outepintr()) zxlogf(LINFO, " outepintr");
            if (gintsts.incomplisoin()) zxlogf(LINFO, " incomplisoin");
            if (gintsts.incomplisoout()) zxlogf(LINFO, " incomplisoout");
            if (gintsts.fetsusp()) zxlogf(LINFO, " fetsusp");
            if (gintsts.resetdet()) zxlogf(LINFO, " resetdet");
            if (gintsts.port_intr()) zxlogf(LINFO, " port_intr");
            if (gintsts.host_channel_intr()) zxlogf(LINFO, " host_channel_intr");
            if (gintsts.ptxfempty()) zxlogf(LINFO, " ptxfempty");
            if (gintsts.lpmtranrcvd()) zxlogf(LINFO, " lpmtranrcvd");
            if (gintsts.conidstschng()) zxlogf(LINFO, " conidstschng");
            if (gintsts.disconnect()) zxlogf(LINFO, " disconnect");
            if (gintsts.sessreqintr()) zxlogf(LINFO, " sessreqintr");
            if (gintsts.wkupintr()) zxlogf(LINFO, " wkupintr");
            zxlogf(LINFO, "\n");

            if (gintsts.usbreset()) {
                HandleReset();
            }
            if (gintsts.usbsuspend()) {
                HandleSuspend();
            }
            if (gintsts.enumdone()) {
                HandleEnumDone();
            }
            if (gintsts.rxstsqlvl()) {
                HandleRxStatusQueueLevel();
            }
            if (gintsts.inepintr()) {
                HandleInEpInterrupt();
            }
            if (gintsts.outepintr()) {
                HandleOutEpInterrupt();
            }
            if (gintsts.nptxfempty()) {
                HandleTxFifoEmpty();
            }
        }
    }

    zxlogf(INFO, "dwc_usb: irq thread finished\n");
    return 0;
}

void Dwc2::UsbDciRequestQueue(usb_request_t* req, const usb_request_complete_t* cb) {
/*
    auto* ep = EndpointFromAddress(req->header.ep_address);
    if (ep == nullptr) {
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
        return;
    }

    fbl::AutoLock lock(&ep->lock);

    if (!ep->enabled) {
        lock.release();
        usb_request_complete(req, ZX_ERR_BAD_STATE, 0, cb);
        return;
    }

    ep->queued_reqs.push(Request(req, *cb, sizeof(usb_request_t)));
    EpQueueNextLocked(ep);
*/
}

zx_status_t Dwc2::UsbDciSetInterface(const usb_dci_interface_protocol_t* interface) {
    // TODO - handle interface == nullptr for tear down path?

    if (dci_intf_.has_value()) {
        zxlogf(ERROR, "%s: dci_intf_ already set\n", __func__);
        return ZX_ERR_BAD_STATE;
    }

    dci_intf_ = ddk::UsbDciInterfaceProtocolClient(interface);

    return ZX_OK;
}

 zx_status_t Dwc2::UsbDciConfigEp(const usb_endpoint_descriptor_t* ep_desc,
                                   const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    return ZX_OK;
}

zx_status_t Dwc2::UsbDciDisableEp(uint8_t ep_address) {
    return ZX_OK;
}

zx_status_t Dwc2::UsbDciEpSetStall(uint8_t ep_address) {
    return ZX_OK;
}

zx_status_t Dwc2::UsbDciEpClearStall(uint8_t ep_address) {

    return ZX_OK;
}

size_t Dwc2::UsbDciGetRequestSize() {
    return Request::RequestSize(sizeof(usb_request_t));
}

zx_status_t Dwc2::UsbDciCancelAll(uint8_t ep) {
    return ZX_ERR_NOT_SUPPORTED;
}


static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = Dwc2::Create;
    return ops;
}();

} // namespace dwc2

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dwc2, dwc2::driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
ZIRCON_DRIVER_END(dwc2)
// clang-format on
