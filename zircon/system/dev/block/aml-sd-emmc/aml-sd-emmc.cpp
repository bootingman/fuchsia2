// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include <bits/limits.h>
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/io-buffer.h>
#include <ddk/mmio-buffer.h>
#include <ddk/metadata.h>
#include <ddk/phys-iter.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/gpio.h>
#include <ddk/protocol/platform/bus.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/platform-device-lib.h>
#include <ddk/protocol/sdmmc.h>
#include <ddktl/pdev.h>

#include <hw/reg.h>
#include <hw/sdmmc.h>
#include <lib/sync/completion.h>
#include <soc/aml-common/aml-sd-emmc.h>
#include <soc/aml-s905d2/s905d2-gpio.h>
#include <soc/aml-s905d2/s905d2-hw.h>
#include <fbl/alloc_checker.h>
#include <fbl/auto_call.h>
#include <fbl/unique_ptr.h>


#include <zircon/assert.h>
#include <zircon/threads.h>
#include <zircon/types.h>

#include "aml-sd-emmc.h"

// Limit maximum number of descriptors to 512 for now
#define AML_DMA_DESC_MAX_COUNT 512
#define AML_SD_EMMC_TRACE(fmt, ...) zxlogf(TRACE, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_INFO(fmt, ...) zxlogf(INFO, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_ERROR(fmt, ...) zxlogf(ERROR, "%s: " fmt, __func__, ##__VA_ARGS__)
#define AML_SD_EMMC_COMMAND(c) ((0x80) | (c))
#define PAGE_MASK (PAGE_SIZE - 1ull)

static inline uint8_t log2_ceil(uint16_t blk_sz) {
    if (blk_sz == 1) {
        return 0;
    }
    uint8_t clz = static_cast<uint8_t>(__builtin_clz(static_cast<uint16_t>(blk_sz - 1)));
    return static_cast<uint8_t>(16 - clz);
}

