/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Windows Clipboard Redirection
 *
 * Copyright 2012 Jason Champion
 * Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
 * Copyright 2015 Thincast Technologies GmbH
 * Copyright 2015 DI (FH) Martin Haimberger <martin.haimberger@thincast.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef _WIN32
#if !defined(UNICODE)
#error "On windows, this file must complete Use Unicode Character Set"
#endif
#endif

#if defined(_WIN32) || defined(ANDROID) || defined(__APPLE__)
#include "freerdp_config.h"
#endif

#include <winpr/crt.h>
#include <winpr/tchar.h>
#include <winpr/stream.h>

#include <freerdp/log.h>
#include <freerdp/server/leagor.h>

#include "wf_client.hpp"
#include <freerdp/channels/leagor_common_context.h>
#include <freerdp/channels/leagor_common2.hpp>

#include <SDL_log.h>
#include "wml_exception.hpp"
#include "serialization/string_utils.hpp"


#define TAG CLIENT_TAG("windows")


void wf_leagor_client_send_explorer_update(rdpContext* context, const LEAGOR_EXPLORER_UPDATE* update)
{
	wfContext* client = (wfContext*)context;
	if (client->leagor == nullptr) {
		// client does not support leagor ext, for example: mstsc.exe.
		return;
	}

	LeagorCommonContext* leagor = client->leagor;
	leagor_send_explorer_update(leagor, update);
}

// int shadow_client_cliprdr_init(rdpShadowClient* client)
BOOL wf_leagor_init(wfContext* wfc, LeagorCommonContext* cliprdr)
{
	SDL_Log("wf_cliprdr_init, sizeof(FILEDESCRIPTOR): %i =>(592) MAX_PATH: %i =>(260), sizeof(wchar_t): %i", (int)sizeof(FILEDESCRIPTOR), MAX_PATH, (int)sizeof(wchar_t));
	cliprdr->server = FALSE;
	VALIDATE(!cliprdr->server, null_str);

	rdpContext* context = (rdpContext*)wfc;

	if (!context || !cliprdr) {
		return FALSE;
	}
	wfc->leagor = cliprdr;

	cliprdr->rdpcontext = (rdpContext*)wfc;

	/* Set server callbacks */
	cliprdr->RecvCapabilities = wf_leagor_common_recv_capabilities;
	cliprdr->ServerOrientationUpdate = wf_leagor_common_orientation_update;
	cliprdr->RecvExplorerUpdate = wf_leagor_client_recv_explorer_update;

	// cliprdr->custom = NULL;
	return 1;
}

void wf_leagor_uninit(wfContext* wfc, LeagorCommonContext* cliprdr)
{
	SDL_Log("wf_leagor_uninit--- wfc: %p", wfc);
	if (wfc != nullptr && wfc->leagor != nullptr)
	{
		// wfc->leagor->Stop(client->leagor);
		SDL_Log("wf_cliprdr_uninit 1");


		// cliprdr_server_context_free(wfc->cliprdr);
		// wfc->cliprdr = NULL;
	}
	SDL_Log("---wf_leagor_uninit X");
}