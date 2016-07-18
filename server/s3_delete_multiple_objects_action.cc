/*
 * COPYRIGHT 2016 SEAGATE LLC
 *
 * THIS DRAWING/DOCUMENT, ITS SPECIFICATIONS, AND THE DATA CONTAINED
 * HEREIN, ARE THE EXCLUSIVE PROPERTY OF SEAGATE TECHNOLOGY
 * LIMITED, ISSUED IN STRICT CONFIDENCE AND SHALL NOT, WITHOUT
 * THE PRIOR WRITTEN PERMISSION OF SEAGATE TECHNOLOGY LIMITED,
 * BE REPRODUCED, COPIED, OR DISCLOSED TO A THIRD PARTY, OR
 * USED FOR ANY PURPOSE WHATSOEVER, OR STORED IN A RETRIEVAL SYSTEM
 * EXCEPT AS ALLOWED BY THE TERMS OF SEAGATE LICENSES AND AGREEMENTS.
 *
 * YOU SHOULD HAVE RECEIVED A COPY OF SEAGATE'S LICENSE ALONG WITH
 * THIS RELEASE. IF NOT PLEASE CONTACT A SEAGATE REPRESENTATIVE
 * http://www.seagate.com/contact
 *
 * Original author:  Kaustubh Deorukhkar   <kaustubh.deorukhkar@seagate.com>
 * Original creation date: 3-Feb-2016
 */

#include "s3_delete_multiple_objects_action.h"
#include "s3_option.h"
#include "s3_error_codes.h"
#include "s3_perf_logger.h"