namespace sdmmc {

/*int AmlSdEmmc::IrqThread() {
    while (1) {
        uint32_t status_irq;
        zx::time timestamp;
        zx_status_t status = irq_.wait(&timestamp);
        if (status != ZX_OK) {
            zxlogf(ERROR, "IrqThread: zx_interrupt_wait got %d\n", status);
            break;
        }
        mtx_lock(&mtx_);
        if (cur_req_ == NULL) {
            status = ZX_ERR_IO_INVALID;
            zxlogf(ERROR, "IrqThread: Got a spurious interrupt\n");
            //TODO(ravoorir): Do some error recovery here and continue instead
            // of breaking.
            mtx_unlock(&mtx_);
            break;
        }

        status_irq = regs_->sd_emmc_status;

        uint32_t rxd_err = get_bits(status_irq, AML_SD_EMMC_STATUS_RXD_ERR_MASK,
                                    AML_SD_EMMC_STATUS_RXD_ERR_LOC);
        if (rxd_err) {
            if (cur_req_->probe_tuning_cmd) {
                AML_SD_EMMC_TRACE("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n",
                                  cur_req_->cmd_idx, status_irq, rxd_err);
            } else {
                AML_SD_EMMC_ERROR("RX Data CRC Error cmd%d, status=0x%x, RXD_ERR:%d\n",
                                  cur_req_->cmd_idx, status_irq, rxd_err);
            }
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_TXD_ERR) {
            AML_SD_EMMC_ERROR("TX Data CRC Error, cmd%d, status=0x%x TXD_ERR\n", cur_req_->cmd_idx,
                              status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_ERR) {
            AML_SD_EMMC_ERROR("Controller does not own the descriptor, cmd%d, status=0x%x\n",
                              cur_req_->cmd_idx, status_irq);
            status = ZX_ERR_IO_INVALID;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_ERR) {
            AML_SD_EMMC_ERROR("Response CRC Error, cmd%d, status=0x%x\n", cur_req_->cmd_idx, status_irq);
            status = ZX_ERR_IO_DATA_INTEGRITY;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_RESP_TIMEOUT) {
            // When mmc dev_ice is being probed with SDIO command this is an expected failure.
            if (cur_req_->probe_tuning_cmd) {
                AML_SD_EMMC_TRACE("No response received before time limit, cmd%d, status=0x%x\n",
                                  cur_req_->cmd_idx, status_irq);
            } else {
                AML_SD_EMMC_ERROR("No response received before time limit, cmd%d, status=0x%x\n",
                                  cur_req_->cmd_idx, status_irq);
            }
            status = ZX_ERR_TIMED_OUT;
            goto complete;
        }
        if (status_irq & AML_SD_EMMC_STATUS_DESC_TIMEOUT) {
            AML_SD_EMMC_ERROR("Descriptor execution timed out, cmd%d, status=0x%x\n", cur_req_->cmd_idx,
                              status_irq);
            status = ZX_ERR_TIMED_OUT;
            goto complete;
        }

        if (!(status_irq & AML_SD_EMMC_STATUS_END_OF_CHAIN)) {
            status = ZX_ERR_IO_INVALID;
            zxlogf(ERROR, "aml_sd_emmc_irq_thread: END OF CHAIN bit is not set status:0x%x\n",
                   status_irq);
            goto complete;
        }

        if (cur_req_->cmd_flags & SDMMC_RESP_LEN_136) {
            cur_req_->response[0] = regs_->sd_emmc_cmd_rsp;
            cur_req_->response[1] = regs_->sd_emmc_cmd_rsp1;
            cur_req_->response[2] = regs_->sd_emmc_cmd_rsp2;
            cur_req_->response[3] = regs_->sd_emmc_cmd_rsp3;
        } else {
            cur_req_->response[0] = regs_->sd_emmc_cmd_rsp;
        }
        if ((!cur_req_->use_dma) && (cur_req_->cmd_flags & SDMMC_CMD_READ)) {
            uint32_t length = cur_req_->blockcount * cur_req_->blocksize;
            if (length == 0 || ((length % 4) != 0)) {
                status = ZX_ERR_INTERNAL;
                goto complete;
            }
            uint32_t data_copied = 0;
            uint32_t* dest = (uint32_t*)cur_req_->virt_buffer;
            volatile uint32_t* src = (volatile uint32_t*)((uintptr_t)mmio_.get() +
                                                          AML_SD_EMMC_PING_BUFFER_BASE);
            while (length) {
                *dest++ = *src++;
                length -= 4;
                data_copied += 4;
            }
        }

    complete:
        cur_req_->status = status;
        regs_->sd_emmc_status = AML_SD_EMMC_IRQ_ALL_CLEAR;
        cur_req_ = NULL;
        sync_completion_signal(&req_completion_);
        mtx_unlock(&mtx_);
    }
    return 0;
}*/

zx_status_t AmlSdEmmc::Init() {
    dev_info_.caps = SDMMC_HOST_CAP_BUS_WIDTH_8 | SDMMC_HOST_CAP_VOLTAGE_330;
    if (board_config_.supports_dma) {
        dev_info_.caps |= SDMMC_HOST_CAP_ADMA2;
        zx_status_t status = io_buffer_init(&descs_buffer_, bti_.get(),
                                            AML_DMA_DESC_MAX_COUNT * sizeof(aml_sd_emmc_desc_t),
                                            IO_BUFFER_RW | IO_BUFFER_CONTIG);
        if (status != ZX_OK) {
            zxlogf(ERROR, "AmlSdEmmc::Init: Failed to allocate dma descriptors\n");
            return status;
        }
        dev_info_.max_transfer_size = AML_DMA_DESC_MAX_COUNT * PAGE_SIZE;
    } else {
        dev_info_.max_transfer_size = AML_SD_EMMC_MAX_PIO_DATA_SIZE;
    }

    dev_info_.max_transfer_size_non_dma = AML_SD_EMMC_MAX_PIO_DATA_SIZE;

    //pdev_.MapMmio(0, &mmio_);
    // Init the Irq thread
    //auto cb = [](void* arg) -> int { return reinterpret_cast<AmlSdEmmc*>(arg)->IrqThread(); };
    //if (thrd_create_with_name(&irq_thread_, cb, this, "aml_sd_emmc_irq_thread") != thrd_success) {
    //    zxlogf(ERROR, "AmlSdEmmc::Init: Failed to init irq thread\n");
    //    return ZX_ERR_INTERNAL;
    //}*

    zxlogf(ERROR, "AmlSdEmmc::BIND: MINE INIT DONE\n");
    sync_completion_reset(&req_completion_);
    return ZX_OK;
}

zx_status_t AmlSdEmmc::Bind() {
    zx_status_t status = DdkAdd("aml-sd-emmc");
    if (status != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Bind: DdkAdd failed\n");
    }

    zxlogf(ERROR, "AmlSdEmmc::BIND: MINE BIND DONE\n");
    return status;
}

zx_status_t AmlSdEmmc::Create(void* ctx, zx_device_t* parent) {
    zx_status_t status = ZX_OK;

    zxlogf(ERROR, "AmlSdEmmc::Create: START START\n");
    ddk::PDev pdev(parent);
    if (!pdev.is_valid()) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Could not get pdev: %d\n", status);
        return ZX_ERR_NO_RESOURCES;
    }

