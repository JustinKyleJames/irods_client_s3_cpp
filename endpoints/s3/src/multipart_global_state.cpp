#include "irods/private/s3_api/log.hpp"
#include <irods/client_connection.hpp>
#include <irods/dstream.hpp>
#include <irods/transport/default_transport.hpp>

#include <boost/bimap.hpp>

#include <unordered_map>

namespace logging = irods::http::logging;

namespace irods::s3::api::multipart_global_state
{
	// This is a part size map which is shared between threads. Whenever part sizes are known
	// they are added to this map. When complete or cancel multipart is executed, the part sizes
	// for that upload_id are removed.
	// The map looks like the following with the key of the first map being the upload_id and the
	// key of the second map being the part number:
	//    {
	//      "1234abcd-1234-1234-1234-123456789abc":
	//        { 0: 4096000,
	//          1: 4096000,
	//          4: 4096000 },
	//      "01234abc-0123-0123-0123-0123456789ab":
	//        { 0: 5192000,
	//          3: 5192000 }
	//    }
	std::unordered_map<std::string, std::unordered_map<unsigned int, uint64_t>> part_size_map;

	// This map holds persistent data needed for each upload_id. This includes the replica_token and
	// replica_number.  In addition it holds shared pointers for the connection, transport, and odstream
	// for the first stream that is opened. These shared pointers are saved to this map so that the first
	// open() to an iRODS data object can remain open throughout the lifecycle of the request.  This
	// stream will be closed in either CompleteMultipartUpload or AbortMultipartUpload.
	std::unordered_map<
		std::string,
		std::tuple<
			irods::experimental::io::replica_token,
			irods::experimental::io::replica_number,
			std::shared_ptr<irods::experimental::client_connection>,
			std::shared_ptr<irods::experimental::io::client::native_transport>,
			std::shared_ptr<irods::experimental::io::odstream>>>
		replica_token_number_and_odstream_map;

	// This map maps an object key to one or more upload_id's. The outer map has a key for the bucket name.
	// Example:
	//   {
	//      "alice-bucket":
	//        { "path/to/file1": "1234abcd-1234-1234-1234-123456789abc",
	//          "path/to/file2": "01234abc-0123-0123-0123-0123456789ab" },
	//      "rods-bucket":
	//        { "path/to/file1": "abc01234-1234-1234-1234-abc123456789" }
	//    }
	std::unordered_map<std::string, boost::bimap<std::string, std::string>> object_key_to_upload_ids_bimap;

	// mutex to protect part_size_map
	std::mutex multipart_global_state_mutex;

	void print_multipart_global_state()
	{
		namespace mpart_global_state = irods::s3::api::multipart_global_state;
		logging::debug(" ---- part_size_map ----");
		for (const auto& [key, value] : mpart_global_state::part_size_map) {
			logging::debug("{}:", key);
			for (const auto& [key2, value2] : value) {
				logging::debug("   {}:{}", key2, value2);
			}
		}
		logging::debug(" ---- replica_token_number_and_odstream_map ----");
		for (const auto& [key, value] : mpart_global_state::replica_token_number_and_odstream_map) {
			logging::debug("{}:", key);
		}
		logging::debug(" ---- object_key_to_upload_ids_bimap ----");
		for (const auto& [key, bimap] : mpart_global_state::object_key_to_upload_ids_bimap) {
			logging::debug("{}:", key);
			for (auto iter = bimap.left.begin(); iter != bimap.left.end(); ++iter) {
				logging::debug("    {}: {}", iter->first, iter->second);
			}
		}
		logging::debug(" ----------------------------------------");
	} // print_multipart_global_state()
} // end namespace irods::s3::api::multipart_global_state
