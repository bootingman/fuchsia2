// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <type_traits>

#include <lib/fidl/coding.h>
#include <lib/fidl/internal.h>

namespace fidl {
namespace {

// All the data in coding tables should be pure data.
static_assert(std::is_standard_layout<fidl_type>::value, "");
static_assert(std::is_standard_layout<FidlStructField>::value, "");
static_assert(std::is_standard_layout<FidlTableField>::value, "");
static_assert(std::is_standard_layout<FidlTypeTag>::value, "");
static_assert(std::is_standard_layout<FidlCodedStruct>::value, "");
static_assert(std::is_standard_layout<FidlCodedUnion>::value, "");
static_assert(std::is_standard_layout<FidlCodedArray>::value, "");
static_assert(std::is_standard_layout<FidlCodedVector>::value, "");
static_assert(std::is_standard_layout<FidlCodedString>::value, "");
static_assert(std::is_standard_layout<FidlCodedHandle>::value, "");

} // namespace
} // namespace fidl
