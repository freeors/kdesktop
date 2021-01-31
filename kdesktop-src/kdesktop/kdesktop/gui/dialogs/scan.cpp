#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/scan.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/stack.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/ethernetip.hpp"
#include "gui/dialogs/edit_box.hpp"
#include "gui/dialogs/messagefs.hpp"
#include "gettext.hpp"
#include "rose_config.hpp"
#include "version.hpp"
#include "base_instance.hpp"

#include "formula_string_utils.hpp"
#include <SDL_peripheral.h>

using namespace std::placeholders;
/*
namespace utils {
std::string from_ipv4(uint32_t ipv4)
{
	net::IPAddress ip = net::IPAddress((const uint8_t*)&ipv4, 4);
	return ip.ToString();
}
}
*/

// <aosp>/frameworks/base/wifi/java/android/net/wifi/WifiManager.java
/** Anything worse than or equal to this will show 0 bars. */
static int MIN_RSSI = -100;

/** Anything better than or equal to this will show the max bars. */
static int MAX_RSSI = -55;

/**
    * Number of RSSI levels used in the framework to initiate
    * {@link #RSSI_CHANGED_ACTION} broadcast
    * @hide
    */
static int RSSI_LEVELS = 4; // 5

static int calculateSignalLevel(int rssi, int numLevels)
{
    if (rssi <= MIN_RSSI) {
        return 0;
    } else if (rssi >= MAX_RSSI) {
        return numLevels - 1;
    } else {
        float inputRange = (MAX_RSSI - MIN_RSSI);
        float outputRange = (numLevels - 1);
        return (int)((float)(rssi - MIN_RSSI) * outputRange / inputRange);
    }
}

static std::string wifi_rssi_icon(int rssi)
{
	int level = calculateSignalLevel(rssi, RSSI_LEVELS);
	char buf[24];
	SDL_snprintf(buf, sizeof(buf), "misc/wifi_level%i.png", level);
	return buf;
}

