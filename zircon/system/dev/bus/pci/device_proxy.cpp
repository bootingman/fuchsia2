#include "device_proxy.h"
#include "common.h"
#include <ddk/binding.h>
#include <ddk/debug.h>

#define DEVICE_PROXY_UNIMPLEMENTED                       \
    zxlogf(INFO, "[DeviceProxy] called %s\n", __func__); \
    return ZX_ERR_NOT_SUPPORTED

// This file contains the PciProtocol implementation that is proxied over
// a channel to the specific pci::Device objects in the PCI Bus Driver.
namespace pci {

zx_status_t DeviceProxy::Create(zx_device_t* parent, zx_handle_t rpcch, const char* name) {
    DeviceProxy* dp = new DeviceProxy(parent, rpcch);
    return dp->DdkAdd(name);
}

zx_status_t DeviceProxy::RpcRequest(uint32_t op, zx_handle_t* handle, PciRpcMsg* req,
                                    PciRpcMsg* resp) {
    if (rpcch_ == ZX_HANDLE_INVALID) {
        return ZX_ERR_NOT_SUPPORTED;
    }

    uint32_t in_handle_cnt = (op == PCI_OP_CONNECT_SYSMEM) ? 1 : 0;
    uint32_t handle_cnt = 0;
    if (handle) {
        // Since only the caller knows if they expected a valid handle back, make
        // sure the handle is invalid if we didn't get one.
        *handle = ZX_HANDLE_INVALID;
        handle_cnt = 1;
    }

    req->ordinal = op;
    zx_channel_call_args_t cc_args = {
        .wr_bytes = req,
        .rd_bytes = resp,
        .rd_handles = handle,
        .wr_num_bytes = sizeof(*req),
        .rd_num_bytes = sizeof(*resp),
        .rd_num_handles = handle_cnt,
        .wr_handles = in_handle_cnt ? &req->handle : NULL,
        .wr_num_handles = in_handle_cnt,
    };

    uint32_t actual_bytes;
    uint32_t actual_handles;
    zx_status_t st = rpcch_.call(0, zx::time(ZX_TIME_INFINITE), &cc_args, &actual_bytes,
                                 &actual_handles);
    if (st != ZX_OK) {
        return st;
    }

    if (actual_bytes != sizeof(*resp)) {
        return ZX_ERR_INTERNAL;
    }

    return resp->ordinal;
}

zx_status_t DeviceProxy::DdkGetProtocol(uint32_t proto_id, void* out) {
    if (proto_id == ZX_PROTOCOL_PCI) {
        auto proto = static_cast<pci_protocol_t*>(out);
        proto->ctx = this;
        proto->ops = &pci_protocol_ops_;
        return ZX_OK;
    }

    return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t DeviceProxy::PciGetBar(uint32_t bar_id, zx_pci_bar_t* out_bar) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};
    zx_handle_t handle;

    req.bar.id = bar_id;
    zx_status_t st = RpcRequest(PCI_OP_GET_BAR, &handle, &req, &resp);
    if (st != ZX_OK) {
        return st;
    }

    *out_bar = resp.bar;
    out_bar->handle = handle;
    if (out_bar->type == ZX_PCI_BAR_TYPE_PIO) {
        // TODO(cja): Figure out once and for all what the story is with IO on ARM.
#if __x86_64__
        // x86 PIO space access requires permission in the I/O bitmap. If an IO BAR
        // is used then the handle returned corresponds to a resource with access to
        // this range of IO space.
        st = zx_ioports_request(handle, static_cast<uint16_t>(out_bar->addr),
                                static_cast<uint32_t>(out_bar->size));
        if (st != ZX_OK) {
            zxlogf(ERROR, "Failed to map IO window for bar into process: %d\n", st);
            return st;
        }
        out_bar->handle = ZX_HANDLE_INVALID;
#else
        zxlogf(INFO, "%s: PIO bars may not be supported correctly on this arch. "
                     "Please have someone check this!\n",
               __func__);
        return ZX_ERR_NOT_SUPPORTED;
#endif
    }

    return ZX_OK;
}

zx_status_t DeviceProxy::PciEnableBusMaster(bool enable) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.enable = enable;
    return RpcRequest(PCI_OP_ENABLE_BUS_MASTER, NULL, &req, &resp);
}

