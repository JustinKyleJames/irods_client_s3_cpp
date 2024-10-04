#ifndef IRODS_S3_API_S3_API_HPP
#define IRODS_S3_API_S3_API_HPP
#include <boost/asio/awaitable.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/bimap.hpp>
#include <boost/beast.hpp>
#include <irods/filesystem.hpp>
#include <boost/asio.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/url.hpp>
#include <irods/client_connection.hpp>
#include <irods/dstream.hpp>
#include <irods/transport/default_transport.hpp>

#include "irods/private/s3_api/log.hpp"
#include "irods/private/s3_api/common.hpp"

#include <unordered_map>

namespace irods::s3::actions
{
	void handle_listobjects_v2(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_listbuckets(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_getobject(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_deleteobject(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_deleteobjects(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_putobject(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_headobject(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_headbucket(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_copyobject(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_createmultipartupload(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_completemultipartupload(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_abortmultipartupload(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_listparts(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

	void handle_listmultipartuploads(
		irods::http::session_pointer_type sess_ptr,
		boost::beast::http::request_parser<boost::beast::http::empty_body>& parser,
		const boost::urls::url_view&);

} //namespace irods::s3::actions

namespace irods::s3::api::multipart_global_state
{
	extern std::unordered_map<std::string, std::unordered_map<unsigned int, uint64_t>> part_size_map;
	extern std::unordered_map<
		std::string,
		std::tuple<
			irods::experimental::io::replica_token,
			irods::experimental::io::replica_number,
			std::shared_ptr<irods::experimental::client_connection>,
			std::shared_ptr<irods::experimental::io::client::native_transport>,
			std::shared_ptr<irods::experimental::io::odstream>>>
		replica_token_number_and_odstream_map;

	extern std::unordered_map<std::string, boost::bimap<std::string, std::string>> object_key_to_upload_ids_bimap;
	extern std::mutex multipart_global_state_mutex;
	void print_multipart_global_state();
} // end namespace irods::s3::api::multipart_global_state
#endif
