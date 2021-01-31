#define GETTEXT_DOMAIN "kdesktop-lib"

#include "gui/dialogs/desktop.hpp"

#include "gui/widgets/label.hpp"
#include "gui/widgets/track.hpp"
#include "gui/widgets/listbox.hpp"
#include "gui/widgets/button.hpp"
#include "gui/widgets/stack.hpp"
#include "gui/widgets/window.hpp"
#include "gui/dialogs/menu.hpp"
#include "gui/dialogs/message.hpp"
#include "gettext.hpp"
#include "formula_string_utils.hpp"
#include "font.hpp"
#include "filesystem.hpp"
#include "base_instance.hpp"
#include <kosapi/gui.h>

using namespace std::placeholders;

#include <freerdp/channels/cliprdr_common2.hpp>

void did_h264_frame(void* custom, const uint8_t* data, size_t len)
{
/*
	if (game_config::os == os_windows) {
		static int time = 0;
		SDL_Log("#%u[%u]did_h264_frame, data: %p, len: %u", time ++, SDL_GetTicks(), data, (uint32_t)len);
	}
*/
	tmemcapture* accapture = reinterpret_cast<tmemcapture*>(custom);
	accapture->decode(0, data, len);
}

UINT wf_leagor_client_recv_explorer_update(LeagorCommonContext* context, const LEAGOR_EXPLORER_UPDATE* update)
{
	VALIDATE(!context->server, null_str);
	wfContext* wfc = (wfContext*)(context->rdpcontext);
	gui2::tdesktop* desktop = reinterpret_cast<gui2::tdesktop*>(wfc->desktop);
	desktop->did_recv_explorer_update(*update);

	return CHANNEL_RC_OK;
}

