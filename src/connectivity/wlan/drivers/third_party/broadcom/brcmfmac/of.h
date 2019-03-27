/*
 * Copyright (c) 2014 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef BRCMF_OF_H
#define BRCMF_OF_H

#include "common.h"
#include "device.h"

#ifdef CONFIG_OF
void brcmf_of_probe(struct brcmf_device* dev, enum brcmf_bus_type bus_type,
                    struct brcmf_mp_device* settings);
#else
static void brcmf_of_probe(struct brcmf_device* dev, enum brcmf_bus_type bus_type,
                           struct brcmf_mp_device* settings) {}
#endif /* CONFIG_OF */

#endif /* BRCMF_OF_H */