namespace gui2 {

tscan::tdiscover::tdiscover(const tble2& ble, SDL_BlePeripheral* peripheral, const tmac_addr& _mac_addr)
	: peripheral(peripheral)
	, queryip_status(queryip_no)
	, prefixlen(nposm)
	, at(nposm)
{
	mac_addr = _mac_addr;
	memset(&queryip_resp, 0, sizeof(queryip_resp));
}

tscan::tscan(tble2& ble)
	: ble_(ble)
	, message_handler_(nullptr)
	, scan_stack_(nullptr)
	, query_list_(nullptr)
	, scan_status_stack_(nullptr)
	, scan_desc_widget_(nullptr)
	, ip_id_("ip")
	, rssi_id_("rssi")
	, scaning_(false)
	, queryip_discover_(nullptr)
	, stop_scan_ticks_(0)
	, queryip_halt_ticks_(0)
	, updateip_halt_ticks_(0)
	, closing_(false)
	, connect_time_(0)
	, disconnect_time_(0)
	, wifilist_time_(0)
	, wifi_desc_widget_(nullptr)
	, wifiap_list_(nullptr)

{
}

void tscan::pre_scan(tgrid& grid, rtc::MessageHandler& message_handler)
{
	message_handler_ = &message_handler;

	utils::string_map symbols;

	std::stringstream ss;

	ss.str("");
	symbols["updateip"] = _("Update IP");
	ss << vgettext2("scan help $updateip", symbols);
	find_widget<tlabel>(&grid, "help", false).set_label(ss.str());

	scan_stack_ = find_widget<tstack>(&grid, "scan_stack", false, true);
	pre_scan_query(*scan_stack_->layer(QUERY_LAYER));
	pre_scan_update(*scan_stack_->layer(UPDATE_LAYER));

	scan_status_stack_ = find_widget<tstack>(&grid, "scan_status_stack", false, true);
	pre_scan_status_label(*scan_status_stack_->layer(LABEL_SCAN_LAYER));
	pre_scan_status_button(*scan_status_stack_->layer(BUTTON_SCAN_LAYER));

	ble_.set_plugin(this);
}

void tscan::scan_post_show()
{
	closing_ = true;
	if (queryip_discover_ != nullptr) {
		std::string text;
		const tdiscover& discover = *queryip_discover_;
		bool querying = queryip_halt_ticks_ != 0;
		stop_action_internal(querying, SDL_GetTicks());

		// disconnect_and_rescan();
		ble_.disconnect_with_disable_reconnect();
		if (scaning_) {
			// scenario: during queryip, But the whole queryip hasn't been done yet.
			VALIDATE(stop_scan_ticks_ != 0, null_str);
			scaning_ = false;
			stop_scan_ticks_ = 0;
		}
		return;
	}

	if (stop_scan_ticks_ != 0) {
		stop_scan_internal();
	}
}

void tscan::scan_navigation_pre_change()
{
	scan_post_show();
}

void tscan::scan_navigation_changed()
{
	VALIDATE(queryip_discover_ == nullptr, null_str);
	connect_time_ = 0;
	disconnect_time_ = 0;
	wifilist_time_ = 0;

	closing_ = false;
	wifiaps_.clear();
	wifiap_list_->clear();

	scan_stack_->set_radio_layer(QUERY_LAYER);
	start_scan_internal();
	scan_status_stack_->set_radio_layer(LABEL_SCAN_LAYER);
}

void tscan::pre_scan_query(tgrid& grid)
{
	tlistbox* list = find_widget<tlistbox>(&grid, "devices", false, true);
	list->enable_select(false);
	query_list_ = list;
}

void tscan::pre_scan_update(tgrid& grid)
{
	tlabel* label = find_widget<tlabel>(&grid, "wifi_label", false, true);
	wifi_desc_widget_ = label;

	tlistbox* list = find_widget<tlistbox>(&grid, "wifis", false, true);
	list->enable_select(false);
	list->set_did_pullrefresh(cfg_2_os_size(48), std::bind(&tscan::did_wifiap_pullrefresh_refresh, this, _1, _2, std::ref(grid)));
	wifiap_list_ = list;

	list->set_did_row_changed(std::bind(&tscan::did_wifiap_changed, this, _1, _2));
}

void tscan::pre_scan_status_label(tgrid& grid)
{
	scan_desc_widget_ = find_widget<tlabel>(&grid, "scan_description", false, true);
}

void tscan::pre_scan_status_button(tgrid& grid)
{
	tbutton* button = find_widget<tbutton>(&grid, "scan", false, true);
	connect_signal_mouse_left_click(
			  *button
			, std::bind(
			&tscan::click_scan
			, this, std::ref(*button)));
	button->set_label(_("Scan again"));
}

std::string tscan::queryip_str(const tdiscover& discover, bool query) const
{
	std::stringstream ss;
	if (query) {
		ss << _("Query IP");
	} else {
		ss << discover.ip.ToString() << "=>";
	}
	ss << " ";
	if (discover.queryip_status == queryip_no) {
		ss << "[1/3]" << _("Connecting");

	} else if (discover.queryip_status == queryip_request) {
		ss << "[2/3]" << _("Send request");

	} else if (discover.queryip_status == queryip_response) {
		ss << "[3/3]" << _("Waiting response");

	} else if (discover.queryip_status == queryip_ok) {
		ss.str("");
		if (query) {
			if (discover.queryip_resp.af == LEAGOR_BLE_AF_INET) {
				ss << discover.ip.ToString() << "/" << utils::from_prefixlen(discover.prefixlen);
			} else if (discover.queryip_resp.af == LEAGOR_BLE_AF_UNSPEC) {
				ss << _("Version dismatch") << " S" << discover.queryip_resp.version << "!=C" << LEAGOR_BLE_VERSION;
			} else {
				ss << _("Unsupport af: ") << discover.queryip_resp.af;
			}
		} else {
			ss << discover.ip.ToString() << "=>" << (" OK");
		}

	} else if (discover.queryip_status == queryip_connectfail) {
		ss << _("Connect fail");

	} else if (discover.queryip_status == queryip_getservicesfail) {
		ss << _("Get serviceDB fail");

	} else if (discover.queryip_status == queryip_errorservices) {
		ss << _("ServiceDB error");

	} else if (discover.queryip_status == queryip_unknownfail) {
		ss << _("Unknown error");

	} else {
		VALIDATE(discover.queryip_status == queryip_invalidsn, null_str);
		ss << _("Unknown device, don't query");
	}

	return ss.str();
}

void tscan::set_discover_row_ip_label(const tdiscover& discover, bool query)
{
	set_discover_row_label(discover, ip_id_, queryip_str(discover, query));
}

void tscan::set_discover_row_label(const tdiscover& discover, const std::string& id, const std::string& label)
{
	VALIDATE(discover.at != nposm && discover.at < query_list_->rows(), null_str);

	ttoggle_panel& row = query_list_->row_panel(discover.at);
	row.set_child_label(id, label);
}

static bool is_valid_sn_char(uint8_t ch)
{
	if ((ch & 0x80) || ch <= 0x20 || ch == 0x7f) {
		return false;
	}
	return true;
}

std::string extract_sn(const uint8_t* data, int len)
{
	if (len != 0) {
		VALIDATE(data != nullptr, null_str);
	}
	if (len < 3) {
		return null_str;
	}
	for (int at = 2; at < len; at ++) {
		uint8_t ch = data[at];
		if (!is_valid_sn_char(ch)) {
			return null_str;
		}
	}
	return std::string((const char*)(data + 2), len - 2);
}

void tscan::did_discover_peripheral(SDL_BlePeripheral& peripheral)
{
	if (closing_) {
		return;
	}

	if (!peripheral.name || peripheral.name[0] == '\0') {
		return;
	}
	if (!tble2::is_discovery_name(peripheral)) {
		return;
	}
	if (queryip_discover_ != nullptr) {
		// now one discover is querying ip, don't cosider other peripheral.
		return;
	}

	tdiscover discover(ble_, &peripheral, tmac_addr(peripheral.mac_addr, utils::cstr_2_str(peripheral.uuid)));
    if (find_discover(peripheral) != nullptr) {
		return;
	}
	discover.sn = extract_sn(discover.peripheral->manufacturer_data, discover.peripheral->manufacturer_data_len);
	if (!discover.sn.empty()) {
		// discover.queryip_status = queryip_no;
	} else {
		bool use_fakesn = game_config::os == os_windows;
		if (use_fakesn) {
			discover.sn = "fack-sn";
		} else {
			discover.queryip_status = queryip_invalidsn;
		}
	}

	// current not moniter this peripheral, put it to non-moniter.
	discovers_.push_back(discover);
	reload_discovers(*query_list_);

	// if ((game_config::os != os_windows || scaning_) && !discover.sn.empty()) {
	if (scaning_ && !discover.sn.empty()) {
		// if scanning, query ip. else do nothing.
		query_ip(discovers_.back());
	}
}

void tscan::did_connect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	SDL_Log("tscan::did_connect_peripheral--- error: %i", error);
	connect_time_ = time(nullptr);
	if (error == tble::bleerr_ok) {
		discover->queryip_status = queryip_request;
	} else if (error == tble::bleerr_connect) {
		discover->queryip_status = queryip_connectfail;

	} else if (error == tble::bleerr_getservices) {
		discover->queryip_status = queryip_getservicesfail;

	} else if (error == tble::bleerr_errorservices) {
		discover->queryip_status = queryip_errorservices;

	} else {
		discover->queryip_status = queryip_unknownfail;
	}
	set_discover_row_ip_label(*discover, queryip_halt_ticks_ != 0);
}