namespace gui2 {

tdesktop::tdesktop(const trdpcookie& rdpcookie)
	: rdpcookie_(rdpcookie)
	, notch_screen_(SDL_IsNotchScreen(get_sdl_window()) == SDL_TRUE)
	, notch_size_(SDL_GetNotchSizeRose())
	, gap_width_(1 * twidget::hdpi_scale)
	, paper_(nullptr)
	, setting_(false)
	, avcapture_ready_(false)
	, mainbar_y_offset_(16 * twidget::hdpi_scale)
	, rdp_context_(nullptr)
	, rdpd_crop_(empty_rect)
	, rdpc_rect_(empty_rect)
	, require_calculate_bar_xy_(true)
	, margin_height_(64 * twidget::hdpi_scale)
	, margining_(false)
	, main_bar_(nullptr)
	, minimap_bar_(nullptr)
	, moving_bar_(nullptr)
	, moving_btn_(nposm)
	, scrolling_bar_(nullptr)
	, will_longpress_ticks_(0)
	, screen_mode_(preferences::screenmode())
	, screen_mode_show_threshold_(5000) // 5 second
	, hide_screen_mode_ticks_(0)
	, ratio_switchable_(preferences::ratioswitchable())
	, visible_percent_(preferences::visiblepercent())
	, scroll_delta_(SDL_Point{0, 0})
	, minimap_dst_(empty_rect)
	, percent_rect_(empty_rect)
	, maybe_percent_(false)
	, bar_change_(nposm)
{
	VALIDATE(rdpcookie_.valid(), null_str);

	load_button_surface();

	bars_.push_back(tbar(bar_main, 48 * twidget::hdpi_scale));
	bars_.push_back(tbar(bar_minimap, 152 * twidget::hdpi_scale));
	VALIDATE((int)bars_.size() == bar_count, null_str);

	for (int at = 0; at < (int)bars_.size(); at ++) {
		tbar& bar = bars_.at(at);

		if (bar.type == bar_main) {
			bar.btns.push_back(btn_off);

		} else if (bar.type == bar_minimap) {
			bar.btns.push_back(btn_minimap);
		}

		refresh_bar(bar);
	}
	calculate_bar_ptr();
	VALIDATE(minimap_bar_->btns.size() == 1, null_str);
}

void tdesktop::rexplorer_pre_show(twindow& window)
{
	if (notch_size_.x != 0 || notch_size_.y != 0) {
		const int left = notch_size_.x != 0? notch_size_.x: nposm;
		const int right = notch_size_.y != 0? notch_size_.y: nposm;
		window_->set_margin(left, right, nposm, nposm);
	}

	tpanel* panel = find_widget<tpanel>(explorer_grid_, "statusbar_panel", false, true);
	panel->set_visible(twidget::INVISIBLE);

	main_stack_->set_did_pip_top_grid_rect(*explorer_grid_, std::bind(&tdesktop::did_pip_top_grid_rect, this, _1, _2, _3, _4));
	if (!drag_copy_.explorering) {
		explorer_grid_->set_visible(twidget::INVISIBLE);
	}

	lt_stack_->set_radio_layer(trexplorer::LT_LABEL_LAYER);
	rexplorer_pre_lt_label(*lt_stack_->layer(trexplorer::LT_LABEL_LAYER));

	paper_ = find_widget<ttrack>(window_, "paper", false, true);
	paper_->disable_rose_draw_bg();
	paper_->set_did_draw(std::bind(&tdesktop::did_draw_paper, this, _1, _2, _3));
	paper_->set_did_create_background_tex(std::bind(&tdesktop::did_create_background_tex, this, _1, _2));
	paper_->set_did_mouse_leave(std::bind(&tdesktop::did_mouse_leave_paper, this, _1, _2, _3));
	paper_->set_did_mouse_motion(std::bind(&tdesktop::did_mouse_motion_paper, this, _1, _2, _3));
	paper_->set_did_left_button_down(std::bind(&tdesktop::did_left_button_down, this, _1, _2));
	paper_->set_did_right_button_up(std::bind(&tdesktop::did_right_button_up, this, _1, _2));
	paper_->connect_signal<event::LONGPRESS>(
		std::bind(
			&tdesktop::signal_handler_longpress_paper
			, this
			, _4, _5)
		, event::tdispatcher::back_child);

	paper_->connect_signal<event::LEFT_BUTTON_UP>(std::bind(
				&tdesktop::signal_handler_left_button_up, this, _5));

	paper_->connect_signal<event::RIGHT_BUTTON_DOWN>(std::bind(
				&tdesktop::signal_handler_right_button_down
					, this, _3, _4, _5));

	paper_->connect_signal<event::WHEEL_UP>(
		std::bind(
				&tdesktop::signal_handler_sdl_wheel_up
			, this
			, _3
			, _6)
		, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_DOWN>(
			std::bind(
				&tdesktop::signal_handler_sdl_wheel_down
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_LEFT>(
			std::bind(
				&tdesktop::signal_handler_sdl_wheel_left
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);

	paper_->connect_signal<event::WHEEL_RIGHT>(
			std::bind(
				&tdesktop::signal_handler_sdl_wheel_right
				, this
				, _3
				, _6)
			, event::tdispatcher::back_child);
}

void tdesktop::rexplorer_post_show()
{
	stop_avcapture();
	paper_ = nullptr;
}

void tdesktop::rexplorer_first_drawn()
{
	start_avcapture();

	VALIDATE(avcapture_.get() != nullptr, null_str);

	// @ip1: 192.168.1.1 => 0x0101a8c0
	rdp_manager_.reset(new net::trdp_manager);
	// 192.168.1.113 ==> 0x7101a8c0
	// 192.168.1.109 ==> 0x6d01a8c0
	uint32_t ip = rdpcookie_.ipv4;
	rdp_manager_->start(ip, 3389, avcapture_.get(), this, *rexplorer_, rexplorer_->send_helper());
}

void tdesktop::rexplorer_resize_screen()
{
	for (int at = 0; at < (int)bars_.size(); at ++) {
		tbar& bar = bars_.at(at);
		bar.tex = nullptr;
	}

	require_calculate_bar_xy_ = true;
}

void tdesktop::start_avcapture()
{
	VALIDATE(avcapture_.get() == nullptr, null_str);
	VALIDATE(rdp_manager_.get() == nullptr, null_str);

	tsetting_lock setting_lock(*this);
/*
	threading::lock lock(recognition_mutex_);

	memset(&nocamera_reporter_, 0, sizeof(nocamera_reporter_));
	memset(http_xmiter_, 0, sizeof(http_xmiter_));
	memset(compare_card_fails_, 0, sizeof(compare_card_fails_));
	camera_directs_.clear();
*/
	VALIDATE(startup_msgs_.empty(), null_str);
	VALIDATE(!avcapture_ready_, null_str);
	VALIDATE(startup_msgs_tex_.get() == nullptr, null_str);
	
	std::vector<trtsp_settings> rtsps;
	trtsp_settings settings("rdp", true, tmemcapture::mem_url);
	rtsps.push_back(settings);

	insert_startup_msg(SDL_GetTicks(), _("Start decoder"), false);
	avcapture_.reset(new tmemcapture(0, *rexplorer_, *this, rtsps, true, tpoint(nposm, nposm), true));
	// remote vidcap isn't enumulated during tavcapture's contructor, so use rtsps.size().

	paper_->set_timer_interval(30);
}

void tdesktop::rexplorer_pre_lt_label(tgrid& grid)
{
	find_widget<tlabel>(&grid, "lt_major_label", false, true)->set_label(_("Local"));

	std::string minor_label = "Unknown";
	if (game_config::os == os_windows) {
		minor_label = "Windows";
	} else if (game_config::os == os_android) {
		minor_label = "Android";
	} else if (game_config::os == os_ios) {
		minor_label = "iOS";
	}
	find_widget<tlabel>(&grid, "lt_minor_label", false, true)->set_label(minor_label);
}

void tdesktop::load_button_surface()
{
	VALIDATE(btn_surfs_.empty(), null_str);
	surface surf = image::get_image("misc/crop.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_mode, surf));

	surf = image::get_image("misc/off.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_off, surf));

	surf = image::get_image("misc/margin.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_margin, surf));
/*
	surf = image::get_image("misc/minimap.png");
	VALIDATE(surf.get() != nullptr, null_str);
	btn_surfs_.insert(std::make_pair(btn_minimap, surf));
*/
	VALIDATE((int)btn_surfs_.size() == btn_count - 2, null_str);
}

void tdesktop::create_move_tex()
{
	VALIDATE(move_tex_.get() == nullptr, null_str);
	surface surf = image::get_image("misc/move.png");
	VALIDATE(surf.get() != nullptr, null_str);
	move_tex_ = SDL_CreateTextureFromSurface(get_renderer(), surf);
}

void tdesktop::create_mask_tex()
{
	VALIDATE(mask_tex_.get() == nullptr, null_str);
	surface mask_surf = create_neutral_surface(128 * twidget::hdpi_scale, 128 * twidget::hdpi_scale);
	fill_surface(mask_surf, 0x80000000);
	// adjust_surface_rect_alpha2(mask_surf, 0, ::create_rect(rect.x - video_dst.x, rect.y - video_dst.y, rect.w, rect.h), true);
	mask_tex_ = SDL_CreateTextureFromSurface(get_renderer(), mask_surf);
}

void tdesktop::refresh_bar(tbar& bar)
{
	VALIDATE(bar.size > 0 && bar.delta.x == 0 && bar.delta.y == 0, null_str);
	// VALIDATE(!bar.btns.empty(), null_str);

	bar.surf = nullptr;
	bar.tex = nullptr;

	if (bar.btns.empty()) {
		VALIDATE(bar.type != bar_minimap, null_str);
		bar.rect = ::create_rect(nposm, nposm, 0, 0);
		bar.visible = false;

	} else if (bar.type != bar_minimap) {
		VALIDATE(!bar.btns.empty(), null_str);
		surface bg_surf = create_neutral_surface(bar.btns.size() * bar.size + (bar.btns.size() - 1) * bar.gap, bar.size);
		VALIDATE(bg_surf.get(), null_str);

		const uint32_t bg_color = 0xffffc04d;
		SDL_Rect dst_rect{0, 0, bg_surf->w, bg_surf->h};
		sdl_fill_rect(bg_surf, &dst_rect, bg_color);
			
		int xoffset = 0;
		for (std::vector<int>::const_iterator it = bar.btns.begin(); it != bar.btns.end(); ++ it, xoffset += bar.size + bar.gap) {
			surface surf = btn_surfs_.find(*it)->second;
			VALIDATE(surf.get(), null_str);
			surf = scale_surface(surf, bar.size, bar.size);
			SDL_Rect dst_rect{xoffset, 0, surf->w, surf->h};
			sdl_blit(surf, nullptr, bg_surf, &dst_rect);
		}

		bar.rect = ::create_rect(nposm, nposm, bg_surf->w, bg_surf->h);
		VALIDATE(bar.rect.h == bar.size, null_str);
		bar.visible = true;
		bar.surf = bg_surf; 

	} else {
		bar.rect = ::create_rect(nposm, nposm, bar.size, bar.size);
		VALIDATE(bar.rect.h == bar.size, null_str);
		bar.visible = screen_mode_ == screenmode_partial;
	}
}

void tdesktop::calculate_bar_ptr()
{
	main_bar_ = nullptr;
	minimap_bar_ = nullptr;

	for (int at = 0; at < (int)bars_.size(); at ++) {
		tbar& bar = bars_.at(at);

		if (bar.type == bar_main) {
			main_bar_ = &bar;

		} else if (bar.type == bar_minimap) {
			minimap_bar_ = &bar;
		}
	}
	VALIDATE(main_bar_ != nullptr && minimap_bar_ != nullptr, null_str);
}

void tdesktop::click_visible_percent(int x, int y)
{
	VALIDATE(rdp_manager_ != nullptr && screen_mode_ == screenmode_partial, null_str);
	tsend_suppress_output_lock suppress_output_lock(rdp_context_);

	std::vector<int> percents;
	percents.push_back(95);
	percents.push_back(90);
	percents.push_back(85);
	percents.push_back(80);
	percents.push_back(60);
	percents.push_back(50);
	percents.push_back(25);

	std::vector<gui2::tmenu::titem> items;
	int initial = nposm;

	std::stringstream ss;
	for (std::vector<int>::const_iterator it = percents.begin(); it != percents.end(); ++ it) {
		int percent = *it;
		ss.str("");
		ss << percent << '%';
		items.push_back(gui2::tmenu::titem(ss.str(), percent));
		if (percent == visible_percent_) {
			initial = percent;
		}
	}
	
	int selected;
	{
		gui2::tmenu dlg(items, initial);
		dlg.show(x, y);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		selected = dlg.selected_val();
	}

	preferences::set_visiblepercent(selected);
	visible_percent_ = selected;

	rdpc_rect_ = empty_rect;
	 // make recalculate
	rdpd_crop_ = empty_rect;
	percent_tex_ = nullptr;
}

void tdesktop::handle_bar_changed(int type)
{
	VALIDATE(type >= barchg_min && type < barchg_count, null_str);
	VALIDATE(bar_change_ == nposm || bar_change_ == type, null_str);

	std::vector<int> desire_btns;
	if (type == barchg_connectionfinished) {
		desire_btns.push_back(btn_mode);
		desire_btns.push_back(btn_off);
		desire_btns.push_back(btn_margin);

	} else if (type == barchg_closed) {

		desire_btns.push_back(btn_off);
	}

	tbar& bar = *main_bar_;
	if (bar.btns == desire_btns) {
		return;
	}
	bar.btns = desire_btns;
	refresh_bar(bar);

	calculate_bar_ptr();
	require_calculate_bar_xy_ = true;
	bar_change_ = nposm;
}

bool tdesktop::btn_in_current_bar(int btn) const
{
	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		for (std::vector<int>::const_iterator it2 = bar.btns.begin(); it2 != bar.btns.end(); ++ it2) {
			int that = *it2;
			if (that == btn) {
				return true;
			}
		}
	}
	return false;
}

void tdesktop::did_rdp_client_connectionfinished()
{
	VALIDATE(rdp_context_ == nullptr, null_str);
	rdp_context_ = &rdp_manager_->rdp_client().rdp_context;
	if (scrolling_bar_ == nullptr && moving_bar_ == nullptr) {
		handle_bar_changed(barchg_connectionfinished);
	} else {
		bar_change_ = barchg_connectionfinished;
	}
}

void tdesktop::did_rdp_client_reset()
{
	rdp_context_ = nullptr;
	rdpc_rect_ = empty_rect;
	// avcapture_ready_ = false;

	if (drag_copy_.explorering) {
		LEAGOR_EXPLORER_UPDATE update;
		memset(&update, 0, sizeof(update));
		update.code = LG_EXPLORER_CODE_HIDDEN;
		did_recv_explorer_update(update);

	} else {
		if (scrolling_bar_ == nullptr && moving_bar_ == nullptr) {
			handle_bar_changed(barchg_closed);
		} else {
			bar_change_ = barchg_closed;
		}
	}
}

void tdesktop::stop_avcapture()
{
	VALIDATE(window_ != nullptr, null_str);

	did_rdp_client_reset();
	rexplorer_->send_helper().clear_msg();
	rdp_manager_.reset();

	// It's almost impossible, but avoid to stop avcapture maybe spend more wdg_timer_s + 2.
	{
		tsetting_lock setting_lock(*this);
		// threading::lock lock(recognition_mutex_);
		avcapture_.reset();
		paper_->set_timer_interval(0);

		startup_msgs_.clear();
		avcapture_ready_ = false;
		startup_msgs_tex_ = nullptr;
	}
	// executor_.reset();
}

void tdesktop::did_h264_frame(const uint8_t* data, size_t len)
{
	avcapture_->decode(0, data, len);
}

void tdesktop::insert_startup_msg(uint32_t ticks, const std::string& msg, bool fail)
{
	VALIDATE_IN_MAIN_THREAD();

	startup_msgs_.push_back(tmsg(ticks, msg, fail));
	create_startup_msgs_tex(get_renderer());
}

void tdesktop::create_startup_msgs_tex(SDL_Renderer* renderer)
{
	if (startup_msgs_.empty()) {
		startup_msgs_tex_ = nullptr;
		return;
	}

	std::stringstream ss;
	uint32_t first_ticks = UINT32_MAX;
	for (std::vector<tmsg>::const_iterator it = startup_msgs_.begin(); it != startup_msgs_.end(); ++ it) {
		const tmsg& msg = *it;
		if (first_ticks == UINT32_MAX) {
			first_ticks = msg.ticks;
		}
		if (!ss.str().empty()) {
			ss << "\n";
		}
		ss << format_elapse_smsec(msg.ticks - first_ticks, false) << " ";
		if (msg.fail) {
			ss << ht::generate_format(msg.msg, color_to_uint32(font::BAD_COLOR));
		} else {
			ss << msg.msg;
		}
	}

	const int font_size = font::SIZE_SMALLER;
	surface text_surf = font::get_rendered_text(ss.str(), 0, font_size, font::BLACK_COLOR);
	startup_msgs_tex_ = SDL_CreateTextureFromSurface(renderer, text_surf);
}

std::vector<trtc_client::tusing_vidcap> tdesktop::app_video_capturer(int id, bool remote, const std::vector<std::string>& device_names)
{
	VALIDATE(remote && device_names.size() == 1, null_str);

	std::vector<trtc_client::tusing_vidcap> ret;
	for (std::vector<std::string>::const_iterator it = device_names.begin(); it != device_names.end(); ++ it) {
		const std::string& name = *it;
		ret.push_back(trtc_client::tusing_vidcap(name, false));
	}
	return ret;
}

void tdesktop::app_handle_notify(int id, int ncode, int at, int64_t i64_value, const std::string& str_value)
{
	VALIDATE(window_ != nullptr, null_str);
	VALIDATE(window_->drawn(), null_str);

	std::string msg;
	utils::string_map symbols;

	uint32_t now = SDL_GetTicks();
	int net_errcode = nposm;
	bool fail = false;
	if (ncode == trtc_client::ncode_startlive555finished) {
		if (at != nposm) {
			fail = i64_value == 0;
			symbols["result"] = fail? _("Fail"): _("Success");
			msg = vgettext2("Start decoder $result", symbols);
		} else {
			msg = vgettext2("Decoder is ready", symbols);
			avcapture_ready_ = true;
		}
	}

	if (!msg.empty()) {
		insert_startup_msg(SDL_GetTicks(), msg, fail);
		paper_->immediate_draw();
	}
}

void tdesktop::render_remote(SDL_Renderer* renderer, SDL_Texture* tex, int tex_width, int tex_height, const SDL_Rect& draw_rect)
{
	SDL_Rect srcrect{0, 0, tex_width, tex_height};
	SDL_Rect dstrect = draw_rect;

	if (drag_copy_.explorering && drag_copy_.vratio > 0) {
		const uint32_t argb = 0x80a0a0a0; // 0xff808080
		SDL_Rect gap_rect = ::create_rect(draw_rect.x + draw_rect.w / 2, draw_rect.y, gap_width_, draw_rect.h);
		render_rect(renderer, gap_rect, argb);

/*
		render_line(renderer, 0xff808080, draw_rect.x + draw_rect.w / 2, draw_rect.y,
			draw_rect.x + draw_rect.w / 2, draw_rect.y + draw_rect.h);
*/

		float vratio = drag_copy_.vratio;
		VALIDATE(vratio > 0, null_str);

		int draw_statusbar_height = 0;
		int draw_navigation_height = 0;
		// statusbar
		if (drag_copy_.statusbar_height != 0) {
			srcrect = ::create_rect(0, 0, tex_width, drag_copy_.statusbar_height);
			draw_statusbar_height = drag_copy_.statusbar_height * vratio;
			dstrect = ::create_rect(draw_rect.x, 0, draw_rect.w, draw_statusbar_height);
			SDL_RenderCopy(renderer, tex, &srcrect, &dstrect);
		}
		// navigation
		if (drag_copy_.navigation_height != 0) {
			srcrect = ::create_rect(0, tex_height - drag_copy_.navigation_height, tex_width, drag_copy_.navigation_height);
			draw_navigation_height = drag_copy_.navigation_height * vratio;
			dstrect = ::create_rect(draw_rect.x, draw_rect.h - draw_navigation_height, draw_rect.w, draw_navigation_height);
			SDL_RenderCopy(renderer, tex, &srcrect, &dstrect);
		}
		
		// middle part
		srcrect = ::create_rect(0, drag_copy_.statusbar_height, tex_width / 2, tex_height - drag_copy_.statusbar_height - drag_copy_.navigation_height);
		dstrect = ::create_rect(draw_rect.x, draw_statusbar_height, draw_rect.w / 2, draw_rect.h - draw_statusbar_height - draw_navigation_height);
	}
	SDL_RenderCopy(renderer, tex, &srcrect, &dstrect);
}

void tdesktop::did_draw_slice(int id, SDL_Renderer* renderer, trtc_client::VideoRenderer** locals, int locals_count, trtc_client::VideoRenderer** remotes, int remotes_count, const SDL_Rect& draw_rect)
{
	VALIDATE(draw_rect.w > 0 && draw_rect.h > 0, null_str);
	VALIDATE(locals_count == 0 && remotes_count == 1, null_str);

	trtc_client::VideoRenderer* sink = remotes[0];
	VALIDATE(sink->frames > 0, null_str);

	SDL_Rect dst = draw_rect;

	if (SDL_RectEmpty(&rdpc_rect_)) {
		hide_screen_mode_ticks_ = SDL_GetTicks() + screen_mode_show_threshold_;
	}
	//
	// renderer "background" section
	//
	if (!drag_copy_.explorering) {
		SDL_Rect draw_rect2{draw_rect.x, margining_? draw_rect.y + margin_height_: draw_rect.y,
			draw_rect.w, margining_? draw_rect.h - 2 * margin_height_: draw_rect.h};

		SDL_Rect crop_rect {0, 0, sink->app_width_, sink->app_height_};
		dst = draw_rect2;

		float widget_per_desktop = 0;
		if (screen_mode_ == screenmode_ratio) {
			tpoint ratio_size(draw_rect2.w, draw_rect2.h);
			ratio_size = calculate_adaption_ratio_size(draw_rect2.w, draw_rect2.h, sink->app_width_, sink->app_height_);
			dst = ::create_rect(draw_rect2.x + (draw_rect2.w - ratio_size.x) / 2, draw_rect2.y + (draw_rect2.h - ratio_size.y) / 2, ratio_size.x, ratio_size.y);

		} else if (screen_mode_ == screenmode_partial) {
			float ratio_hori = 1.0 * draw_rect2.w / sink->app_width_;
			float ratio_vert = 1.0 * draw_rect2.h / sink->app_height_;
			crop_rect = rdpd_crop_;
			if (ratio_hori <= ratio_vert) {
				// cut 90% from horizontal axis
				widget_per_desktop = 100.0 * draw_rect2.w / (sink->app_width_ * visible_percent_);
				int require_draw_h = 1.0 * sink->app_height_ * widget_per_desktop;
				if (crop_rect.w == 0) {
					crop_rect.x = 1.0 * sink->app_width_ * (100 - visible_percent_) / 200;
					crop_rect.w = sink->app_width_ - crop_rect.x * 2;
				}
				if (require_draw_h <= draw_rect2.h) {
					if (crop_rect.h == 0) {
						crop_rect.y = 0;
						crop_rect.h = sink->app_height_;
					}

					dst.h = require_draw_h;
					dst.y = draw_rect2.y + (draw_rect2.h - require_draw_h) / 2;

				} else {
					if (crop_rect.h == 0) {
						crop_rect.h = draw_rect2.h / widget_per_desktop;
						crop_rect.y = (sink->app_height_ - crop_rect.h) / 2;
					}
				}
				
			} else {
				// cut 90% from vertical axis
				widget_per_desktop = 100.0 * draw_rect2.h / (sink->app_height_ * visible_percent_);
				int require_draw_w = 1.0 * sink->app_width_ * widget_per_desktop;
				if (crop_rect.h == 0) {
					crop_rect.y = 1.0 * sink->app_height_ * (100 - visible_percent_) / 200;
					crop_rect.h = sink->app_height_ - crop_rect.y * 2;
				}
				if (require_draw_w <= draw_rect2.w) {
					if (crop_rect.w == 0) {
						crop_rect.x = 0;
						crop_rect.w = sink->app_width_;
					}

					dst.w = require_draw_w;
					dst.x = draw_rect2.x + (draw_rect2.w - require_draw_w) / 2;

				} else {
					if (crop_rect.w == 0) {
						crop_rect.w = draw_rect2.w / widget_per_desktop;
						crop_rect.x = (sink->app_width_ - crop_rect.w) / 2;
					}
				}
			}
			
		}
		rdpd_crop_ = crop_rect;
		rdpc_rect_ = dst;

		if (scrolling_bar_ != nullptr && screen_mode_ == screenmode_partial) {
			VALIDATE(widget_per_desktop != 0, null_str);
			// x axis
			scroll_delta_.x = scrolling_bar_->delta.x / widget_per_desktop;
			if (crop_rect.x + scroll_delta_.x < 0) {
				scroll_delta_.x = 0 - crop_rect.x;
			} else if (crop_rect.x + scroll_delta_.x + crop_rect.w > sink->app_width_) {
				scroll_delta_.x = sink->app_width_ - crop_rect.x - crop_rect.w;
			}
			crop_rect.x += scroll_delta_.x;

			// y axis
			scroll_delta_.y = scrolling_bar_->delta.y / widget_per_desktop;
			if (crop_rect.y + scroll_delta_.y < 0) {
				scroll_delta_.y = 0 - crop_rect.y;
			} else if (crop_rect.y + scroll_delta_.y + crop_rect.h > sink->app_height_) {
				scroll_delta_.y = sink->app_height_ - crop_rect.y - crop_rect.h;
			}
			crop_rect.y += scroll_delta_.y;
		}

		SDL_RendererFlip flip = SDL_FLIP_NONE; // SDL_FLIP_HORIZONTAL: SDL_FLIP_NONE;
		SDL_RenderCopyEx(renderer, sink->tex_.get(), &crop_rect, &dst, 0, NULL, flip);

	} else {
		VALIDATE(screen_mode_ == screenmode_scale, null_str);
		VALIDATE(!margining_, null_str);

		dst = draw_rect;
		rdpd_crop_ = ::create_rect(0, 0, sink->app_width_, sink->app_height_);
		rdpc_rect_ = dst;

		render_remote(renderer, sink->tex_.get(), sink->app_width_, sink->app_height_, dst);
	}

	if (rdp_context_ == nullptr) {
		// The connection was successful and is now disconnected
		if (mask_tex_.get() == nullptr) {
			create_mask_tex();
		}
		if (mask_tex_.get() != nullptr) {
			dst = draw_rect;
			SDL_RenderCopy(renderer, mask_tex_.get(), NULL, &dst);
		}
	}

	const uint32_t now = SDL_GetTicks();
	if (scrolling_bar_ != nullptr && will_longpress_ticks_ != 0 && now >= will_longpress_ticks_) {
		// if not motion, did_mouse_motion_paper cannot be called, so place in this timer handler.
		VALIDATE(moving_bar_ == nullptr, null_str);
		VALIDATE(moving_btn_ == nposm, null_str);
		moving_bar_ = scrolling_bar_;
		moving_btn_ = moving_bar_->btns.back();

		scrolling_bar_->delta.y = 0;
		scrolling_bar_ = nullptr;
		will_longpress_ticks_ = 0;
		maybe_percent_ = false;
	}

	if (!require_calculate_bar_xy_ && minimap_bar_->visible) {
		// mini desktop
		tpoint ratio_size(minimap_bar_->rect.w, minimap_bar_->rect.h);
		ratio_size = calculate_adaption_ratio_size(minimap_bar_->rect.w, minimap_bar_->rect.h, sink->app_width_, sink->app_height_);
		SDL_Rect desktop_dst = ::create_rect(minimap_bar_->rect.x + (minimap_bar_->rect.w - ratio_size.x) / 2, 
			minimap_bar_->rect.y + (minimap_bar_->rect.h - ratio_size.y) / 2, ratio_size.x, ratio_size.y);

		if (moving_bar_ == minimap_bar_) {
			desktop_dst.x += minimap_bar_->delta.x;
		}
		SDL_RenderCopyEx(renderer, sink->tex_.get(), nullptr, &desktop_dst, 0, NULL, SDL_FLIP_NONE);

		minimap_dst_ = desktop_dst;

		// widget background
		dst = minimap_dst_;
		render_rect_frame(renderer, dst, 0xffff0000, 1);

		bool show_percent = true;
		if (show_percent) {
			if (percent_tex_.get() == nullptr) {
				std::stringstream msg_ss;
				msg_ss << visible_percent_ << '%';
				surface surf = font::get_rendered_text(msg_ss.str(), 0, font::SIZE_LARGEST + 10 * twidget::hdpi_scale, font::BLUE_COLOR);
				percent_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
			}

			if (percent_tex_.get() != nullptr) {
				int width2, height2;
				SDL_QueryTexture(percent_tex_.get(), NULL, NULL, &width2, &height2);

				dst.x = minimap_dst_.x + minimap_dst_.w - width2;
				dst.y = minimap_dst_.y;
				dst.w = width2;
				dst.h = height2;
				SDL_RenderCopy(renderer, percent_tex_.get(), nullptr, &dst);
				percent_rect_ = dst;
			}
		}

		// croped frame
		float ratio = 1.0 * ratio_size.x / sink->app_width_;
		dst.x = desktop_dst.x + ratio * (rdpd_crop_.x + scroll_delta_.x);
		dst.w = ratio * rdpd_crop_.w;
		dst.y = desktop_dst.y + ratio * (rdpd_crop_.y + scroll_delta_.y);
		dst.h = ratio * rdpd_crop_.h;
		render_rect_frame(renderer, dst, 0xff00ff00, 1);

		if (moving_bar_ == minimap_bar_) {
			if (move_tex_.get() == nullptr) {
				create_move_tex();
			}
			if (move_tex_.get() != nullptr) {
				int width2, height2;
				SDL_QueryTexture(move_tex_.get(), NULL, NULL, &width2, &height2);

				dst = minimap_dst_;
				dst.w = width2;
				dst.h = height2;
				SDL_RenderCopy(renderer, move_tex_.get(), nullptr, &dst);
			}
		}
	}
}

texture tdesktop::did_create_background_tex(ttrack& widget, const SDL_Rect& draw_rect)
{
	surface bg_surf = create_neutral_surface(draw_rect.w, draw_rect.h);
	uint32_t bg_color = 0xff303030;
	fill_surface(bg_surf, bg_color);

	return SDL_CreateTextureFromSurface(get_renderer(), bg_surf.get());
}

void tdesktop::did_draw_paper(ttrack& widget, const SDL_Rect& draw_rect, const bool bg_drawn)
{
	SDL_Renderer* renderer = get_renderer();

	if (!bg_drawn) {
		// SDL_RenderCopy(renderer, widget.background_texture().get(), NULL, &draw_rect);
		render_remote(renderer, widget.background_texture().get(), draw_rect.w, draw_rect.h, draw_rect);
	}

	if (avcapture_ready_) {
		avcapture_->draw_slice(renderer, draw_rect);
	}

	SDL_Rect dstrect;
	if ((SDL_RectEmpty(&rdpc_rect_) || rdp_context_ == nullptr) && startup_msgs_tex_.get()) {
		int width2, height2;
		SDL_QueryTexture(startup_msgs_tex_.get(), NULL, NULL, &width2, &height2);

		dstrect = ::create_rect(draw_rect.x, draw_rect.y + draw_rect.h - height2 - 64 * twidget::hdpi_scale, width2, height2);
		SDL_RenderCopy(renderer, startup_msgs_tex_.get(), nullptr, &dstrect);
	}

	if (require_calculate_bar_xy_) {
		for (int at = 0; at < (int)bars_.size(); at ++) {
			tbar& bar = bars_.at(at);
			if (bar.btns.empty()) {
				continue;
			}
			VALIDATE(bar.rect.w > 0 && bar.rect.w > 0, null_str);
			VALIDATE(bar.tex.get() == nullptr, null_str);

			if (bar.type == bar_main) {
				bar.rect.x = draw_rect.x + preferences::mainbarx();
				if (bar.rect.x < draw_rect.x || bar.rect.x + bar.rect.w > draw_rect.x + draw_rect.w) {
					bar.rect.x = draw_rect.x;
				}
				bar.rect.y = draw_rect.y + mainbar_y_offset_;
				
			} else if (bar.type == bar_minimap) {
				bar.rect.x = draw_rect.x + preferences::minimapbarx();
				if (bar.rect.x < draw_rect.x || bar.rect.x + bar.rect.w > draw_rect.x + draw_rect.w) {
					bar.rect.x = draw_rect.x + (draw_rect.w - bar.rect.w) / 2;
				}
				bar.rect.y = draw_rect.y + (draw_rect.h - bar.rect.h) / 2;
			}
			if (bar.surf.get() != nullptr) {
				bar.tex = SDL_CreateTextureFromSurface(get_renderer(), bar.surf);
			}
		}
		require_calculate_bar_xy_ = false;
	}


	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible || bar.type == bar_minimap) {
			continue;
		}
		dstrect = bar.rect;
		dstrect.x += bar.delta.x;
		SDL_RenderCopy(renderer, bar.tex.get(), nullptr, &dstrect);
	}

	const uint32_t now = SDL_GetTicks();
	if (hide_screen_mode_ticks_ != 0 && now < hide_screen_mode_ticks_) {
		utils::string_map symbols;
		std::string msg = game_config::screen_modes.find(screen_mode_)->second;
		if (screen_mode_ == screenmode_partial) {
			utils::string_map symbols;
			symbols["percent"] = str_cast(visible_percent_);
			msg = vgettext2("Visible area display $percent%", symbols);
		}

		if (screen_mode_tex_.get() == nullptr || msg != screen_mode_msg_) {
			surface surf = font::get_rendered_text(msg, 0, font::SIZE_LARGEST, font::GOOD_COLOR);
			screen_mode_tex_ = SDL_CreateTextureFromSurface(renderer, surf);
			screen_mode_msg_ = msg;
		}

		if (screen_mode_tex_.get() != nullptr) {
			int width2, height2;
			SDL_QueryTexture(screen_mode_tex_.get(), NULL, NULL, &width2, &height2);

			SDL_Rect dstrect{draw_rect.x, draw_rect.y + mainbar_y_offset_, width2, height2};
			SDL_RenderCopy(renderer, screen_mode_tex_.get(), nullptr, &dstrect);
		}

	} else if (hide_screen_mode_ticks_ != 0) {
		hide_screen_mode_ticks_ = 0;
	}
}

SDL_Point tdesktop::wf_orientation_adjust_pos(const SDL_Point& src) const
{
	wfContext* context = (wfContext*)rdp_context_;
	rdpSettings* settings = context->context.settings;

	SDL_Point result2 = src;

	int initial = context->leagor->initialOrientation;
	bool initial_landscape = initial == KOS_DISPLAY_ORIENTATION_0 || initial == KOS_DISPLAY_ORIENTATION_180;
	// int orientation = KOS_DISPLAY_ORIENTATION_0; // KOS_DISPLAY_ORIENTATION_90
	int orientation = context->leagor->currentOrientation;

    if (initial_landscape && orientation == KOS_DISPLAY_ORIENTATION_180) {
		// orientation = 2
		result2.x = settings->DesktopWidth - 1 - src.x;
		result2.y = settings->DesktopHeight - 1 - src.y;

    } else if (initial_landscape && (orientation == KOS_DISPLAY_ORIENTATION_90 || orientation == KOS_DISPLAY_ORIENTATION_270)) {
		// We need to preserve the aspect ratio of the display.
		float displayAspect = (float) settings->DesktopHeight / (float) settings->DesktopWidth;
		int videoWidth, videoHeight;
		int outWidth, outHeight;
        videoWidth = settings->DesktopHeight;
        videoHeight = settings->DesktopWidth;

		if (videoHeight > videoWidth * displayAspect) {
			// limited by narrow width; reduce height
			outWidth = videoWidth;
			outHeight = videoWidth * displayAspect;
		} else {
			// limited by short height; restrict width
			outHeight = videoHeight;
			outWidth = videoHeight / displayAspect;
		}

		const int offX = (videoWidth - outWidth) / 2;
		const int offY = (videoHeight - outHeight) / 2;
		SDL_Rect display_rect{offY, offX, outHeight, outWidth};
	
		SDL_Point res1 = src;
		if (res1.x < display_rect.x) {
			return SDL_Point{nposm, nposm};
		} else if (res1.x >= display_rect.x + display_rect.w) {
			return SDL_Point{nposm, nposm};
		}
		result2.y = videoWidth * (res1.x - display_rect.x) / display_rect.w;
		result2.x = videoHeight * res1.y / display_rect.h;

		if (result2.y >= videoWidth) {
			result2.y = videoWidth - 1;
		}
		if (result2.x >= videoHeight) {
			result2.x = videoHeight - 1;
		}

		if (orientation == KOS_DISPLAY_ORIENTATION_90) {
			result2.x = videoHeight - 1 - result2.x;
		} else {
			// orientation == KOS_DISPLAY_ORIENTATION_270
			result2.y = videoWidth - 1 - result2.y;
		}

		// result.y = videoHeight - result.y;
		SDL_Log("video(%i x %i), display_rect(%i, %i, %i, %i),  (%i, %i)==>(%i, %i)", 
			videoWidth, videoHeight, display_rect.x, display_rect.y, display_rect.w, display_rect.h, res1.x, res1.y, result2.x, result2.y);

    } else if (!initial_landscape && (orientation == KOS_DISPLAY_ORIENTATION_90 || orientation == KOS_DISPLAY_ORIENTATION_270)) {
		result2.x = src.y;
		result2.y = src.x;

		if (orientation == KOS_DISPLAY_ORIENTATION_90) {
			result2.x = settings->DesktopHeight - 1 - result2.x;
		} else {
			// orientation == KOS_DISPLAY_ORIENTATION_270
			result2.y = settings->DesktopWidth - 1 - result2.y;
		}

	} else if (!initial_landscape && (orientation == KOS_DISPLAY_ORIENTATION_0 || orientation == KOS_DISPLAY_ORIENTATION_180)) { 

		int videoWidth = settings->DesktopHeight;
        int videoHeight = settings->DesktopWidth;
		result2.x = videoWidth * src.x / settings->DesktopWidth;
		result2.y = videoHeight * src.y / settings->DesktopHeight;
		if (orientation == KOS_DISPLAY_ORIENTATION_180) {
			result2.x = videoWidth - 1 - result2.x;
			result2.y = videoHeight - 1 - result2.y;
		}
	}

	return result2;
}

SDL_Point tdesktop::wf_scale_mouse_pos(const SDL_Rect& rdpc_rect, int x, int y)
{
	int ww, wh, dw, dh;
	wfContext* context = (wfContext*)rdp_context_;
	rdpSettings* settings;

	SDL_Point result{nposm, nposm};
	if (moving_bar_ != nullptr || scrolling_bar_ != nullptr) {
		return result;
	}

	const bool all_valid = true;
	if (all_valid) {
		if (x < rdpc_rect.x) {
			x = rdpc_rect.x;
		} else if (x >= rdpc_rect.x + rdpc_rect.w) {
			x = rdpc_rect.x + rdpc_rect.w - 1;
		}

		if (y < rdpc_rect.y) {
			y = rdpc_rect.y;
		} else if (y >= rdpc_rect.y + rdpc_rect.h) {
			y = rdpc_rect.y + rdpc_rect.h - 1;
		}
	}

	if (!context || !point_in_rect(x, y, rdpc_rect)) {
		return result;
	}

	x -= rdpc_rect.x;
	y -= rdpc_rect.y;

	settings = context->context.settings;
	VALIDATE(settings != nullptr, null_str);

	ww = rdpc_rect.w;
	wh = rdpc_rect.h;
	dw = rdpd_crop_.w; // settings->DesktopWidth
	dh = rdpd_crop_.h; // settings->DesktopHeight

	result.x = x * dw / ww + rdpd_crop_.x + scroll_delta_.x;
	result.y = y * dh / wh + rdpd_crop_.y + scroll_delta_.y;

	result = wf_orientation_adjust_pos(result);

	return result;
}

bool tdesktop::send_extra_mouse_up_event(ttrack& widget, const tpoint& coordinate)
{
	if (!SDL_RectEmpty(&rdpc_rect_) && !point_in_rect(coordinate.x, coordinate.y, rdpc_rect_)) {
		int x, y;
		uint32_t status = SDL_GetMouseState(&x, &y);
		if (x < rdpc_rect_.x) {
			x = rdpc_rect_.x;
		} else if (x >= rdpc_rect_.x + rdpc_rect_.w) {
			x = rdpc_rect_.x + rdpc_rect_.w - 1;
		}

		if (y < rdpc_rect_.y) {
			y = rdpc_rect_.y;
		} else if (y >= rdpc_rect_.y + rdpc_rect_.h) {
			y = rdpc_rect_.y + rdpc_rect_.h - 1;
		}

		if (status & SDL_BUTTON(1)) {
			SDL_Log("send_extra_mouse_up_event, left up, (%i, %i)", x, y);
			wf_scale_mouse_event(PTR_FLAGS_BUTTON1, x, y);
		}
		if (status & SDL_BUTTON(3)) {
			SDL_Log("send_extra_mouse_up_event, right up, (%i, %i)", x, y);
			wf_scale_mouse_event(PTR_FLAGS_BUTTON2, x, y);
		}
		return true;
	}
	return false;
}

void tdesktop::post_set_screenmode()
{
	minimap_bar_->visible = screen_mode_ == screenmode_partial;

	rdpc_rect_ = empty_rect;
	rdpd_crop_ = empty_rect;
	minimap_dst_ = empty_rect;
	percent_rect_ = empty_rect;
	scroll_delta_ = SDL_Point{0, 0};
}

void tdesktop::did_mouse_leave_paper(ttrack& widget, const tpoint& first, const tpoint& last)
{
	send_extra_mouse_up_event(widget, last);

	const int widget_x = widget.get_x();
	int width = widget.get_width();

	int desire_btn = maybe_percent_? btn_percent: moving_btn_;
	if (moving_bar_ != nullptr) {
		VALIDATE(scrolling_bar_ == nullptr, null_str);
		VALIDATE(moving_bar_->delta.y == 0, null_str);
		VALIDATE(!maybe_percent_, null_str);

		moving_bar_->rect.x += moving_bar_->delta.x;
		moving_bar_->delta.x = 0;

		if (moving_bar_->rect.x < widget_x) {
			moving_bar_->rect.x = widget_x;
		} else if (moving_bar_->rect.x > widget_x + width - moving_bar_->rect.w) {
			moving_bar_->rect.x = widget_x + width - moving_bar_->rect.w;
		}

		const int preferencesx = moving_bar_->rect.x - widget.get_x();
		if (moving_bar_->type == bar_main) {
			preferences::set_mainbarx(preferencesx);

		} else if (moving_bar_->type == bar_minimap) {
			preferences::set_minimapbarx(preferencesx);
		}
		moving_bar_ = nullptr;
		moving_btn_ = nposm;

	} else {
		VALIDATE(moving_btn_ = nposm, null_str);
		
		if (scrolling_bar_ != nullptr) {
			rdpd_crop_.x += scroll_delta_.x;
			rdpd_crop_.y += scroll_delta_.y;
			scroll_delta_ = SDL_Point{0, 0};

			scrolling_bar_->delta = SDL_Point{0, 0};
			scrolling_bar_ = nullptr;
			will_longpress_ticks_ = 0;
			maybe_percent_ = false;

		} else {
			VALIDATE(will_longpress_ticks_ == 0, null_str);
			VALIDATE(!maybe_percent_, null_str);
		}
	}

	if (bar_change_ != nposm) {
		handle_bar_changed(bar_change_);

		if (!btn_in_current_bar(desire_btn)) {
			desire_btn = nposm;
		}
	}

	if (is_null_coordinate(last)) {
		return;
	}

	if (desire_btn == btn_mode) {
		screen_mode_ ++;
		if (!ratio_switchable_ && screen_mode_ == screenmode_ratio) {
			screen_mode_ ++;
		}
		if (screen_mode_ == screenmode_count) {
			// below switch screen_mode_ logic requrie screenmode_min not be screenmode_ratio.
			screen_mode_ = screenmode_min;
		}
		
		post_set_screenmode();
		preferences::set_screenmode(screen_mode_);

	} else if (desire_btn == btn_off) {
		window_->set_retval(twindow::CANCEL);

	} else if (desire_btn == btn_margin) {
		margining_ = !margining_;

	} else if (desire_btn == btn_percent) {
		// gui2::tdesktop::tmsg_visible_percent* pdata = new gui2::tdesktop::tmsg_visible_percent;
		rtc::Thread::Current()->Post(RTC_FROM_HERE, rexplorer_, tdesktop::MSG_VISIBLE_PERCENT, nullptr);
	}
}

bool tdesktop::require_extra_mouse_motion() const
{
	// if server is windows and local is ios or android. send extra mouse_motion when left/right pressed or released
	// add released is for safe.
	VALIDATE(rdp_context_ != nullptr, null_str);
	rdpSettings* settings = rdp_context_->settings;
	if (settings->ServerOsMajorType == OSMAJORTYPE_WINDOWS) {
		if (settings->OsMajorType == OSMAJORTYPE_ANDROID || settings->OsMajorType == OSMAJORTYPE_IOS) {
			return true;
		}
	}
	return false;
}

void tdesktop::wf_scale_mouse_event(uint16_t flags, int x, int y)
{
	SDL_Point rdpd_pt = wf_scale_mouse_pos(rdpc_rect_, x, y);
	if (rdpd_pt.x != nposm && rdpd_pt.y != nposm) {
		if ((flags & PTR_FLAGS_BUTTON1) || (flags & PTR_FLAGS_BUTTON2)) {
			if (require_extra_mouse_motion()) {
				freerdp_input_send_mouse_event(rdp_context_->input, PTR_FLAGS_MOVE, rdpd_pt.x, rdpd_pt.y);
			}
		}
		freerdp_input_send_mouse_event(rdp_context_->input, flags, rdpd_pt.x, rdpd_pt.y);
	}
}

void tdesktop::did_mouse_motion_paper(ttrack& widget, const tpoint& first, const tpoint& last)
{
	// if (!send_extra_mouse_up_event(widget, last)) {
		wf_scale_mouse_event(PTR_FLAGS_MOVE, last.x, last.y);
	// }

	if (is_null_coordinate(first)) {
		return;
	}

	if (moving_bar_ != nullptr) {
		VALIDATE(scrolling_bar_ == nullptr, null_str);
		VALIDATE(moving_bar_->delta.y == 0, null_str);

		moving_bar_->delta.x = last.x - first.x;
		if (moving_btn_ != nposm) {
			const int threshold = 3 * twidget::hdpi_scale; // moving_bar_->size / 4
			moving_btn_ = posix_abs(moving_bar_->delta.x) <= threshold? moving_btn_: nposm;	
		}
		if (moving_btn_ != nposm) {
			if (last.y < moving_bar_->rect.y || last.y >= moving_bar_->rect.y + moving_bar_->rect.h) {
				moving_btn_ = nposm;
			}
		}
	} else if (scrolling_bar_ != nullptr) {
		scrolling_bar_->delta.x = last.x - first.x;
		scrolling_bar_->delta.y = last.y - first.y;
		if (will_longpress_ticks_ != 0) {
			if (SDL_GetTicks() < will_longpress_ticks_) {
				const int threshold = 3 * twidget::hdpi_scale; // scrolling_bar_->size / 4
				will_longpress_ticks_ = posix_abs(scrolling_bar_->delta.x) <= threshold? will_longpress_ticks_: 0;
				if (maybe_percent_) {
					maybe_percent_ = posix_abs(scrolling_bar_->delta.x) <= threshold;
				}
			}
		}
	}
	paper_->immediate_draw();
}

void tdesktop::signal_handler_longpress_paper(bool& halt, const tpoint& coordinate)
{
	halt = true;
/*
	if (!work_mode_) {
		return;
	}

	// long-press requires complex processes, current maybe pending other app_message task.
	// use desire_enter_settingsmode_, dont' waste this long-press.
	desire_enter_settingsmode_ = true;
*/
}

void tdesktop::signal_handler_left_button_up(const tpoint& coordinate)
{
	wf_scale_mouse_event(PTR_FLAGS_BUTTON1, coordinate.x, coordinate.y);
}

void tdesktop::signal_handler_right_button_down(bool& handled, bool& halt, const tpoint& coordinate)
{
	halt = handled = true;
	for (std::vector<tbar>::const_iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		const tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		if (point_in_rect(coordinate.x, coordinate.y, bar.type != bar_minimap? bar.rect: minimap_dst_)) {
			return;
		}
	}
	wf_scale_mouse_event(PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON2, coordinate.x, coordinate.y);
}

void tdesktop::did_left_button_down(ttrack& widget, const tpoint& coordinate)
{
	VALIDATE(moving_bar_ == nullptr, null_str);
	VALIDATE(moving_btn_ == nposm, null_str);
	VALIDATE(scrolling_bar_ == nullptr, null_str);
	VALIDATE(will_longpress_ticks_ == 0, null_str);
	VALIDATE(!maybe_percent_, null_str);
	
	for (std::vector<tbar>::iterator it = bars_.begin(); it != bars_.end(); ++ it) {
		tbar& bar = *it;
		if (!bar.visible) {
			continue;
		}
		if (point_in_rect(coordinate.x, coordinate.y, bar.type != bar_minimap? bar.rect: minimap_dst_)) {
			if (bar.type != bar_minimap) {
				moving_bar_ = &bar;
				int xoffset = bar.rect.x;
				for (std::vector<int>::const_iterator it2 = bar.btns.begin(); it2 != bar.btns.end(); ++ it2, xoffset += bar.size + bar.gap) {
					SDL_Rect rect{xoffset, bar.rect.y, bar.size, bar.size};
					if (point_in_rect(coordinate.x, coordinate.y, rect)) {
						moving_btn_ = *it2;
					}
				}

			} else {
				scrolling_bar_ = &bar;
				const int longpress_threshold = 2000;
				will_longpress_ticks_ = SDL_GetTicks() + longpress_threshold;
				maybe_percent_ = point_in_rect(coordinate.x, coordinate.y, percent_rect_);
			}
			return;
		}
	}
	wf_scale_mouse_event(PTR_FLAGS_DOWN | PTR_FLAGS_BUTTON1, coordinate.x, coordinate.y);
}

void tdesktop::did_right_button_up(ttrack& widget, const tpoint& coordinate)
{
	wf_scale_mouse_event(PTR_FLAGS_BUTTON2, coordinate.x, coordinate.y);
}

void tdesktop::signal_handler_sdl_wheel_up(bool& handled, const tpoint& coordinate)
{
	int orientation = KOS_DISPLAY_ORIENTATION_0;

	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	if (orientation == KOS_DISPLAY_ORIENTATION_0 || orientation == KOS_DISPLAY_ORIENTATION_180) {
		wf_scale_mouse_event(PTR_FLAGS_WHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120, coordinate.x, coordinate.y);
	} else {
		wf_scale_mouse_event(PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120, coordinate.x, coordinate.y);
	}
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_down(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(PTR_FLAGS_WHEEL | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_left(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(PTR_FLAGS_HWHEEL | PTR_FLAGS_WHEEL_NEGATIVE | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::signal_handler_sdl_wheel_right(bool& handled, const tpoint& coordinate)
{
	// why 120? reference to wf_event_process_WM_MOUSEWHEEL in <FreeRDP>/client/Windows/wf_event.c
	wf_scale_mouse_event(PTR_FLAGS_HWHEEL | 120, coordinate.x, coordinate.y);
	handled = true;
}

void tdesktop::rexplorer_OnMessage(rtc::Message* msg)
{
	const uint32_t now = SDL_GetTicks();

	switch (msg->message_id) {
	case MSG_STARTUP_MSG:
		{
			tmsg_startup_msg* pdata = static_cast<tmsg_startup_msg*>(msg->pdata);
			VALIDATE(pdata->ticks != 0 && !pdata->msg.empty(), null_str);
			if (pdata->rdpstatus == rdpstatus_connectionfinished) {
				did_rdp_client_connectionfinished();

			} else if (pdata->rdpstatus == rdpstatus_connectionclosed) {
				did_rdp_client_reset();
			}
			insert_startup_msg(pdata->ticks, pdata->msg, pdata->fail);
		}
		break;

	case MSG_EXPLORER:
		{
			tmsg_explorer* pdata = static_cast<tmsg_explorer*>(msg->pdata);
			const LEAGOR_EXPLORER_UPDATE& update = pdata->update;

			bool update_explorer_grid = false;
			if (update.code == LG_EXPLORER_CODE_SHOWN) {
				if (!drag_copy_.explorering) {
					update_explorer_grid = true;

					drag_copy_.explorering = true;
					drag_copy_.statusbar_height = update.data1;
					drag_copy_.navigation_height = update.data2;
					drag_copy_.vratio = 0;
					drag_copy_.hratio = 0;
					drag_copy_.previous_margining = margining_;
					margining_ = false;
					if (screen_mode_ != preferences::screenmode()) {
						std::stringstream err;
						err << "screen_mode_(" << screen_mode_ << ") != preferences::screenmode()(" << preferences::screenmode() << ")";
						VALIDATE(screen_mode_ == preferences::screenmode(), err.str());
					}
					if (screen_mode_ != screenmode_scale) {
						screen_mode_ = screenmode_scale;
						post_set_screenmode();
					}

					handle_bar_changed(barchg_dragcopy);
				}
			} else if (update.code == LG_EXPLORER_CODE_HIDDEN) {
				if (drag_copy_.explorering) {
					update_explorer_grid = true;

					drag_copy_.explorering = false;
					drag_copy_.vratio = 0;
					drag_copy_.hratio = 0;
					margining_ = drag_copy_.previous_margining;
					if (screen_mode_ != preferences::screenmode()) {
						screen_mode_ = preferences::screenmode();
						post_set_screenmode();
					}
					handle_bar_changed(rdp_context_ != nullptr? barchg_connectionfinished: barchg_closed);
				}

			} else if (update.code == LG_EXPLORER_CODE_START_DRAG) {
				SDL_Log("rexplorer_OnMessage, LG_EXPLORER_CODE_START_DRAG");
				VALIDATE(drag_copy_.hratio != 0, null_str);
				VALIDATE(drag_copy_.vratio != 0, null_str);
				int x = update.data1 * drag_copy_.hratio;
				int y = update.data2 * drag_copy_.vratio;
				drag_copy_.files = update.data3 & LG_START_DRAG_FILES_MASK;
				drag_copy_.has_folder = update.data3 & LG_START_DRAG_HAS_FOLDER_FLAG? true: false;
				drag_copy_.src_is_server = true;
				rexplorer_->start_drag2(tpoint(x, y), drag_copy_.files, drag_copy_.has_folder);

			} else if (update.code == LG_EXPLORER_CODE_END_DRAG) {
				SDL_Log("rexplorer_OnMessage, LG_EXPLORER_CODE_END_DRAG");

			} else if (update.code == LG_EXPLORER_CODE_CAN_PASTE) {
				int x = update.data1 * drag_copy_.hratio;
				int y = update.data2 * drag_copy_.vratio;
				drag_copy_.server_allow_paste = update.data3 & 0x1? true: false;
				SDL_Log("rexplorer_OnMessage, LG_EXPLORER_CODE_CAN_PASTE, (%i, %i), %s", x, y, update.data3? "can": "cannot");
			}

			if (update_explorer_grid) {
				tgrid* explorer_grid = main_stack_->layer(trexplorer::EXPLORER_LAYER);
				explorer_grid->set_visible(drag_copy_.explorering? twidget::VISIBLE: twidget::INVISIBLE);
			}
		}
		break;

	case MSG_VISIBLE_PERCENT:
		if (rdp_manager_ != nullptr) {
			click_visible_percent(percent_rect_.x + percent_rect_.w, percent_rect_.y + percent_rect_.h);
		}
		break;
	}
}


void tdesktop::rdp_copy(net::RdpClientRose& rdpc, tlistbox& list)
{
	std::vector<std::string> files;
	if (list.cursel() != nullptr) {
		files.push_back(rexplorer_->selected_full_name(nullptr));
	} else {
		files = rexplorer_->selected_full_multinames();
	}
	std::string files_str = utils::join_with_null(files);
	rdpc.hdrop_copied(files_str);
}

void tdesktop::rdp_paste(net::RdpClientRose& rdpc, const std::string& path)
{
	VALIDATE(!path.empty(), null_str);
	int err_code = cliprdr_errcode_ok;
	char err_msg[512];

	// const std::string title = cliprdr_msgid_2_str(cliprdr_msgid_pastingwarn, game_config::get_app_msgstr(null_str));
	utils::string_map symbols;
	symbols["src"] = _("Remote");
	symbols["dst"] = _("Local");
	const std::string title = vgettext2("$src ---> $dst", symbols);
	tprogress_default_slot slot(std::bind(&net::RdpClientRose::hdrop_paste, &rdpc, _1, path, &err_code, err_msg, sizeof(err_msg)),
	 	"misc/remove.png", gui2::tgrid::HORIZONTAL_ALIGN_LEFT);
	gui2::run_with_progress(slot, null_str, null_str, 0, SDL_Rect{nposm, nposm, nposm, nposm});
	const std::string msg = cliprdr_code_2_str(err_msg, err_code);
	if (!msg.empty()) {
		gui2::show_message(null_str, msg);
	}
	rexplorer_->update_file_lists();
}

void tdesktop::rdp_delete(net::RdpClientRose& rdpc, tlistbox& list)
{
	std::vector<std::string> files;
	if (list.cursel() != nullptr) {
		files.push_back(rexplorer_->selected_full_name(nullptr));
	} else {
		files = rexplorer_->selected_full_multinames();
	}
	VALIDATE(!files.empty(), null_str);

	if (!gui2::show_confirm_delete(files, false)) {
		return;
	}

	tprogress_default_slot slot(std::bind(&gui2::delete_files, _1, files),
	 	"misc/remove.png", gui2::tgrid::HORIZONTAL_ALIGN_LEFT);
	gui2::run_with_progress(slot, null_str, null_str, 0, SDL_Rect{nposm, nposm, nposm, nposm});

	rexplorer_->update_file_lists();
}

void tdesktop::rexplorer_click_edit(tbutton& widget)
{
	net::RdpClientRose& rdpc_ = rdp_manager_->rdp_delegate();
	enum {copy, cut, paste, op_delete};

	std::vector<gui2::tmenu::titem> items;
	
	if (file_list_->cursel() != nullptr || !file_list_->multiselected_rows().empty()) {
		items.push_back(gui2::tmenu::titem(cliprdr_msgid_2_str(cliprdr_msgid_copy, null_str), copy));
		// items.push_back(gui2::tmenu::titem(_("Cut"), cut));
	}
	if (rdpc_.can_hdrop_paste()) {
		// paste
		items.push_back(gui2::tmenu::titem(cliprdr_msgid_2_str(cliprdr_msgid_paste, null_str), paste));
	}

	if (file_list_->cursel() != nullptr || !file_list_->multiselected_rows().empty()) {
		items.push_back(gui2::tmenu::titem(_("Delete"), op_delete));
	}


	if (items.empty()) {
		gui2::show_message(null_str, cliprdr_msgid_2_str(cliprdr_msgid_nooperator, null_str));
		return;
	}

	int selected;
	{
		gui2::tmenu dlg(items, nposm);
		dlg.show(widget.get_x(), widget.get_y() + widget.get_height() + 16 * twidget::hdpi_scale);
		int retval = dlg.get_retval();
		if (dlg.get_retval() != gui2::twindow::OK) {
			return;
		}
		// absolute_draw();
		selected = dlg.selected_val();
	}

	if (selected == copy) {
		rdp_copy(rdpc_, *file_list_);

	} else if (selected == paste) {
		rdp_paste(rdpc_, rexplorer_->current_dir());

	} else if (selected == op_delete) {
		rdp_delete(rdpc_, *file_list_);
	}
}

void tdesktop::rexplorer_timer_handler(uint32_t now)
{
}

void tdesktop::did_recv_explorer_update(const LEAGOR_EXPLORER_UPDATE& update)
{
	gui2::tdesktop::tmsg_explorer* pdata = new gui2::tdesktop::tmsg_explorer;
	pdata->update = update;

	instance->sdl_thread().Post(RTC_FROM_HERE, rexplorer_, tdesktop::MSG_EXPLORER, pdata);
}

void tdesktop::rexplorer_handler_longpress_item(const tpoint& coordinate, ttoggle_panel& widget)
{
	VALIDATE(rdp_context_ != nullptr, null_str);
	rdpSettings* settings = rdp_context_->settings;
	if (settings->ServerOsMajorType == OSMAJORTYPE_ANDROID || settings->ServerOsMajorType == OSMAJORTYPE_IOS) {
		wf_scale_mouse_event(PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, coordinate.x, coordinate.y);
	}

	drag_copy_.simulate_down_rdpc_pt.x = coordinate.x;
	drag_copy_.simulate_down_rdpc_pt.y = coordinate.y;

	net::RdpClientRose& rdpc = rdp_manager_->rdp_delegate();
	{
		trexplorer::tdisable_click_open_dir_lock lock(*rexplorer_);
		file_list_->select_row(widget.at());
	}
	rdp_copy(rdpc, *file_list_);

	SDL_Point selecteds = rexplorer_->analysis_selected();
	drag_copy_.files = selecteds.x + selecteds.y;
	drag_copy_.has_folder = selecteds.x > 0;
	drag_copy_.src_is_server = false;

	SDL_Point rdpd_pt = wf_scale_mouse_pos(rdpc_rect_, coordinate.x, coordinate.y);
	uint32_t data3 = LG_START_DRAG_FORM_DATA3(drag_copy_.files, drag_copy_.has_folder? 1: 0);
	bool ret = rdpc.push_explorer_update(LG_EXPLORER_CODE_START_DRAG, rdpd_pt.x, rdpd_pt.y, data3);
	VALIDATE(ret, null_str);

	rexplorer_->start_drag2(coordinate, drag_copy_.files, drag_copy_.has_folder);
}

bool tdesktop::rexplorer_did_drag_mouse_motion(const int x, const int y)
{
	std::string dest_path;
	if (drag_copy_.src_is_server) {
		SDL_Rect widget_rect = file_list_->get_rect();
		if (point_in_rect(x, y, widget_rect)) {
			dest_path = rexplorer_->current_dir();
		}
	} else {
		// When dragged, floating-surface is always the widget selected and does not trigger track widget's motion.
		// Although the mouse is over on track-widget.
		wf_scale_mouse_event(PTR_FLAGS_MOVE, x, y);
		if (drag_copy_.server_allow_paste) {
			dest_path = SERVER_ALLOW_PASTE;
		}
	}
	surface surf = rexplorer_->get_drag_surface(drag_copy_.files, drag_copy_.has_folder, dest_path);
	if (surf.get() != window_->curr_drag_surf()) {
		window_->set_drag_surface(surf, false);
	}

	return true;
}

void tdesktop::rexplorer_drag_stoped(const int x, const int y, bool up_result)
{
	bool src_is_server = drag_copy_.src_is_server;
	drag_copy_.reset_drag_session();
	
	if (rdp_context_ == nullptr) {
		return;
	}

	SDL_Point rdpd_pt{x, y};
	if (up_result && rdp_context_ != nullptr) {
		rdpd_pt = wf_scale_mouse_pos(rdpc_rect_, x, y);
		VALIDATE(rdpd_pt.x != nposm && rdpd_pt.y != nposm, null_str);
	}
	net::RdpClientRose& rdpc = rdp_manager_->rdp_delegate();
	rdpc.push_explorer_update(LG_EXPLORER_CODE_END_DRAG, rdpd_pt.x, rdpd_pt.y, LG_END_DRAG_FORM_DATA3(up_result));

	if (src_is_server) {
		if (!up_result) {
			return;
		}
		SDL_Rect widget_rect = file_list_->get_rect();
		if (!point_in_rect(x, y, widget_rect)) {
			return;
		}
	
		std::string dest_path = rexplorer_->current_dir();
		ttoggle_panel* row = file_list_->visible_area_point_2_row(x, y);
		if (row != nullptr) {
			trexplorer::tfile2 file = rexplorer_->row_2_file2(row->at());
			if (file.dir) {
				// dest_path += trexplorer::path_delim + file.name;
			}
		}

		rdp_paste(rdp_manager_->rdp_delegate(), dest_path);

	} else {
		VALIDATE(drag_copy_.hratio != 0 && drag_copy_.vratio != 0, null_str);
		VALIDATE(rdp_context_ != nullptr, null_str);
		rdpSettings* settings = rdp_context_->settings;
		if (settings->ServerOsMajorType == OSMAJORTYPE_ANDROID || settings->ServerOsMajorType == OSMAJORTYPE_IOS) {
			wf_scale_mouse_event(PTR_FLAGS_BUTTON1, drag_copy_.simulate_down_rdpc_pt.x, drag_copy_.simulate_down_rdpc_pt.y);
		}

		if (!file_list_->multiselect()) {
			// selected is a directory, which must be set to unselected for return to normal state.
			VALIDATE(file_list_->cursel() != nullptr, null_str);
			trexplorer::tfile2 file = rexplorer_->row_2_file2(file_list_->cursel()->at());
			if (file.dir) {
				file_list_->select_row(nposm);
			}
		}
	}
}

SDL_Rect tdesktop::did_pip_top_grid_rect(tstack& widget, tgrid& grid, int width, int height)
{
	wfContext* context = (wfContext*)rdp_context_;
	if (context == nullptr) {
		VALIDATE(false, null_str);
		return SDL_Rect{0, 0, width, height};
	}

	const int x = width / 2 + gap_width_;
	if (height == nposm) {
		return SDL_Rect{0, 0, width - x, height};
	}

	rdpSettings* settings = context->context.settings;

	drag_copy_.hratio = 1.0 * width / settings->DesktopWidth;
	drag_copy_.vratio = 1.0 * height / settings->DesktopHeight;
	float vratio = drag_copy_.vratio;

	int draw_statusbar_height = drag_copy_.statusbar_height * vratio;
	int draw_navigation_height = drag_copy_.navigation_height * vratio;

	return SDL_Rect{x, draw_statusbar_height, width - x, height - draw_statusbar_height - draw_navigation_height};
}

} // namespace gui2

