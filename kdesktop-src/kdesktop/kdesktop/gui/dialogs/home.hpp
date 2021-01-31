#ifndef GUI_DIALOGS_HOME_HPP_INCLUDED
#define GUI_DIALOGS_HOME_HPP_INCLUDED

#include "gui/dialogs/dialog.hpp"
#include "gui/dialogs/scan.hpp"
#include "game_config.hpp"

#include "scripts/rose_lua_kernel.hpp"

#include "aplt_net.hpp"

namespace gui2 {

class tbutton;
class ttuggle_button;
class ttext_box;
class tgrid;
class treport;
class tstack;

class thome: public tdialog, public tscan
{
public:
	enum tresult {DESKTOP = 1, APPLET0 = 100};
	enum {RDP_LAYER, SCAN_LAYER, APPLET_LAYER, MORE_LAYER};
	explicit thome(tpbremotes& pbremotes, tble2& ble, std::vector<applet::tapplet>& applets, int startup_layer);
	~thome();

	const trdpcookie& rdpcookie() const { return rdpcookie_; }

private:
	/** Inherited from tdialog. */
	void pre_show() override;

	/** Inherited from tdialog. */
	void post_show() override;

	/** Inherited from tdialog, implemented by REGISTER_DIALOG. */
	virtual const std::string& window_id() const;

	void pre_rdp(tgrid& grid);
	void pre_applet(tgrid& grid);
	void pre_more(tgrid& grid);
	bool did_navigation_pre_change(treport& report, ttoggle_button& from, ttoggle_button& to);
	void did_navigation_changed(treport& report, ttoggle_button& row);
	void click_insert(tbutton& widget);
	// rdp layer
	void click_orientation(tbutton& widget);
	void click_rdp(tbutton& widget);

	void did_ratio_switchable_changed(ttoggle_button& widget);
	void did_text_box_changed(tgrid& grid, ttext_box& widget);

	void reload_devices(tpbremotes& pbremotes);
	bool can_rdp() const;

	// applet layer
	void reload_applets(tlistbox& list);
	void did_applet_changed(tlistbox& list, ttoggle_panel& row);

	// magic section
	void pre_magic(tgrid& grid);
	void click_login(tbutton& widget);
	void click_logout(tbutton& widget);
	void click_syncapplets(tbutton& widget);
	void click_magic_vericon(tbutton& widget);

	void app_timer_handler(uint32_t now) override;
	void app_OnMessage(rtc::Message* msg) override;

private:
	tpbremotes& pbremotes_;
	std::vector<applet::tapplet>& applets_;
	const int startup_layer_;
	tstack* body_widget_;
	tbutton* insert_widget_;
	int current_layer_;
	tlistbox* applets_widget_;

	ttext_box* ipaddr_;
	trdpcookie rdpcookie_;
};

} // namespace gui2

#endif

