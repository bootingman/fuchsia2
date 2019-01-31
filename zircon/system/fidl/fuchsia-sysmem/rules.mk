# Copyright 2018 The Fuchsia Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_TYPE := fidl

MODULE_PACKAGE := fidl

MODULE_FIDL_LIBRARY := fuchsia.sysmem

MODULE_SRCS += \
	$(LOCAL_DIR)/allocator.fidl \
	$(LOCAL_DIR)/allocator_2.fidl \
	$(LOCAL_DIR)/collection.fidl \
	$(LOCAL_DIR)/collections.fidl \
	$(LOCAL_DIR)/constraints.fidl \
	$(LOCAL_DIR)/driver_connector.fidl \
	$(LOCAL_DIR)/formats.fidl \
	$(LOCAL_DIR)/format_modifier.fidl \
	$(LOCAL_DIR)/image_formats.fidl \
	$(LOCAL_DIR)/usages.fidl \

include make/module.mk
