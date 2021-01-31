#ifndef GUI_DIALOGS_ETHERNETIP_HPP_INCLUDED
#define GUI_DIALOGS_ETHERNETIP_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "gui/widgets/text_box2.hpp"
#include "ble2.hpp"

#include <net/base/ip_endpoint.h>

namespace gui2 {

class tbutton;

class tethernetip: public tdialog
{
public:
	explicit tethernetip(tble2& ble, tble2::tether3elem& ether3elem);

private:
	/** Inherited from tdialog. */
	void pre_show() override;

	/** Inherited from tdialog. */
	void post_show() override;

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	enum {etype_ip, etype_subnetmask, etype_gateway};
	void did_text_box_changed(ttext_box& widget, int type);
	void click_update(tbutton& widget);
	bool can_update() const;

private:
	tble2& ble_;
	tble2::tether3elem& ether3elem_;
	tbutton* update_widget_;

	ttext_box2* ipaddr_;
	ttext_box2* subnet_mask_;
	ttext_box2* gateway_;
};

} // namespace gui2

#endif