    zx::bti bti;
    if ((status = pdev.GetBti(0, &bti)) != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get BTI: %d\n", status);
        return status;
    }

    std::optional<ddk::MmioBuffer> mmio;
    //status = pdev.MapMmio(0, &mmio);
    //if (status != ZX_OK) {
    //    zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get mmio: %d\n", status);
    //    return status;
    //}
    //mmio->Write32(0x1000023c, 0);
    zxlogf(ERROR, "AmlSdEmmc::Create: This write is succesful. mmio->get():%p\n", mmio->get());
    //Pin the mmio
    std::optional<ddk::MmioPinnedBuffer> pinned_mmio;
    //status = mmio->Pin(bti, &pinned_mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to pin mmio: %d\n", status);
        return status;
    }



    // Populate board specific information
    aml_sd_emmc_config_t config;
    size_t actual;
    status = device_get_metadata(parent, DEVICE_METADATA_PRIVATE, &config, sizeof(config), &actual);
    if (status != ZX_OK || actual != sizeof(config)) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get metadata: %d\n", status);
        return status;
    }

    zx::interrupt irq;
    if ((status = pdev.GetInterrupt(0, &irq)) != ZX_OK) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get interrupt: %d\n", status);
        return status;
    }

    ddk::GpioProtocolClient reset_gpio = pdev.GetGpio(0);
    if (!reset_gpio.is_valid()) {
        zxlogf(ERROR, "AmlSdEmmc::Create: Failed to get GPIO\n");
        return ZX_ERR_NO_RESOURCES;
    }

    auto dev = fbl::make_unique<AmlSdEmmc>(parent, pdev, std::move(bti), *std::move(mmio),
                                           *std::move(pinned_mmio),
                                           config, std::move(irq), reset_gpio);

    if ((status = dev->Init()) != ZX_OK) {
        return status;
    }

    pdev_protocol_t mypdev;
    if ((status = device_get_protocol(parent, ZX_PROTOCOL_PDEV, &mypdev)) != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: ZX_PROTOCOL_PLATFORM_DEV not available\n");
    }

    status = pdev_map_mmio_buffer(&mypdev, 0, ZX_CACHE_POLICY_UNCACHED_DEVICE, &dev->mymmio_);
    if (status != ZX_OK) {
        zxlogf(ERROR, "aml_sd_emmc_bind: pdev_map_mmio_buffer failed %d\n", status);
    }
    if ((status = dev->Bind()) != ZX_OK) {
        return status;
    }
    
    // devmgr is now in charge of the device.
    //__UNUSED auto* dummy = dev.release();
    return ZX_OK;
}


void AmlSdEmmc::DdkUnbind() {
    DdkRemove();
}

void AmlSdEmmc::DdkRelease() {
    if (irq_thread_)
        thrd_join(irq_thread_, NULL);
    io_buffer_release(&descs_buffer_);
    delete this;
}

