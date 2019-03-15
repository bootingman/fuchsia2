// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <hwreg/bitfields.h>

namespace sdhci {

class MciPower : public hwreg::RegisterBase<MciPower, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<MciPower>(0x24000); }

    DEF_BIT(9, sw_rst_req);
    DEF_BIT(8, sw_rst_config);
};

class MciArgument : public hwreg::RegisterBase<MciArgument, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<MciArgument>(0x24008); }

    DEF_FIELD(31, 0, cmd_arg);
};

class MciCmd : public hwreg::RegisterBase<MciCmd, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<MciCmd>(0x2400c); }

    DEF_BIT(10, enable);
    DEF_BIT(8, interrupt);
    DEF_BIT(7, longrsp);
    DEF_BIT(6, response);
    DEF_FIELD(5, 0, cmd_index);
};

class MciResp : public hwreg::RegisterBase<MciResp, uint32_t> {
public:
    static auto Get(uint32_t index) {
        return hwreg::RegisterAddr<MciResp>(0x24014 + (4 * index));
    }

    DEF_FIELD(31, 0, status);
};

class MciStatus : public hwreg::RegisterBase<MciStatus, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<MciStatus>(0x24034); }

    DEF_BIT(11, cmd_active);
    DEF_BIT(7, cmd_sent);
    DEF_BIT(6, cmd_response_end);
    DEF_BIT(3, data_timeout_fail);
    DEF_BIT(2, cmd_timeout_fail);
    DEF_BIT(1, data_crc_fail);
    DEF_BIT(0, cmd_crc_fail);
};

class MciClear : public hwreg::RegisterBase<MciClear, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<MciClear>(0x24038); }

    DEF_BIT(7, cmd_sent_clr);
    DEF_BIT(6, cmd_response_end_clr);
    DEF_BIT(3, data_timeout_fail_clr);
    DEF_BIT(2, cmd_timeout_fail_clr);
    DEF_BIT(1, data_crc_fail_clr);
    DEF_BIT(0, cmd_crc_fail_clr);
};

class HcReg8 : public hwreg::RegisterBase<HcReg8, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<HcReg8>(0x24908); }

    DEF_FIELD(31, 0, cmd_arg);
};

class HcRegC : public hwreg::RegisterBase<HcRegC, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<HcRegC>(0x2490c); }

    DEF_FIELD(29, 24, cmd_indx);
    DEF_FIELD(23, 22, cmd_type);
    DEF_FIELD(17, 16, cmd_resp_type_sel);
};

class HcReg30 : public hwreg::RegisterBase<HcReg30, uint32_t> {
public:
    static auto Get() { return hwreg::RegisterAddr<HcReg30>(0x24930); }

    DEF_BIT(0, cmd_complete);
};

}  // namespace sdhci
