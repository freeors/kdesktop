#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/ethernetip.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/window.hpp"
#include "gettext.hpp"

using namespace std::placeholders;

// @netmask: 192.168.1.1 => 0x0101a8c0
int prefixlen_from_netmask(uint32_t netmask)
{
	if (netmask == 0 || netmask == UINT32_MAX) {
		return nposm;
	}
	uint32_t ipv4 = SDL_Swap32(netmask);
	int last_1_at = nposm;
	bool found_zero = false;
	int at = 31;
	for (; at >= 0; at --) {
		if (!found_zero) {
			if ((ipv4 & (1u << at)) == 0) {
				found_zero = true;
				last_1_at = at + 1;
			}
		} else if ((ipv4 & (1u << at)) != 0) {
			return nposm;
		}
	}
	VALIDATE(at == -1, null_str);
	return 32 - last_1_at;
}

namespace gui2 {

REGISTER_DIALOG(kdesktop, ethernetip)

tethernetip::tethernetip(tble2& ble, tble2::tether3elem& ether3elem)
	: ble_(ble)
	, ether3elem_(ether3elem)
	, update_widget_(nullptr)
{
	ether3elem_.clear();
}

void tethernetip::pre_show()
{
	window_->set_escape_disabled(true);
	window_->set_label("misc/white_background.png");

	find_widget<tlabel>(window_, "title", false).set_label(_("Update IP"));

	tbutton* button = find_widget<tbutton>(window_, "update", false, true);
	connect_signal_mouse_left_click(
			*button
			, std::bind(
			&tethernetip::click_update
			, this, std::ref(*button)));
	update_widget_ = button;

	const int max_ipv4_str_chars = 3 + 1 + 3 + 1 + 3 + 1 + 3;
	// ip addr
	ipaddr_ = new ttext_box2(*window_, *find_widget<tcontrol>(window_, "ipaddr", false, true), "textbox", null_str);
	ipaddr_->set_did_text_changed(std::bind(&tethernetip::did_text_box_changed, this, _1, etype_ip));
	ipaddr_->text_box()->set_maximum_chars(max_ipv4_str_chars);
	ipaddr_->text_box()->set_placeholder("192.168.1.160");
	ipaddr_->text_box()->set_label("192.168.1.160");

	// subnet mask
	subnet_mask_ = new ttext_box2(*window_, *find_widget<tcontrol>(window_, "subnet_mask", false, true), "textbox", null_str);
	subnet_mask_->set_did_text_changed(std::bind(&tethernetip::did_text_box_changed, this, _1, etype_subnetmask));
	subnet_mask_->text_box()->set_maximum_chars(max_ipv4_str_chars);
	subnet_mask_->text_box()->set_placeholder("255.255.255.0");
	subnet_mask_->text_box()->set_label("255.255.255.0");

	// gateway
	gateway_  = new ttext_box2(*window_, *find_widget<tcontrol>(window_, "gateway", false, true), "textbox", null_str);
	gateway_->set_did_text_changed(std::bind(&tethernetip::did_text_box_changed, this, _1, etype_gateway));
	gateway_->text_box()->set_maximum_chars(max_ipv4_str_chars);
	gateway_->text_box()->set_placeholder("192.168.1.1");
	gateway_->text_box()->set_label("192.168.1.1");

	update_widget_->set_active(can_update());
}

void tethernetip::post_show()
{
/*
	ether3elem_.ipv4 = 0x7401a8c0; // 192.168.1.116
	ether3elem_.prefixlen = 24;
	ether3elem_.gateway = 0x0101a8c0; // 192.168.1.1
*/
}

void tethernetip::did_text_box_changed(ttext_box& widget, int type)
{
	if (type == etype_ip) {
		ether3elem_.ipv4 = utils::to_ipv4(widget.label());
	} else if (type == etype_subnetmask) {
		ether3elem_.prefixlen = prefixlen_from_netmask(utils::to_ipv4(subnet_mask_->text_box()->label()));
	} else {
		VALIDATE(type == etype_gateway, null_str);
		ether3elem_.gateway = utils::to_ipv4(gateway_->text_box()->label());
	}
	update_widget_->set_active(can_update());
}

void tethernetip::click_update(tbutton& widget)
{
	window_->set_retval(twindow::OK);
}

bool tethernetip::can_update() const
{
	return ether3elem_.valid();
}

} // namespace gui2

