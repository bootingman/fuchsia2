// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <threads.h>

#include <ddktl/device.h>
#include <ddktl/protocol/sdhci.h>
#include <lib/mmio/mmio.h>
#include <lib/zx/interrupt.h>

namespace sdhci {

class Msm8x53Sdhci;
using DeviceType = ddk::Device<Msm8x53Sdhci>;

class Msm8x53Sdhci : public DeviceType, public ddk::SdhciProtocol<Msm8x53Sdhci, ddk::base_protocol> {

public:
    static zx_status_t Create(void* ctx, zx_device_t* parent);

    virtual ~Msm8x53Sdhci() = default;

    void DdkRelease() { delete this; }

    zx_status_t Bind();
    zx_status_t Init();

    zx_status_t SdhciGetInterrupt(zx::interrupt* out_irq);
    zx_status_t SdhciGetMmio(zx::vmo* out_mmio, zx_off_t* out_offset);
    zx_status_t SdhciGetBti(uint32_t index, zx::bti* out_bti);
    uint32_t SdhciGetBaseClock();
    uint64_t SdhciGetQuirks();
    void SdhciHwReset();

private:
    Msm8x53Sdhci(zx_device_t* parent, ddk::MmioBuffer core_mmio, ddk::MmioBuffer hc_mmio,
                 ddk::MmioBuffer clk_mmio, zx::interrupt irq, zx::vmo hc_vmo,
                 zx_off_t hc_vmo_offset)
        : DeviceType(parent), core_mmio_(std::move(core_mmio)), hc_mmio_(std::move(hc_mmio)),
          clk_mmio_(std::move(clk_mmio)), irq_(std::move(irq)), hc_vmo_(std::move(hc_vmo)),
          hc_vmo_offset_(hc_vmo_offset) {}

    int IrqThread();

    ddk::MmioBuffer core_mmio_;
    ddk::MmioBuffer hc_mmio_;
    ddk::MmioBuffer clk_mmio_;
    zx::interrupt irq_;
    thrd_t irq_thread_;
    zx::vmo hc_vmo_;
    const zx_off_t hc_vmo_offset_;
};

}  // namespace sdhci
