// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "msm8x53-sdhci.h"

#include <unistd.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/platform-defs.h>
#include <ddktl/pdev.h>
#include <fbl/alloc_checker.h>
#include <fbl/unique_ptr.h>

#include "msm8x53-sdhci-reg.h"

namespace sdhci {

zx_status_t Msm8x53Sdhci::Create(void* ctx, zx_device_t* parent) {
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "%s: ZX_PROTOCOL_PDEV not available\n", __FILE__);
        return ZX_ERR_NO_RESOURCES;
    }

    std::optional<ddk::MmioBuffer> core_mmio;
    zx_status_t status = pdev.MapMmio(0, &core_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> hc_mmio;
    if ((status = pdev.MapMmio(1, &hc_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    std::optional<ddk::MmioBuffer> clk_mmio;
    if ((status = pdev.MapMmio(2, &clk_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: MapMmio failed\n", __FILE__);
        return status;
    }

    pdev_mmio_t hc_pdev_mmio;
    if ((status = pdev.GetMmio(1, &hc_pdev_mmio)) != ZX_OK) {
        zxlogf(ERROR, "%s: GetMmio failed\n", __FILE__);
        return status;
    }

    zx::vmo hc_vmo(hc_pdev_mmio.vmo);

    zx::interrupt irq;
    if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
        zxlogf(ERROR, "%s: Failed to map interrupt\n", __FILE__);
        return status;
    }

    fbl::AllocChecker ac;
    fbl::unique_ptr<Msm8x53Sdhci> device(
        new (&ac) Msm8x53Sdhci(parent, *std::move(core_mmio), *std::move(hc_mmio),
                               *std::move(clk_mmio), std::move(irq), std::move(hc_vmo),
                               hc_pdev_mmio.offset));
    if (!ac.check()) {
        zxlogf(ERROR, "%s: Msm8x53Sdhci alloc failed\n", __FILE__);
        return ZX_ERR_NO_MEMORY;
    }

    if ((status = device->Init()) != ZX_OK) {
        return status;
    }

    if ((status = device->Bind()) != ZX_OK) {
        return status;
    }

    __UNUSED auto* dummy = device.release();

    return ZX_OK;
}

static void EnableClk(ddk::MmioBuffer* mmio, const uint32_t addr) {
    uint32_t cbcr = mmio->Read32(addr);
    mmio->Write32(cbcr | 1, addr);

    for (int i = 0; i < 500; i++) {
        uint32_t status = mmio->Read32(addr) & 0xf000'0000;
        if (status == 0x0000'0000 || status == 0x2000'0000)
            return;
        usleep(1);
    }

    zxlogf(INFO, "[SDMMC] Failed to enable clock %08x\n", addr);
}

static void SetClkRate(ddk::MmioBuffer* mmio, const uint32_t addr) {
    constexpr uint32_t m_val = 1;
    constexpr uint32_t n_val = 4;
    constexpr uint32_t div_val = 12;
    constexpr uint32_t src_val = 0;
    constexpr uint32_t div_src_mask = 0x0000'071f;
    constexpr uint32_t mode_mask = 0x0000'3000;
    constexpr uint32_t mode_dual_edge = 0x0000'2000;

    // [0] CMD
    // [1] CFG
    // [2] M
    // [3] N
    // [4] D

    uint32_t cfg = mmio->Read32(addr + 0x04);

    mmio->Write32(m_val, addr + 0x08);
    mmio->Write32(~(n_val - m_val) * !!n_val, addr + 0x0c);
    mmio->Write32(~n_val, addr + 0x10);

    cfg &= ~div_src_mask;
    cfg |= (2 * div_val) - 1;
    cfg |= (src_val << 8);

    cfg &= ~mode_mask;
    cfg |= mode_dual_edge;

    mmio->Write32(cfg, addr + 0x04);

    uint32_t cmd = mmio->Read32(addr);
    mmio->Write32(cmd | 1, addr);

    for (int i = 0; i < 500; i++) {
        if ((mmio->Read32(addr) & 1) == 0)
            return;
        usleep(1);
    }

    zxlogf(INFO, "[SDMMC] Failed to set clock rate for %08x\n", addr);
}

zx_status_t Msm8x53Sdhci::Init() {
    // constexpr uint32_t kSdcc1AppsCbcr = 0x42018;
    // constexpr uint32_t kSdcc1AhbCbcr = 0x4201c;

    // constexpr uint32_t kFifoAltEn = 1 << 10;
    // Note: mci_removed is false.
    // zxlogf(INFO, "[SDMMC] MCI_VERSION=%08x\n", mmio_.Read32(0x24050));

    // EnableClk(&clk_mmio_, kSdcc1AhbCbcr);
    // EnableClk(&clk_mmio_, kSdcc1AppsCbcr);
    // SetClkRate(&clk_mmio_, 0x42004);

    hc_mmio_.Write32(0xa1c, 0x10c);  // REQUIRED
    zxlogf(INFO, "[SDMMC] CORE_VENDOR_SPEC=%08x\n", hc_mmio_.Read32(0x10c));

    // Set CORE_HC_MODE
    core_mmio_.Write32(1, 0x78);  // REQUIRED
    // core_mmio_.Write32((1 << 13) | 1, 0x78);
    // zxlogf(INFO, "[SDMMC] CORE_HC_MODE=%08x\n", core_mmio_.Read32(0x78));

    // hc_mmio_.Write32(hc_mmio_.Read32(0x1b0) & ~kFifoAltEn, 0x1b0);

    return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetInterrupt(zx::interrupt* out_irq) {
    out_irq->reset(irq_.release());
    return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset) {
    out_mmio->reset(hc_vmo_.release());
    *out_offset = hc_vmo_offset_;
    return ZX_OK;
}

zx_status_t Msm8x53Sdhci::SdhciGetBti(uint32_t index, zx::bti* out_bti) {
    ddk::PDev pdev(parent());
    if (!pdev.is_valid()) {
        return ZX_ERR_NO_RESOURCES;
    }

    return pdev.GetBti(index, out_bti);
}

uint32_t Msm8x53Sdhci::SdhciGetBaseClock() {
    zxlogf(INFO, "[SDMMC] %s\n", __func__);
    return 0;
}

uint64_t Msm8x53Sdhci::SdhciGetQuirks() {
    return SDHCI_QUIRK_STRIP_RESPONSE_CRC_PRESERVE_ORDER;
}

void Msm8x53Sdhci::SdhciHwReset() {
}

zx_status_t Msm8x53Sdhci::Bind() {
    zx_status_t status = DdkAdd("msm8x53-sdhci");
    if (status != ZX_OK) {
        zxlogf(ERROR, "%s: DdkAdd failed\n", __FILE__);
    }

    return status;
}

}  // namespace sdhci

static zx_driver_ops_t msm8x53_sdhci_driver_ops = []() -> zx_driver_ops_t {
    zx_driver_ops_t ops;
    ops.version = DRIVER_OPS_VERSION;
    ops.bind = sdhci::Msm8x53Sdhci::Create;
    return ops;
}();

ZIRCON_DRIVER_BEGIN(msm8x53_sdhci, msm8x53_sdhci_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_QUALCOMM),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_QUALCOMM_SDC1),
ZIRCON_DRIVER_END(msm8x53_sdhci)