zx_status_t AmlSdEmmc::SdmmcHostInfo(sdmmc_host_info_t* info) {
    memcpy(info, &dev_info_, sizeof(dev_info_));
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcSetBusWidth(sdmmc_bus_width_t bw) {
    //uint32_t config = regs_->sd_emmc_cfg;
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcSetBusFreq(uint32_t freq) {
   return ZX_OK;
}

void AmlSdEmmc::aml_sd_emmc_init_regs() {
    uint32_t config = 0;
    uint32_t clk_val = 0;
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_CO_PHASE_MASK,
                AML_SD_EMMC_CLOCK_CFG_CO_PHASE_LOC, AML_SD_EMMC_DEFAULT_CLK_CORE_PHASE);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_SRC_MASK, AML_SD_EMMC_CLOCK_CFG_SRC_LOC,
                AML_SD_EMMC_DEFAULT_CLK_SRC);
    update_bits(&clk_val, AML_SD_EMMC_CLOCK_CFG_DIV_MASK, AML_SD_EMMC_CLOCK_CFG_DIV_LOC,
                AML_SD_EMMC_DEFAULT_CLK_DIV);
    clk_val |= AML_SD_EMMC_CLOCK_CFG_ALWAYS_ON;
    
    regs_ = (aml_sd_emmc_regs_t*)mymmio_.vaddr;
    //this->mymmio_->Write32(0x1000023c, 0);
    regs_->sd_emmc_clock = clk_val;
    zxlogf(ERROR, "AmlSdEmmc::init_regs. Write successful\n");
    //zxlogf(ERROR, "AmlSdEmmc::init_regs. Reading: 0x%x\n",
//this->mmio_->Read32(0));

    update_bits(&config, AML_SD_EMMC_CFG_BL_LEN_MASK, AML_SD_EMMC_CFG_BL_LEN_LOC,
                AML_SD_EMMC_DEFAULT_BL_LEN);
    update_bits(&config, AML_SD_EMMC_CFG_RESP_TIMEOUT_MASK, AML_SD_EMMC_CFG_RESP_TIMEOUT_LOC,
                AML_SD_EMMC_DEFAULT_RESP_TIMEOUT);
    update_bits(&config, AML_SD_EMMC_CFG_RC_CC_MASK, AML_SD_EMMC_CFG_RC_CC_LOC,
                AML_SD_EMMC_DEFAULT_RC_CC);
    update_bits(&config, AML_SD_EMMC_CFG_BUS_WIDTH_MASK, AML_SD_EMMC_CFG_BUS_WIDTH_LOC,
                AML_SD_EMMC_CFG_BUS_WIDTH_1BIT);

    //regs_->sd_emmc_cfg = config;
    //regs_->sd_emmc_status = AML_SD_EMMC_IRQ_ALL_CLEAR;
    //regs_->sd_emmc_irq_en = AML_SD_EMMC_IRQ_ALL_CLEAR;
}

void AmlSdEmmc::SdmmcHwReset() {
   zxlogf(ERROR, "AmlSdEmmc:: SdmmcHwReset START\n");
   if (reset_gpio_.is_valid()) {
        reset_gpio_.ConfigOut(0);
        usleep(10 * 1000);
        reset_gpio_.ConfigOut(1);
        usleep(10 * 1000);
   }
   aml_sd_emmc_init_regs();
   zxlogf(ERROR, "AmlSdEmmc:: SdmmcHwReset STOP\n");
}

zx_status_t AmlSdEmmc::SdmmcSetTiming(sdmmc_timing_t timing) {
   return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcSetSignalVoltage(sdmmc_voltage_t voltage) {
    //Amlogic controller does not allow to modify voltage
    //We do not return an error here since things work fine without switching the voltage.
    return ZX_OK;
}

zx_status_t AmlSdEmmc::SdmmcRequest(sdmmc_req_t* req) {
    zxlogf(ERROR, "AmlSdEmmc: IN SDMMCREQUEST \n");
   return ZX_OK;
}


zx_status_t AmlSdEmmc::SdmmcPerformTuning(uint32_t tuning_cmd_idx) {
   return ZX_OK;
}

static zx_driver_ops_t aml_sd_emmc_driver_ops = []() {
    zx_driver_ops_t driver_ops;
    driver_ops.version = DRIVER_OPS_VERSION;
    driver_ops.bind =  AmlSdEmmc::Create;
    return driver_ops;
}();

}; // sdmmc
ZIRCON_DRIVER_BEGIN(aml_sd_emmc, sdmmc::aml_sd_emmc_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PROTOCOL, ZX_PROTOCOL_PDEV),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_AMLOGIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_AMLOGIC_SD_EMMC),
ZIRCON_DRIVER_END(aml_sd_emmc)
