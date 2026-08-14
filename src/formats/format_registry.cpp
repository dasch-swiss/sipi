// Static format-handler registry for SipiImage.
//
// Defines `SipiImage::io`, the map from format key (`tif`/`jpx`/`jpg`/`png`)
// to the concrete SipiIO handler instance. This translation unit lives in
// //src/formats — which depends one-way on //src:engine — precisely so the
// engine's SipiImage.cpp does NOT include the concrete handlers. That inverts
// the former SipiImage<->handler include cycle: the engine references `io`,
// and the linker resolves the definition here when //src/formats is on the
// link line (via //src:sipi_lib and //src/ffi:sipi_ffi).

#include <memory>
#include <string>
#include <unordered_map>

#include "SipiImage.h"
#include "SipiIOJ2k.h"
#include "SipiIOJpeg.h"
#include "SipiIOPng.h"
#include "SipiIOTiff.h"

namespace Sipi {

std::unordered_map<std::string, std::shared_ptr<SipiIO>> SipiImage::io = {
  { "tif", std::make_shared<SipiIOTiff>() },
  { "jpx", std::make_shared<SipiIOJ2k>() },
  { "jpg", std::make_shared<SipiIOJpeg>() },
  { "png", std::make_shared<SipiIOPng>() }
};

}  // namespace Sipi
