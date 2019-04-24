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

static zx_status_t usb_dwc_softreset_core(dwc_usb_t* dwc) {
/* do we need this? */
    auto grstctl = GRSTCTL::Get();
    while (grstctl.ReadFrom(dwc->mmio()).ahbidle() == 0) {
        usleep(1000);
    }

//    dwc_grstctl_t grstctl = {0};
//    grstctl.csftrst = 1;
    GRSTCTL::Get().ReadFrom(dwc->mmio()).set_csftrst(1).WriteTo(dwc->mmio());

    for (int i = 0; i < 1000; i++) {
        if (grstctl.ReadFrom(dwc->mmio()).csftrst() == 0) {
            usleep(10 * 1000);
            return ZX_OK;
        }
        usleep(1000);
    }
    return ZX_ERR_TIMED_OUT;
}

static zx_status_t usb_dwc_setupcontroller(dwc_usb_t* dwc) {
    auto* mmio = dwc->mmio();

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

    dwc_flush_fifo(dwc, 0x10);

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
        mtx_init(&ep->lock, mtx_plain);
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

    if ((status = usb_dwc_softreset_core(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed to reset core.\n");
        goto error_return;
    }

    if ((status = usb_dwc_setupcontroller(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed setup controller.\n");
        goto error_return;
    }

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
    auto status = pdev_.MapMmio(0, &mmio_);
    if (status != ZX_OK) {
        return status;
    }

    status = pdev_.GetInterrupt(0, &irq_);
    if (status != ZX_OK) {
        return status;
    }

    status = DdkAdd("dwc2");
    if (status != ZX_OK) {
        return status;
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

    // Now that the usb-peripheral driver has bound, we can start things up.
    int rc = thrd_create_with_name(&irq_thread_,
                                   [](void* arg) -> int {
                                       return reinterpret_cast<Dwc2*>(arg)->IrqThread();
                                   },
                                   reinterpret_cast<void*>(this),
                                   "mt-usb-irq-thread");
    if (rc != thrd_success) {
        return ZX_ERR_INTERNAL;
    }

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
    return ZX_OK;
}


static zx_driver_ops_t driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = dwc_bind;
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
