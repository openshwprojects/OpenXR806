/* keys.c - Bluetooth key handling */

/*
 * Copyright (c) 2015-2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <string.h>
#include <stdlib.h>
#include <ble/sys/atomic.h>
#include <ble/sys/util.h>

#include <settings/settings.h>

#include <bluetooth/bluetooth.h>
#include <bluetooth/conn.h>
#include <bluetooth/hci.h>

#ifdef BT_DBG_ENABLED //Wewisetek : modify for compiler error
#undef BT_DBG_ENABLED
#endif
#define BT_DBG_ENABLED IS_ENABLED(CONFIG_BT_DEBUG_KEYS)
#define LOG_MODULE_NAME bt_keys
#include "common/log.h"

#include "common/rpa.h"
#include "gatt_internal.h"
#include "hci_core.h"
#include "smp.h"
#include "settings.h"
#include "keys.h"

#if !defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
static struct bt_keys key_pool[CONFIG_BT_MAX_PAIRED];
#else
static struct bt_keys *key_pool;
#endif

#define BT_KEYS_STORAGE_LEN_COMPAT (BT_KEYS_STORAGE_LEN - sizeof(uint32_t))

#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
static uint32_t aging_counter_val;
static struct bt_keys *last_keys_updated;
#endif /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
void keys_dynamic_mem_alloc(void)
{
	key_pool = (struct bt_keys *)k_malloc(sizeof(struct bt_keys) * CONFIG_BT_MAX_PAIRED);
	if (key_pool == NULL) {
		BT_ERR("Alloc key pool error");
		return ;
	}
	memset(key_pool, 0, sizeof(struct bt_keys) * CONFIG_BT_MAX_PAIRED);
}

void keys_dynamic_mem_free(void)
{
	k_free(key_pool);
	key_pool = NULL;
}
#endif

#if XRADIO
void bt_keys_debug_all_key(void)
{
	int i;

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		struct bt_keys *keys = &key_pool[i];
		(void)keys;

		BT_DBG("KEY[%d] id(%d) state(0x%x) addr(%s) keys(0x%x) \n"
			   "        ltk: rand(%02x%02x) ediv(%02x%02x) val(%02x%02x)\n"
			   "        irk: val(%02x%02x) rpa(%s)",
			i, keys->id, keys->state, bt_addr_le_str(&keys->addr), keys->keys,
			keys->ltk.rand[0], keys->ltk.rand[1], keys->ltk.ediv[0], keys->ltk.ediv[1], keys->ltk.val[0], keys->ltk.val[0],
			keys->irk.val[0], keys->irk.val[0], bt_addr_str(&(keys->irk.rpa)));
	}
}
#endif

struct bt_keys *bt_keys_get_addr(uint8_t id, const bt_addr_le_t *addr)
{
	struct bt_keys *keys;
	int i;
#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	size_t first_free_slot = CONFIG_BT_MAX_PAIRED;
#else
	size_t first_free_slot = ARRAY_SIZE(key_pool);
#endif

	BT_DBG("%s", bt_addr_le_str(addr));

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		keys = &key_pool[i];

		if (keys->id == id && !bt_addr_le_cmp(&keys->addr, addr)) {
			return keys;
		}

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
		if (first_free_slot == CONFIG_BT_MAX_PAIRED &&
#else
		if (first_free_slot == ARRAY_SIZE(key_pool) &&
#endif
		    !bt_addr_le_cmp(&keys->addr, BT_ADDR_LE_ANY)) {
			first_free_slot = i;
		}
	}

#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	if (first_free_slot == CONFIG_BT_MAX_PAIRED) {
#else
	if (first_free_slot == ARRAY_SIZE(key_pool)) {
#endif
		struct bt_keys *oldest = &key_pool[0];
		bt_addr_le_t oldest_addr;

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
		for (i = 1; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
		for (i = 1; i < ARRAY_SIZE(key_pool); i++) {
#endif
			struct bt_keys *current = &key_pool[i];

			if (current->aging_counter < oldest->aging_counter) {
				oldest = current;
			}
		}

		/* Use a copy as bt_unpair will clear the oldest key. */
		bt_addr_le_copy(&oldest_addr, &oldest->addr);
		bt_unpair(oldest->id, &oldest_addr);
		if (!bt_addr_le_cmp(&oldest->addr, BT_ADDR_LE_ANY)) {
			first_free_slot = oldest - &key_pool[0];
		}
	}

