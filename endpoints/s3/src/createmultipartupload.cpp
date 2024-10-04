#include "irods/private/s3_api/s3_api.hpp"
#include "irods/private/s3_api/authentication.hpp"
#include "irods/private/s3_api/bucket.hpp"
#include "irods/private/s3_api/common_routines.hpp"
#include "irods/private/s3_api/connection.hpp"
#include "irods/private/s3_api/log.hpp"
#include "irods/private/s3_api/common.hpp"
#include "irods/private/s3_api/session.hpp"
#include "irods/private/s3_api/configuration.hpp"

#include <irods/irods_exception.hpp>

#include <boost/bimap.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>

#include <fmt/format.h>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace fs = irods::experimental::filesystem;
namespace logging = irods::http::logging;

void irods::s3::actions::handle_createmultipartupload(
	irods::http::session_pointer_type session_ptr,
	boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
	const boost::urls::url_view& url)
{
	namespace mpart_global_state = irods::s3::api::multipart_global_state;

	beast::http::response<beast::http::empty_body> response;

	// Authenticate
	auto irods_username = irods::s3::authentication::authenticates(parser, url);
	if (!irods_username) {
		logging::error("{}: Failed to authenticate.", __FUNCTION__);
		response.result(beast::http::status::forbidden);
		logging::debug("{}: returned [{}]", __FUNCTION__, response.reason());
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

	logging::debug("{} s3_bucket={} s3_key={}", __FUNCTION__, s3_bucket.string(), s3_key.string());

	fs::path path;
	if (auto bucket = irods::s3::resolve_bucket(url.segments()); bucket.has_value()) {
		path = bucket.value();
		path = irods::s3::finish_path(path, url.segments());
		logging::debug("{}: CreateMultipartUpload path={}", __FUNCTION__, path.string());
	}
	else {
		logging::error("{}: Failed to resolve bucket", __FUNCTION__);
		response.result(beast::http::status::forbidden);
		logging::debug("{}: returned [{}]", __FUNCTION__, response.reason());
		session_ptr->send(std::move(response));
		return;
	}

	beast::http::response<beast::http::string_body> string_body_response(std::move(response));
	string_body_response.result(boost::beast::http::status::ok);

	// create the UploadId
	std::string upload_id = boost::lexical_cast<std::string>(boost::uuids::random_generator()());

	// Update object_key_to_upload_ids_bimap.
	// Make sure that there is not already an open request for this path.
	// If there is, reject this request. If not create a new entry for it and continue.
	{
		std::lock_guard<std::mutex> guard(mpart_global_state::multipart_global_state_mutex);

		// if an entry does not exist for this bucket in object_key_to_upload_ids_bimap, go ahead and create it
		if (mpart_global_state::object_key_to_upload_ids_bimap.find(s3_bucket.string()) ==
		    mpart_global_state::object_key_to_upload_ids_bimap.end())
		{
			mpart_global_state::object_key_to_upload_ids_bimap[s3_bucket.string()] =
				boost::bimap<std::string, std::string>();
		}

		auto& key_to_upload_id_bimap = mpart_global_state::object_key_to_upload_ids_bimap[s3_bucket.string()];

		if (key_to_upload_id_bimap.left.find(s3_key.string()) != key_to_upload_id_bimap.left.end()) {
			auto existing_upload_id = key_to_upload_id_bimap.left.at(s3_key.string());
			logging::error(
				"{}: There is already an open upload ID [{}] open for key [{}]. Rejecting this request.",
				__func__,
				existing_upload_id,
				s3_key.string());
			response.result(beast::http::status::bad_request);
			logging::debug("{}: returned [{}]", __func__, response.reason());
			session_ptr->send(std::move(response));
			return;
		}
		key_to_upload_id_bimap.insert({s3_key.string(), upload_id});
	}

	boost::property_tree::ptree document;
	document.add("InitiateMultipartUploadResult", "");
	document.add("InitiateMultipartUploadResult.Bucket", s3_bucket.c_str());
	document.add("InitiateMultipartUploadResult.Key", s3_key.c_str());
	document.add("InitiateMultipartUploadResult.UploadId", upload_id.c_str());

	std::stringstream s;
	boost::property_tree::xml_parser::xml_writer_settings<std::string> settings;
	settings.indent_char = ' ';
	settings.indent_count = 4;
	boost::property_tree::write_xml(s, document, settings);
	string_body_response.body() = s.str();
	std::cout << "------ CreateMultipartUpload Response Body -----" << std::endl;
	std::cout << s.str() << std::endl;

	string_body_response.prepare_payload();
	session_ptr->send(std::move(string_body_response));
}
