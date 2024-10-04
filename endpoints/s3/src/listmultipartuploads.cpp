#include "irods/private/s3_api/s3_api.hpp"
#include "irods/private/s3_api/authentication.hpp"
#include "irods/private/s3_api/bucket.hpp"
#include "irods/private/s3_api/common_routines.hpp"
#include "irods/private/s3_api/connection.hpp"
#include "irods/private/s3_api/log.hpp"
#include "irods/private/s3_api/common.hpp"
#include "irods/private/s3_api/session.hpp"
#include "irods/private/s3_api/configuration.hpp"
#include "irods/private/s3_api/globals.hpp"

#include <irods/dstream.hpp>
#include <irods/transport/default_transport.hpp>
#include <irods/irods_exception.hpp>
#include <irods/irods_at_scope_exit.hpp>

#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <fmt/format.h>
#include <spdlog/spdlog.h>
#include <regex>
#include <cstdio>
#include <vector>
#include <mutex>
#include <memory>
#include <string>
#include <sstream>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace fs = irods::experimental::filesystem;
namespace logging = irods::http::logging;

namespace
{
	std::regex upload_id_pattern("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}");
} //namespace

void irods::s3::actions::handle_listmultipartuploads(
	irods::http::session_pointer_type session_ptr,
	boost::beast::http::request_parser<boost::beast::http::empty_body>& empty_body_parser,
	const boost::urls::url_view& url)
{
	namespace mpart_global_state = irods::s3::api::multipart_global_state;

	beast::http::response<beast::http::empty_body> response;

	// Authenticate
	auto irods_username = irods::s3::authentication::authenticates(empty_body_parser, url);
	if (!irods_username) {
		logging::error("{}: Failed to authenticate.", __func__);
		response.result(beast::http::status::forbidden);
		logging::debug("{}: returned [{}]", __func__, response.reason());
		session_ptr->send(std::move(response));
		return;
	}

	auto conn = irods::get_connection(*irods_username);

	std::filesystem::path s3_bucket;
	//std::filesystem::path s3_key;

	for (auto seg : url.encoded_segments()) {
		s3_bucket = seg.decode();
		break;
	}

	logging::debug("{} s3_bucket={}", __func__, s3_bucket.string());

	beast::http::response<beast::http::string_body> string_body_response(std::move(response));
	string_body_response.result(beast::http::status::ok);

	// Now send the response
	// Example response (abbreviated):
	// <ListMultipartUploadsResult>
	//   <Bucket>string</Bucket>
	//   <Prefix>string</Prefix>  // TODO
	//   <IsTruncated>false</IsTruncated>
	//   <Upload>
	//      <UploadId>string</UploadId>
	//      <Key>string</Key>
	//   </Upload>
	//   ...
	//   <CommonPrefixes> // TODO
	//      <Prefix>string</Prefix>
	//   </CommonPrefixes>
	//   ...
	// </ListMultipartUploadsResult>

	boost::property_tree::ptree document;
	boost::property_tree::xml_parser::xml_writer_settings<std::string> settings;
	settings.indent_char = ' ';
	settings.indent_count = 4;
	std::stringstream s;

	document.add("ListMultipartUploadsResult.Bucket", s3_bucket.string());
	document.add("ListMultipartUploadsResult.IsTruncated", "false");

	{
		std::lock_guard<std::mutex> guard(mpart_global_state::multipart_global_state_mutex);

		auto& key_to_upload_id_bimap = mpart_global_state::object_key_to_upload_ids_bimap[s3_bucket.string()];

		for (auto iter = key_to_upload_id_bimap.left.begin(); iter != key_to_upload_id_bimap.left.end(); ++iter) {
			boost::property_tree::ptree object;
			object.put("UploadId", iter->second);
			object.put("Key", iter->first);
			document.add_child("ListMultipartUploadsResult.Upload", object);
		}
	}
	boost::property_tree::write_xml(s, document, settings);
	string_body_response.body() = s.str();
	logging::debug("{}: response\n{}", __func__, s.str());
	string_body_response.result(boost::beast::http::status::ok);
	logging::debug("{}: returned [{}]", __func__, string_body_response.reason());
	session_ptr->send(std::move(string_body_response));
	return;
}
