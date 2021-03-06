/*
 * Copyright (c) 2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 */

#pragma once

#ifndef __MOTR_PUT_KEY_VALUE_ACTION_H__
#define __MOTR_PUT_KEY_VALUE_ACTION_H__

#include <gtest/gtest_prod.h>
#include <memory>

#include "s3_factory.h"
#include "motr_action_base.h"

class MotrPutKeyValueAction : public MotrAction {
  m0_uint128 index_id;
  std::string json_value;
  std::shared_ptr<MotrAPI> s3_motr_api;
  std::shared_ptr<S3MotrKVSWriter> motr_kv_writer;
  std::shared_ptr<S3MotrKVSWriterFactory> motr_kvs_writer_factory_ptr;

  bool is_valid_json(std::string);

 public:
  MotrPutKeyValueAction(
      std::shared_ptr<MotrRequestObject> req,
      std::shared_ptr<MotrAPI> motr_api = nullptr,
      std::shared_ptr<S3MotrKVSWriterFactory> motr_kvs_writer_factory =
          nullptr);

  void setup_steps();
  void read_and_validate_key_value();
  void put_key_value();
  void put_key_value_successful();
  void put_key_value_failed();
  void consume_incoming_content();
  void send_response_to_s3_client();

  FRIEND_TEST(MotrPutKeyValueActionTest, ValidateKeyValueValidIndexValidValue);
  FRIEND_TEST(MotrPutKeyValueActionTest, PutKeyValueSuccessful);
  FRIEND_TEST(MotrPutKeyValueActionTest, PutKeyValue);
  FRIEND_TEST(MotrPutKeyValueActionTest, PutKeyValueFailed);
  FRIEND_TEST(MotrPutKeyValueActionTest, ValidJson);
  FRIEND_TEST(MotrPutKeyValueActionTest, InvalidJson);
};
#endif
