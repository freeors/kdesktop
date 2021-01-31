/* $Id: title_screen.cpp 48740 2011-03-05 10:01:34Z mordante $ */
/*
   Copyright (C) 2008 - 2011 by Mark de Wever <koraq@xs4all.nl>


   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY.

   See the COPYING file for more details.
*/

#define GETTEXT_DOMAIN "kdesktop-lib"

#include "ble2.hpp"

#include <time.h>
#include "gettext.hpp"
#include "help.hpp"
#include "filesystem.hpp"
#include "sound.hpp"
#include "wml_exception.hpp"

#include <iomanip>
using namespace std::placeholders;

#include <algorithm>

#include "base_instance.hpp"

enum {atyle_none, atype_playback, atype_apns};
static int alert_type = atype_playback;

#define MAX_RESERVE_TEMPS		6
#define RESERVE_FILE_DATA_LEN   (sizeof(ttemperature) * MAX_RESERVE_TEMPS)

#define THRESHOLD_HDERR_REF		5
#define RESISTANCE_OUTRANDE		UINT16_MAX

const char* tble2::uuid_my_service = "fd00";
const char* tble2::uuid_write_characteristic = "fd01";
const char* tble2::uuid_notify_characteristic = "fd03";

bool tble2::tether3elem::valid() const 
{
	return ipv4 != 0 && prefixlen >= 1 && prefixlen <= 31 && gateway != 0 && utils::is_same_net(ipv4, gateway, prefixlen);
}

bool tble2::is_discovery_name(const SDL_BlePeripheral& peripheral)
{
	if (game_config::os != os_windows) {
		if (peripheral.manufacturer_data_len < 2) {
			return false;
		}
		const uint16_t launcher_manufacturer_id_ = 65520; // 0xfff0 ==> (xmit)f0 ff
		if (peripheral.manufacturer_data[0] != posix_lo8(launcher_manufacturer_id_) || peripheral.manufacturer_data[1] != posix_hi8(launcher_manufacturer_id_)) {
			return false;
		}
	}

	const std::string lower_name = utils::lowercase(peripheral.name);
	if (lower_name.empty()) {
		return false;
	}
	// rdpd, rk3399(ios)
	if (game_config::os != os_ios) {
		return !strcmp(lower_name.c_str(), "rdpd");
	} else {
		return !strcmp(lower_name.c_str(), "rdpd") || !strcmp(lower_name.c_str(), "rk3399");
	}
}

void fill_interval_fields(uint8_t* data, int normal_interval, int fast_interval, int fast_perieod)
{
    data[0] = posix_lo8(normal_interval);
    data[1] = posix_hi8(normal_interval);
    data[2] = posix_lo8(fast_interval);
    data[3] = posix_hi8(fast_interval);
    data[4] = posix_lo8(fast_perieod);
    data[5] = posix_hi8(fast_perieod);
}

tble2::tble2(base_instance* instance)
	: tble(instance, connector_)
	, status_(nposm)
	, plugin_(NULL)
	, buf_(true)
{
	buf_.set_did_read(std::bind(&tble2::did_read_ble, this, _1, _2, _3));

	disable_reconnect_ = true;

	uint8_t data[6];
	{
		ttask& task = insert_task(taskid_postready);
		// step0: notify read
		task.insert(nposm, uuid_my_service, uuid_notify_characteristic, option_notify);
	}

	{
		ttask& task = insert_task(taskid_queryip);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}

	{
		ttask& task = insert_task(taskid_updateip);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}

	{
		ttask& task = insert_task(taskid_connectwifi);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}

	{
		ttask& task = insert_task(taskid_removewifi);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}

	{
		ttask& task = insert_task(taskid_refreshwifilist);
		// step0: set time
		task.insert(nposm, uuid_my_service, uuid_write_characteristic, option_write, data, 6);
	}
}

tble2::~tble2()
{
}

void tble2::disconnect_with_disable_reconnect()
{
	mac_addr_.clear();
	connector_.clear();
	disconnect_peripheral();
}

void tble2::connect_wifi(const std::string& ssid, const std::string& password)
{
	VALIDATE(!ssid.empty() && !password.empty(), null_str);
	VALIDATE(status_ == status_updateip, null_str);

	tuint8data data;
	buf_.form_connectwifi_req(ssid, password, data);

	tble::ttask& task = get_task(taskid_connectwifi);
	tstep& step = task.get_step(0);
	step.set_data(data.ptr, data.len);
	if (game_config::os != os_windows) {
		task.execute(*this);
	}
}

void tble2::remove_wifi(const std::string& ssid)
{
	VALIDATE(!ssid.empty(), null_str);
	VALIDATE(status_ == status_updateip, null_str);

	tuint8data data;
	buf_.form_removewifi_req(ssid, data);

	tble::ttask& task = get_task(taskid_removewifi);
	tstep& step = task.get_step(0);
	step.set_data(data.ptr, data.len);
	if (game_config::os != os_windows) {
		task.execute(*this);
	}
}

void tble2::refresh_wifi()
{
	VALIDATE(status_ == status_updateip, null_str);

	tuint8data data;
	buf_.form_send_packet(msg_wifilist_req, nullptr, 0, data);

	tble::ttask& task = get_task(taskid_refreshwifilist);
	tstep& step = task.get_step(0);
	step.set_data(data.ptr, data.len);
	if (game_config::os != os_windows) {
		task.execute(*this);
	}
}

