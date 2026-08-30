/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_METADATA_INTERNAL_ICC_LCMS2_H
#define SIPI_METADATA_INTERNAL_ICC_LCMS2_H

#include <lcms2.h>

#include "metadata/icc.h"

namespace Sipi {

extern void icc_error_logger(cmsContext ContextID, cmsUInt32Number ErrorCode, const char *Text);

/*!
 * Typed view of an Icc instance's opaque profile handle.
 * \param[in] icc_p Icc instance to view
 * \returns Handle to the underlying littleCMS profile
 */
[[nodiscard]] inline cmsHPROFILE iccProfileHandle(const Icc &icc_p) { return icc_p.profileHandle(); }

}// namespace Sipi

#endif