void tscan::did_disconnect_peripheral(SDL_BlePeripheral& peripheral, const int error)
{
	if (queryip_discover_ != nullptr) {
		stop_action_internal(queryip_halt_ticks_ != 0, SDL_GetTicks());
	}
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	disconnect_time_ = time(nullptr);


	SDL_Log("tscan::did_disconnect_peripheral--- error: %i, queryip_status: %i", error, discover->queryip_status);
	if (discover->queryip_status == queryip_ok) {
		return;
	}

	// Connection may be unexpectedly disconnected while sending request and waiting for response.
	if (scaning_) {
		SDL_Log("tscan::did_disconnect_peripheral, exceptly disconnect, queryip_status: %i, rescan", 
			discover->queryip_status);
		ble_.start_scan();
	}
}

void tscan::did_start_queryip(SDL_BlePeripheral& peripheral)
{
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}

	discover->queryip_status = queryip_response;
}

void tscan::did_query_ip(SDL_BlePeripheral& peripheral, const tblebuf::tqueryip_resp& resp)
{
	SDL_Log("tscan::did_query_ip--- version: %i, af: %i, ipv4: 0x%08x", resp.version, resp.af, resp.a.ipv4);

	if (queryip_discover_ != nullptr) {
		stop_action_internal(queryip_halt_ticks_ != 0, SDL_GetTicks());
	}
	tdiscover* discover = find_discover(peripheral);
	if (discover == nullptr) {
		return;
	}
	discover->queryip_status = queryip_ok;
	discover->queryip_resp = resp;
	discover->ip = net::IPAddress((const uint8_t*)&resp.a.ipv4, 4);
	discover->prefixlen = 0;

	set_discover_row_ip_label(*discover, true);

	disconnect_and_rescan();
	SDL_Log("---tscan::did_query_ip X");
}

