//
// Created by Ivan Subotic on 20.03.2024.
//

#include "iiif_handler.hpp"

namespace handlers::iiif_handler {

    // Implementation of the parse_iiif_url function
    auto parse_iiif_url(const std::string &url) noexcept -> std::expected<IIIFUrlParseResult, std::string> {
        IIIFUrlParseResult result = {IIIF, {}};


        if (url.empty()) {
            // Return an unexpected result with an error message
            return std::unexpected<std::string>("URL is empty");
        }

        // Parsing logic here...
        // Populate result based on parsing

        return result;
    }

} // namespace handlers::iiif_handler
