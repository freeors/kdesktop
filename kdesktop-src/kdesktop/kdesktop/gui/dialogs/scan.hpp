#ifndef GUI_DIALOGS_SCAN_HPP_INCLUDED
#define GUI_DIALOGS_SCAN_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "ble2.hpp"

#include <net/base/ip_endpoint.h>

namespace gui2 {

class tlistbox;
class ttoggle_panel;
class tbutton;
class tstack;
class tlabel;
class tgrid;

enum {MSG_HOME_POPUP_WIFI_PASSWORD = tdialog::POST_MSG_MIN_APP};

struct tmsg_home_popup_wifi_password: public rtc::MessageData
{
	tmsg_home_popup_wifi_password(const std::string& ssid, uint32_t flags)
		: ssid(ssid)
		, flags(flags)
	{}

	const std::string ssid;
	const uint32_t flags;
};

class tscan: public tble_plugin
{
public:
	enum {LABEL_SCAN_LAYER, BUTTON_SCAN_LAYER};
	enum {QUERY_LAYER, UPDATE_LAYER};

	enum {queryip_no, queryip_request, queryip_response, queryip_ok, 
		queryip_connectfail, queryip_getservicesfail, queryip_errorservices, queryip_unknownfail, queryip_invalidsn};
	struct tdiscover {
		tdiscover(const tble2& ble, SDL_BlePeripheral* peripheral, const tmac_addr& _mac_addr);

		SDL_BlePeripheral* peripheral;
		tmac_addr mac_addr;
		int queryip_status;
		net::IPAddress ip;
		int prefixlen;
		tblebuf::tqueryip_resp queryip_resp;
		int at;
		std::string sn;
	};

	struct twifiap {
		twifiap(const tdiscover& discover, const std::string& ssid, int rssi, uint32_t flags)
			: discover(discover)
			, ssid(ssid)
			, rssi(rssi)
			, flags(flags)
		{}

		const tdiscover& discover;
		std::string ssid;
		int rssi;
		uint32_t flags;
	};

	explicit tscan(tble2& ble);

protected:
	void pre_scan(tgrid& grid, rtc::MessageHandler& message_handler);
	void scan_post_show();

	void scan_navigation_pre_change();
	void scan_navigation_changed();

	void pre_scan_query(tgrid& grid);
	void pre_scan_update(tgrid& grid);

	void pre_scan_status_label(tgrid& grid);
	void pre_scan_status_button(tgrid& grid);
	void scan_timer_handler(uint32_t now);

	void did_discover_peripheral(SDL_BlePeripheral& peripheral) override;
	void did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error) override;
	void did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error) override;
	void did_start_queryip(SDL_BlePeripheral& peripheral) override;
	void did_query_ip(SDL_BlePeripheral& peripheral, const tblebuf::tqueryip_resp& resp) override;
	void did_recv_wifilist(SDL_BlePeripheral& peripheral, const uint8_t* data, int len) override;

	void reload_discovers(tlistbox& list);
	void reload_wifiaps(tlistbox& list);
	void visible_updateip_buttons(tlistbox& list, bool visible);
	void click_scan(tbutton& widget);
	void did_wifiap_changed(tlistbox& list, ttoggle_panel& row);
	void click_updateip(ttoggle_panel& row, tbutton& widget);
	void click_rdp(ttoggle_panel& row, tbutton& widget);
	void query_ip(tdiscover& discover);
	void update_ip(tdiscover& discover);
	void start_scan_internal();
	void stop_scan_internal();
	// action include queryip, updateip
	void stop_action_internal(bool querying, uint32_t now);
	void set_scan_description_label(uint32_t now);

	tdiscover* find_discover(const SDL_BlePeripheral& peripheral);
	void disconnect_and_rescan();
	std::string queryip_str(const tdiscover& discover, bool query) const;
	void set_discover_row_ip_label(const tdiscover& discover, bool query);
	void set_discover_row_label(const tdiscover& discover, const std::string& id, const std::string& label);
	void set_wifi_description_label(uint32_t current_ip, bool flags);

	struct twifilist_resp {
		twifilist_resp(std::vector<twifiap>& wifiaps)
			: current_ip(0)
			, flags(0)
			, wifiaps(wifiaps)
		{}
		void clear()
		{
			current_ip = 0;
			flags = 0;
			wifiaps.clear();
		}

		uint32_t current_ip;
		uint32_t flags;
		std::vector<twifiap>& wifiaps;
	};
	bool parse_wifilist_resp(const uint8_t* data, int len, const tdiscover& discover, twifilist_resp& result) const;
	bool did_verify_ssid_password(const std::string& label) const;
	void click_update_connect(ttoggle_panel& row, tbutton& widget);

	void did_wifiap_pullrefresh_refresh(ttrack& widget, const SDL_Rect& rect, tgrid& layer);
	void popup_wifi_password(const std::string& ssid);
	bool in_updateip_status() const;
	void scan_OnMessage(rtc::Message* msg);

private:
	tble2& ble_;
	rtc::MessageHandler* message_handler_;
	tstack* scan_stack_;
	tlistbox* query_list_;
	tstack* scan_status_stack_;
	tlabel* scan_desc_widget_;
	const std::string ip_id_;
	const std::string rssi_id_;
	bool closing_;
	time_t connect_time_;
	time_t disconnect_time_;
	time_t wifilist_time_;

	bool scaning_;
	uint32_t stop_scan_ticks_;

	std::vector<tdiscover> discovers_;
	std::vector<twifiap> wifiaps_;

	const tdiscover* queryip_discover_;
	uint32_t queryip_halt_ticks_;
	uint32_t updateip_halt_ticks_;

	// update ip
	tlabel* wifi_desc_widget_;
	tlistbox* wifiap_list_;
};

} // namespace gui2

#endif

