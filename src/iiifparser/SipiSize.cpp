/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <assert.h>
#include <stdlib.h>
#include <syslog.h>

#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <cmath>
#include <climits>

#include <stdio.h>
#include <string.h>


#include "shttps/Global.h"
#include "shttps/Parsing.h"
#include "SipiError.h"
#include "SipiSize.h"

static const char __file__[] = __FILE__;

namespace Sipi {

    size_t SipiSize::limitdim = 32000;

    SipiSize::SipiSize(std::string str) {
        nx = ny = 0;
        percent = 0.F;
        canonical_ok = false;

        try {
            if (str.empty() || str == "full" || str == "max") {
                size_type = SizeType::FULL;
            } else if (str.find("pct") != std::string::npos) {
                size_type = SizeType::PERCENTS;
                std::string percent_str = str.substr(4);
                percent = shttps::Parsing::parse_float(percent_str);

                if (percent < 0.0) percent = 1.0;
                if (percent > 100.0) percent = 100.0;
            } else if (str.find("red") != std::string::npos) {
                size_type = SizeType::REDUCE;
                std::string reduce_str = str.substr(4);
                reduce = static_cast<int>(shttps::Parsing::parse_int(reduce_str));
                if (reduce < 0) reduce = 0;
            } else {
                bool exclamation_mark = str[0] == '!';

                if (exclamation_mark) {
                    str.erase(0, 1);
                }

                size_t comma_pos = str.find(',');

                if (comma_pos == std::string::npos) {
                    throw SipiError(__file__, __LINE__, "Could not parse IIIF size parameter: " + str);
                }

                std::string width_str = str.substr(0, comma_pos);
                std::string height_str = str.substr(comma_pos + 1);

                if ((width_str.empty() && height_str.empty()) ||
                    (size_type == SizeType::MAXDIM && (width_str.empty() || height_str.empty()))) {
                    throw SipiError(__file__, __LINE__, "Could not parse IIIF size parameter: " + str);
                }

                if (width_str.empty()) {
                    ny = shttps::Parsing::parse_int(height_str);

                    if (ny == 0) {
                        throw SipiError(__file__, __LINE__, "IIIF height cannot be zero");
                    }

                    size_type = SizeType::PIXELS_Y;
                } else if (height_str.empty()) {
                    nx = shttps::Parsing::parse_int(width_str);

                    if (nx == 0) {
                        throw SipiError(__file__, __LINE__, "IIIF width cannot be zero");
                    }

                    size_type = SizeType::PIXELS_X;
                } else {
                    nx = shttps::Parsing::parse_int(width_str);
                    ny = shttps::Parsing::parse_int(height_str);

                    if (nx == 0 || ny == 0) {
                        throw SipiError(__file__, __LINE__,
                                        "IIIF size would result in a width or height of zero: " + str);
                    }

                    if (exclamation_mark) {
                        size_type = SizeType::MAXDIM;
                    } else {
                        size_type = SizeType::PIXELS_XY;
                    }
                }

                if (nx > limitdim) nx = limitdim;
                if (ny > limitdim) ny = limitdim;
            }
        } catch (SipiError &sipi_error) {
            throw sipi_error;
        } catch (shttps::Error &error) {
            throw SipiError(__file__, __LINE__, "Could not parse IIIF size parameter: " + str);
        }
    }

    //-------------------------------------------------------------------------

    bool SipiSize::operator>(const SipiSize &s) {
        if (!canonical_ok) {
            throw SipiError(__file__, __LINE__, "Final size not yet determined");
        }

        return ((w > s.w) || (h > s.h));
    }
    //-------------------------------------------------------------------------

    bool SipiSize::operator>=(const SipiSize &s) {
        if (!canonical_ok) {
            throw SipiError(__file__, __LINE__, "Final size not yet determined");
        }

        return ((w >= s.w) || (h >= s.h));
    }
    //-------------------------------------------------------------------------

    bool SipiSize::operator<(const SipiSize &s) {
        if (!canonical_ok) {
            throw SipiError(__file__, __LINE__, "Final size not yet determined");
        }

        return ((w < s.w) && (h < s.h));
    }
    //-------------------------------------------------------------------------

