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

void irods::s3::actions::handle_listparts(
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
	std::filesystem::path s3_key;

	bool on_bucket = true;
	for (auto seg : url.encoded_segments()) {
		if (on_bucket) {
			on_bucket = false;
			s3_bucket = seg.decode();
		}
		else {
			s3_key = s3_key / seg.decode();
		}
	}

	logging::debug("{} s3_bucket={} s3_key={}", __func__, s3_bucket.string(), s3_key.string());

	fs::path path;
	if (auto bucket = irods::s3::resolve_bucket(url.segments()); bucket.has_value()) {
		path = bucket.value();
		path = irods::s3::finish_path(path, url.segments());
		logging::debug("{}: ListParts path={}", __func__, path.string());
	}
	else {
		logging::error("{}: Failed to resolve bucket", __func__);
		response.result(beast::http::status::forbidden);
		logging::debug("{}: returned [{}]", __func__, response.reason());
		session_ptr->send(std::move(response));
		return;
	}

	// get the uploadId from the param list
	std::string upload_id;
	if (const auto upload_id_param = url.params().find("uploadId"); upload_id_param != url.params().end()) {
		upload_id = (*upload_id_param).value;
	}

	if (upload_id.empty()) {
		logging::error("{}: Did not receive an uploadId", __func__);
		response.result(beast::http::status::bad_request);
		logging::debug("{}: returned [{}]", __func__, response.reason());
		session_ptr->send(std::move(response));
		return;
	}

	// Do not allow an upload_id that is not in the format we have defined. People could do bad things
	// if we didn't enforce this.
	if (!std::regex_match(upload_id, upload_id_pattern)) {
		logging::error("{}: Upload ID [{}] was not in expected format.", __func__, upload_id);
		response.result(beast::http::status::bad_request);
		logging::debug("{}: returned [{}]", __func__, response.reason());
		session_ptr->send(std::move(response));
		return;
	}

	beast::http::response<beast::http::string_body> string_body_response(std::move(response));
	string_body_response.result(beast::http::status::ok);

	// If we have the upload_id but the paths do not match, just send an empty response.
	bool paths_match = true;
	{
		std::lock_guard<std::mutex> guard(mpart_global_state::multipart_global_state_mutex);

		auto& key_to_upload_id_bimap = mpart_global_state::object_key_to_upload_ids_bimap[s3_bucket.string()];

		if (key_to_upload_id_bimap.right.find(upload_id) != key_to_upload_id_bimap.right.end()) {
			auto expected_key = key_to_upload_id_bimap.right.at(upload_id);
			if (expected_key != s3_key.string()) {
				paths_match = false;
			}
		}
	}

	// Now send the response
	// Example response (abbreviated):
	// <ListPartsResult>
	//    <Bucket>string</Bucket>
	//    <Key>string</Key>
	//    <UploadId>string</UploadId>
	//    <IsTruncated>boolean</IsTruncated>
	//    <Part>
	//       <PartNumber>integer</PartNumber>
	//       <Size>long</Size>
	//    </Part>
	//    ...
	// </ListPartsResult>*/

	boost::property_tree::ptree document;
	boost::property_tree::xml_parser::xml_writer_settings<std::string> settings;
	settings.indent_char = ' ';
	settings.indent_count = 4;
	std::stringstream s;

	document.add("ListPartsResult.Bucket", s3_bucket.string());
	document.add("ListPartsResult.Key", s3_key.string());
	document.add("ListPartsResult.UploadId", upload_id);
	document.add("ListPartsResult.IsTruncated", "false");

	if (paths_match) {
		std::lock_guard<std::mutex> guard(mpart_global_state::multipart_global_state_mutex);
		if (mpart_global_state::part_size_map.find(upload_id) != mpart_global_state::part_size_map.end()) {
			auto& parts_map = mpart_global_state::part_size_map.at(upload_id);
			for (std::pair<const unsigned int, uint64_t>& part_info : parts_map) {
				boost::property_tree::ptree object;
				object.put("PartNumber", part_info.first);
				object.put("Size", part_info.second);
				document.add_child("ListPartsResult.Part", object);
			}
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
