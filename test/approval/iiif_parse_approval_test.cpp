#include "ApprovalTests.hpp"
#include "gtest/gtest.h"

#include "handlers/iiif_handler.hpp"

#include <sstream>

/// Helper to format parse results as a string for approval comparison.
static std::string format_parse_result(const std::string &uri)
{
    auto result = handlers::iiif_handler::parse_iiif_uri(uri);
    std::ostringstream out;
    out << "URI: " << uri << "\n";
    if (result.has_value()) {
        out << "Result: " << result->to_string() << "\n";
    } else {
        out << "Error: " << result.error() << "\n";
    }
    return out.str();
}

TEST(IIIFParseApproval, ValidIIIFUrls)
{
    std::ostringstream out;
    out << format_parse_result("/prefix/image.jp2/full/max/0/default.jpg");
    out << format_parse_result("/prefix/image.jp2/0,0,256,256/max/0/default.jpg");
    out << format_parse_result("/prefix/image.jp2/full/pct:50/90/default.png");
    out << format_parse_result("/prefix/image.jp2/square/256,/!180/gray.tif");
    out << format_parse_result("/prefix/image.jp2/pct:10,10,50,50/,200/0/bitonal.jpg");
    ApprovalTests::Approvals::verify(out.str());
}

TEST(IIIFParseApproval, SpecialEndpoints)
{
    std::ostringstream out;
    out << format_parse_result("/prefix/image.jp2/info.json");
    out << format_parse_result("/prefix/image.jp2/knora.json");
    out << format_parse_result("/prefix/image.jp2/file");
    out << format_parse_result("/prefix/image.jp2");
    ApprovalTests::Approvals::verify(out.str());
}

TEST(IIIFParseApproval, InvalidUrls)
{
    std::ostringstream out;
    out << format_parse_result("/prefix//image.jp2");
    out << format_parse_result("/prefix/image.jp2/max/0/default.jpg");
    out << format_parse_result("/prefix/image.jp2/full/max/default.jpg");
    out << format_parse_result("/prefix/image.jp2/full/max/!/default.jpg");
    out << format_parse_result("/prefix/image.jp2/full/max/0/jpg");
    ApprovalTests::Approvals::verify(out.str());
}