void tble2::app_calculate_mac_addr(SDL_BlePeripheral& peripheral)
{
    if (peripheral.manufacturer_data && peripheral.manufacturer_data_len >= 8) {
        int start = 2;
        if (peripheral.manufacturer_data_len > 8) {
            unsigned char flag = peripheral.manufacturer_data[2];
            if (flag & 0x1) {
                start = 8;
            } else {
                start = 3;
            }
            
        }
        peripheral.mac_addr[0] = peripheral.manufacturer_data[start];
        peripheral.mac_addr[1] = peripheral.manufacturer_data[start + 1];
        peripheral.mac_addr[2] = peripheral.manufacturer_data[start + 2];
        peripheral.mac_addr[3] = peripheral.manufacturer_data[start + 3];
        peripheral.mac_addr[4] = peripheral.manufacturer_data[start + 4];
        peripheral.mac_addr[5] = peripheral.manufacturer_data[start + 5];
        
    } else {
        peripheral.mac_addr[0] = '\0';
    }
}

void tble2::app_discover_peripheral(SDL_BlePeripheral& peripheral)
{
    if (!connector_.valid() && !connecting_peripheral_ && !peripheral_  && mac_addr_equal(mac_addr_.mac_addr, peripheral.mac_addr) && is_discovery_name(peripheral)) {
        connect_peripheral(peripheral);
    }
	if (plugin_) {
		plugin_->did_discover_peripheral(peripheral);
	}
}

void tble2::app_release_peripheral(SDL_BlePeripheral& peripheral)
{
	if (plugin_) {
		plugin_->did_release_peripheral(peripheral);
	}
}

bool tble2::is_right_services()
{
	// even thouth this peripheral has 3 service, but for ios, only can discover 1 service.
	// if (peripheral_->valid_services != 3) {
	if (peripheral_->valid_services == 0) {
		return false;
	}
	const SDL_BleService* my_service = nullptr;
	for (int n = 0; n < peripheral_->valid_services; n ++) {
		const SDL_BleService& service = peripheral_->services[n];
		if (SDL_BleUuidEqual(service.uuid, uuid_my_service)) {
			my_service = &service;
		}
	}
	if (my_service == nullptr) {
		return false;
	}
	if (my_service->valid_characteristics != 3) {
		return false;
	}

	std::vector<std::string> chars;
	chars.push_back(uuid_write_characteristic);
	chars.push_back(uuid_notify_characteristic);
	for (std::vector<std::string>::const_iterator it = chars.begin(); it != chars.end(); ++ it) {
		int n = 0;
		for (; n < my_service->valid_characteristics; n ++) {
			const SDL_BleCharacteristic& char2 = my_service->characteristics[n];
			if (SDL_BleUuidEqual(char2.uuid, it->c_str())) {
				break;
			}
		}
		if (n == my_service->valid_characteristics) {
			return false;
		}
	}

	return true;
}

void tble2::app_connect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
    if (!error) {
		SDL_Log("tble2::app_connect_peripheral, will execute task#%i", taskid_postready);
		tble::ttask& task = get_task(taskid_postready);
		task.execute(*this);
    }

	if (plugin_) {
		plugin_->did_connect_peripheral(peripheral, error);
	}
}

void tble2::app_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	if (plugin_) {
		plugin_->did_disconnect_peripheral(peripheral, error);
	}
}

void tble2::app_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error)
{
	if (plugin_) {
		plugin_->did_discover_characteristics(peripheral, service, error);
	}
}

void tble2::app_read_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const unsigned char* data, int len)
{
	VALIDATE(peripheral_ == &peripheral, null_str);
	if (SDL_BleUuidEqual(characteristic.uuid, uuid_notify_characteristic) && plugin_ != nullptr) {
		buf_.enqueue(data, len);
	}
}

void tble2::did_read_ble(int cmd, const uint8_t* data, int len)
{
	// std::string str = rtc::hex_encode((const char*)data, len);
	// SDL_Log("%u tble2::did_read_ble, %s", SDL_GetTicks(), str.c_str());

	SDL_BlePeripheral& peripheral = *peripheral_;
	if (cmd == msg_queryip_resp) {
		tblebuf::tqueryip_resp resp = buf_.parse_queryip_resp(data, len);
		plugin_->did_query_ip(peripheral, resp);

	} else if (cmd == msg_wifilist_resp) {
		if (status_ == status_updateip) {
			plugin_->did_recv_wifilist(peripheral, data, len);
		}
	}
}

void tble2::app_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error)
{
	if (plugin_) {
		plugin_->did_write_characteristic(peripheral, characteristic, error);
	}
}

void tble2::app_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error)
{
	if (plugin_) {
		plugin_->did_notify_characteristic(peripheral, characteristic, error);
	}
}

bool tble2::app_task_callback(ttask& task, int step_at, bool start)
{
	SDL_Log("tble2::app_task_callback--- task#%i, step_at: %i, %s, current_step_: %i", task.id(), step_at, start? "prefix": "postfix", current_step_);

	if (start) {
		if (task.id() == taskid_queryip) {
			if (step_at == 0) {
				tstep& step = task.get_step(0);
				tuint8data data;
				buf_.form_queryip_req(data);
				step.set_data(data.ptr, data.len);
			}
		} else if (task.id() == taskid_updateip) {
			if (step_at == 0) {
				tstep& step = task.get_step(0);

				tuint8data data;
				buf_.form_send_packet(msg_updateip_req, nullptr, 0, data);
				step.set_data(data.ptr, data.len);
			}
		}
	} else {
		if (task.id() == taskid_postready) {
			if (step_at == (int)(task.steps().size() - 1)) {
				if (plugin_ != nullptr) {
					plugin_->did_start_queryip(*peripheral_);
				}

				int taskid = nposm;
				if (status_ == status_queryip) {
					taskid = taskid_queryip;
				} else if (status_ == status_updateip) {
					taskid = taskid_updateip;
				} else {
					VALIDATE(false, null_str);
				}
				
				tble::ttask& task = get_task(taskid);
				task.execute(*this);
			}
		}
	}
	return true;
}




