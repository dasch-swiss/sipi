//
// Created by Ivan Subotic on 20.03.2024.
//

#ifndef IIIF_HANDLER_HPP
#define IIIF_HANDLER_HPP

#include <expected>
#include <sstream>
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

    inline std::string requestTypeToString(const RequestType type) {
        switch (type) {
        case IIIF:
            return "IIIF";
        case INFO_JSON:
            return "INFO_JSON";
        case KNORA_JSON:
            return "KNORA_JSON";
        case REDIRECT:
            return "REDIRECT";
        case FILE_DOWNLOAD:
            return "FILE_DOWNLOAD";
        case UNDEFINED:
            return "UNDEFINED";
        default:
            return "Unknown RequestType";
        }
    }


    // Struct to hold the result of parsing an IIIF URL
    struct IIIFUrlParseResult {
        RequestType request_type;
        std::vector<std::string> params;

        [[nodiscard]] std::string to_string() const {
            std::stringstream result;

            // Assuming you have a function to convert RequestType to string
            // If not, you'll need to implement this according to your enum
            std::string requestTypeStr = requestTypeToString(request_type);

            result << "request_type: " << requestTypeStr << ", params: ";

            // Concatenate params
            for (size_t i = 0; i < params.size(); ++i) {
                result << params[i];
                if (i != params.size() - 1) {
                    result << ", "; // Add a separator between the params, but not after the last one
                }
            }

            return result.str();
        }
    };

    // Free function to parse an IIIF URL returning either the result or an error message
    [[nodiscard]] auto
    parse_iiif_url(const std::string &uri) noexcept -> std::expected<IIIFUrlParseResult, std::string>;

} // namespace handlers::iiif_handler


#endif //IIIF_HANDLER_HPP
