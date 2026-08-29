/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <mutex>
#include <ostream>

#include "metadata/xmp.h"

/*!
 * SIPI stores the XMP packet verbatim: it neither parses nor validates the
 * RDF/XML payload, because Exiv2's XMP parser is not thread-safe at the
 * Exiv2 version SIPI depends on. xmpBytes() returns exactly the bytes it
 * was constructed with.
 */

namespace Sipi {

XmpMutex xmp_mutex;

void xmplock_func(void *pLockData, bool lockUnlock)
{
  auto *m = static_cast<XmpMutex *>(pLockData);
  if (lockUnlock) {
    m->lock.lock();
  } else {
    m->lock.unlock();
  }
}
//=========================================================================

Xmp::Xmp(const std::string &xmp) { __xmpstr = xmp; }
//============================================================================

Xmp::Xmp(const char *xmp) { __xmpstr = xmp; }
//============================================================================

Xmp::Xmp(const char *xmp, int len)
{
  std::string buf(xmp, len);
  __xmpstr = buf;
}
//============================================================================


Xmp::~Xmp() {}
//============================================================================


// Returns the RDF/XML packet exactly as it was constructed.
std::string Xmp::xmpBytes() { return __xmpstr; }
//============================================================================

std::ostream &operator<<(std::ostream &outstr, const Xmp &) { return outstr; }
//============================================================================

}// namespace Sipi
