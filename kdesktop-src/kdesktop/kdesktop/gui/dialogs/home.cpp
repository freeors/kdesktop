#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/home.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/toggle_button.hpp"
#include "gui/widgets/text_box.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/report.hpp"
#include "gui/widgets/stack.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/menu.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/edit_box.hpp"
#include "gettext.hpp"
#include "version.hpp"
#include "formula_string_utils.hpp"
#include "config_cache.hpp"

#include <net/base/ip_endpoint.h>
using namespace std::placeholders;

#include "sound.hpp"
#include "aplt_net.hpp"

namespace gui2 {

REGISTER_DIALOG(kdesktop, home)

thome::thome(tpbremotes& pbremotes, tble2& ble, std::vector<applet::tapplet>& applets, int startup_layer)
	: tscan(ble)
	, pbremotes_(pbremotes)
	, applets_(applets)
	, startup_layer_(startup_layer)
	, body_widget_(nullptr)
	, insert_widget_(nullptr)
	, current_layer_(nposm)
	, applets_widget_(nullptr)
{
	set_timer_interval(800);
}

thome::~thome()
{
}

void thome::pre_show()
{
	window_->set_escape_disabled(true);
	window_->set_label("misc/white_background.png");

	find_widget<tlabel>(window_, "title", false).set_label(game_config::get_app_msgstr(null_str));

	tbutton* button = find_widget<tbutton>(window_, "insert", false, true);
	connect_signal_mouse_left_click(
				*button
			, std::bind(
			&thome::click_insert
			, this, std::ref(*button)));
	insert_widget_ = button;

	body_widget_ = find_widget<tstack>(window_, "body", false, true);
	pre_rdp(*body_widget_->layer(RDP_LAYER));
	pre_scan(*body_widget_->layer(SCAN_LAYER), *this);
	pre_applet(*body_widget_->layer(APPLET_LAYER));
	pre_more(*body_widget_->layer(MORE_LAYER));

	treport* report = find_widget<treport>(window_, "navigation", false, true);
	// tcontrol* item = &report->insert_item(null_str, _("Remote desktop"));
	tcontrol* item;
	const bool english_error = false;
	if (english_error) {
		item = &report->insert_item(null_str, _("Remote desktop"));
	} else {
		item = &report->insert_item(null_str, _("Remote"));
	}
	item->set_icon("misc/rdp.png");

	item = &report->insert_item(null_str, _("Remote IP"));
	item->set_icon("misc/scan.png");

	item = &report->insert_item(null_str, _("Applet"));
	item->set_icon("misc/applet.png");

	item = &report->insert_item(null_str, _("More"));
	item->set_icon("misc/more.png");

	report->set_did_item_pre_change(std::bind(&thome::did_navigation_pre_change, this, _1, _2, _3));
	report->set_did_item_changed(std::bind(&thome::did_navigation_changed, this, _1, _2));
	report->select_item(startup_layer_);
}

void thome::post_show()
{
	scan_post_show();
	rdpcookie_.landscape = preferences::landscape();
}

void thome::pre_rdp(tgrid& grid)
{
	std::stringstream ss;
	utils::string_map symbols;

	const int max_ipv4_str_chars = 3 + 1 + 3 + 1 + 3 + 1 + 3;
	// ip addr
	ipaddr_ = find_widget<ttext_box>(&grid, "ipaddr", false, true);
	ipaddr_->set_did_text_changed(std::bind(&thome::did_text_box_changed, this, std::ref(grid), _1));
	ipaddr_->set_maximum_chars(max_ipv4_str_chars);
	ipaddr_->set_placeholder("192.168.1.109");
	ipaddr_->set_label(preferences::currentremote());

	tbutton* button = find_widget<tbutton>(&grid, "orientation", false, true);
	connect_signal_mouse_left_click(
				*button
			, std::bind(
			&thome::click_orientation
			, this, std::ref(*button)));
	button->set_label(preferences::landscape()? _("Landscape"): _("Portrait"));

	button = find_widget<tbutton>(&grid, "desktop", false, true);
	connect_signal_mouse_left_click(
				*button
			, std::bind(
			&thome::click_rdp
			, this, std::ref(*button)));
	button->set_active(can_rdp());

	ttoggle_button* toggle = find_widget<ttoggle_button>(&grid, "ratio_switchable", false, true);
	toggle->set_value(preferences::ratioswitchable());
	toggle->set_did_state_changed(std::bind(&thome::did_ratio_switchable_changed, this, _1));
	symbols.clear();
	symbols["item"] = game_config::screen_modes.find(screenmode_ratio)->second;
	toggle->set_label(vgettext2("When switching mode, can select '$item'", symbols));
	{
		int ii = 0;
		// toggle->set_label(null_str);
	}
}

void thome::pre_applet(tgrid& grid)
{
	tlistbox* list = find_widget<tlistbox>(&grid, "applets", false, true);
	list->enable_select(false);
	list->set_did_row_changed(std::bind(&thome::did_applet_changed, this, _1, _2));
	applets_widget_ = list;

	reload_applets(*list);
}

void thome::pre_more(tgrid& grid)
{
	std::stringstream ss;

	ss.str("");
	ss << " V" << game_config::version.str(true);
	find_widget<tlabel>(&grid, "version", false).set_label(ss.str());

	pre_magic(grid);
}

bool thome::did_navigation_pre_change(treport& report, ttoggle_button& from, ttoggle_button& to)
{
	if (from.at() == SCAN_LAYER) {
		scan_navigation_pre_change();

	} else if (from.at() == MORE_LAYER) {
		tgrid& grid = *body_widget_->layer(MORE_LAYER);
		find_widget<tgrid>(&grid, "generate_grid", false, true)->set_visible(twidget::INVISIBLE);
	}
	return true;
}

void thome::did_navigation_changed(treport& report, ttoggle_button& row)
{
	tgrid* current_layer = body_widget_->layer(row.at());
	body_widget_->set_radio_layer(row.at());

	current_layer_ = row.at();

	// left of title alwyas has 36x36 space, for keep, use twidget::HIDDEN not twidget::INVISIBLE.
	insert_widget_->set_visible(current_layer_ == APPLET_LAYER? twidget::VISIBLE: twidget::HIDDEN);

	if (row.at() == RDP_LAYER) {

	} else if (row.at() == SCAN_LAYER) {
		scan_navigation_changed();
	}
}

void thome::click_insert(tbutton& widget)
{
	if (current_layer_ != APPLET_LAYER) {
		return;
	}

	std::map<int, std::string> msgs;
	msgs.insert(std::make_pair(applet::type_distribution, _("Download from Applet Store")));
	msgs.insert(std::make_pair(applet::type_development, _("Download test version")));

	std::vector<gui2::tmenu::titem> items;
	for (std::map<int, std::string>::const_iterator it = msgs.begin(); it != msgs.end(); it ++) {
		std::string icon = it->first == applet::type_distribution? "misc/applet_store.png": "misc/bug.png";
		items.push_back(gui2::tmenu::titem(it->second, it->first, icon));
	}

	int type = nposm;
	{
		gui2::tmenu dlg(items, nposm);
		dlg.show(widget.get_x(), widget.get_y() + widget.get_height() + 16 * twidget::hdpi_scale);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		type = dlg.selected_val();
	}

	std::string bundleid;
	{
		std::string title = msgs.find(type)->second;
		gui2::tedit_box_param param(title, null_str, "xxx.xxx.xxx", bundleid, null_str, null_str, dgettext("rose-lib", "OK"), 20, gui2::tedit_box_param::show_cancel);
		param.did_text_changed = std::bind(&utils::is_rose_bundleid, _1, '.');
		{
			gui2::tedit_box dlg(param);
			dlg.show(nposm, window_->get_height() / 4);
			if (dlg.get_retval() != twindow::OK) {
				return;
			}
		}
		bundleid = param.result;
	}

	applet::tapplet net_applet;
	gui2::tprogress_default_slot slot(std::bind(&net::cswamp_getapplet, _1, type, bundleid, std::ref(net_applet), false));
	bool ret = gui2::run_with_progress(slot, null_str, _("Get applet"), 1);
	if (!ret) {
		return;
	}

	bool updated = false;
	for (std::vector<applet::tapplet>::iterator it = applets_.begin(); it != applets_.end(); ++ it) {
		applet::tapplet& aplt = *it;
		if ((aplt.type == applet::type_distribution || aplt.type == applet::type_development) && aplt.bundleid == bundleid) {
			updated = true;
			aplt = net_applet;
			break;
		} else if (aplt.type == applet::type_studio) {
			updated = true;
			applets_.insert(it, net_applet);
			break;
		}
	}
	if (!updated) {
		applets_.push_back(net_applet);
	}
	reload_applets(*applets_widget_);
}

void thome::reload_devices(tpbremotes& pbremotes)
{
	tlistbox* list = find_widget<tlistbox>(window_, "devices", false, true);

	std::map<std::string, std::string> data;
	for (std::set<tpbgroup>::const_iterator it = pbremotes.groups.begin(); it != pbremotes.groups.end(); ++ it) {
		const tpbgroup& group = *it;

		data.clear();
		data.insert(std::make_pair("name", group.name));
		data.insert(std::make_pair("description", group.uuid));
		list->insert_row(data);
		for (std::set<tpbdevice>::const_iterator it2 = group.devices.begin(); it2 != group.devices.end(); ++ it2) {
			const tpbdevice& device = *it2;
			data.clear();
			data.insert(std::make_pair("name", device.name));
			data.insert(std::make_pair("description", device.uuid));
			list->insert_row(data);
		}
	}
}

void thome::click_orientation(tbutton& widget)
{
	preferences::set_landscape(!preferences::landscape());
	widget.set_label(preferences::landscape()? _("Landscape"): _("Portrait"));
}

void thome::click_rdp(tbutton& widget)
{
	VALIDATE(can_rdp(), null_str);
	net::IPAddress address((const uint8_t*)&rdpcookie_.ipv4, 4);
	preferences::set_currentremote(address.ToString());
	window_->set_retval(DESKTOP);
}

void thome::did_ratio_switchable_changed(ttoggle_button& widget)
{
	preferences::set_ratioswitchable(widget.get_value());
}

bool thome::can_rdp() const
{
	return rdpcookie_.valid();
}

void thome::did_text_box_changed(tgrid& grid, ttext_box& widget)
{
	rdpcookie_.ipv4 = utils::to_ipv4(widget.label());

	tbutton* button = find_widget<tbutton>(&grid, "desktop", false, true);
	button->set_active(can_rdp());
}

void thome::reload_applets(tlistbox& list)
{
	list.clear();

	std::map<std::string, std::string> data;
	for (std::vector<applet::tapplet>::const_iterator it = applets_.begin(); it != applets_.end(); ++ it) {
		const applet::tapplet& applet = *it;

		data.clear();
		data.insert(std::make_pair("icon", applet.icon));
		data.insert(std::make_pair("label", applet.title + "-" + applet.version.str(true) + "(" + applet::srctypes.find(applet.type)->second + ")"));
		list.insert_row(data);
	}
}

void thome::did_applet_changed(tlistbox& list, ttoggle_panel& row)
{
	const int at = row.at();
	VALIDATE(at < (int)applets_.size(), null_str);

	window_->set_retval(APPLET0 + at);
}

// magic section ===
void thome::pre_magic(tgrid& grid)
{
	if (game_config::os == os_windows) {
		tbutton* button = find_widget<tbutton>(&grid, "login", false, true);
		connect_signal_mouse_left_click(
					*button
				, std::bind(
				&thome::click_login
				, this, std::ref(*button)));

		button = find_widget<tbutton>(&grid, "logout", false, true);
		connect_signal_mouse_left_click(
					*button
				, std::bind(
				&thome::click_logout
				, this, std::ref(*button)));

		button = find_widget<tbutton>(&grid, "syncapplet", false, true);
		connect_signal_mouse_left_click(
					*button
				, std::bind(
				&thome::click_syncapplets
				, this, std::ref(*button)));


		button = find_widget<tbutton>(&grid, "vericon", false, true);
/*
		connect_signal_mouse_left_click(
					*button
				, std::bind(
				&thome::click_magic_vericon
				, this, std::ref(*button)));
*/
	}

	find_widget<tgrid>(&grid, "generate_grid", false, true)->set_visible(twidget::INVISIBLE);
}

void thome::click_magic_vericon(tbutton& widget)
{
	VALIDATE(game_config::os == os_windows, null_str);

	tgrid& grid = *body_widget_->layer(MORE_LAYER);
	find_widget<tgrid>(&grid, "generate_grid", false, true)->set_visible(twidget::VISIBLE);
}

void thome::click_login(tbutton& widget)
{
	tgrid& grid = *body_widget_->layer(MORE_LAYER);

	bool ret;
	const std::string username = "ancientcc";
	const std::string deviceid = current_user.deviceid;

	net::tcswamp_login_result result;
	if (current_user.pwcookie.empty()) {
		// deviceid: a11ee49e9fe14e72a295e94d3263af73
		// pwcookie: 3ddab1aa88a84431b60322d77eb12ae0
		const std::string password = "123456";

		gui2::tprogress_default_slot slot(std::bind(&net::cswamp_login, _1, net::cswamp_login_type_password, 
			username, password, deviceid, "kdesktop(Win10)", false, std::ref(result)));
		ret = gui2::run_with_progress(slot, null_str, _("Login"), 1);
	} else {
		gui2::tprogress_default_slot slot(std::bind(&net::cswamp_login, _1, net::cswamp_login_type_cookie, 
			username, current_user.pwcookie, deviceid, "launcher(Win10)", false, std::ref(result)));
		ret = gui2::run_with_progress(slot, null_str, _("Login"), 1);
	}
	if (!ret) {
		return;
	}

	current_user.sessionid = result.sessionid;
	current_user.pwcookie = result.pwcookie;
	preferences::set_pwcookie(current_user.pwcookie);

	// find_widget<tbutton>(&grid, "logout", false, true)->set_active(true);
}

void thome::click_logout(tbutton& widget)
{
	VALIDATE(current_user.valid(), null_str);


	gui2::tprogress_default_slot slot(std::bind(&net::cswamp_logout, _1, current_user.sessionid, false));
	gui2::run_with_progress(slot, null_str, _("Logout"), 1);
	current_user.did_logout();
}

void thome::click_syncapplets(tbutton& widget)
{
	VALIDATE(current_user.valid(), null_str);

	std::vector<std::string> adds;
	adds.push_back("aplt.leagor.blesmart");
	adds.push_back("aplt.leagor.iaccess123");
	// adds.push_back("aplt.leagor.key");

	std::vector<std::string> removes;
	removes.push_back("aplt.leagor.key123");

	gui2::tprogress_default_slot slot(std::bind(&net::cswamp_syncappletlist, _1, current_user.sessionid, net::app_launcher, std::ref(adds), std::ref(removes), false));
	bool ret = gui2::run_with_progress(slot, null_str, null_str, 1);
	if (!ret) {
		return;
	}
}

// === magic section

void thome::app_timer_handler(uint32_t now)
{
	scan_timer_handler(now);
}

void thome::app_OnMessage(rtc::Message* msg)
{
	switch (msg->message_id) {
	case MSG_HOME_POPUP_WIFI_PASSWORD:
		scan_OnMessage(msg);
	}
	if (msg->pdata) {
		delete msg->pdata;
		msg->pdata = nullptr;
	}
}

} // namespace gui2

