/* Require Rose v1.0.19 or above. $ */

#define GETTEXT_DOMAIN "kdesktop-lib"

#include "base_instance.hpp"
#include "gui/dialogs/message.hpp"
#include "gui/dialogs/chat.hpp"
#include "gui/dialogs/home.hpp"
#include "gui/dialogs/scan.hpp"
#include "gui/dialogs/desktop.hpp"
#include "gui/dialogs/rexplorer.hpp"
#include "gui/widgets/window.hpp"
#include "game_end_exceptions.hpp"
#include "wml_exception.hpp"
#include "gettext.hpp"
#include "loadscreen.hpp"
#include "formula_string_utils.hpp"
#include "help.hpp"
#include "version.hpp"
#include "game_config.hpp"

#include "ble2.hpp"

#include "aplt_net.hpp"

class game_instance: public base_instance
{
public:
	game_instance(rtc::PhysicalSocketServer& ss, int argc, char** argv);

	tble2& ble() { return ble_; }
	tpbremotes& pbremotes() { return pbremotes_; }
	std::vector<applet::tapplet>& applets() { return applets_; }

	void makesure_deviceid();

private:
	void app_load_settings_config(const config& cfg) override;
	void app_pre_setmode(tpre_setmode_settings& settings) override;
	void app_load_pb() override;

private:
	tble2 ble_;
	tpbremotes pbremotes_;
};

game_instance::game_instance(rtc::PhysicalSocketServer& ss, int argc, char** argv)
	: base_instance(ss, argc, argv)
	, ble_(this)
{
}

void game_instance::app_load_settings_config(const config& cfg)
{
	VALIDATE(screenmode_min != screenmode_ratio, "Switch mode in rdp must not screenmode_ratio be min screen mode");
	game_config::screen_modes.insert(std::make_pair(screenmode_scale, _("Scale to screen")));
	game_config::screen_modes.insert(std::make_pair(screenmode_ratio, _("Keep aspect ratio")));
	game_config::screen_modes.insert(std::make_pair(screenmode_partial, _("Partial")));
	VALIDATE(game_config::screen_modes.size() == screenmode_count, null_str);

	game_config::version = version_info(cfg["version"].str());
	VALIDATE(game_config::version.is_rose_recommended(), null_str);
}

void game_instance::app_pre_setmode(tpre_setmode_settings& settings)
{	
	// settings.default_font_size = 18;
	settings.fullscreen = false;
	// settings.silent_background = false;
	settings.landscape = false;
	if (game_config::os == os_windows) {
		// settings.landscape = true;
		// settings.min_width = 480;
		// settings.min_height = 360;
	}
}

void game_instance::app_load_pb()
{
	pbremotes_.version = 1;
	pbremotes_.timestamp = time(nullptr);

	std::set<tpbgroup>& groups = pbremotes_.groups;

	std::set<tpbdevice> devices;
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "first_a", 0, 0));
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "second_b", 0, 0));
	std::pair<std::set<tpbgroup>::iterator, bool> ins = groups.insert(tpbgroup(SDL_CreateGUID(SDL_TRUE), "first", devices));
	VALIDATE(ins.second, null_str);

	devices.clear();
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "first_1", 0, 0));
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "second_2", 0, 0));
	ins = groups.insert(tpbgroup(SDL_CreateGUID(SDL_TRUE), "second", devices));
	VALIDATE(ins.second, null_str);

	devices.clear();
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "first_A", 0, 0));
	devices.insert(tpbdevice(SDL_CreateGUID(SDL_TRUE), "second_B", 0, 0));
	ins = groups.insert(tpbgroup(SDL_CreateGUID(SDL_TRUE), "third", devices));
	VALIDATE(ins.second, null_str);
	
		