#endif  /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	if (first_free_slot < CONFIG_BT_MAX_PAIRED) {
#else
	if (first_free_slot < ARRAY_SIZE(key_pool)) {
#endif
		keys = &key_pool[first_free_slot];
		keys->id = id;
		bt_addr_le_copy(&keys->addr, addr);
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
		keys->aging_counter = ++aging_counter_val;
		last_keys_updated = keys;
#endif  /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
		BT_DBG("created %p for %s", keys, bt_addr_le_str(addr));
		return keys;
	}

	BT_DBG("unable to create keys for %s", bt_addr_le_str(addr));

	return NULL;
}

void bt_foreach_bond(uint8_t id, void (*func)(const struct bt_bond_info *info,
					   void *user_data),
		     void *user_data)
{
	int i;

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		struct bt_keys *keys = &key_pool[i];

		if (keys->keys && keys->id == id) {
			struct bt_bond_info info;

			bt_addr_le_copy(&info.addr, &keys->addr);
			func(&info, user_data);
		}
	}
}

void bt_keys_foreach(int type, void (*func)(struct bt_keys *keys, void *data),
		     void *data)
{
	int i;

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		if ((key_pool[i].keys & type)) {
			func(&key_pool[i], data);
		}
	}
}

struct bt_keys *bt_keys_find(int type, uint8_t id, const bt_addr_le_t *addr)
{
	int i;

	BT_DBG("type %d %s", type, bt_addr_le_str(addr));

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		if ((key_pool[i].keys & type) && key_pool[i].id == id &&
		    !bt_addr_le_cmp(&key_pool[i].addr, addr)) {
			return &key_pool[i];
		}
	}

	return NULL;
}

struct bt_keys *bt_keys_get_type(int type, uint8_t id, const bt_addr_le_t *addr)
{
	struct bt_keys *keys;

	BT_DBG("type %d %s", type, bt_addr_le_str(addr));

	keys = bt_keys_find(type, id, addr);
	if (keys) {
		return keys;
	}

	keys = bt_keys_get_addr(id, addr);
	if (!keys) {
		return NULL;
	}

	bt_keys_add_type(keys, type);

	return keys;
}

struct bt_keys *bt_keys_find_irk(uint8_t id, const bt_addr_le_t *addr)
{
	int i;

	BT_DBG("%s", bt_addr_le_str(addr));

	if (!bt_addr_le_is_rpa(addr)) {
		return NULL;
	}

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		if (!(key_pool[i].keys & BT_KEYS_IRK)) {
			continue;
		}

		if (key_pool[i].id == id &&
		    !bt_addr_cmp(&addr->a, &key_pool[i].irk.rpa)) {
			BT_DBG("cached RPA %s for %s",
			       bt_addr_str(&key_pool[i].irk.rpa),
			       bt_addr_le_str(&key_pool[i].addr));
			return &key_pool[i];
		}
	}

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		if (!(key_pool[i].keys & BT_KEYS_IRK)) {
			continue;
		}

		if (key_pool[i].id != id) {
			continue;
		}

		if (bt_rpa_irk_matches(key_pool[i].irk.val, &addr->a)) {
			BT_DBG("RPA %s matches %s",
			       bt_addr_str(&key_pool[i].irk.rpa),
			       bt_addr_le_str(&key_pool[i].addr));

			bt_addr_copy(&key_pool[i].irk.rpa, &addr->a);

			return &key_pool[i];
		}
	}

	BT_DBG("No IRK for %s", bt_addr_le_str(addr));

	return NULL;
}

struct bt_keys *bt_keys_find_addr(uint8_t id, const bt_addr_le_t *addr)
{
	int i;

	BT_DBG("%s", bt_addr_le_str(addr));

#if defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	for (i = 0; i < CONFIG_BT_MAX_PAIRED; i++) {
#else
	for (i = 0; i < ARRAY_SIZE(key_pool); i++) {
#endif
		if (key_pool[i].id == id &&
		    !bt_addr_le_cmp(&key_pool[i].addr, addr)) {
			return &key_pool[i];
		}
	}

	return NULL;
}

void bt_keys_add_type(struct bt_keys *keys, int type)
{
	keys->keys |= type;
}

