/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cassert>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

#include "lcms2.h"

#include "processing.h"

#include "metadata/icc.h"
#include "metadata/internal/icc_lcms2.h"
#include "metadata/photometric_interpretation.h"
#include "observability/profiling.h"
#include "util/checked_arith.h"

namespace Sipi {

namespace processing {

  Result<void> convertYCC2RGB(SipiImage &img)
  {
    const size_t nx = img.getNx();
    const size_t ny = img.getNy();
    const size_t nc = img.getNc();
    const size_t bps = img.getBps();

    if (nc < 3) {
      return std::unexpected(SipiValueError{
        ErrorCode::kMalformedInput, "YCbCr conversion needs at least 3 channels, got " + std::to_string(nc) });
    }

    if (bps == 8) {
      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }
      const byte *inbuf = img.pixels_view().data();
      std::vector<byte> outbuf(*buf_size);

      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          auto Y = (double)inbuf[nc * (j * nx + i) + 2];
          auto Cb = (double)inbuf[nc * (j * nx + i) + 1];
          ;
          auto Cr = (double)inbuf[nc * (j * nx + i) + 0];

          int r = (int)(Y + 1.40200 * (Cr - 0x80));
          int g = (int)(Y - 0.34414 * (Cb - 0x80) - 0.71414 * (Cr - 0x80));
          int b = (int)(Y + 1.77200 * (Cb - 0x80));

          outbuf[nc * (j * nx + i) + 0] = std::max(0, std::min(255, r));
          outbuf[nc * (j * nx + i) + 1] = std::max(0, std::min(255, g));
          outbuf[nc * (j * nx + i) + 2] = std::max(0, std::min(255, b));

          for (size_t k = 3; k < nc; k++) { outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf), nx, ny, nc, bps);
    } else if (bps == 16) {
      const auto buf_size = checked_buf_size(nx, ny, nc, 2);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=2)" });
      }
      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      // Sized with nc, matching the 8-bit path above: YCbCr->RGB conversion
      // does not drop a channel, it converts the first three and copies any
      // remaining ones through unchanged (see the k=3.. loop below).
      std::vector<byte> outbuf_v(*buf_size);
      word *outbuf = (word *)outbuf_v.data();

      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          auto Y = (double)inbuf[nc * (j * nx + i) + 2];
          auto Cb = (double)inbuf[nc * (j * nx + i) + 1];
          ;
          auto Cr = (double)inbuf[nc * (j * nx + i) + 0];

          int r = (int)(Y + 1.40200 * (Cr - 32768));
          int g = (int)(Y - 0.34414 * (Cb - 32768) - 0.71414 * (Cr - 32768));
          int b = (int)(Y + 1.77200 * (Cb - 32768));

          outbuf[nc * (j * nx + i) + 0] = std::max(0, std::min(65535, r));
          outbuf[nc * (j * nx + i) + 1] = std::max(0, std::min(65535, g));
          outbuf[nc * (j * nx + i) + 2] = std::max(0, std::min(65535, b));

          for (size_t k = 3; k < nc; k++) { outbuf[nc * (j * nx + i) + k] = inbuf[nc * (j * nx + i) + k]; }
        }
      }

      img.set_pixels(std::move(outbuf_v), nx, ny, nc, bps);
    } else {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Bits per sample is not supported for operation: " + std::to_string(bps) });
    }
    return {};
  }

  //============================================================================

  Result<void> convertToIcc(SipiImage &img, const Icc &target_icc_p, int new_bps)
  {
    SIPI_ZONE_N("SipiImage::convertToIcc");
    cmsSetLogErrorHandler(icc_error_logger);
    cmsUInt32Number in_formatter, out_formatter;

    const size_t nx = img.getNx();
    const size_t ny = img.getNy();
    const size_t nc = img.getNc();
    const size_t bps = img.getBps();
    const PhotometricInterpretation photo = img.getPhoto();

    std::shared_ptr<Icc> icc = img.getIcc();
    if (icc == nullptr) {
      switch (nc) {
      case 1: {
        icc = std::make_shared<Icc>(icc_GRAY_D50);// assume gray value image with D50
        break;
      }

      case 3: {
        icc = std::make_shared<Icc>(icc_sRGB);// assume sRGB
        break;
      }

      case 4: {
        icc = std::make_shared<Icc>(icc_CMYK_standard);// assume CYMK
        break;
      }

      default: {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Cannot assign ICC profile to image: unsupported channel count nc=" + std::to_string(nc)
            + " (expected 1, 3, or 4)" + ", dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", bps=" + std::to_string(bps) + ", colorspace=" + to_string(photo) });
      }
      }
    }
    unsigned int nnc = cmsChannelsOf(cmsGetColorSpace(iccProfileHandle(target_icc_p)));

    if (!((new_bps == 8) || (new_bps == 16))) {
      return std::unexpected(
        SipiValueError{ ErrorCode::kMalformedInput, "Unsupported bits/sample (" + std::to_string(bps) + ")" });
    }

    in_formatter = icc->iccFormatter(static_cast<int>(bps), static_cast<int>(nc), photo);
    out_formatter = target_icc_p.iccFormatter(new_bps);

    std::unique_ptr<std::remove_pointer_t<cmsHTRANSFORM>, decltype(&cmsDeleteTransform)> hTransform(
      cmsCreateTransform(
        iccProfileHandle(*icc), in_formatter, iccProfileHandle(target_icc_p), out_formatter, INTENT_PERCEPTUAL, 0),
      &cmsDeleteTransform);

    if (hTransform == nullptr) {
      return std::unexpected(SipiValueError{ ErrorCode::kMetadataParseFailed,
        "Failed to create color transform" + std::string(", dimensions=") + std::to_string(nx) + "x"
          + std::to_string(ny) + ", channels=" + std::to_string(nc) + ", bps=" + std::to_string(bps)
          + ", colorspace=" + to_string(photo)
          + ", source_profile_type=" + std::to_string(static_cast<int>(icc->getProfileType()))
          + ", target_profile_type=" + std::to_string(static_cast<int>(target_icc_p.getProfileType())) });
    }

    const auto buf_size = checked_buf_size(nx, ny, nnc, static_cast<size_t>(new_bps) / 8);
    if (!buf_size) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
          + ", channels=" + std::to_string(nnc) + ", elem=" + std::to_string(static_cast<size_t>(new_bps) / 8)
          + ")" });
    }
    std::vector<byte> outbuf(*buf_size);
    cmsDoTransform(hTransform.get(), img.pixels_view().data(), outbuf.data(), nx * ny);

    PhotometricInterpretation new_photo = photo;
    PredefinedProfiles targetPT = target_icc_p.getProfileType();
    switch (targetPT) {
    case icc_GRAY_D50: {
      new_photo = PhotometricInterpretation::MINISBLACK;
      break;
    }

    case icc_RGB:
    case icc_sRGB:
    case icc_AdobeRGB: {
      new_photo = PhotometricInterpretation::RGB;
      break;
    }

    case icc_CMYK_standard: {
      new_photo = PhotometricInterpretation::SEPARATED;
      break;
    }

    case icc_LAB: {
      new_photo = PhotometricInterpretation::CIELAB;
      break;
    }

    default: {
      // do nothing at the moment
    }
    }

    img.set_pixels(std::move(outbuf), nx, ny, nnc, static_cast<size_t>(new_bps));
    img.set_icc(std::make_shared<Icc>(target_icc_p));
    img.setPhoto(new_photo);
    return {};
  }

  /*==========================================================================*/


  Result<void> removeChannel(SipiImage &img, const unsigned int channel, const bool force_gray_alpha)
  {
    const size_t nx = img.getNx();
    const size_t ny = img.getNy();
    const size_t nc = img.getNc();
    const size_t bps = img.getBps();
    const PhotometricInterpretation photo = img.getPhoto();

    if ((nc == 1) || (channel >= nc)) {
      std::string msg = "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(channel);
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput, msg });
    }

    std::vector<ExtraSamples> es = img.getEs();

    const bool has_removable_extra_samples = !es.empty();
    const bool has_two_or_less_channels = nc < 3;
    const bool has_three_channels = nc == 3;

    if (has_removable_extra_samples) {

      // cleanup the extra samples
      // TODO: figure out why this can even happen
      if (has_two_or_less_channels) {
        // Assumtion: An image with two or less channels cannot have extra samples
        assert(has_two_or_less_channels && has_removable_extra_samples);
        es.clear();
      }

      // TODO: figure out when this can happen. Maybe two channels with alpha is not allowed or even possible?
      if (has_three_channels) {
        std::string msg = "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(channel);
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput, msg });
      }

      // TODO: figure out when this can happen and if this can/should be caught earlier
      const bool cmyk_image = (nc == 4) && (photo == PhotometricInterpretation::SEPARATED);
      if (cmyk_image) {
        std::string msg = "Cannot remove component: nc=" + std::to_string(nc) + " chan=" + std::to_string(channel);
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput, msg });
      }
    }

    constexpr int _8bps = 8;
    constexpr int _16bps = 16;

    /**
     * Purge the channel from the image.
     * The image is stored in a single array, so we need to remove the
     * corresponding pixel values. We do this by copying the original and
     * omitting the channel to be removed.
     */
    auto purge_channel_pixels = [](const auto &original_pixels,
                                  auto &changed_pixels,
                                  const size_t nx,
                                  const size_t ny,
                                  const size_t nc,
                                  const size_t channel_to_remove,
                                  const size_t new_nc) {
      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          for (size_t k = 0; k < nc; k++) {
            if (k == channel_to_remove) { continue; }
            // Compact: channels after the removed one shift down by one slot
            // in the output, or a non-terminal removal overruns the
            // new_nc-wide row into the next pixel (or past the buffer end).
            const size_t out_k = (k < channel_to_remove) ? k : k - 1;
            changed_pixels[new_nc * (j * nx + i) + out_k] = original_pixels[nc * (j * nx + i) + k];
          }
        }
      }
    };

    /**
     * Purge the channel from the image.
     * The image is stored in a single array, so we need to remove the
     * corresponding pixel values. We do this by copying the original and
     * omitting the channel to be removed. Additionally, we add middle gray
     * (128) to each pixel's color component where the alpha channel is 0.
     */
    auto purge_channel_pixels_with_gray_alpha = [](const auto &original_pixels,
                                                  auto &changed_pixels,
                                                  const size_t nx,
                                                  const size_t ny,
                                                  const size_t nc,
                                                  const size_t channel_to_remove,
                                                  const size_t new_nc) {
      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          for (size_t k = 0; k < nc; k++) {
            if (k == channel_to_remove) { continue; }
            // See purge_channel_pixels above: compact the removed channel's
            // slot instead of leaving a gap that overruns the output row.
            const size_t out_k = (k < channel_to_remove) ? k : k - 1;
            changed_pixels[new_nc * (j * nx + i) + out_k] =
              (original_pixels[nc * (j * nx + i) + channel_to_remove] == 0) ? 128
                                                                            : original_pixels[nc * (j * nx + i) + k];
          }
        }
      }
    };


    const auto extra_sample_to_remove = channel - ((photo == PhotometricInterpretation::SEPARATED) ? 4 : 3);
    const bool is_alpha_channel = es.at(extra_sample_to_remove) == ExtraSamples::ASSOCALPHA;
    const bool is_rgb_image = photo == PhotometricInterpretation::RGB;

    /*
     * 8 bit per sample.
     * Since we want to remove a channel, we need to remove the corresponding
     * pixel values, since the whole image is stored in a single array.
     * - if the image is RGB, we additionally check apply_gray_alpha and if true,
     *   we add middle gray (128) to each pixel's color component where the alpha
     *   channel is 0.
     */
    const size_t new_nc = nc - 1;
    if (bps == _8bps) {
      const byte *original_pixels = img.pixels_view().data();
      const auto changed_buf_size = checked_buf_size(nx, ny, new_nc, 1);
      if (!changed_buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(new_nc) + ", elem=1)" });
      }
      std::vector<byte> changed_v(*changed_buf_size);
      byte *changed_pixels = changed_v.data();

      // only force gray values if the image is RGB and the alpha channel is the channel to be removed
      const bool force_gray_values = force_gray_alpha && is_alpha_channel && is_rgb_image;
      if (force_gray_values) {
        purge_channel_pixels_with_gray_alpha(original_pixels, changed_pixels, nx, ny, nc, channel, new_nc);
      } else {
        purge_channel_pixels(original_pixels, changed_pixels, nx, ny, nc, channel, new_nc);
      }

      img.set_pixels(std::move(changed_v), nx, ny, new_nc, bps);
    } else if (bps == _16bps) {
      const auto *original_pixels = reinterpret_cast<const unsigned short *>(img.pixels_view().data());
      const auto changed_buf_size = checked_buf_size(nx, ny, new_nc, 2);
      if (!changed_buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(new_nc) + ", elem=2)" });
      }
      std::vector<byte> changed_v(*changed_buf_size);
      auto *changed_pixels = reinterpret_cast<unsigned short *>(changed_v.data());

      purge_channel_pixels(original_pixels, changed_pixels, nx, ny, nc, channel, new_nc);

      img.set_pixels(std::move(changed_v), nx, ny, new_nc, bps);
    } else {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Bits per sample is not supported for operation: " + std::to_string(bps) });
    }

    // remove the extra sample that we removed from the image
    es.erase(es.begin() + extra_sample_to_remove);
    img.setEs(std::move(es));
    return {};
  }

  //============================================================================


  Result<void> removeExtraSamples(SipiImage &img, const bool force_gray_alpha)
  {
    const size_t content_channels = (img.getPhoto() == PhotometricInterpretation::SEPARATED ? 4 : 3);
    const size_t extra_channels = img.getEs().size();
    for (size_t i = content_channels; i < (extra_channels + content_channels); i++) {
      if (auto r = removeChannel(img, i, force_gray_alpha); !r) { return std::unexpected(r.error()); }
    }
    return {};
  }

  //============================================================================


  Result<void> to8bps(SipiImage &img)
  {
    // little-endian architecture assumed
    //
    // we just use the shift-right operater (>> 8) to devide the values by 256 (2^8)!
    // This is the most efficient and fastest way
    //
    if (img.getBps() == 16) {
      const size_t nx = img.getNx();
      const size_t ny = img.getNy();
      const size_t nc = img.getNc();

      const auto buf_size = checked_buf_size(nx, ny, nc, 1);
      if (!buf_size) {
        return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
          "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
            + ", channels=" + std::to_string(nc) + ", elem=1)" });
      }

      const word *inbuf = reinterpret_cast<const word *>(img.pixels_view().data());
      std::vector<byte> outbuf(*buf_size);
      for (size_t j = 0; j < ny; j++) {
        for (size_t i = 0; i < nx; i++) {
          for (size_t k = 0; k < nc; k++) {
            // divide pixel values by 256 using ">> 8"
            outbuf[nc * (j * nx + i) + k] = (inbuf[nc * (j * nx + i) + k] >> 8);
          }
        }
      }

      img.set_pixels(std::move(outbuf), nx, ny, nc, 8);
    }
    return {};
  }

  //============================================================================


  Result<void> toBitonal(SipiImage &img)
  {
    SIPI_ZONE_N("SipiImage::toBitonal");
    if ((img.getPhoto() != PhotometricInterpretation::MINISBLACK)
        && (img.getPhoto() != PhotometricInterpretation::MINISWHITE)) {
      if (auto r = convertToIcc(img, Icc(icc_GRAY_D50), 8); !r) { return std::unexpected(r.error()); }
    }

    const size_t nx = img.getNx();
    const size_t ny = img.getNy();

    std::span<byte> pixels = img.pixels_writable();

    bool doit = false;// will be set true if we find a value not equal 0 or 255

    for (size_t i = 0; i < nx * ny; i++) {
      if (!doit && (pixels[i] != 0) && (pixels[i] != 255)) doit = true;
    }

    if (!doit) return {};// we have to do nothing, it's already bitonal

    // must be signed!! Error propagation my result in values < 0 or > 255
    const auto outbuf_bytes = checked_buf_size(nx, ny, 1, sizeof(short));
    if (!outbuf_bytes) {
      return std::unexpected(SipiValueError{ ErrorCode::kMalformedInput,
        "Pixel buffer size overflow (dimensions=" + std::to_string(nx) + "x" + std::to_string(ny)
          + ", channels=1, elem=" + std::to_string(sizeof(short)) + ")" });
    }
    std::vector<short> outbuf(*outbuf_bytes / sizeof(short));

    for (size_t i = 0; i < nx * ny; i++) {
      outbuf[i] = pixels[i];// copy buffer
    }

    for (size_t y = 0; y < ny; y++) {
      for (size_t x = 0; x < nx; x++) {
        short oldpixel = outbuf[y * nx + x];
        outbuf[y * nx + x] = (oldpixel > 127) ? 255 : 0;
        int properr = (oldpixel - outbuf[y * nx + x]);
        if (x < (nx - 1)) outbuf[y * nx + (x + 1)] += (7 * properr) >> 4;
        if ((x > 0) && (y < (ny - 1))) outbuf[(y + 1) * nx + (x - 1)] += (3 * properr) >> 4;
        if (y < (ny - 1)) outbuf[(y + 1) * nx + x] += (5 * properr) >> 4;
        if ((x < (nx - 1)) && (y < (ny - 1))) outbuf[(y + 1) * nx + (x + 1)] += properr >> 4;
      }
    }

    for (size_t i = 0; i < nx * ny; i++) pixels[i] = static_cast<byte>(outbuf[i]);
    return {};
  }

}// namespace processing
}// namespace Sipi
