#ifndef CKB_C_STDLIB_CKB_TYPE_ID_H_
#define CKB_C_STDLIB_CKB_TYPE_ID_H_

#include <string.h>

#include "blake2b.h"
#include "blockchain.h"
#include "ckb_syscalls.h"

// Given an offset into args of current script, this function validates if
// current transaction confronts to the type ID rules. A 32-byte data starting
// from offset in current runnning script args, will be used as the actual type
// ID.
int ckb_validate_type_id(size_t offset);

#ifndef CKB_TYPE_ID_DECLARATION_ONLY

#ifdef CKB_TYPE_ID_DEBUG
#define DEBUG(s) ckb_debug(s)
#else
#define DEBUG(s)
#endif /* CKB_TYPE_ID_DEBUG */

int _ckb_load_type_id(size_t offset, uint8_t* type_id) {
  // TODO: it is possible to simply load the script header, then only load the
  // 32 byte requested data based on offsets.
  uint8_t current_script[32768];
  uint64_t len = 32768;
  int ret = ckb_load_script(current_script, &len, 0);
  if (ret != CKB_SUCCESS) {
    DEBUG("Cannot load current script!");
    return CKB_USER_ERROR;
  }
  if (len > 32768) {
    DEBUG("Current script is too large!");
    return CKB_USER_ERROR;
  }

  mol_seg_t script_seg;
  script_seg.ptr = (uint8_t*)current_script;
  script_seg.size = len;
  if (MolReader_Script_verify(&script_seg, false) != MOL_OK) {
    DEBUG("Corrupted data!");
    return CKB_USER_ERROR;
  }

  mol_seg_t args_seg = MolReader_Script_get_args(&script_seg);
  mol_seg_t args_bytes_seg = MolReader_Bytes_raw_bytes(&args_seg);
  if (offset + 32 > args_bytes_seg.size) {
    DEBUG("Length of type id is incorrect!");
    return CKB_USER_ERROR;
  }
  memcpy(type_id, &args_bytes_seg.ptr[offset], 32);
  return CKB_SUCCESS;
}

// 1 for true, 0 for false
int _ckb_has_type_id_cell(size_t index, int is_input) {
  uint64_t len = 0;
  size_t source =
      is_input == 1 ? CKB_SOURCE_GROUP_INPUT : CKB_SOURCE_GROUP_OUTPUT;
  int ret = ckb_load_cell(NULL, &len, 0, index, source);
  return ret == CKB_SUCCESS ? 1 : 0;
}

int _ckb_locate_first_type_id_output_index(uint64_t* index) {
  uint8_t current_script_hash[32];
  uint64_t len = 32;
  int ret = ckb_load_script_hash(current_script_hash, &len, 0);
  if (ret != CKB_SUCCESS) {
    DEBUG("Error loading current script hash!");
    return ret;
  }
  if (len != 32) {
    DEBUG("Invalid script hash length!");
    return CKB_USER_ERROR;
  }

  uint64_t i = 0;
  while (1) {
    uint8_t buffer[32];
    len = 32;
    ret = ckb_load_cell_by_field(buffer, &len, 0, i, CKB_SOURCE_OUTPUT,
                                 CKB_CELL_FIELD_TYPE_HASH);
    if (ret != CKB_SUCCESS) {
      DEBUG("Error fetching output type hash to locate type id index!");
      return ret;
    }
    if (len != 32) {
      DEBUG("Invalid type hash length!");
      return CKB_USER_ERROR;
    }
    if (memcmp(buffer, current_script_hash, 32) == 0) {
      break;
    }
    i += 1;
  }
  *index = i;
  return CKB_SUCCESS;
}

int ckb_validate_type_id(size_t offset) {
  int has_second_type_id_input = _ckb_has_type_id_cell(1, 1);
  int has_second_type_id_output = _ckb_has_type_id_cell(1, 0);
  if (has_second_type_id_input || has_second_type_id_output) {
    DEBUG("There can only be at most one input and one output type ID cell!");
    return CKB_USER_ERROR;
  }

  int has_first_type_id_input = _ckb_has_type_id_cell(0, 1);
  if (!has_first_type_id_input) {
    // We are creating a new type ID cell here. Additional checkings are needed
    // to ensure the type ID is legit.
    uint8_t current_type_id[32];
    int ret = _ckb_load_type_id(offset, current_type_id);
    if (ret != CKB_SUCCESS) {
      return ret;
    }
    uint64_t index = 0xFFFFFFFFFFFFFFFF;
    ret = _ckb_locate_first_type_id_output_index(&index);
    if (ret != CKB_SUCCESS) {
      return ret;
    }
    // The type ID is calculated as the blake2b (with CKB's personalization) of
    // the first CellInput in current transaction, and the created output cell
    // index(in 64-bit little endian unsigned integer).
    uint8_t buffer[128];
    uint64_t len = 128;
    ret = ckb_checked_load_input(buffer, &len, 0, 0, CKB_SOURCE_INPUT);
    if (ret != CKB_SUCCESS) {
      DEBUG("Cannot load the first input of the transaction!");
      return ret;
    }
    blake2b_state blake2b_ctx;
    blake2b_init(&blake2b_ctx, 32);
    blake2b_update(&blake2b_ctx, buffer, len);
    blake2b_update(&blake2b_ctx, (uint8_t*)(&index), 8);
    uint8_t expected_type_id[32];
    blake2b_final(&blake2b_ctx, expected_type_id, 32);
    if (memcmp(expected_type_id, current_type_id, 32) != 0) {
      DEBUG("Invalid type ID!");
      return CKB_USER_ERROR;
    }
  }
  return CKB_SUCCESS;
}

#endif /* CKB_TYPE_ID_DECLARATION_ONLY */

#endif /* CKB_C_STDLIB_CKB_TYPE_ID_H_ */