void tscan::set_wifi_description_label(uint32_t current_ip, bool flags)
{
	utils::string_map symbols;
	std::stringstream ss;

	ss << (flags & LEAGOR_BLE_FLAG_WIFI_ENABLED? _("Wifi is enabled"): _("Wifi is disabled"));
	ss << "  ";

	symbols["ip"] = utils::from_ipv4(current_ip);
	ss << vgettext2("Using IP: $ip", symbols);
	wifi_desc_widget_->set_label(ss.str());
}

void tscan::did_recv_wifilist(SDL_BlePeripheral& peripheral, const uint8_t* data, int len)
{
	// SDL_Log("tscan::did_recv_wifilist--- len: %i", len);
	if (queryip_discover_ != nullptr && updateip_halt_ticks_ != 0) {
		const tdiscover& discover = *queryip_discover_;

		twifilist_resp result(wifiaps_);
		parse_wifilist_resp(data, len, discover, result);
		// SDL_Log("tscan::did_recv_wifilist, flags: 0x%x, wifiaps_.size(): %i", result.flags, wifiaps_.size());
		wifilist_time_ = time(nullptr);
		set_wifi_description_label(result.current_ip, result.flags);
		reload_wifiaps(*wifiap_list_);
	}
	// SDL_Log("tscan::did_recv_wifilist X, len: %i", len);
}

void tscan::disconnect_and_rescan()
{
	SDL_Log("disconnect_and_rescan");
	ble_.disconnect_with_disable_reconnect();
	if (scaning_) {
		ble_.start_scan();
	}
	SDL_Log("---tscan::disconnect_and_rescan X");
}

tscan::tdiscover* tscan::find_discover(const SDL_BlePeripheral& peripheral)
{
	int at = 0;
	for (std::vector<tdiscover>::iterator it = discovers_.begin(); it != discovers_.end(); it ++, at ++) {
		tdiscover& discover = *it;
		if (discover.peripheral == &peripheral) {
			return &discover;
		}
	}
	return nullptr;
}

void tscan::query_ip(tdiscover& discover)
{
	VALIDATE(discover.queryip_status == queryip_no, null_str);
	VALIDATE(scaning_, null_str);
	VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);

	const int queryip_threshold = game_config::os != os_windows? 15000: 2000; // 15 second
	queryip_halt_ticks_ = SDL_GetTicks() + queryip_threshold;
	queryip_discover_ = &discover;

	VALIDATE(stop_scan_ticks_ != 0, null_str);
	stop_scan_ticks_ += queryip_threshold;
	// make ensure stop scan must be after query ip end.
	stop_scan_ticks_ = SDL_max(stop_scan_ticks_, queryip_halt_ticks_ + 1000);

	ble_.set_status(tble2::status_queryip);
	ble_.connect_peripheral(*discover.peripheral);
}

void tscan::update_ip(tdiscover& discover)
{
	// VALIDATE(discover.queryip_status == queryip_ok, null_str);
	VALIDATE(!scaning_, null_str);
	VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);

	discover.queryip_status = queryip_no;
	set_discover_row_ip_label(discover, false);

	// const int updateip_threshold = game_config::os != os_windows? 25000: 5000; // 25 second
	const int updateip_threshold = 24 * 3600 * 1000; // 1 hour
	updateip_halt_ticks_ = SDL_GetTicks() + updateip_threshold;
	queryip_discover_ = &discover;

	ble_.set_status(tble2::status_updateip);
	ble_.connect_peripheral(*discover.peripheral);
}