/*
	const std::string src = game_config::path + "/" + game_config::generate_app_dir(game_config::app) + "/tflites";
	faceprint::copy_model_2_preferences(src);

	protobuf::load_sha1pb(EVENTS_PB, true);
	const int events_pb_version = 1;
	if (pb_events_.version() != events_pb_version || pb_events_.events_size() == 0) {
		pb_events_.set_version(events_pb_version);
		pb_events_.set_next_image_index(0);
	}

	protobuf::load_sha1pb(GAUTH_ACTIONS_PB, true);
	const int gauth_actions_pb_version = 1;
	if (pb_gauth_actions_.version() != gauth_actions_pb_version || pb_gauth_actions_.actions_size() == 0) {
		pb_gauth_actions_.set_version(gauth_actions_pb_version);
	}
*/
}

void game_instance::makesure_deviceid()
{
	current_user.deviceid = preferences::deviceid();
	if (!utils::is_guid(current_user.deviceid, false)) {
		current_user.deviceid = utils::create_guid(false);
		preferences::set_deviceid(current_user.deviceid);
	}
	current_user.pwcookie = preferences::pwcookie();
}

/**
 * Setups the game environment and enters
 * the titlescreen or game loops.
 */
static int do_gameloop(int argc, char** argv)
{
	SDL_SetHint(SDL_HINT_BLE, "1");

	rtc::PhysicalSocketServer ss;
	instance_manager<game_instance> manager(ss, argc, argv, "kdesktop", "#rose");
	game_instance& game = manager.get();

	game.makesure_deviceid();

	try {
		preferences::set_use_rose_keyboard(false);

		int startup_layer = gui2::thome::RDP_LAYER;
		for (; ;) {
			game.loadscreen_manager().reset();
			const font::floating_label_context label_manager;
			cursor::set(cursor::NORMAL);

			int res;
			trdpcookie rdpcookie;
			{
				gui2::thome dlg(game.pbremotes(), game.ble(), game.applets(), startup_layer);
				dlg.show();
				res = static_cast<gui2::thome::tresult>(dlg.get_retval());
				rdpcookie = dlg.rdpcookie();
				startup_layer = gui2::thome::RDP_LAYER;
			}

			if (res == gui2::thome::DESKTOP) {
				gui2::trexplorer::tentry extra(null_str, null_str, null_str);
				if (game_config::os == os_windows) {
					// extra = gui2::trexplorer::tentry(game_config::path + "/data/gui/default/scene", _("gui/scene"), "misc/dir_res.png");
				} else if (game_config::os == os_android) {
					extra = gui2::trexplorer::tentry("/sdcard", "/sdcard", "misc/dir_res.png");
				}

				VALIDATE(!game_config::fullscreen, null_str);
				instance->set_fullscreen(true);
				if (rdpcookie.landscape) {
					instance->set_orientation(base_instance::orientation_landscape, true);
				} else {
					instance->set_mode();
				}
				{
					// require first swip-up not result to enter background.
					thome_indicator_lock indicator_lock(2);
					gui2::tdesktop slot(rdpcookie);
					// since launcher has used unified rules for the all os, here according to it.
					gui2::trexplorer dlg(slot, null_str, extra, false, true); // game_config::mobile
					dlg.show();
					int res = dlg.get_retval();
					if (res != gui2::twindow::OK) {
						// continue;
					}
				}
				VALIDATE(game_config::fullscreen, null_str);
				instance->set_fullscreen(false);
				if (rdpcookie.landscape) {
					instance->set_orientation(base_instance::orientation_portrait, true);
				} else {
					instance->set_mode();
				}
			} else if (res >= gui2::thome::APPLET0) {
				const applet::tapplet& applet = game.applets()[res - gui2::thome::APPLET0];
				applet::texecutor executor(game.lua(), applet);
				executor.run();
				startup_layer = gui2::thome::APPLET_LAYER;
			}
		}

	} catch (twml_exception& e) {
		e.show();

	} catch (CVideo::quit&) {
		//just means the game should quit
		SDL_Log("SDL_main, catched CVideo::quit\n");

	} catch (game_logic::formula_error& e) {
		gui2::show_error_message(e.what());
	} 

	return 0;
}

int main(int argc, char** argv)
{
	try {
		do_gameloop(argc, argv);
	} catch (twml_exception& e) {
		// this exception is generated when create instance.
		e.show();
	}

	return 0;
}