#ifdef CONFIG_FLASH_ARBIT
#define BT_KEYS_STORE_SAVE    (0)
#define BT_KEYS_STORE_DELETE  (1)
struct bt_keys_store_entry {
	sys_snode_t node;
	uint8_t opt;
	struct bt_keys keys;
};
struct key_store {
	struct k_thread thread;
	struct k_sem sem;
	struct k_mutex mutex;
	struct k_queue queue;
	bool init;
};
static struct key_store key_store = {
	.init = false,
};
static K_KERNEL_STACK_DEFINE(keys_store_thread_stack, CONFIG_BT_HCI_ECC_STACK_SIZE);
/**
  * @brief Thread for keys asynchronous store.
  *
  * @param *p1,*p1,*p1: unused thread pointer.
  *
  */
static void keys_store_thread(void *p1, void *p2, void *p3)
{
	struct bt_keys_store_entry *p_entry;
	char key[BT_SETTINGS_KEY_MAX];
	int res;

	while (1) {
		k_sem_take(&key_store.sem, K_FOREVER);
		while (1) {
			k_mutex_lock(&key_store.mutex, K_TICKS_FOREVER);
			p_entry = k_queue_get(&key_store.queue, 0);
			k_mutex_unlock(&key_store.mutex);
			if (p_entry == NULL) {
				break;
			}
			if (p_entry->keys.id) {
				char id[4];
				u8_to_dec(id, sizeof(id), p_entry->keys.id);
				bt_settings_encode_key(key, sizeof(key), "keys", &p_entry->keys.addr, id);
			} else {
				bt_settings_encode_key(key, sizeof(key), "keys", &p_entry->keys.addr, NULL);
			}
			if (p_entry->opt == BT_KEYS_STORE_SAVE) {
				res = settings_save_one(key, p_entry->keys.storage_start, BT_KEYS_STORAGE_LEN);
				if (res) {
					BT_ERR("Failed to save keys (err %d)", res);
				}
				BT_DBG("Stored keys for %s (%s)", bt_addr_le_str(&p_entry->keys->addr),
				       log_strdup(key));
			} else if (p_entry->opt == BT_KEYS_STORE_DELETE) {
				BT_DBG("Deleting key %s", log_strdup(key));
				res = settings_save_one(key, NULL, 0);
				if (res) {
					BT_ERR("Failed to delete keys (err %d)", res);
				}
			} else {
				BT_ERR("%s erro", __FUNCTION__);
			}
			k_free(p_entry);
		}
	}
}

/**
  * @brief Init resouce of keys store.
  *
  * @retval Status.
  */
static int key_store_init(void)
{
	if (k_mutex_init(&key_store.mutex) != OS_OK) {
		return -ENOENT;
	}
	if (k_sem_init(&key_store.sem, 0, 1) != OS_OK) {
		return -ENOENT;
	}
	k_queue_init(&key_store.queue);
	k_thread_create(&key_store.thread,
					keys_store_thread_stack,
					K_KERNEL_STACK_SIZEOF(keys_store_thread_stack),
					keys_store_thread,
					NULL, NULL, NULL,
					K_PRIO_PREEMPT(2), 0, K_NO_WAIT);
	return 0;
}
#endif /* CONFIG_FLASH_ARBIT */

#ifdef CONFIG_FLASH_ARBIT
/**
  * @brief Delete Keys Asynchronous.
  *
  * @param *keys : keys to be delete.
  */