S3DeleteMultipleObjectsAction::S3DeleteMultipleObjectsAction(std::shared_ptr<S3RequestObject> req) : S3Action(req), is_request_content_corrupt(false), is_request_too_large(false), delete_index(0) {
  s3_log(S3_LOG_DEBUG, "Constructor\n");
  std::shared_ptr<ClovisAPI> s3_clovis_api = std::make_shared<ConcreteClovisAPI>();
  clovis_kv_reader = std::make_shared<S3ClovisKVSReader>(request);
  clovis_kv_writer = std::make_shared<S3ClovisKVSWriter>(request, s3_clovis_api);
  clovis_writer = std::make_shared<S3ClovisWriter>(request);
  setup_steps();
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::setup_steps(){
  s3_log(S3_LOG_DEBUG, "Setting up the action\n");
  add_task(std::bind( &S3DeleteMultipleObjectsAction::validate_request, this ));
  add_task(std::bind( &S3DeleteMultipleObjectsAction::fetch_bucket_info, this ));
  add_task(std::bind( &S3DeleteMultipleObjectsAction::fetch_objects_info, this ));
  add_task(std::bind( &S3DeleteMultipleObjectsAction::delete_objects, this ));
  add_task(std::bind( &S3DeleteMultipleObjectsAction::send_response_to_s3_client, this ));
  // ...
}

void S3DeleteMultipleObjectsAction::validate_request() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  if (request->get_content_length() > 0) {
    if (request->has_all_body_content()) {
      validate_request_body(request->get_full_body_content_as_string());
    } else {
      request->listen_for_incoming_data(
          std::bind(&S3DeleteMultipleObjectsAction::consume_incoming_content, this),
          request->get_content_length()
        );
    }
  } else {
    validate_request_body("");
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::consume_incoming_content() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  if (request->has_all_body_content()) {
    validate_request_body(request->get_full_body_content_as_string());
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::validate_request_body(std::string content) {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  MD5hash calc_md5;
  calc_md5.Update(content.c_str(), content.length());
  calc_md5.Finalize();

  std::string req_md5_str = request->get_header_value("content-md5");
  std::string calc_md5_str = calc_md5.get_md5_base64enc_string();
  if (calc_md5_str != req_md5_str) {
    // Request payload was corrupted in transit.
    is_request_content_corrupt = true;
    send_response_to_s3_client();
  } else {
    delete_request.initialize(content);
    if (delete_request.isOK()) {
      if (delete_request.get_count() > 1000) {
        is_request_too_large = true;
        send_response_to_s3_client();
      } else {
        next();
      }
    } else {
      invalid_request = true;
      send_response_to_s3_client();
    }
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::fetch_bucket_info() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  bucket_metadata = std::make_shared<S3BucketMetadata>(request);
  bucket_metadata->load(std::bind( &S3DeleteMultipleObjectsAction::next, this), std::bind( &S3DeleteMultipleObjectsAction::fetch_bucket_info_failed, this));
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::fetch_bucket_info_failed() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  s3_log(S3_LOG_ERROR, "Fetching of bucket information failed\n");
  send_response_to_s3_client();
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::fetch_objects_info() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  if (delete_index < delete_request.get_count()) {
    keys_to_delete.clear();
    keys_to_delete = delete_request.get_keys(delete_index, S3Option::get_instance()->get_clovis_idx_fetch_count());

    clovis_kv_reader->get_keyval(get_bucket_index_name(), keys_to_delete, std::bind( &S3DeleteMultipleObjectsAction::delete_objects, this), std::bind( &S3DeleteMultipleObjectsAction::fetch_objects_info_failed, this));
    delete_index += keys_to_delete.size();
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::fetch_objects_info_failed() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  if (clovis_kv_reader->get_state() == S3ClovisKVSReaderOpState::missing) {
    for (auto& key : keys_to_delete) {
      delete_objects_response.add_success(key);
    }
  }
  if (delete_index < delete_request.get_count()) {
    fetch_objects_info();
  } else {
    send_response_to_s3_client();
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  auto& kvps = clovis_kv_reader->get_key_values();
  objects_metadata.clear();

  std::vector<struct m0_uint128> oids;
  for (auto& kv : kvps) {
    if (!kv.second.empty()) {
      s3_log(S3_LOG_DEBUG, "Delete Object = %s\n", kv.first.c_str());
      auto object = std::make_shared<S3ObjectMetadata>(request);
      object->from_json(kv.second);
      objects_metadata.push_back(object);
      oids.push_back(object->get_oid());
    } else {
      s3_log(S3_LOG_DEBUG, "Delete Object missing = %s\n", kv.first.c_str());
      delete_objects_response.add_success(kv.first);
    }
  }
  // Now trigger the delete.
  clovis_writer->delete_objects(oids, std::bind( &S3DeleteMultipleObjectsAction::delete_objects_successful, this), std::bind( &S3DeleteMultipleObjectsAction::delete_objects_failed, this));
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects_successful() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  int i = 0;
  for (auto& obj : objects_metadata) {
    if (clovis_writer->get_op_ret_code_for(i) == 0 ||
        clovis_writer->get_op_ret_code_for(i) == -ENOENT) {
      delete_objects_response.add_success(obj->get_object_name());
    } else {
      // TODO - ACL may also return AccessDenied
      delete_objects_response.add_failure(obj->get_object_name(), "InternalError");
      obj->mark_invalid();
    }
  }
  delete_objects_metadata();
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects_failed() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  s3_log(S3_LOG_ERROR, "Deletion of objects failed\n");
  send_response_to_s3_client();
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects_metadata() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  std::vector<std::string> keys;
  for (auto& obj : objects_metadata) {
    if (obj->get_state() != S3ObjectMetadataState::invalid) {
      keys.push_back(obj->get_object_name());
    }
  }

  clovis_kv_writer->delete_keyval(get_bucket_index_name(), keys, std::bind( &S3DeleteMultipleObjectsAction::delete_objects_metadata_successful, this), std::bind( &S3DeleteMultipleObjectsAction::delete_objects_metadata_failed, this));
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects_metadata_successful() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  if (delete_index < delete_request.get_count()) {
    fetch_objects_info();
  } else {
    send_response_to_s3_client();
  }
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}

void S3DeleteMultipleObjectsAction::delete_objects_metadata_failed() {
  s3_log(S3_LOG_DEBUG, "Entering\n");
  // TODO - handle metadata failure when object is deleted.
  send_response_to_s3_client();
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}


void S3DeleteMultipleObjectsAction::send_response_to_s3_client() {
  s3_log(S3_LOG_DEBUG, "Entering\n");

  if (is_request_content_corrupt) {
    S3Error error("BadDigest", request->get_request_id(), request->get_object_uri());
    std::string& response_xml = error.to_xml();
    request->set_out_header_value("Content-Type", "application/xml");
    request->set_out_header_value("Content-Length", std::to_string(response_xml.length()));
    request->send_response(error.get_http_status_code(), response_xml);
  } else if (is_request_too_large) {
    S3Error error("MaxMessageLengthExceeded", request->get_request_id(), request->get_object_uri());
    std::string& response_xml = error.to_xml();
    request->set_out_header_value("Content-Type", "application/xml");
    request->set_out_header_value("Content-Length", std::to_string(response_xml.length()));
    request->send_response(error.get_http_status_code(), response_xml);
  } else if (bucket_metadata->get_state() == S3BucketMetadataState::missing) {
    // Invalid Bucket Name
    S3Error error("NoSuchBucket", request->get_request_id(), request->get_object_uri());
    std::string& response_xml = error.to_xml();
    request->set_out_header_value("Content-Type", "application/xml");
    request->set_out_header_value("Content-Length", std::to_string(response_xml.length()));

    request->send_response(error.get_http_status_code(), response_xml);
  } else if (clovis_kv_reader->get_state() == S3ClovisKVSReaderOpState::failed ||
             clovis_writer->get_state()    == S3ClovisWriterOpState::failed ||
             clovis_kv_writer->get_state() == S3ClovisKVSWriterOpState::failed ) {
    S3Error error("InternalError", request->get_request_id(), request->get_object_uri());
    std::string& response_xml = error.to_xml();
    request->set_out_header_value("Content-Type", "application/xml");
    request->set_out_header_value("Content-Length", std::to_string(response_xml.length()));

    request->send_response(error.get_http_status_code(), response_xml);
  } else {
    std::string& response_xml = delete_objects_response.to_xml();

    request->set_out_header_value("Content-Length", std::to_string(response_xml.length()));
    request->set_out_header_value("Content-Type", "application/xml");
    s3_log(S3_LOG_DEBUG, "Object list response_xml = %s\n", response_xml.c_str());

    request->send_response(S3HttpSuccess200, response_xml);
  }
  request->resume();

  done();
  i_am_done();  // self delete
  s3_log(S3_LOG_DEBUG, "Exiting\n");
}