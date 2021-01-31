#ifndef BLE2_HPP_INCLUDED
#define BLE2_HPP_INCLUDED

#include <set>
#include <vector>
#include <map>
#include <filesystem.hpp>
#include "ble.hpp"
#include "thread.hpp"
#include "freerdp/channels/leagor_ble.hpp"

class tble_plugin
{
public:
	virtual void did_discover_peripheral(SDL_BlePeripheral& peripheral) {}
	virtual void did_release_peripheral(SDL_BlePeripheral& peripheral) {}
	virtual void did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error) {}
	virtual void did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error) {}
    virtual void did_discover_services(SDL_BlePeripheral& peripheral, const int error) {}
	virtual void did_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error) {}
	virtual void did_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error) {}
	virtual void did_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error) {}
	virtual void did_start_queryip(SDL_BlePeripheral& peripheral) {}
	virtual void did_query_ip(SDL_BlePeripheral& peripheral, const tblebuf::tqueryip_resp& resp) {}
	virtual void did_recv_wifilist(SDL_BlePeripheral& peripheral, const uint8_t* data, int len) {}
};

class tble2: public tble
{
public:
	enum {taskid_postready, taskid_queryip, taskid_updateip, taskid_connectwifi, taskid_removewifi, taskid_refreshwifilist};
	enum {status_queryip, status_updateip};
	struct tether3elem
	{
		tether3elem()
		{
			clear();
		}

		bool valid() const;

		void clear()
		{
			ipv4 = 0;
			prefixlen = 0;
			gateway = 0;
		}

		uint32_t ipv4;
		int prefixlen;
		uint32_t gateway;
	};

	static const char* uuid_my_service;
	static const char* uuid_write_characteristic;
	static const char* uuid_notify_characteristic;
	static bool is_discovery_name(const SDL_BlePeripheral& peripheral);

	tble2(base_instance* instance);
	~tble2();

	void set_plugin(tble_plugin* plugin)
    {
        plugin_ = plugin;
    }

	void disconnect_with_disable_reconnect();

	void set_status(int status) { status_ = status; }
	int status() const { return status_; }
	tblebuf& blebuf() { return buf_; }

	void connect_wifi(const std::string& ssid, const std::string& password);
	void remove_wifi(const std::string& ssid);
	void refresh_wifi();

private:
	bool is_right_services() override;
	void did_read_ble(int cmd, const uint8_t* data, int len);

	void app_discover_peripheral(SDL_BlePeripheral& peripheral);
	void app_release_peripheral(SDL_BlePeripheral& peripheral);
	void app_connect_peripheral(SDL_BlePeripheral& peripheral, const int error);
	void app_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error);
	void app_discover_characteristics(SDL_BlePeripheral& peripheral, SDL_BleService& service, const int error);
	void app_read_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const unsigned char* data, int len);
	void app_write_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error);
	void app_notify_characteristic(SDL_BlePeripheral& peripheral, SDL_BleCharacteristic& characteristic, const int error);
	bool app_task_callback(ttask& task, int step_at, bool start);
    void app_calculate_mac_addr(SDL_BlePeripheral& peripheral);
    
private:
	int status_;
	tble_plugin* plugin_;

	tconnector connector_;
	threading::mutex mutex_;

	tblebuf buf_;
};

#endif
