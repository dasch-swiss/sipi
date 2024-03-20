//
// Created by Ivan Subotic on 20.03.2024.
//

#include "iiif_handler.hpp"

#include <regex>

#include "Connection.h"

namespace handlers::iiif_handler {

    // Implementation of the parse_iiif_url function
    auto parse_iiif_url(const std::string &uri) noexcept -> std::expected<IIIFUrlParseResult, std::string> {

        RequestType request_type { UNDEFINED };

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
        if (!parts.empty())
            qualform_ok = std::regex_match(parts[parts.size() - 1], std::regex(qualform_ex));

        bool rotation_ok = false;
        if (parts.size() > 1)
            rotation_ok = std::regex_match(parts[parts.size() - 2], std::regex(rotation_ex));

        bool size_ok = false;
        if (parts.size() > 2)
            size_ok = std::regex_match(parts[parts.size() - 3], std::regex(size_ex));

        bool region_ok = false;
        if (parts.size() > 3)
            region_ok = std::regex_match(parts[parts.size() - 4], std::regex(region_ex));

        if ((pos = parts[parts.size() - 1].find('.', 0)) != std::string::npos) {
            std::string fname_body = parts[parts.size() - 1].substr(0, pos);
            std::string fname_extension = parts[parts.size() - 1].substr(pos + 1, std::string::npos);
            //
            // we will serve IIIF syntax based image
            //
            if (qualform_ok && rotation_ok && size_ok && region_ok) {
                if (parts.size() >= 6) {
                    // we have a prefix
                    std::stringstream prefix;
                    for (int i = 0; i < (parts.size() - 5); i++) {
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
                    if (!qualform_ok)
                        errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
                    if (!rotation_ok)
                        errmsg << " Error in rotation: \"" << parts[parts.size() - 2] << "\"!";
                    if (!size_ok)
                        errmsg << " Error in size: \"" << parts[parts.size() - 3] << "\"!";
                    if (!region_ok)
                        errmsg << " Error in region: \"" << parts[parts.size() - 4] << "\"!";
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
                // we have something like "http:://{server}/{prefix}/{id}/info.json
                //
                if (parts.size() >= 3) {
                    // we have a prefix
                    std::stringstream prefix;
                    for (int i = 0; i < (parts.size() - 2); i++) {
                        if (i > 0)
                            prefix << "/";
                        prefix << parts[i];
                    }
                    params.push_back(prefix.str()); // iiif_prefix
                } else if (parts.size() == 2) {
                    // we have no prefix
                    params.push_back(""); // iiif_prefix
                } else {
                    return std::unexpected("IIIF url not correctly formatted!");
                }
                params.push_back(parts[parts.size() - 2]); // iiif_identifier
                request_type = INFO_JSON;
            } else if ((fname_body == "knora") && (fname_extension == "json")) {
                //
                // we have something like "http:://{server}/{prefix}/{id}/knora.json
                //
                if (parts.size() >= 3) {
                    // we have a prefix
                    std::stringstream prefix;
                    for (int i = 0; i < (parts.size() - 2); i++) {
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
                // we have something like "http:://{server}/{prefix}/{id}" with id as "body.ext"
                //
                if (qualform_ok || rotation_ok || size_ok || region_ok) {
                    std::stringstream errmsg;
                    errmsg << "IIIF url not correctly formatted:";
                    if (!qualform_ok && !parts.empty())
                        errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
                    if (!rotation_ok && (parts.size() > 1))
                        errmsg << " Error in rotation: \"" << parts[parts.size() - 2] << "\"!";
                    if (!size_ok && (parts.size() > 2))
                        errmsg << " Error in size: \"" << parts[parts.size() - 3] << "\"!";
                    if (!region_ok && (parts.size() > 3))
                        errmsg << " Error in region: \"" << parts[parts.size() - 4] << "\"!";
                    return std::unexpected(errmsg.str());
                }
                if (parts.size() >= 2) {
                    // we have a prefix
                    std::stringstream prefix;
                    for (int i = 0; i < (parts.size() - 1); i++) {
                        if (i > 0)
                            prefix << "/";
                        prefix << parts[i];
                    }
                    params.push_back(prefix.str()); // iiif_prefix
                } else if (parts.size() == 1) {
                    // we have no prefix
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
            if (parts.size() >= 3) {
                // we have a prefix
                //
                // we have something like "http:://{server}/{prefix}/{id}/file
                //
                std::stringstream prefix;
                for (int i = 0; i < (parts.size() - 2); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 2) {
                // we have no prefix
                //
                // we have something like "http:://{server}/{id}/file
                //
                params.emplace_back(""); // iiif_prefix
            } else {
                return std::unexpected("IIIF url not correctly formatted!");
            }
            params.push_back(parts[parts.size() - 2]); // iiif_identifier
            request_type = FILE_DOWNLOAD;
        } else {
            //
            // we have something like "http:://{server}/{prefix}/{id}" with id as "body_without_ext"
            //
            if (qualform_ok || rotation_ok || size_ok || region_ok) {
                std::stringstream errmsg;
                errmsg << "IIIF url not correctly formatted:";
                if (!qualform_ok && (parts.size() > 0))
                    errmsg << " Error in quality: \"" << parts[parts.size() - 1] << "\"!";
                if (!rotation_ok && (parts.size() > 1))
                    errmsg << " Error in rotation: \"" << parts[parts.size() - 2] << "\"!";
                if (!size_ok && (parts.size() > 2))
                    errmsg << " Error in size: \"" << parts[parts.size() - 3] << "\"!";
                if (!region_ok && (parts.size() > 3))
                    errmsg << " Error in region: \"" << parts[parts.size() - 4] << "\"!";
                return std::unexpected(errmsg.str());
            }
            if (parts.size() >= 2) {
                // we have a prefix
                std::stringstream prefix;
                for (int i = 0; i < (parts.size() - 1); i++) {
                    if (i > 0)
                        prefix << "/";
                    prefix << parts[i];
                }
                params.push_back(prefix.str()); // iiif_prefix
            } else if (parts.size() == 1) {
                // we have no prefix
                params.emplace_back(""); // iiif_prefix
            } else {
                std::stringstream errmsg;
                errmsg << "IIIF url not correctly formatted:";
                if (!qualform_ok && (parts.size() > 0))
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

        return IIIFUrlParseResult{request_type, params};
    }

} // namespace handlers::iiif_handler