void tscan::reload_discovers(tlistbox& list)
{
	list.clear();

	std::stringstream ss;
	std::map<std::string, std::string> data;
	int at = 0;
	for (std::vector<tdiscover>::iterator it = discovers_.begin(); it != discovers_.end(); it ++, at ++) {
		data.clear();

		tdiscover& discover = *it;
        VALIDATE(discover.peripheral != nullptr, null_str);
		data.insert(std::make_pair("name", discover.peripheral->name));
		ss.str("");
		ss << (discover.sn.empty()? _("Invalid SN"): discover.sn);
		ss << "(" << discover.mac_addr.str() << ")";
		data.insert(std::make_pair("macaddr", ss.str()));
		
		if (discover.peripheral->manufacturer_data_len > 2) {
		}
		data.insert(std::make_pair("rssi", str_cast(discover.peripheral->rssi)));
		data.insert(std::make_pair("ip", queryip_str(discover, true)));

		ttoggle_panel& row = list.insert_row(data);

		tbutton* button = find_widget<tbutton>(&row, "updateip", false, true);
		connect_signal_mouse_left_click(
				  *button
				, std::bind(
				&tscan::click_updateip
				, this, std::ref(row), std::ref(*button)));
		button->set_label(_("Update IP"));
		button->set_visible(twidget::INVISIBLE);

		button = find_widget<tbutton>(&row, "desktop", false, true);
		connect_signal_mouse_left_click(
				  *button
				, std::bind(
				&tscan::click_rdp
				, this, std::ref(row), std::ref(*button)));
		button->set_label(_("Remote desktop"));
		button->set_visible(twidget::INVISIBLE);

		discover.at = row.at();
	}
}

void tscan::reload_wifiaps(tlistbox& list)
{
	list.clear();

	std::stringstream ss;
	std::map<std::string, std::string> data;
	int at = 0;
	for (std::vector<twifiap>::iterator it = wifiaps_.begin(); it != wifiaps_.end(); it ++, at ++) {
		data.clear();

		twifiap& ap = *it;
        VALIDATE(ap.discover.peripheral != nullptr, null_str);
		data.insert(std::make_pair("rssi", wifi_rssi_icon(ap.rssi)));
		ss.str("");
		ss << ap.ssid;
		if (game_config::os == os_windows) {
			ss << "(" << ap.rssi << ")";
		}
		data.insert(std::make_pair("ssid", ss.str()));
		if (ap.flags & SDL_WifiApFlagConnected) {
			data.insert(std::make_pair("connected", _("Connected")));
		}

		ttoggle_panel& row = list.insert_row(data);
/*
		tbutton* button = find_widget<tbutton>(&row, "connect", false, true);
		connect_signal_mouse_left_click(
				  *button
				, std::bind(
				&tscan::click_update_connect
				, this, std::ref(row), std::ref(*button)));
*/
	}
}

void tscan::visible_updateip_buttons(tlistbox& list, bool visible)
{
	VALIDATE(!scaning_, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);
	// VALIDATE(queryip_discover_ == nullptr, null_str);
	VALIDATE(queryip_halt_ticks_ == 0 && updateip_halt_ticks_ == 0, null_str);

	int rows = list.rows();
	VALIDATE(rows == discovers_.size(), null_str);
	for (int at = 0; at < rows; at ++) {
		const tdiscover& discover = discovers_[at];
		ttoggle_panel& row = list.row_panel(at);
		tbutton* upateip_widget = find_widget<tbutton>(&row, "updateip", false, true);
		tbutton* desktop_widget = find_widget<tbutton>(&row, "desktop", false, true);
		if (visible) {
			VALIDATE(upateip_widget->get_visible() == twidget::INVISIBLE, null_str);
			VALIDATE(desktop_widget->get_visible() == twidget::INVISIBLE, null_str);
			if (discover.queryip_resp.af == LEAGOR_BLE_AF_INET || game_config::os == os_windows) {
				upateip_widget->set_visible(twidget::VISIBLE);
				// desktop_widget->set_visible(twidget::VISIBLE);
			}
		} else {
			upateip_widget->set_visible(twidget::INVISIBLE);
			// desktop_widget->set_visible(twidget::INVISIBLE);
		}
	}
}

void tscan::start_scan_internal()
{
	VALIDATE(!scaning_, null_str);
	VALIDATE(stop_scan_ticks_ == 0, null_str);
	scaning_ = !scaning_;

	uint32_t now = SDL_GetTicks();
	const int scan_threshold = game_config::os != os_windows? 10000: 2000/*5000*/; // 10 second
	// const int scan_threshold = game_config::os != os_windows? 64 * 3600 * 1000: 2000/*5000*/; // 10 second

	stop_scan_ticks_ = now + scan_threshold;
	set_scan_description_label(now);

	discovers_.clear();
	query_list_->clear();
	ble_.start_scan();
}

