/*
 *
 *  BlueZ - Bluetooth protocol stack for Linux
 *
 *  Copyright (C) 2014  Instituto Nokia de Tecnologia - INdT
 *
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>

#include "log.h"
#include "lib/uuid.h"
#include "attrib/att.h"
#include "src/shared/util.h"

#include "gatt-dbus.h"
#include "gatt.h"

/* Common GATT UUIDs */
static const bt_uuid_t primary_uuid  = { .type = BT_UUID16,
					.value.u16 = GATT_PRIM_SVC_UUID };

static const bt_uuid_t chr_uuid = { .type = BT_UUID16,
					.value.u16 = GATT_CHARAC_UUID };

struct btd_attribute {
	uint16_t handle;
	bt_uuid_t type;
	uint16_t value_len;
	uint8_t value[0];
};

static GList *local_attribute_db;
static uint16_t next_handle = 0x0001;

static inline void put_uuid_le(const bt_uuid_t *src, void *dst)
{
	if (src->type == BT_UUID16)
		put_le16(src->value.u16, dst);
	else if (src->type == BT_UUID32)
		put_le32(src->value.u32, dst);
	else
		/* Convert from 128-bit BE to LE */
		bswap_128(&src->value.u128, dst);
}

/*
 * Helper function to create new attributes containing constant/static values.
 * eg: declaration of services/characteristics, and characteristics with
 * fixed values.
 */
static struct btd_attribute *new_const_attribute(const bt_uuid_t *type,
							const uint8_t *value,
							uint16_t len)
{
	struct btd_attribute *attr;

	attr = malloc0(sizeof(struct btd_attribute) + len);
	if (attr == NULL)
		return NULL;

	attr->type = *type;
	memcpy(&attr->value, value, len);
	attr->value_len = len;

	return attr;
}

static int local_database_add(uint16_t handle, struct btd_attribute *attr)
{
	attr->handle = handle;

	local_attribute_db = g_list_append(local_attribute_db, attr);

	return 0;
}

struct btd_attribute *btd_gatt_add_service(const bt_uuid_t *uuid)
{
	struct btd_attribute *attr;
	uint16_t len = bt_uuid_len(uuid);
	uint8_t value[len];

	/*
	 * Service DECLARATION
	 *
	 *   TYPE         ATTRIBUTE VALUE
	 * +-------+---------------------------------+
	 * |0x2800 | 0xYYYY...                       |
	 * | (1)   | (2)                             |
	 * +------+----------------------------------+
	 * (1) - 2 octets: Primary/Secondary Service UUID
	 * (2) - 2 or 16 octets: Service UUID
	 */

	/* Set attribute value */
	put_uuid_le(uuid, value);

	attr = new_const_attribute(&primary_uuid, value, len);
	if (attr == NULL)
		return NULL;

	if (local_database_add(next_handle, attr) < 0) {
		free(attr);
		return NULL;
	}

	/* TODO: missing overflow checking */
	next_handle = next_handle + 1;

	return attr;
}

struct btd_attribute *btd_gatt_add_char(const bt_uuid_t *uuid,
							uint8_t properties)
{
	struct btd_attribute *char_decl, *char_value = NULL;

	/* Attribute value length */
	uint16_t len = 1 + 2 + bt_uuid_len(uuid);
	uint8_t value[len];

	/*
	 * Characteristic DECLARATION
	 *
	 *   TYPE         ATTRIBUTE VALUE
	 * +-------+---------------------------------+
	 * |0x2803 | 0xXX 0xYYYY 0xZZZZ...           |
	 * | (1)   |  (2)   (3)   (4)                |
	 * +------+----------------------------------+
	 * (1) - 2 octets: Characteristic declaration UUID
	 * (2) - 1 octet : Properties
	 * (3) - 2 octets: Handle of the characteristic Value
	 * (4) - 2 or 16 octets: Characteristic UUID
	 */

	value[0] = properties;

	/*
	 * Since we don't know yet the characteristic value attribute
	 * handle, we skip and set it later.
	 */

	put_uuid_le(uuid, &value[3]);

	char_decl = new_const_attribute(&chr_uuid, value, len);
	if (char_decl == NULL)
		goto fail;

	char_value = new0(struct btd_attribute, 1);
	if (char_value == NULL)
		goto fail;

	if (local_database_add(next_handle, char_decl) < 0)
		goto fail;

	next_handle = next_handle + 1;

	/*
	 * Characteristic VALUE
	 *
	 *   TYPE         ATTRIBUTE VALUE
	 * +----------+---------------------------------+
	 * |0xZZZZ... | 0x...                           |
	 * |  (1)     |  (2)                            |
	 * +----------+---------------------------------+
	 * (1) - 2 or 16 octets: Characteristic UUID
	 * (2) - N octets: Value is read dynamically from the service
	 * implementation (external entity).
	 */

	char_value->type = *uuid;

	/* TODO: Read & Write callbacks */

	if (local_database_add(next_handle, char_value) < 0)
		/* TODO: remove declaration */
		goto fail;

	next_handle = next_handle + 1;

	/*
	 * Update characteristic value handle in characteristic declaration
	 * attribute. For local attributes, we can assume that the handle
	 * representing the characteristic value will get the next available
	 * handle. However, for remote attribute this assumption is not valid.
	 */
	put_le16(char_value->handle, &char_decl->value[1]);

	return char_value;

fail:
	free(char_decl);
	free(char_value);

	return NULL;
}

void gatt_init(void)
{
	DBG("Starting GATT server");

	gatt_dbus_manager_register();
}

void gatt_cleanup(void)
{
	DBG("Stopping GATT server");

	gatt_dbus_manager_unregister();
}
