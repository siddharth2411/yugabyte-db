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

#include "yb/cdc/cdcsdk_unique_record_id.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"

namespace yb {

namespace cdc {

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(
    RowMessage_Op op, uint64_t commit_time, std::string& docdb_txn_id, uint64_t record_time,
    uint32_t write_id, std::string& table_id, std::string& primary_key)
    : op_(op),
      commit_time_(commit_time),
      docdb_txn_id_(docdb_txn_id),
      record_time_(record_time),
      write_id_(write_id),
      table_id_(table_id),
      primary_key_(primary_key) {}

CDCSDKUniqueRecordID::CDCSDKUniqueRecordID(const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  this->op_ = record->row_message().op();
  this->commit_time_ = record->row_message().commit_time();
  switch (this->op_) {
    case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
    case RowMessage_Op_BEGIN:
      this->docdb_txn_id_ = "";
      this->record_time_ = 0;
      this->write_id_ = 0;
      this->table_id_ = "";
      this->primary_key_ = "";
      break;
    case RowMessage_Op_SAFEPOINT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_COMMIT:
      this->docdb_txn_id_ = "";
      this->record_time_ = std::numeric_limits<uint64_t>::max();
      this->write_id_ = std::numeric_limits<uint32_t>::max();
      this->table_id_ = "";
      this->primary_key_ = "";
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      if (record->row_message().has_transaction_id()) {
        this->docdb_txn_id_ = record->row_message().transaction_id();
      } else {
        this->docdb_txn_id_ = "";
      }
      this->record_time_ = record->row_message().record_time();
      this->write_id_ = record->cdc_sdk_op_id().write_id();
      this->table_id_ = record->row_message().table_id();
      this->primary_key_ = record->row_message().primary_key();
      break;
    case RowMessage_Op_UNKNOWN: FALLTHROUGH_INTENDED;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ:
      // This should never happen as we only invoke this constructor after ensuring that the value
      // is not one of these.
      LOG(FATAL) << "Unexpected record received: " << record->DebugString();
  }
}

uint64_t CDCSDKUniqueRecordID::GetCommitTime() const { return commit_time_; }

bool CDCSDKUniqueRecordID::CanFormUniqueRecordId(
    const std::shared_ptr<CDCSDKProtoRecordPB>& record) {
  RowMessage_Op op = record->row_message().op();
  switch (op) {
    case RowMessage_Op_DDL: FALLTHROUGH_INTENDED;
    case RowMessage_Op_BEGIN: FALLTHROUGH_INTENDED;
    case RowMessage_Op_COMMIT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_SAFEPOINT:
      return record->row_message().has_commit_time();
      break;
    case RowMessage_Op_INSERT: FALLTHROUGH_INTENDED;
    case RowMessage_Op_DELETE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UPDATE:
      return (
          record->row_message().has_commit_time() && record->row_message().has_record_time() &&
          record->cdc_sdk_op_id().has_write_id() && record->row_message().has_table_id() &&
          record->row_message().has_primary_key());
      break;
    case RowMessage_Op_TRUNCATE: FALLTHROUGH_INTENDED;
    case RowMessage_Op_READ: FALLTHROUGH_INTENDED;
    case RowMessage_Op_UNKNOWN:
      return false;
      break;
  }

  return false;
}

bool IsSafepointOp(const RowMessage_Op op) {
  return op == RowMessage_Op_SAFEPOINT;
}

bool IsCommitOp(const RowMessage_Op op) {
  return op == RowMessage_Op_COMMIT;
}

bool IsBeginOrCommitOp(const RowMessage_Op op) {
  return op == RowMessage_Op_BEGIN || op == RowMessage_Op_COMMIT;
}

bool CDCSDKUniqueRecordID::lessThan(
    const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id) {
  if (this->commit_time_ != curr_unique_record_id->commit_time_) {
    return this->commit_time_ < curr_unique_record_id->commit_time_;
  }

  // Safepoint record should always get the lowest priority in PQ.
  if (IsSafepointOp(this->op_) || IsSafepointOp(curr_unique_record_id->op_)) {
    return !(IsSafepointOp(this->op_));
  }

  if (this->docdb_txn_id_ != curr_unique_record_id->docdb_txn_id_) {
    return this->docdb_txn_id_ < curr_unique_record_id->docdb_txn_id_;
  }

  if (this->record_time_ != curr_unique_record_id->record_time_) {
    return this->record_time_ < curr_unique_record_id->record_time_;
  }

  if (this->write_id_ != curr_unique_record_id->write_id_) {
    return this->write_id_ < curr_unique_record_id->write_id_;
  }

  if (this->table_id_ != curr_unique_record_id->table_id_) {
    return this->table_id_ < curr_unique_record_id->table_id_;
  }

  return this->primary_key_ < curr_unique_record_id->primary_key_;
}

// Return true iff, curr_unique_record_id > last_seen_unique_record_id
bool CDCSDKUniqueRecordID::CanGenerateLSN(
    const std::shared_ptr<CDCSDKUniqueRecordID>& last_seen_unique_record_id,
    const std::shared_ptr<CDCSDKUniqueRecordID>& curr_unique_record_id) {
  if (last_seen_unique_record_id->commit_time_ != curr_unique_record_id->commit_time_) {
    return last_seen_unique_record_id->commit_time_ < curr_unique_record_id->commit_time_;
  }

  // Skip comparing docdb_txn_id if the current record is a BEGIN/COMMIT record since we want to
  // ship all txns with same commit_time in a single txn from VWAL.
  if (!IsBeginOrCommitOp(last_seen_unique_record_id->op_) &&
      !IsBeginOrCommitOp(curr_unique_record_id->op_)) {
    if (last_seen_unique_record_id->docdb_txn_id_ != curr_unique_record_id->docdb_txn_id_) {
      return last_seen_unique_record_id->docdb_txn_id_ < curr_unique_record_id->docdb_txn_id_;
    }
  }

  if (last_seen_unique_record_id->record_time_ != curr_unique_record_id->record_time_) {
    return last_seen_unique_record_id->record_time_ < curr_unique_record_id->record_time_;
  }

  if (last_seen_unique_record_id->write_id_ != curr_unique_record_id->write_id_) {
    return last_seen_unique_record_id->write_id_ < curr_unique_record_id->write_id_;
  }

  if (last_seen_unique_record_id->table_id_ != curr_unique_record_id->table_id_) {
    return last_seen_unique_record_id->table_id_ < curr_unique_record_id->table_id_;
  }

  return last_seen_unique_record_id->primary_key_ < curr_unique_record_id->primary_key_;
}

std::string CDCSDKUniqueRecordID::ToString() const {
  std::string result = "";
  switch (op_) {
    case RowMessage_Op_DDL:
      result = Format("op: DDL");
      break;
    case RowMessage_Op_BEGIN:
      result = Format("op: BEGIN");
      break;
    case RowMessage_Op_COMMIT:
      result = Format("op: COMMIT");
      break;
    case RowMessage_Op_SAFEPOINT:
      result = Format("op: SAFEPOINT");
      break;
    case RowMessage_Op_INSERT:
      result = Format("op: INSERT");
      break;
    case RowMessage_Op_DELETE:
      result = Format("op: DELETE");
      break;
    case RowMessage_Op_UPDATE:
      result = Format("op: UPDATE");
      break;
    case RowMessage_Op_TRUNCATE:
      result = Format("op: TRUNCATE");
      break;
    case RowMessage_Op_READ:
      result = Format("op: READ");
      break;
    case RowMessage_Op_UNKNOWN:
      result = Format("op: UNKNOWN");
      break;
  }

  result += Format(", commit_time: $0", commit_time_);
  result += Format(", docdb_txn_id: $0", docdb_txn_id_);
  result += Format(", record_time: $0", record_time_);
  result += Format(", write_id: $0", write_id_);
  result += Format(", table_id: $0", table_id_);
  result += Format(", encoded_primary_key: $0", primary_key_);
  return result;
}

}  // namespace cdc
}  // namespace yb