void bt_keys_clear(struct bt_keys *keys)
{
#if IS_ENABLED(CONFIG_BT_SETTINGS)
	struct bt_keys_store_entry *p_entry;
#endif

	if (keys == NULL) {
		return ;
	}
	BT_DBG("%s (keys 0x%04x)", bt_addr_le_str(&keys->addr), keys->keys);
	if (keys->state & BT_KEYS_ID_ADDED) {
		bt_id_del(keys);
	}
#if IS_ENABLED(CONFIG_BT_SETTINGS)
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {

		if (key_store.init == false) {
			key_store.init = true;
			key_store_init();
		}
		k_mutex_lock(&key_store.mutex, K_TICKS_FOREVER);
		p_entry = (struct bt_keys_store_entry *)malloc(sizeof(struct bt_keys_store_entry));
		if (p_entry == NULL) {
			BT_ERR("%s malloc fail\n", __FUNCTION__);
			k_mutex_unlock(&key_store.mutex);
			return ;
		}
		p_entry->opt = BT_KEYS_STORE_DELETE;
		memcpy(&p_entry->keys, keys, sizeof(struct bt_keys));
		k_queue_append(&key_store.queue, p_entry);
		k_mutex_unlock(&key_store.mutex);
		k_sem_give(&key_store.sem);
	}
#endif

	(void)memset(keys, 0, sizeof(*keys));
}
#else
void bt_keys_clear(struct bt_keys *keys)
{
	BT_DBG("%s (keys 0x%04x)", bt_addr_le_str(&keys->addr), keys->keys);

	if (keys->state & BT_KEYS_ID_ADDED) {
		bt_id_del(keys);
	}

#if IS_ENABLED(CONFIG_BT_SETTINGS)
	if (IS_ENABLED(CONFIG_BT_SETTINGS)) {
		char key[BT_SETTINGS_KEY_MAX];

		/* Delete stored keys from flash */
		if (keys->id) {
			char id[4];

			u8_to_dec(id, sizeof(id), keys->id);
			bt_settings_encode_key(key, sizeof(key), "keys",
					       &keys->addr, id);
		} else {
			bt_settings_encode_key(key, sizeof(key), "keys",
					       &keys->addr, NULL);
		}

		BT_DBG("Deleting key %s", log_strdup(key));
		settings_delete(key);
	}
#endif

	(void)memset(keys, 0, sizeof(*keys));
}
#endif /* CONFIG_FLASH_ARBIT */

#if defined(CONFIG_BT_SETTINGS)
#ifdef CONFIG_FLASH_ARBIT
/**
  * @brief Save Keys Asynchronous.
  *
  * @param *keys : keys to be save.
  */
int bt_keys_store(struct bt_keys *keys)
{
	struct bt_keys_store_entry *p_entry;

	if (keys == NULL) {
		return -ENOENT;
	}
	if (key_store.init == false) {
		if (key_store_init() != 0) {
			return -ENOENT;
		}
		key_store.init = true;
	}
	k_mutex_lock(&key_store.mutex, K_TICKS_FOREVER);
	p_entry = (struct bt_keys_store_entry *)malloc(sizeof(struct bt_keys_store_entry));
	if (p_entry == NULL) {
		BT_ERR("%s malloc fail\n", __FUNCTION__);
		k_mutex_unlock(&key_store.mutex);
		return -ENOENT;
	}
	p_entry->opt = BT_KEYS_STORE_SAVE;
	memcpy(&p_entry->keys, keys, sizeof(struct bt_keys));
	k_queue_append(&key_store.queue, p_entry);
	k_mutex_unlock(&key_store.mutex);
	k_sem_give(&key_store.sem);
	return 0;
}

#else

int bt_keys_store(struct bt_keys *keys)
{
	char key[BT_SETTINGS_KEY_MAX];
	int err;

	if (keys->id) {
		char id[4];

		u8_to_dec(id, sizeof(id), keys->id);
		bt_settings_encode_key(key, sizeof(key), "keys", &keys->addr,
				       id);
	} else {
		bt_settings_encode_key(key, sizeof(key), "keys", &keys->addr,
				       NULL);
	}

	err = settings_save_one(key, keys->storage_start, BT_KEYS_STORAGE_LEN);
	if (err) {
		BT_ERR("Failed to save keys (err %d)", err);
		return err;
	}

	BT_DBG("Stored keys for %s (%s)", bt_addr_le_str(&keys->addr),
	       log_strdup(key));

	return 0;
}
#endif /* CONFIG_FLASH_ARBIT */