void tscan::stop_scan_internal()
{
	VALIDATE(stop_scan_ticks_ != 0, null_str);
	VALIDATE(scaning_, null_str);

	scaning_ = !scaning_;
	stop_scan_ticks_ = 0;
	// tble::tdisable_reconnect_lock lock(ble_, true);
	if (queryip_discover_ != nullptr) {
		queryip_discover_ = nullptr;
	}
	ble_.stop_scan();

	ble_.disconnect_with_disable_reconnect();
	scan_status_stack_->set_radio_layer(BUTTON_SCAN_LAYER);
	visible_updateip_buttons(*query_list_, true);
}

void tscan::stop_action_internal(bool querying, uint32_t now)
{
	VALIDATE(queryip_discover_ != nullptr, null_str);
	queryip_discover_ = nullptr;
	if (querying) {
		VALIDATE(updateip_halt_ticks_ == 0, null_str);
		if (stop_scan_ticks_ != 0 && now < queryip_halt_ticks_) {
			stop_scan_ticks_ -= queryip_halt_ticks_ - now;
		}
		queryip_halt_ticks_ = 0;
	} else {
		VALIDATE(queryip_halt_ticks_ == 0, null_str);
		updateip_halt_ticks_ = 0;
	}
}

void tscan::click_scan(tbutton& widget)
{
	start_scan_internal();

	scan_status_stack_->set_radio_layer(LABEL_SCAN_LAYER);
}

bool tscan::did_verify_ssid_password(const std::string& label) const
{
	if (label.empty()) {
		return false;
	}
	return true;
}

void tscan::did_wifiap_changed(tlistbox& list, ttoggle_panel& row)
{
	const int at = row.at();
	VALIDATE(at < (int)wifiaps_.size(), null_str);

	const twifiap& ap2 = wifiaps_[at];

	// during dialog.show, 1)wifilist maybe change. 2)ble maybe disconnect.
	const std::string ssid = wifiaps_[at].ssid;

	gui2::tmsg_home_popup_wifi_password* pdata = new gui2::tmsg_home_popup_wifi_password(ssid, ap2.flags);
	rtc::Thread::Current()->Post(RTC_FROM_HERE, message_handler_, MSG_HOME_POPUP_WIFI_PASSWORD, pdata);
}

void tscan::popup_wifi_password(const std::string& ssid)
{
	utils::string_map symbols;
	std::string title = ssid;
	std::string initial;
	// std::string initial = "9z10-17gg-si2t";
	gui2::tedit_box_param param(title, _("Password"), null_str, initial, null_str, null_str, _("Connect"), 30, gui2::tedit_box_param::show_cancel);
	param.did_text_changed = std::bind(&tscan::did_verify_ssid_password, this, _1);
	{
		gui2::tedit_box dlg(param);
		dlg.show(nposm, wifiap_list_->get_window()->get_height() / 5);
		if (dlg.get_retval() != twindow::OK) {
			return;
		}
	}

	if (game_config::os == os_windows) {
		const tdiscover& discover = *queryip_discover_;
		wifiaps_.clear();
		int count = 6;
		char buf[32];
		for (int n = 0; n < count; n ++) {
			SDL_snprintf(buf, sizeof(buf), "kos-%02i", n);
			int rssi = 36 + n;
			bool connected = false;
			if (n == 2) {
				connected = true;
			} else if (n == 6) {
				rssi = 172;
			}
			wifiaps_.push_back(twifiap(discover, buf, rssi, connected));
		}
		reload_wifiaps(*wifiap_list_);
	}

	if (in_updateip_status()) {
		ble_.connect_wifi(ssid, param.result);
	}
}

void tscan::did_wifiap_pullrefresh_refresh(ttrack& widget, const SDL_Rect& rect, tgrid& layer)
{
	if (ble_.status() == tble2::status_updateip && ble_.is_connected()) {
		ble_.refresh_wifi();
	}

/*
	VALIDATE(current_user.valid(), null_str);
	gui2::tprogress_default_slot slot(std::bind(&net::do_filelist, current_user.sessionid, std::ref(network_items_)));
	run_with_progress(slot, null_str, null_str, 0, rect);
*/
}

