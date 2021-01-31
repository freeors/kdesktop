// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef WF_CLIENT_HPP_
#define WF_CLIENT_HPP_

#include <freerdp/freerdp.h>
// #include <freerdp/channels/channels.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/client/leagor.h>

// System menu constants
// #define SYSCOMMAND_ID_SMARTSIZING 1000
	struct wfContext
	{
		rdpContext context;
		void* decoder;
		void* desktop;

		CliprdrClientContext* cliprdr;
		LeagorCommonContext* leagor;
	};

	/**
	 * Client Interface
	 */

	// FREERDP_API int RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);
	// FREERDP_API int freerdp_client_set_window_size(wfContext* wfc, int width, int height);
	// FREERDP_API void wf_size_scrollbars(wfContext* wfc, UINT32 client_width, UINT32 client_height);

	BOOL wf_cliprdr_init(wfContext* wfc, CliprdrClientContext* cliprdr);
	void wf_cliprdr_uninit(wfContext* wfc, CliprdrClientContext* cliprdr);
	BOOL wf_leagor_init(wfContext* wfc, LeagorCommonContext* leagor);
	void wf_leagor_uninit(wfContext* wfc, LeagorCommonContext* leagor);

	UINT wf_leagor_client_recv_explorer_update(LeagorCommonContext* context, const LEAGOR_EXPLORER_UPDATE* update);
	void wf_leagor_client_send_explorer_update(rdpContext* context, const LEAGOR_EXPLORER_UPDATE* update);

#endif // WF_CLIENT_HPP_