zx_status_t DeviceProxy::PciResetDevice() {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    return RpcRequest(PCI_OP_RESET_DEVICE, NULL, &req, &resp);
}

zx_status_t DeviceProxy::PciMapInterrupt(zx_status_t which_irq, zx::interrupt* out_handle) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};
    zx_handle_t handle;

    req.irq.which_irq = which_irq;
    zx_status_t st = RpcRequest(PCI_OP_MAP_INTERRUPT, &handle, &req, &resp);
    if (st == ZX_OK) {
        *out_handle = zx::interrupt(handle);
    }
    return st;
}

zx_status_t DeviceProxy::PciQueryIrqMode(zx_pci_irq_mode_t mode, uint32_t* out_max_irqs) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.irq.mode = mode;
    zx_status_t st = RpcRequest(PCI_OP_QUERY_IRQ_MODE, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_max_irqs = resp.irq.max_irqs;
    }
    return st;
}

zx_status_t DeviceProxy::PciSetIrqMode(zx_pci_irq_mode_t mode, uint32_t requested_irq_count) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.irq.mode = mode;
    req.irq.requested_irqs = requested_irq_count;
    return RpcRequest(PCI_OP_SET_IRQ_MODE, NULL, &req, &resp);
}

zx_status_t DeviceProxy::PciGetDeviceInfo(zx_pcie_device_info_t* out_info) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    zx_status_t st = RpcRequest(PCI_OP_GET_DEVICE_INFO, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_info = resp.info;
    }
    return st;
}

zx_status_t DeviceProxy::PciConfigRead(uint16_t offset, size_t width, uint32_t* out_value) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.cfg.offset = offset;
    req.cfg.width = static_cast<uint16_t>(width);
    zx_status_t st = RpcRequest(PCI_OP_CONFIG_READ, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_value = resp.cfg.value;
    }
    return st;
}

zx_status_t DeviceProxy::PciConfigWrite(uint16_t offset, size_t width, uint32_t value) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.cfg.offset = offset;
    req.cfg.width = static_cast<uint16_t>(width);
    req.cfg.value = value;
    return RpcRequest(PCI_OP_CONFIG_WRITE, NULL, &req, &resp);
}

zx_status_t DeviceProxy::PciGetFirstCapability(uint8_t cap_id, uint8_t* out_offset) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.cap.id = cap_id;
    req.cap.offset = kPciCapabilityOffsetFirst;
    zx_status_t st = RpcRequest(PCI_OP_GET_NEXT_CAPABILITY, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_offset = static_cast<uint8_t>(resp.cap.offset);
    }
    return st;
}

zx_status_t DeviceProxy::PciGetNextCapability(uint8_t cap_id, uint8_t offset,
                                              uint8_t* out_offset) {
    PciRpcMsg req = {};
    PciRpcMsg resp = {};

    req.cap.id = cap_id;
    req.cap.offset = offset;
    zx_status_t st = RpcRequest(PCI_OP_GET_NEXT_CAPABILITY, NULL, &req, &resp);
    if (st == ZX_OK) {
        *out_offset = static_cast<uint8_t>(resp.cap.offset);
    }
    return st;
}

// TODO(ZX-3146): These methods need to be deleted, or refactored.
zx_status_t DeviceProxy::PciGetAuxdata(const char* args,
                                       void* out_data_buffer,
                                       size_t data_size,
                                       size_t* out_data_actual) {
    DEVICE_PROXY_UNIMPLEMENTED;
}

zx_status_t DeviceProxy::PciGetBti(uint32_t index, zx::bti* out_bti) {
    DEVICE_PROXY_UNIMPLEMENTED;
}

} // namespace pci

static zx_status_t pci_device_proxy_create(void* ctx, zx_device_t* parent, const char* name,
                                           const char* args, zx_handle_t rpcch) {
    char proxy_name[ZX_MAX_NAME_LEN] = {};
    snprintf(proxy_name, sizeof(proxy_name), "%s.proxy", name);
    return pci::DeviceProxy::Create(parent, rpcch, proxy_name);
}

static zx_driver_ops_t pci_device_proxy_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .init = nullptr,
    .bind = nullptr,
    .create = pci_device_proxy_create,
    .release = nullptr,
};

ZIRCON_DRIVER_BEGIN(pci_device_proxy, pci_device_proxy_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(pci_device_proxy)