void tscan::click_updateip(ttoggle_panel& row, tbutton& widget)
{
	VALIDATE(wifiaps_.empty(), null_str);

	tdiscover& discover = discovers_[row.at()];
	if (!(discover.queryip_resp.flags & LEAGOR_BLE_FLAG_WIFI_ENABLED)) {
		const std::string msg = _("The device does not enable Wifi, update IP will automatically enable Wifi on the device, do you want to continue?");
		const int res = gui2::show_messagefs(msg, _("Continue"));
		if (res == gui2::twindow::CANCEL) {
			return;
		}
	}

	// visible_updateip_buttons(*query_list_, false);
	update_ip(discover);

	set_scan_description_label(SDL_GetTicks());
	find_widget<tlabel>(scan_stack_->layer(UPDATE_LAYER), "sn", false, true)->set_label(discover.sn);

	scan_status_stack_->set_radio_layer(LABEL_SCAN_LAYER);
	scan_stack_->set_radio_layer(UPDATE_LAYER);
}

void tscan::click_rdp(ttoggle_panel& row, tbutton& widget)
{
	int ii = 0;
}

void tscan::set_scan_description_label(uint32_t now)
{
	utils::string_map symbols;
	std::stringstream ss;

	if (stop_scan_ticks_ != 0) {
		// query ip
		VALIDATE(stop_scan_ticks_ > now, null_str);
		int elapse = stop_scan_ticks_ - now;
		symbols["elapse"] = format_elapse_hms(elapse / 1000);
		std::string msg = vgettext2("Scan will stop after $elapse", symbols);
		scan_desc_widget_->set_label(msg);

	} else {
		// update ip
		if (queryip_discover_ != nullptr) {
			// connecting, or connected
			const tdiscover& discover = *queryip_discover_;
			if (discover.queryip_status != queryip_request && discover.queryip_status != queryip_response) {
				ss << queryip_str(discover, false);
			} else {
				symbols["time"] = format_time_hms(wifilist_time_);
				ss << vgettext2("BLE was connectioned, $time received Wifi List", symbols);
			}
		} else {
			ss << format_time_hms(disconnect_time_) << " ";
			symbols["elapse"] = format_elapse_hms(time(nullptr) - disconnect_time_);
			ss << vgettext2("BLE connection was disconnected, and for $elapse", symbols);
		}
		scan_desc_widget_->set_label(ss.str());
	}
}

bool tscan::in_updateip_status() const
{
	return queryip_discover_ != nullptr && updateip_halt_ticks_ != 0;
}

void tscan::click_update_connect(ttoggle_panel& row, tbutton& widget)
{
}

bool tscan::parse_wifilist_resp(const uint8_t* data, int len, const tdiscover& discover, twifilist_resp& result) const
{
	result.clear();
	const int min_wifilist_payload_len = LEAGOR_BLE_AF_SIZE + LEAGOR_BLE_ADDRESS_SIZE + LEAGOR_BLE_FLAGS_SIZE + 1;
	if (len < min_wifilist_payload_len || data[len - 1] != '\0') {
		return false;
	}

	bool fail = false;
	const uint8_t* rd_ptr = data;
	int af = rd_ptr[0];
	if (af != LEAGOR_BLE_AF_INET) {
		return false;
	}
	rd_ptr += LEAGOR_BLE_AF_SIZE;
	result.current_ip = posix_mku32(posix_mku16(rd_ptr[0], rd_ptr[1]), posix_mku16(rd_ptr[2], rd_ptr[3]));
	rd_ptr += LEAGOR_BLE_ADDRESS_SIZE;
	result.flags = posix_mku32(posix_mku16(rd_ptr[0], rd_ptr[1]), posix_mku16(rd_ptr[2], rd_ptr[3]));
	rd_ptr += LEAGOR_BLE_FLAGS_SIZE;
	int count = rd_ptr[0];
	rd_ptr ++;

	for (int at = 0; at < count; at ++) {
		// 4(rssi) + 4(flags) + n(ssid) + 1('\0')
		if (len - (rd_ptr - data) < 6) {
			fail = true;
			break;
		}
		int rssi = posix_mki32(posix_mku16(rd_ptr[0], rd_ptr[1]), posix_mku16(rd_ptr[2], rd_ptr[3]));
		// memcpy(&rssi, rd_ptr, sizeof(int));
		rd_ptr += 4;
		uint32_t ap_flags = posix_mki32(posix_mku16(rd_ptr[0], rd_ptr[1]), posix_mku16(rd_ptr[2], rd_ptr[3]));
		rd_ptr += 4;

		int unread_bytes = len - (rd_ptr - data);
		int ssid_len = 0;
		for (; ssid_len < unread_bytes; ssid_len ++) {
			if (rd_ptr[ssid_len] == '\0') {
				break;
			}
		}
		VALIDATE(ssid_len < unread_bytes, null_str);
		result.wifiaps.push_back(twifiap(discover, (const char*)rd_ptr, rssi, ap_flags)),
		rd_ptr += ssid_len + 1;
	}
	if (fail || (int)result.wifiaps.size() != count) {
		result.clear();
	}
	return true;
}

