/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <mutex>
#include <pthread.h>

#include "SipiError.hpp"
#include "metadata/xmp.h"

/*!
 * ToDo: remove provisional code as soon as Exiv2::Xmp is thread safe (expected v.26)
 * ATTENTION!!!!!!!!!
 * Since the Xmp-Part of Exiv2 Version 0.25 is not thread safe, we omit for the moment
 * the use of Exiv2::Xmp for processing XMP. We just transfer the XMP string as is. This
 * is bad, since we are not able to modifiy it. But we'll try again with Exiv2 v0.26!
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

Xmp::Xmp(const std::string &xmp)
{
  __xmpstr = xmp;// provisional code until Exiv2::Xmp is threadsafe
  return;// provisional code until Exiv2::Xmp is threadsafe
  // TODO: Testing required if now Exiv2::Xmp is thread save
  /*
  try {
      if (Exiv2::XmpParser::decode(xmpData, xmp) != 0) {
          Exiv2::XmpParser::terminate();
          throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
      }
  }
  catch(Exiv2::Error &err) {
      throw SipiError(thisSourceFile, __LINE__, err.what());
  }
   */
}
//============================================================================

Xmp::Xmp(const char *xmp)
{
  __xmpstr = xmp;// provisional code until Exiv2::Xmp is threadsafe
  return;// provisional code until Exiv2::Xmp is threadsafe
  // TODO: Testing required if now Exiv2::Xmp is thread save
  /*
  try {
      if (Exiv2::XmpParser::decode(xmpData, xmp) != 0) {
          Exiv2::XmpParser::terminate();
          throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
      }
  }
  catch(Exiv2::Error &err) {
      throw SipiError(thisSourceFile, __LINE__, err.what());
  }
   */
}
//============================================================================

Xmp::Xmp(const char *xmp, int len)
{
  std::string buf(xmp, len);
  __xmpstr = buf;// provisional code until Exiv2::Xmp is threadsafe
  return;// provisional code until Exiv2::Xmp is threadsafe
  // TODO: Testing required if now Exiv2::Xmp is thread save
  /*
  try {
      if (Exiv2::XmpParser::decode(xmpData, buf) != 0) {
          Exiv2::XmpParser::terminate();
          throw SipiError(thisSourceFile, __LINE__, "Could not parse XMP!");
      }
  }
  catch(Exiv2::Error &err) {
      throw SipiError(thisSourceFile, __LINE__, err.what());
  }
   */
}
//============================================================================


Xmp::~Xmp()
{
  // Exiv2::XmpParser::terminate();
}
//============================================================================


// Provisional implementation: returns the cached RDF/XML packet directly.
// The Exiv2::XmpParser::encode path remains the long-term replacement, but
// it is not thread-safe in the Exiv2 version we depend on. Once Exiv2's
// XMP encoder becomes thread-safe, swap in the parser-driven path here
// without touching call sites.
std::string Xmp::xmpBytes() { return __xmpstr; }
//============================================================================

std::ostream &operator<<(std::ostream &outstr, const Xmp &rhs)
{
  /*
  for (Exiv2::XmpData::const_iterator md = rhs.xmpData.begin();
  md != rhs.xmpData.end(); ++md) {
      outstr << std::setfill(' ') << std::left
          << std::setw(44)
          << md->key() << " "
          << std::setw(9) << std::setfill(' ') << std::left
          << md->typeName() << " "
          << std::dec << std::setw(3)
          << std::setfill(' ') << std::right
          << md->count() << "  "
          << std::dec << md->value()
          << std::endl;
  }
   */
  return outstr;
}
//============================================================================

}// namespace Sipi
