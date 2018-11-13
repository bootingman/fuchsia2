// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_STATIC_USER_ID_PROVIDER_H_
#define PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_STATIC_USER_ID_PROVIDER_H_

#include <string>

#include "peridot/bin/ledger/p2p_provider/public/user_id_provider.h"

namespace p2p_provider {

// Implementation of |UserIdProvider| that always returns a static value, passed
// in the constructor.
class StaticUserIdProvider : public UserIdProvider {
 public:
  StaticUserIdProvider(std::string user_id);
  ~StaticUserIdProvider() override;

  void GetUserId(fit::function<void(Status, std::string)> callback) override;

 private:
  const std::string user_id_;
};

}  // namespace p2p_provider

#endif  // PERIDOT_BIN_LEDGER_P2P_PROVIDER_IMPL_STATIC_USER_ID_PROVIDER_H_