static int keys_set(const char *name, size_t len_rd, settings_read_cb read_cb,
		    void *cb_arg)
{
	struct bt_keys *keys;
	bt_addr_le_t addr;
	uint8_t id;
	ssize_t len;
	int err;
	char val[BT_KEYS_STORAGE_LEN];
	const char *next;

	if (!name) {
		BT_ERR("Insufficient number of arguments");
		return -EINVAL;
	}

	len = read_cb(cb_arg, val, sizeof(val));
	if (len < 0) {
		BT_ERR("Failed to read value (err %zd)", len);
		return -EINVAL;
	}

	BT_DBG("name %s val %s", log_strdup(name),
	       (len) ? bt_hex(val, sizeof(val)) : "(null)");

	err = bt_settings_decode_key(name, &addr);
	if (err) {
		BT_ERR("Unable to decode address %s", name);
		return -EINVAL;
	}

	settings_name_next(name, &next);

	if (!next) {
		id = BT_ID_DEFAULT;
	} else {
		id = strtol(next, NULL, 10);
	}

	if (!len) {
		keys = bt_keys_find(BT_KEYS_ALL, id, &addr);
		if (keys) {
			(void)memset(keys, 0, sizeof(*keys));
			BT_DBG("Cleared keys for %s", bt_addr_le_str(&addr));
		} else {
			BT_WARN("Unable to find deleted keys for %s",
				bt_addr_le_str(&addr));
		}

		return 0;
	}

	keys = bt_keys_get_addr(id, &addr);
	if (!keys) {
		BT_ERR("Failed to allocate keys for %s", bt_addr_le_str(&addr));
		return -ENOMEM;
	}
	if (len != BT_KEYS_STORAGE_LEN) {
		do {
			/* Load shorter structure for compatibility with old
			 * records format with no counter.
			 */
			if (IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST) &&
			    len == BT_KEYS_STORAGE_LEN_COMPAT) {
				BT_WARN("Keys for %s have no aging counter",
					bt_addr_le_str(&addr));
				memcpy(keys->storage_start, val, len);
				continue;
			}

			BT_ERR("Invalid key length %zd != %u", len,
			       BT_KEYS_STORAGE_LEN);
			bt_keys_clear(keys);

			return -EINVAL;
		} while (0);
	} else {
		memcpy(keys->storage_start, val, len);
	}

	BT_DBG("Successfully restored keys for %s", bt_addr_le_str(&addr));
#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
	if (aging_counter_val < keys->aging_counter) {
		aging_counter_val = keys->aging_counter;
	}
#endif  /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */
	return 0;
}

static void id_add(struct bt_keys *keys, void *user_data)
{
	bt_id_add(keys);
}

static int keys_commit(void)
{
	BT_DBG("");

	/* We do this in commit() rather than add() since add() may get
	 * called multiple times for the same address, especially if
	 * the keys were already removed.
	 */
	if (IS_ENABLED(CONFIG_BT_CENTRAL) && IS_ENABLED(CONFIG_BT_PRIVACY)) {
		bt_keys_foreach(BT_KEYS_ALL, id_add, NULL);
	} else {
		bt_keys_foreach(BT_KEYS_IRK, id_add, NULL);
	}

	return 0;
}

#if defined(CONFIG_BLEHOST_Z_ITERABLE_SECTION)
SETTINGS_STATIC_HANDLER_DEFINE(bt_keys, "bt/keys", NULL, keys_set, keys_commit,
			       NULL);
#elif defined(CONFIG_SETTINGS_DYNAMIC_HANDLERS)
struct settings_handler bt_keys_handler = {
		.name = "bt/keys",
		.h_get = NULL,
		.h_set = keys_set,
		.h_commit = keys_commit,
		.h_export = NULL
};
#endif
#endif /* CONFIG_BT_SETTINGS */

#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
void bt_keys_update_usage(uint8_t id, const bt_addr_le_t *addr)
{
	struct bt_keys *keys = bt_keys_find_addr(id, addr);

	if (!keys) {
		return;
	}

	if (last_keys_updated == keys) {
		return;
	}

	keys->aging_counter = ++aging_counter_val;
	last_keys_updated = keys;

	BT_DBG("Aging counter for %s is set to %u", bt_addr_le_str(addr),
	       keys->aging_counter);

	if (IS_ENABLED(CONFIG_BT_KEYS_SAVE_AGING_COUNTER_ON_PAIRING)) {
		bt_keys_store(keys);
	}
}

#endif  /* CONFIG_BT_KEYS_OVERWRITE_OLDEST */

#if defined(CONFIG_BT_DEINIT)
void bt_keys_mem_deinit(void)
{
#if !defined(CONFIG_BT_VAR_MEM_DYNC_ALLOC)
	memset(&key_pool, 0, sizeof(struct bt_keys) * ARRAY_SIZE(key_pool));
#endif

#if IS_ENABLED(CONFIG_BT_KEYS_OVERWRITE_OLDEST)
	aging_counter_val = 0;
	last_keys_updated = NULL;
#endif
}
#endif
