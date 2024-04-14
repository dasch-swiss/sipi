/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "SipiCommon.h"

namespace Sipi {

void memcpy(void *to, const void *from, size_t len)
{
  char *toptr = (char *)to;
  char *fromptr = (char *)from;
  while (toptr < (char *)to + len) { *toptr++ = *fromptr++; }
}
}// namespace Sipi