    bool SipiSize::operator<=(const SipiSize &s) {
        if (!canonical_ok) {
            throw SipiError(__file__, __LINE__, "Final size not yet determined");
        }

        return ((w <= s.w) && (h <= s.h));
    }
    //-------------------------------------------------------------------------

    SipiSize::SizeType
    SipiSize::get_size(size_t img_w, size_t img_h, size_t &w_p, size_t &h_p, int &reduce_p, bool &redonly_p) {
        redonly = false;
        if (reduce_p == -1) reduce_p = INT_MAX;
        int max_reduce = reduce_p;
        reduce_p = 0;


        float img_w_float = static_cast<float>(img_w);
        float img_h_float = static_cast<float>(img_h);

        switch (size_type) {
            case SipiSize::PIXELS_XY: {
                //
                // first we check how far the new image width and image height can be reached by a reduce factor
                //
                int sf_w = 1;
                int reduce_w = 0;
                bool exact_match_w = true;
                w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf_w)));

                while ((w > nx) && (reduce_w < max_reduce)) {
                    sf_w *= 2;
                    w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf_w)));
                    reduce_w++;
                }

                if (w < nx) {
                    // we do not have an exact match. Go back one level with reduce
                    exact_match_w = false;
                    sf_w /= 2;
                    reduce_w--;
                }
                if (w > nx) exact_match_w = false;

                int sf_h = 1;
                int reduce_h = 0;
                bool exact_match_h = true;
                h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf_h)));

                while ((h > ny) && (reduce_w < max_reduce)) {
                    sf_h *= 2;
                    h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf_h)));
                    reduce_h++;
                }

                if (h < ny) {
                    // we do not have an exact match. Go back one level with reduce
                    exact_match_h = false;
                    sf_h /= 2;
                    reduce_h--;
                }
                if (h > ny) exact_match_h = false;

                if (exact_match_w && exact_match_h && (reduce_w == reduce_h)) {
                    reduce_p = reduce_w;
                    redonly = true;
                } else {
                    reduce_p = reduce_w < reduce_h ? reduce_w : reduce_h; // min()
                    redonly = false;
                }

                w = nx;
                h = ny;
                break;
            }

            case SipiSize::PIXELS_X: {
                //
                // first we check how far the new image width can be reached by a reduce factor
                //
                int sf_w = 1;
                int reduce_w = 0;
                bool exact_match_w = true;
                w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf_w)));
                while ((w > nx) && (reduce_w < max_reduce)) {
                    sf_w *= 2;
                    w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf_w)));
                    reduce_w++;
                }
                if (w < nx) {
                    // we do not have an exact match. Go back one level with reduce
                    exact_match_w = false;
                    sf_w /= 2;
                    reduce_w--;
                }
                if (w > nx) exact_match_w = false;

                w = nx;
                reduce_p = reduce_w;
                redonly = exact_match_w; // if exact_match, then reduce only

                if (exact_match_w) {
                    h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf_w)));
                } else {
                    h = static_cast<size_t>(ceilf(static_cast<float>(img_h * nx) / img_w_float));
                }
                break;
            }

            case SipiSize::PIXELS_Y: {
                //
                // first we check how far the new image height can be reached by a reduce factor
                //
                int sf_h = 1;
                int reduce_h = 0;
                bool exact_match_h = true;
                h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf_h)));

                while ((h > ny) && (reduce_h < max_reduce)) {
                    sf_h *= 2;
                    h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf_h)));
                    reduce_h++;
                }

                if (h < ny) {
                    // we do not have an exact match. Go back one level with reduce
                    exact_match_h = false;
                    sf_h /= 2;
                    reduce_h--;
                }
                if (h > ny) exact_match_h = false;

                h = ny;
                reduce_p = reduce_h;
                redonly = exact_match_h; // if exact_match, then reduce only

                if (exact_match_h) {
                    w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf_h)));
                } else {
                    w = static_cast<size_t>(ceilf(static_cast<float>(img_w * ny) / img_h_float));
                }
                break;
            }

            case SipiSize::PERCENTS: {
                w = static_cast<size_t>(ceilf(img_w * percent / 100.F));
                h = static_cast<size_t>(ceilf(img_h * percent / 100.F));

                reduce_p = 0;
                float r = 100.F / percent;
                float s = 1.0;

                while ((2.0 * s <= r) && (reduce_p < max_reduce)) {
                    s *= 2.0;
                    reduce_p++;
                }

                if (fabs(s - r) < 1.0e-5) {
                    redonly = true;
                }
                break;
            }
            case SipiSize::REDUCE: {
                if (reduce == 0) {
                    w = img_w;
                    h = img_h;
                    reduce_p = 0;
                    redonly = true;
                    break;
                }

                int sf = 1;
                for (int i = 0; i < reduce; i++) sf *= 2;

                w = static_cast<size_t>(ceilf(img_w_float / static_cast<float>(sf)));
                h = static_cast<size_t>(ceilf(img_h_float / static_cast<float>(sf)));
                if (reduce > max_reduce) {
                    reduce_p = max_reduce;
                    redonly = false;
                }
                else {
                    reduce_p = reduce;
                    redonly = true;
                }
                break;
            }

            case SipiSize::MAXDIM: {
                float fx = static_cast<float>(nx) / img_w_float;
                float fy = static_cast<float>(ny) / img_h_float;

                float r;
                if (fx < fy) { // scaling has to be done by fx
                    w = nx;
                    h = static_cast<size_t>(ceilf(img_h * fx));

                    r = img_w_float / static_cast<float>(w);
                } else { // scaling has to be done fy
                    w = static_cast<size_t>(ceilf(img_w * fy));
                    h = ny;

                    r = img_h_float / static_cast<float>(h);
                }

                float s = 1.0;
                reduce_p = 0;

                while ((2.0 * s <= r) && (reduce_p < max_reduce)) {
                    s *= 2.0;
                    reduce_p++;
                }

                if (fabsf(s - r) < 1.0e-5) {
                    redonly = true;
                }
                break;
            }

            case SipiSize::FULL: {
                w = img_w;
                h = img_h;
                reduce_p = 0;
                redonly = true;
                break;
            }
        }


        w_p = w;
        h_p = h;

        if (reduce_p < 0) reduce_p = 0;
        redonly_p = redonly;

        std::stringstream ss;
        ss << "get_size: img_w=" << img_w << " img_h=" << img_h << " w_p=" << w_p << " h_p=" << h_p << " reduce=" << reduce_p
           << " reduce only=" << redonly;
        syslog(LOG_DEBUG, "%s", ss.str().c_str());

        canonical_ok = true;

        return size_type;
    }
    //-------------------------------------------------------------------------


    void SipiSize::canonical(char *buf, int buflen) {
        int n;

        if (!canonical_ok && (size_type != SipiSize::FULL)) {
            std::string msg = "Canonical size not determined!";
            throw SipiError(__file__, __LINE__, msg);
        }

        switch (size_type) {
            case SipiSize::PIXELS_XY: {
                n = snprintf(buf, buflen, "%ld,%ld", w, h);
                break;
            }

            case SipiSize::PIXELS_X: {
                n = snprintf(buf, buflen, "%ld,", w);
                break;
            }

            case SipiSize::PIXELS_Y: {
                n = snprintf(buf, buflen, "%ld,", w);
                break;
            }

            case SipiSize::MAXDIM: {
                n = snprintf(buf, buflen, "%ld,", w);
                break;
            }

            case SipiSize::PERCENTS: {
                n = snprintf(buf, buflen, "%ld,", w);
                break;
            }

            case SipiSize::REDUCE: {
                n = snprintf(buf, buflen, "%ld,", w);
                break;
            }

            case SipiSize::FULL: {
                n = snprintf(buf, buflen, "full"); // replace with "max" for version 3.0 of iiif
            }
        }

        if ((n < 0) || (n >= buflen)) {
            throw SipiError(__file__, __LINE__, "Error creating size string!");
        }
    }
    //-------------------------------------------------------------------------

    //-------------------------------------------------------------------------
    // Output to stdout for debugging etc.
    //
    std::ostream &operator<<(std::ostream &outstr, const SipiSize &rhs) {
        outstr << "IIIF-Server Size parameter:";
        outstr << "  Size type: " << rhs.size_type;
        outstr << " | percent = " << std::to_string(rhs.percent) << " | nx = " << std::to_string(rhs.nx) << " | ny = "
               << std::to_string(rhs.ny) << " | reduce = " << rhs.reduce;
        return outstr;
    };
    //-------------------------------------------------------------------------

}
