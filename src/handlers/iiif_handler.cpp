//
// Created by Ivan Subotic on 20.03.2024.
//

#include "iiif_handler.hpp"

#include <regex>

#include "Connection.h"

namespace handlers::iiif_handler {

template <typename T>
std::string vector_to_string(const std::vector<T>& vec) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        if (i > 0) {
            oss << ", ";
        }
        oss << vec[i];
    }
    oss << "]";
    return oss.str();
}


/**
 * This function parses parts of an IIIF URI and returns a struct with the result.
 *
 * In general, the IIIF URI schema looks like this:
 * {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
 *
 * The string that is passed to this function is expected to be already stripped
 * off of the {schema} and {server} parts, thus only getting:
 * {/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format},
 * e.g., "/iiif/2/image.jpg/full/200,/0/default.jpg".
 */
[[nodiscard]]
auto parse_iiif_uri(const std::string& uri) noexcept -> std::expected<IIIFUriParseResult, std::string> {

    // std::cout << ">> parsing IIIF URI: " << uri << std::endl;

    RequestType request_type {UNDEFINED};

    std::vector<std::string> parts;
    size_t pos = 0;
    size_t old_pos = 0;

    //
    // IIIF URi schema:
    // {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
    //
    // The slashes "/" separate the different parts...
    //
    while ((pos = uri.find('/', pos)) != std::string::npos) {
        pos++;
        if (pos == 1) {
            // if first char is a token skip it!
            old_pos = pos;
            continue;
        }
        parts.push_back(shttps::urldecode(uri.substr(old_pos, pos - old_pos - 1)));
        old_pos = pos;
    }

    if (old_pos != uri.length()) {
        parts.push_back(shttps::urldecode(uri.substr(old_pos, std::string::npos)));
    }

    if (parts.empty()) {
        return std::unexpected("No parameters/path given");
    }

    // std::cout << ">> parts found: " << vector_to_string(parts) << std::endl;

    std::vector<std::string> params;

    //
    // below are regex expressions for the different parts of the IIIF URL
    //
    std::string qualform_ex = R"(^(color|gray|bitonal|default)\.(jpg|tif|png|jp2)$)";
    std::string rotation_ex = R"(^!?[-+]?[0-9]*\.?[0-9]*$)";
    std::string size_ex = R"(^(\^?max)|(\^?pct:[0-9]*\.?[0-9]*)|(\^?[0-9]*,)|(\^?,[0-9]*)|(\^?!?[0-9]*,[0-9]*)$)";
    std::string region_ex =
        R"(^(full)|(square)|([0-9]+,[0-9]+,[0-9]+,[0-9]+)|(pct:[0-9]*\.?[0-9]*,[0-9]*\.?[0-9]*,[0-9]*\.?[0-9]*,[0-9]*\.?[0-9]*)$)";

    bool qualform_ok = false;
    if (!parts.empty()) {
        // check if last part is a valid quality format
        qualform_ok = std::regex_match(parts[parts.size() - 1], std::regex(qualform_ex));
    }

    bool rotation_ok = false;
    if (parts.size() > 1) {
        // check if second last part is a valid rotation
        rotation_ok = std::regex_match(parts[parts.size() - 2], std::regex(rotation_ex));
    }

    bool size_ok = false;
    if (parts.size() > 2) {
        // check if third last part is a valid size
        size_ok = std::regex_match(parts[parts.size() - 3], std::regex(size_ex));
    }

    bool region_ok = false;
    if (parts.size() > 3) {
        // check if fourth last part is a valid region
        region_ok = std::regex_match(parts[parts.size() - 4], std::regex(region_ex));
    }

    // analyze the last part of the URL and look for a dot
    if ((pos = parts[parts.size() - 1].find('.', 0)) != std::string::npos) {

        // we have a dot and we will split the last part into a body and an extension
        // at this point we know that the last part could be either a quality format, a knora.json or a info.json
        std::string fname_body = parts[parts.size() - 1].substr(0, pos);
        std::string fname_extension = parts[parts.size() - 1].substr(pos + 1, std::string::npos);

        // Let's check if we have a valid IIIF URL
        if (qualform_ok && rotation_ok && size_ok && region_ok) {
            //
            // we potentially have a valid IIIF URL
            //
            if (parts.size() >= 6) {
                // we have a prefix
                std::stringstream prefix;
                for (size_t i = 0; i < (parts.size() - 5); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 5) {
                // we have no prefix
                params.emplace_back(""); // iiif_prefix
            } else {
                std::stringstream errmsg;
                errmsg << "IIIF url not correctly formatted:";
                return std::unexpected(errmsg.str());
            }
            params.push_back(parts[parts.size() - 5]); // iiif_identifier
            params.push_back(parts[parts.size() - 4]); // iiif_region
            params.push_back(parts[parts.size() - 3]); // iiif_size
            params.push_back(parts[parts.size() - 2]); // iiif_rotation
            params.push_back(parts[parts.size() - 1]); // iiif_qualityformat
            request_type = IIIF;
        } else if ((fname_body == "info") && (fname_extension == "json")) {
            //
            // we potentially have something like "http:://{server}/{prefix}/{id}/info.json
            //
            if (parts.size() >= 3) {
                // we have a prefix
                std::stringstream prefix;
                for (size_t i = 0; i < (parts.size() - 2); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 2) {
                // we have no prefix
                params.emplace_back(""); // iiif_prefix
            } else {
                return std::unexpected("IIIF url not correctly formatted!");
            }
            params.push_back(parts[parts.size() - 2]); // iiif_identifier
            request_type = INFO_JSON;
        } else if ((fname_body == "knora") && (fname_extension == "json")) {
            //
            // we potentially have something like "http:://{server}/{prefix}/{id}/knora.json
            //
            if (parts.size() >= 3) {
                // we have a prefix
                std::stringstream prefix;
                for (size_t i = 0; i < (parts.size() - 2); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 2) {
                // we have no prefix
                params.emplace_back(""); // iiif_prefix
            } else {
                return std::unexpected("IIIF url not correctly formatted!");
            }
            params.push_back(parts[parts.size() - 2]); // iiif_identifier
            request_type = KNORA_JSON;
        } else {
            //
            // we potentially have something like "/{prefix}/{id}" with id as "body.ext"
            //
            if (parts.size() >= 2) {
                // we have a prefix
                //
                // we have something like "/{prefix}/{id}" with id as "body.ext"
                //
                std::stringstream prefix;
                for (size_t i = 0; i < (parts.size() - 1); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 1) {
                // we have no prefix
                //
                // we have something like "/{id}" with id as "body.ext"
                //
                params.emplace_back(""); // iiif_prefix
            } else {
                std::stringstream errmsg;
                errmsg << "IIIF url not correctly formatted:";
                if (!qualform_ok && (!parts.empty()))
                    errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
                if (!rotation_ok && (parts.size() > 1))
                    errmsg << " Error in rotation: \"" << parts[parts.size() - 2] << "\"!";
                if (!size_ok && (parts.size() > 2))
                    errmsg << " Error in size: \"" << parts[parts.size() - 3] << "\"!";
                if (!region_ok && (parts.size() > 3))
                    errmsg << " Error in region: \"" << parts[parts.size() - 4] << "\"!";
                return std::unexpected(errmsg.str());
            }
            params.push_back(parts[parts.size() - 1]); // iiif_identifier
            request_type = REDIRECT;
        }
    } else if (parts[parts.size() - 1] == "file") {
        //
        // we potentially have something like "/{prefix}/{id}/file
        //
        if (parts.size() >= 3) {
            // we have a prefix
            //
            // we have something like "/{prefix}/{id}/file
            //
            std::stringstream prefix;
            for (size_t i = 0; i < (parts.size() - 2); i++) {
                if (i > 0)
                    prefix << "/";
                prefix << parts[i];
            }
            params.push_back(prefix.str()); // iiif_prefix
        } else if (parts.size() == 2) {
            // we have no prefix
            //
            // we have something like "/{id}/file
            //
            params.emplace_back(""); // iiif_prefix
        } else {
            return std::unexpected("IIIF url not correctly formatted!");
        }
        params.push_back(parts[parts.size() - 2]); // iiif_identifier
        request_type = FILE_DOWNLOAD;
    } else {
        //
        // we potentially have something like "/{prefix}/{id}" with id as "body_without_ext"
        //
        if (parts.size() >= 2) {
            // we have a prefix
            std::stringstream prefix;
            for (size_t i = 0; i < (parts.size() - 1); i++) {
                if (i > 0)
                    prefix << "/";
                prefix << parts[i];
            }
            params.push_back(prefix.str()); // iiif_prefix
        } else if (parts.size() == 1) {
            // we have no prefix
            params.emplace_back(""); // iiif_prefix
        } else {
            return std::unexpected("IIIF url not correctly formatted!");
        }
        params.push_back(parts[parts.size() - 1]); // iiif_identifier
        request_type = REDIRECT;
    }

    // std::cout << ">> params: " << vector_to_string(params) << ", request_type: " << request_type << std::endl;

    return IIIFUriParseResult {request_type, params};
}
} // namespace handlers::iiif_handler