void tscan::scan_timer_handler(uint32_t now)
{

	if (game_config::os == os_windows && queryip_discover_ != nullptr && updateip_halt_ticks_ != 0) {
		const tdiscover& discover = *queryip_discover_;
		uint32_t flags = SDL_WifiGetFlags();
		// wifi_desc_widget_->set_label(flags & SDL_WifiFlagEnable? _("Wifi is enabled"): _("Wifi is disabled"));

		// if ((flags & SDL_WifiFlagScanResultChanged) && wifiaps_.empty()) {
		if (flags & SDL_WifiFlagScanResultChanged) {
			wifiaps_.clear();
			SDL_WifiScanResult* results;
			int count = SDL_WifiGetScanResults(&results);
			tuint8data data;
			ble_.blebuf().form_wifilist_resp_ipv4(instance->current_ip(), flags, results, count, data);
			twifilist_resp resp(wifiaps_);
			parse_wifilist_resp(data.ptr + LEAGOR_BLE_MTU_HEADER_SIZE, data.len - LEAGOR_BLE_MTU_HEADER_SIZE, discover, resp);
			set_wifi_description_label(resp.current_ip, resp.flags);

			VALIDATE(count == (int)wifiaps_.size(), null_str);
			VALIDATE(instance->current_ip() == resp.current_ip, null_str);
			VALIDATE(flags == resp.flags, null_str);
			reload_wifiaps(*wifiap_list_);
		}
	}

	if (queryip_discover_ != nullptr) {
		std::string text;
		const tdiscover& discover = *queryip_discover_;
		uint32_t halt_ticks = queryip_halt_ticks_;
		bool querying = true;
		if (halt_ticks == 0) {
			halt_ticks = updateip_halt_ticks_;
			querying = false;
		} else {
			VALIDATE(updateip_halt_ticks_ == 0, null_str);
		}
		VALIDATE(halt_ticks > 0, null_str);
		if (now >= halt_ticks) {
			stop_action_internal(querying, now);
			disconnect_and_rescan();
			if (querying) {
				text = _("Can not get IP within limited time");
			} else {
				text = _("Can not update IP within limited time");
			}
			
		} else {
			uint32_t resi = (halt_ticks - now) / 1000;
			text = str_cast(resi);
		}
		set_discover_row_label(discover, rssi_id_, text);
	}

	if (stop_scan_ticks_ != 0) {
		// When querying IP, don't elapse scan ticks.
		VALIDATE(scaning_, null_str);
		if (now < stop_scan_ticks_) {
			set_scan_description_label(now);

		} else {
			stop_scan_internal();
		}
	} else {
		// updage ip
		set_scan_description_label(now);
	}
}

void tscan::scan_OnMessage(rtc::Message* msg)
{
	switch (msg->message_id) {
	case MSG_HOME_POPUP_WIFI_PASSWORD:
		{
			tmsg_home_popup_wifi_password* pdata = static_cast<tmsg_home_popup_wifi_password*>(msg->pdata);
			if (pdata->flags & SDL_WifiApFlagConnected) {
				utils::string_map symbols;
				symbols["ssid"] = pdata->ssid;
				const std::string msg = vgettext2("Do you want to delete SSID: $ssid? Once deleted, the password needs to be re-entered when connecting again", symbols);
				const int res = gui2::show_messagefs(msg);
				if (res == gui2::twindow::CANCEL) {
					return;
				}
				ble_.remove_wifi(pdata->ssid);
			} else {
				popup_wifi_password(pdata->ssid);
			}
		}
		break;
	}
}

} // namespace gui2

