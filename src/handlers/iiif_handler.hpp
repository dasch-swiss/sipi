//
// Created by Ivan Subotic on 20.03.2024.
//

#ifndef IIIF_HANDLER_HPP
#define IIIF_HANDLER_HPP

#include <expected>
#include <string>
#include <vector>

namespace handlers::iiif_handler {

    enum RequestType {
        IIIF,
        INFO_JSON,
        KNORA_JSON,
        REDIRECT,
        FILE_DOWNLOAD,
        UNDEFINED
    };


    // Struct to hold the result of parsing an IIIF URL
    struct IIIFUrlParseResult {
        RequestType request_type;
        std::vector<std::string> params;
    };

    // Free function to parse an IIIF URL returning either the result or an error message
    [[nodiscard]] auto parse_iiif_url(const std::string &url) noexcept -> std::expected<IIIFUrlParseResult, std::string>;

} // namespace handlers::iiif_handler


#endif //IIIF_HANDLER_HPP
