// Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include "yb/cdc/cdc_service.pb.h"
#include "yb/common/entity_ids_types.h"

namespace yb {
namespace cdc {

class CDCSDKUniqueRecordID {
 public:
  explicit CDCSDKUniqueRecordID(const std::shared_ptr<CDCSDKProtoRecordPB>& record);

  explicit CDCSDKUniqueRecordID(
      RowMessage_Op op, uint64_t commit_time, std::string& docdb_txn_id, uint64_t record_time,
      uint32_t write_id, std::string& table_id, std::string& primary_key);

  static bool CanFormUniqueRecordId(const std::shared_ptr<CDCSDKProtoRecordPB>& record);

  bool lessThan(const std::shared_ptr<CDCSDKUniqueRecordID>& curr_record_unique_id);

  uint64_t GetCommitTime() const;

  std::string ToString() const;

 private:
  RowMessage_Op op_;
  uint64_t commit_time_;
  std::string docdb_txn_id_;
  uint64_t record_time_;
  uint32_t write_id_;
  std::string table_id_;
  std::string primary_key_;
};

}  // namespace cdc
}  // namespace yb
