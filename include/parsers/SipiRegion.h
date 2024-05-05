/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * This file handles regions of interests / cropping
 */
#ifndef __sipi_region_h
#define __sipi_region_h

#include <string>

namespace Sipi {

/*!
 * \class SipiRegion
 *
 * This class handles the region attribute of sipi and parses the IIIF region part
 */
class SipiRegion
{
public:
  typedef enum {
    FULL,//!< no region, full image
    SQUARE,//!< largest square
    COORDS,//!< region given by x, y, width, height
    PERCENTS//!< region (x,y,width height) given in percent of full image
  } CoordType;

private:
  CoordType coord_type;
  float rx, ry, rw, rh;
  int x, y;
  size_t w, h;
  bool canonical_ok;

public:
  /*!
   * Default constructor. Initializes to full image
   */
  inline SipiRegion()
  {
    coord_type = FULL;
    rx = ry = rw = rh = 0.F;
    canonical_ok = false;
  }

  /*!
   * Constructor taking coordinates
   *
   * \param[in] x X position
   * \param[in] y Y position
   * \param[in] w Width of region
   * \param[in] h Height of region
   */
  inline SipiRegion(int x, int y, size_t w, size_t h)
  {
    coord_type = COORDS;
    rx = (float)x;
    ry = (float)y;
    rw = (float)w;
    rh = (float)h;
    canonical_ok = false;
  };

  /*!
   * Constructor using IIIF region parameter string
   *
   * \param[in] str IIF region string
   */
  SipiRegion(std::string str);

  /*!
   * Get the coordinate type that has bee used for construction of the region
   *
   * \returns CoordType
   */
  inline CoordType getType() const { return coord_type; };

  /*!
   * Get the region parameters to do the actual cropping. The parameters returned are
   * adjusted so that the returned region is within the bounds of the original image
   *
   * \param[in] nx Width of original image
   * \param[in] ny Height of original image
   * \param[out] p_x X position of region
   * \param[out] p_y Y position of region
   * \param[out] p_w Width of region
   * \param[out] p_h height of region
   *
   * \returns CoordType
   */
  CoordType crop_coords(size_t nx, size_t ny, int &p_x, int &p_y, size_t &p_w, size_t &p_h);

  /*!
   * Returns the canoncial IIIF string for the given region
   *
   * \param[in] Pointer to character buffer
   * \param[in] Length of the character buffer
   */
  void canonical(char *buf, int buflen);

  friend std::ostream &operator<<(std::ostream &lhs, const SipiRegion &rhs);
};

}// namespace Sipi

#endif
