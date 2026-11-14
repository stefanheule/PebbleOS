/* SPDX-FileCopyrightText: 2026 Core Devices LLC */
/* SPDX-License-Identifier: Apache-2.0 */

#pragma once

#include "api.h"

/**
 * @defgroup services_blob_db_settings_blob_db Settings database
 * @ingroup services_blob_db
 * @brief System and notification settings synced both ways (::BlobDBIdSettings).
 *
 * Exposes the shell preferences and notification preferences settings files through BlobDB so
 * the phone can reuse its BlobDB sync. Only a whitelist of settings, kept in
 * @c settings_blob_db.c, is accessible and synced.
 * @{
 */

/**
 * @brief Initialize the settings database.
 *
 * Registers a settings file change callback that syncs whitelisted settings to the phone.
 */
void settings_blob_db_init(void);

/**
 * @brief Insert or replace a record in the settings database.
 *
 * The key is the setting name, with or without a trailing NUL. Non-whitelisted settings are
 * rejected with @c E_INVALID_OPERATION. The record is stored as synced and the in-memory
 * preferences are updated.
 *
 * @param key Key data.
 * @param key_len Length of @p key in bytes.
 * @param val Value data.
 * @param val_len Length of @p val in bytes.
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_insert(const uint8_t *key, int key_len, const uint8_t *val, int val_len);

/**
 * @brief Get the length of a record in the settings database.
 *
 * The key is the setting name, with or without a trailing NUL.
 *
 * @param key Key data.
 * @param key_len Length of @p key in bytes.
 * @return Length of the value in bytes, 0 if not found, or a negative error code.
 */
int settings_blob_db_get_len(const uint8_t *key, int key_len);

/**
 * @brief Read a record from the settings database.
 *
 * The key is the setting name, with or without a trailing NUL.
 *
 * @param key Key data.
 * @param key_len Length of @p key in bytes.
 * @param[out] val_out Buffer for the value.
 * @param val_len Size of @p val_out in bytes.
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_read(const uint8_t *key, int key_len, uint8_t *val_out, int val_len);

/**
 * @brief Delete a record from the settings database.
 *
 * The key is the setting name, with or without a trailing NUL. Only whitelisted settings can
 * be deleted.
 *
 * @param key Key data.
 * @param key_len Length of @p key in bytes.
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_delete(const uint8_t *key, int key_len);

/**
 * @brief Build the list of records of the settings database not yet synced to the phone.
 *
 * Only whitelisted settings are listed.
 *
 * @return Heap-allocated list (free with blob_db_util_free_dirty_list()), NULL if none.
 */
BlobDBDirtyItem *settings_blob_db_get_dirty_list(void);

/**
 * @brief Mark a record of the settings database as synced.
 *
 * The key is the setting name, with or without a trailing NUL.
 *
 * @param key Key data.
 * @param key_len Length of @p key in bytes.
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_mark_synced(const uint8_t *key, int key_len);

/**
 * @brief Check whether the settings database holds records not yet synced to the phone.
 *
 * Only whitelisted settings are considered.
 *
 * @param[out] is_dirty_out Set to true if at least one record is dirty.
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_is_dirty(bool *is_dirty_out);

/**
 * @brief Delete all records of the settings database.
 *
 * No-op: settings file writes are already persistent, so records are kept.
 *
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_flush(void);

/**
 * @brief Mark all whitelisted settings as dirty.
 *
 * Triggers a full sync of the settings to the phone.
 *
 * @return @c S_SUCCESS on success, an error code otherwise.
 */
status_t settings_blob_db_mark_all_dirty(void);

/**
 * @brief Insert or update a setting unless the watch copy is newer.
 *
 * @param key Setting name.
 * @param key_len Length of @p key in bytes.
 * @param val Value data.
 * @param val_len Length of @p val in bytes.
 * @param timestamp Modification time of the incoming value.
 * @retval S_SUCCESS Inserted.
 * @retval E_INVALID_OPERATION Watch copy is newer, or the setting is not whitelisted.
 * @return Other error codes on failure.
 */
status_t settings_blob_db_insert_with_timestamp(const uint8_t *key, int key_len, const uint8_t *val,
                                                int val_len, time_t timestamp);

/**
 * @brief Check whether the connected phone supports settings sync.
 *
 * @return true if the phone advertises the @c settings_sync_support capability.
 */
bool settings_blob_db_phone_supports_sync(void);

/** @brief Largest synced setting value, in bytes. */
#define SETTINGS_BLOB_DB_VALUE_MAX 64

/**
 * @brief Number of synced settings: both whitelists, the shell prefs first.
 *
 * @return Number of names settings_blob_db_synced_name() returns.
 */
size_t settings_blob_db_num_synced(void);

/**
 * @brief Name of a synced setting.
 *
 * @param index Index, below settings_blob_db_num_synced().
 * @return Name, or NULL if @p index is out of range.
 */
const char *settings_blob_db_synced_name(size_t index);

/**
 * @brief Copy the current (in-memory) value of a synced setting.
 *
 * @param name Setting name, without a trailing null.
 * @param name_len Length of @p name.
 * @param[out] val_out Value, truncated to @p val_max bytes.
 * @param val_max Size of @p val_out.
 * @return The value's size as stored, or 0 if @p name is not a synced setting this watch keeps.
 */
int settings_blob_db_get_current(const uint8_t *name, size_t name_len, void *val_out,
                                 size_t val_max);

/**
 * @brief Change a synced setting as if it was changed on the watch.
 *
 * Updates the in-memory value as a phone write does, but leaves the record dirty so it syncs to
 * the phone. Nothing is written if the value is unchanged.
 *
 * @param name Setting name, without a trailing null.
 * @param name_len Length of @p name.
 * @param val Value.
 * @param val_len Length of @p val; must be the setting's stored size.
 * @return true if the setting was written.
 */
bool settings_blob_db_set_local(const uint8_t *name, size_t name_len, const uint8_t *val,
                                size_t val_len);

/** @} */
