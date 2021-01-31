// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rdp_client.h"

#include <utility>
#include "util_c.h"
#include "gettext.hpp"
#include "formula_string_utils.hpp"
#include "version.hpp"

#include "base/bind.h"
#include "base/compiler_specific.h"
#include "base/location.h"
#include "base/logging.h"
#include "base/single_thread_task_runner.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/sys_byteorder.h"
#include "base/threading/thread_task_runner_handle.h"
#include "build/build_config.h"
#include "net/base/net_errors.h"
#include "net/server/rdp_connection.h"
#include "net/socket/server_socket.h"
#include "net/socket/stream_socket.h"
#include "net/socket/tcp_server_socket.h"

#include <SDL_stdinc.h>
#include <SDL_log.h>

#include "rose_config.hpp"
#include "base_instance.hpp"

#include "base/files/file_util.h"
#include "net/cert/x509_certificate.h"
#include "crypto/rsa_private_key.h"
#include "net/ssl/ssl_server_config.h"
#include <net/socket/client_socket_factory.h>
#include <net/base/host_port_pair.h>
#include <net/socket/client_socket_handle.h>
#include <net/ssl/ssl_config_service_defaults.h>
#include <net/ssl/ssl_client_session_cache.h>

#include "third_party/boringssl/src/include/openssl/evp.h"
#include "third_party/boringssl/src/include/openssl/ssl.h"
#include "json/json.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"
#include "base/strings/utf_string_conversions.h"

#include <../libfreerdp/core/rdp.h>
#include <../libfreerdp/core/nego.h>
#include <freerdp/client/cmdline.h>

// #include <freerdp/freerdp.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpei.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/client/encomsp.h>
#include <freerdp/client/cliprdr.h>
#include <freerdp/gdi/gfx.h>

#include "gui/dialogs/desktop.hpp"

#include <../winpr/include/winpr/wlog.h>

namespace net {

static void wf_OnChannelConnectedEventHandler(void* context, ChannelConnectedEventArgs* e)
{
	wfContext* wfc = (wfContext*)context;
	rdpSettings* settings = wfc->context.settings;

	if (strcmp(e->name, RDPEI_DVC_CHANNEL_NAME) == 0)
	{
	}
	else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0)
	{
		if (!settings->SoftwareGdi) {
			SDL_Log("Channel " RDPGFX_DVC_CHANNEL_NAME " does not support hardware acceleration, using fallback.");
		}

		SDL_Log("wf_OnChannelConnectedEventHandler, Channel[%s]", RDPGFX_DVC_CHANNEL_NAME);

		RdpgfxClientContext* gfxcontext = (RdpgfxClientContext*)e->pInterface;
		gfxcontext->custom = wfc->decoder;
		// gdi_graphics_pipeline_init(wfc->context.gdi, (RdpgfxClientContext*)e->pInterface);
		// gdi_graphics_pipeline_init((rdpGdi*)wfc->decoder, (RdpgfxClientContext*)e->pInterface);
	}
	else if (strcmp(e->name, RAIL_SVC_CHANNEL_NAME) == 0) {
		// wf_rail_init(wfc, (RailClientContext*)e->pInterface);

	} else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
		wf_cliprdr_init(wfc, (CliprdrClientContext*)e->pInterface);

	} else if (strcmp(e->name, LEAGOR_SVC_CHANNEL_NAME) == 0) {
		wf_leagor_init(wfc, (LeagorCommonContext*)e->pInterface);

	} else if (strcmp(e->name, ENCOMSP_SVC_CHANNEL_NAME) == 0) {
	}
}

static void wf_OnChannelDisconnectedEventHandler(void* context, ChannelDisconnectedEventArgs* e)
{
	wfContext* wfc = (wfContext*)context;
	rdpSettings* settings = wfc->context.settings;

	if (strcmp(e->name, RDPEI_DVC_CHANNEL_NAME) == 0)
	{
	}
	else if (strcmp(e->name, RDPGFX_DVC_CHANNEL_NAME) == 0)
	{
		gdi_graphics_pipeline_uninit(wfc->context.gdi, (RdpgfxClientContext*)e->pInterface);
	}
	else if (strcmp(e->name, RAIL_SVC_CHANNEL_NAME) == 0)
	{
		// wf_rail_uninit(wfc, (RailClientContext*)e->pInterface);

	} else if (strcmp(e->name, CLIPRDR_SVC_CHANNEL_NAME) == 0) {
		wf_cliprdr_uninit(wfc, (CliprdrClientContext*)e->pInterface);

	} else if (strcmp(e->name, LEAGOR_SVC_CHANNEL_NAME) == 0) {
		wf_leagor_uninit(wfc, (LeagorCommonContext*)e->pInterface);

	} else if (strcmp(e->name, ENCOMSP_SVC_CHANNEL_NAME) == 0) {
	}
}

static SSIZE_T did_rose_read_layer(rdpContext* context, BYTE* data, size_t bytes)
{
	rdpRdp* rdp = context->rdp;
	RdpClientRose* rose = reinterpret_cast<RdpClientRose*>(rdp->rose.rose_delegate);
	RdpClient* connection = reinterpret_cast<RdpClient*>(rdp->rose.rose_connection);
	VALIDATE(connection != nullptr && &connection->rdp_context == context, null_str);
	if (rose->client() == nullptr) {
		return 0;
	}
	VALIDATE(rose->client() == connection, null_str);
	return connection->did_read_layer(data, bytes);
}

static int did_rose_write_layer(rdpContext* context, BYTE* data, size_t bytes)
{
	rdpRdp* rdp = context->rdp;
	RdpClientRose* rose = reinterpret_cast<RdpClientRose*>(rdp->rose.rose_delegate);
	RdpClient* connection = reinterpret_cast<RdpClient*>(rdp->rose.rose_connection);
	VALIDATE(connection != nullptr && &connection->rdp_context == context, null_str);
	if (rose->client() == nullptr) {
		return 0;
	}
	VALIDATE(rose->client() == connection, null_str);
	return connection->did_write_layer(data, bytes);
}

RdpClient::RdpClient(const IPEndPoint& server_address, RdpClient::Delegate* delegate, rdpContext& rdp_context)
    : server_address_(server_address)
	, delegate_(delegate)
	, rdp_context(rdp_context)
	, weak_ptr_factory_(this)
	, socket_factory_(ClientSocketFactory::GetDefaultFactory())
	, next_state_(STATE_NONE)
	// , resolve_result_(net::OK)
	// , describeResponseHandler_(nullptr)
	// , describeAuthenticator_(nullptr)
	// , read_by_rtpinterface_(false)
	// , in_rtpinterface_iocomplete_(false)
	, create_ticks_(SDL_GetTicks())
    , handshaked_ticks_(0)
	, max_read_buffer_size_(512 * 1024)
	, max_write_buffer_size_(512 * 1024)
	, read_buf_(new HttpConnection::ReadIOBuffer())
    , write_buf_(new HttpConnection::QueuedWriteIOBuffer())
	, ssl_config_service_(new SSLConfigServiceDefaults)
	, cert_verifier_(new MockCertVerifier)
    , transport_security_state_(new TransportSecurityState)
    , ct_verifier_(new DoNothingCTVerifier)
    , ct_policy_enforcer_(std::make_unique<DefaultCTPolicyEnforcer>())
	, ssl_client_session_cache_(std::make_unique<SSLClientSessionCache>(SSLClientSessionCache::Config()))
	, read_layer_ptr_(nullptr)
	, read_layer_size_(0)
	, read_layer_offset_(0)
	, tid_(SDL_ThreadID())
{
	cert_verifier_->set_default_result(OK);
	context_.reset(new SSLClientContext(ssl_config_service_.get(),
                                               cert_verifier_.get(),
                                               transport_security_state_.get(),
                                               ct_policy_enforcer_.get(),
                                               ssl_client_session_cache_.get(),
                                               nullptr));

/*
	EXPECT_CALL(*ct_policy_enforcer_, CheckCompliance(_, _, _))
        .WillRepeatedly(
            Return(ct::CTPolicyCompliance::CT_POLICY_COMPLIES_VIA_SCTS));
*/
}

RdpClient::~RdpClient()
{
	// here, delegate_->send_startup_msg cannot send successfully, because webrc-invoke is disable by send_helper_.clear_msg();
}

int RdpClient::SendData(const std::string& data)
{
	VALIDATE_IN_RDPC_THREAD();

    VALIDATE(!data.empty(), null_str);
    if (next_state_ == STATE_TRANSPORT_CONNECT_COMPLETE) {
        // cr_data = data;
        return data.size();
    }

    bool writing_in_progress = !write_buf_->IsEmpty();
    if (write_buf_->Append(data) && !writing_in_progress) {
        DoWriteLoop(TRAFFIC_ANNOTATION_FOR_TESTS);
    }

    return data.size();
}

void RdpClient::OnIOComplete(int result)
{
	DoLoop(result);
}

int RdpClient::DoLoop(int result)
{
	DCHECK_NE(next_state_, STATE_NONE);

	int rv = result;
	do {
		State state = next_state_;
		next_state_ = STATE_NONE;
		switch (state) {
		case STATE_TRANSPORT_CONNECT:
			DCHECK_EQ(net::OK, rv);
			rv = DoTransportConnect();
			break;
		case STATE_TRANSPORT_CONNECT_COMPLETE:
			rv = DoTransportConnectComplete(rv);
			break;
		case STATE_WRITE_CR:
			rv = DoWriteCR();
			break;
		case STATE_WRITE_CR_COMPLETE:
			rv = DoWriteCRComplete(rv);
			break;
		case STATE_READ_CC:
			DCHECK_EQ(OK, rv);
			rv = DoReadCC();
			break;
		case STATE_READ_CC_COMPLETE:
			rv = DoReadCCComplete(rv);
			break;
		case STATE_HANDSHAKE:
			DCHECK_EQ(OK, rv);
			rv = DoHandshake();
			break;
		case STATE_HANDSHAKE_COMPLETE:
			rv = DoHandshakeComplete(rv);
			break;
		case STATE_WRITE_MCS_REQUEST:
			DCHECK_EQ(OK, rv);
			rv = DoMcsBegin();
			break;
		case STATE_READ_MCS_RESPONSE:
			rv = DoWriteMcsRequestComplete(rv);
			break;
		case STATE_RDP_CONNECTION_COMPLETE:
			rv = DoRdpConnectionComplete(rv);
			break;
		case STATE_READ_PDU:
			DCHECK_EQ(OK, rv);
			rv = DoReadPDU();
			break;
		case STATE_READ_PDU_COMPLETE:
			rv = DoReadPDUComplete(rv);
			break;
		default:
			NOTREACHED();
			rv = net::ERR_FAILED;
			break;
		}
	} while (rv != net::ERR_IO_PENDING && next_state_ != STATE_NONE);

	return rv;
}

int RdpClient::DoTransportConnect() {
	utils::string_map symbols;
	symbols["ip"] = server_address_.ToString();
	delegate_->send_startup_msg(SDL_GetTicks(), vgettext2("Connect to $ip", symbols), false, nposm);

	next_state_ = STATE_TRANSPORT_CONNECT_COMPLETE;

	// We don't yet have a TCP socket (or we used to have one, but it got closed).  Set it up now.
	ctrl_socket_ = socket_factory_->CreateTransportClientSocket(AddressList(server_address_), nullptr, nullptr, net_log2_.net_log(), net_log2_.source());

	if (ctrl_socket_.get() == nullptr) {
		delegate_->send_startup_msg(SDL_GetTicks(), _("Cannot create local socket"), true, nposm);
		return ERR_FAILED;
	}
      
	// Connect to the remote endpoint:
	int connectResult = ctrl_socket_->Connect(base::Bind(&RdpClient::OnIOComplete, base::Unretained(this)));
	if (connectResult < 0 && connectResult != net::ERR_IO_PENDING) {
		symbols["errcode"] = str_cast(connectResult);
		delegate_->send_startup_msg(SDL_GetTicks(), vgettext2("Connect fail. errcode: $errcode", symbols), true, nposm);
		return ERR_FAILED;
	}
	return connectResult;
}

int RdpClient::DoTransportConnectComplete(int result)
{
	if (result != OK) {
		Close(result);
        return result;
    }
    next_state_ = STATE_WRITE_CR;
    return OK;
}

int RdpClient::DoWriteCR()
{
    next_state_ = STATE_WRITE_CR_COMPLETE;

	rdpRdp* rdp = rdp_context.rdp;
	rdpNego* nego = rdp->nego;
	// bool rval = nego_send_negotiation_request(nego);
	// VALIDATE(rval, null_str);
	// return write_buf_->IsEmpty()? OK: ERR_IO_PENDING;
	wStream* s = nego_get_negotiation_request(nego);
	VALIDATE(s != nullptr, null_str);

	std::string data((const char*)Stream_Buffer(s), Stream_GetPosition(s));
	Stream_Free(s, TRUE);

	bool writing_in_progress = !write_buf_->IsEmpty();
    VALIDATE(!writing_in_progress, null_str);

    write_buf_->Append(data);
    return DoWriteLoop(TRAFFIC_ANNOTATION_FOR_TESTS);
}

int RdpClient::DoWriteCRComplete(int result)
{
    if (result != OK) {
        return result;
    }
    next_state_ = STATE_READ_CC;
    return OK;
}

int RdpClient::DoReadCC()
{
    next_state_ = STATE_READ_CC_COMPLETE;
    return DoReadLoop();
}

int RdpClient::DoReadCCComplete(int result)
{
    if (result != OK) {
        return result;
    }
    next_state_ = STATE_HANDSHAKE;
    return OK;
}

int RdpClient::DoHandshake()
{
	delegate_->send_startup_msg(SDL_GetTicks(), _("Start SSL handshake"), false, nposm);

    next_state_ = STATE_HANDSHAKE_COMPLETE;

	std::unique_ptr<StreamSocket> transport_socket;
	const HostPortPair host_and_port = HostPortPair::FromIPEndPoint(server_address_);
    const SSLConfig ssl_config;
/*
    std::unique_ptr<ClientSocketHandle> connection(new ClientSocketHandle);
    connection->SetSocket(std::move(ctrl_socket_));
    std::unique_ptr<SSLClientSocket> client(
		socket_factory_->CreateSSLClientSocket(std::move(connection), host_and_port, ssl_config, context_));
*/
/*
	std::unique_ptr<SSLClientSocket> CreateSSLClientSocket(
      std::unique_ptr<StreamSocket> transport_socket,
      const HostPortPair& host_and_port,
      const SSLConfig& ssl_config);
*/
	std::unique_ptr<SSLClientSocket> client(
		socket_factory_->CreateSSLClientSocket(context_.get(), std::move(ctrl_socket_), host_and_port, ssl_config));

	int rv = client->Connect(base::Bind(&RdpClient::OnHandshakeComplete, weak_ptr_factory_.GetWeakPtr()));
	ctrl_socket_ = std::move(client);

    if (rv == ERR_IO_PENDING) {
        return rv;
    }
    rv = HandleHandshakeResult(rv);
    return rv;
}

void RdpClient::OnHandshakeComplete(int result)
{
    // SDL_Log("RdpConnection::OnIOComplete, result: %i", result);
    VALIDATE(next_state_ == STATE_HANDSHAKE_COMPLETE, null_str);

    result = HandleHandshakeResult(result);
    DoLoop(result);
}

int RdpClient::HandleHandshakeResult(int result)
{
    // 0(OK) is ok for SocketImpl::Handshake
    if (result < 0) {
		Close(result);
		return result == 0 ? ERR_CONNECTION_CLOSED : result;
	}
	delegate_->send_startup_msg(SDL_GetTicks(), _("SSL handshake success"), false, nposm);
    return OK;
}

int RdpClient::DoHandshakeComplete(int result)
{
    if (result != OK) {
        return result;
    }
    next_state_ = STATE_WRITE_MCS_REQUEST;

    handshaked_ticks_ = SDL_GetTicks();
    return OK;
}

int RdpClient::DoMcsBegin()
{
	delegate_->send_startup_msg(SDL_GetTicks(), _("Send MCS Connect Initial PDU"), false, nposm);
    next_state_ = STATE_READ_MCS_RESPONSE;

	rdpRdp* rdp = rdp_context.rdp;
	rdp_client_connect_mcs(rdp);
	return OK;
}

int RdpClient::DoWriteMcsRequestComplete(int result)
{
	next_state_ = STATE_RDP_CONNECTION_COMPLETE;
	// receive 'Server MCS Connect Response PDU with GCC Conference Create Response'
    return DoReadLoop();
}

int RdpClient::DoRdpConnectionComplete(int result)
{
    if (result != OK) {
        return result;
    }
	rdpRdp* rdp = rdp_context.rdp;
	if (rdp->state != CONNECTION_STATE_ACTIVE) {
		VALIDATE(next_state_ = STATE_RDP_CONNECTION_COMPLETE, null_str);
		return DoReadLoop();
	}

	next_state_ = STATE_READ_PDU;
    return OK;
}

int RdpClient::DoReadPDU()
{
    next_state_ = STATE_READ_PDU_COMPLETE;
    return DoReadLoop();
}

int RdpClient::DoReadPDUComplete(int result)
{
    if (result != OK) {
        return result;
    }
    return DoReadPDU();
}

int RdpClient::DoReadLoop()
{
    // Increases read buffer size if necessary.
    HttpConnection::ReadIOBuffer* read_buf = read_buf_.get();
    if (read_buf->RemainingCapacity() == 0 && !read_buf->IncreaseCapacity()) {
        SDL_Log("RdpClient::DoReadLoop, will Close()");
        Close(net::ERR_FILE_NO_SPACE);
        return ERR_CONNECTION_CLOSED;
    }

    int buf_len = read_buf->RemainingCapacity();
    int rv = socket()->Read(
        read_buf,
        buf_len,
        base::Bind(&RdpClient::OnReadCompleted,
                    weak_ptr_factory_.GetWeakPtr()));
    if (rv == ERR_IO_PENDING) {
        return rv;
    }
    rv = HandleReadResult(rv);
    return rv;
}

void RdpClient::OnReadCompleted(int rv)
{
    // SDL_Log("RdpConnection::OnReadCompleted, connection_id: %i, result: %i", connection_id, rv);
    rv = HandleReadResult(rv);
    DoLoop(rv);
}

int RdpClient::HandleReadResult(int rv)
{
	if (rv <= 0) {
		Close(rv);
		return rv == 0 ? ERR_CONNECTION_CLOSED : rv;
	}

	HttpConnection::ReadIOBuffer* read_buf = read_buf_.get();
	read_buf->DidRead(rv);

    const char* data_ptr = read_buf->StartOfBuffer();
    const int data_size = read_buf->GetSize();

    {
        RdpClient::tdid_read_lock lock(*this, (const uint8_t*)data_ptr, data_size);
        // SDL_Log("RdpClient::HandleReadResult, data_size: %i, next_state_: %i", data_size, next_state_);

		rdpRdp* rdp = rdp_context.rdp;
		if (next_state_ == STATE_READ_CC_COMPLETE) {
			rdpRdp* rdp = rdp_context.rdp;
			rdpNego* nego = rdp->nego;
			bool bval = nego_recv_cc(nego);
			VALIDATE(bval, null_str);

		} else {
			bool previal_not_active = rdp->state != CONNECTION_STATE_ACTIVE;
			VALIDATE(next_state_ == STATE_RDP_CONNECTION_COMPLETE || next_state_ == STATE_READ_PDU_COMPLETE, null_str);
			// this read may be have multi-pdu.
			int iret = 1;
			while (iret == 1 && lock.consumed() < data_size) {
				iret = rose_did_read(rdp);

				if (previal_not_active && rdp->state == CONNECTION_STATE_ACTIVE) {
					// this read maybe have both connection sequence response and post-connection response.
					// must call OnConnectioComplae immediately. because it create drdynvc channel, 
					// and data may have DVC Capabilities Request PDU, it require valid drdynvc channel.
					previal_not_active = false;
					int consumed = lock.consumed();
					SDL_Log("HandleReadResult, CONNECTION_STATE_ACTIVE, consumed: %i, data_size: %i", consumed, data_size);
					delegate_->send_startup_msg(SDL_GetTicks(), _("Finish connection sequence"), false, rdpstatus_connectionfinished);
					// next_state_ = STATE_READ_PDU;

					SDL_Log("Finish connection sequence");
					bool ok = delegate_->OnConnectionComplete();
					if (!ok) {
						return ERR_ACCESS_DENIED;
					}
				}
			}
		}

        // server_.delegate_->OnRdpRequest(*this, (const uint8_t*)data_ptr, data_size);
        int consumed_size = lock.consumed();
        VALIDATE(consumed_size <= data_size, null_str);
        if (consumed_size != 0) {
            read_buf->DidConsume(consumed_size);
        }
    }
	return OK;
}

int RdpClient::DoWriteLoop(NetworkTrafficAnnotationTag traffic_annotation) {
    HttpConnection::QueuedWriteIOBuffer* write_buf = write_buf_.get();
    if (write_buf->GetSizeToWrite() == 0) {
        return OK;
    }

    int rv = socket()->Write(
        write_buf, write_buf->GetSizeToWrite(),
        base::Bind(&RdpClient::OnWriteCompleted,
                    weak_ptr_factory_.GetWeakPtr(),
                    traffic_annotation),
        traffic_annotation);
    if (rv == ERR_IO_PENDING) {
        return rv;
    }
    rv = HandleWriteResult(rv);
    return rv;
}

void RdpClient::OnWriteCompleted(NetworkTrafficAnnotationTag traffic_annotation, int rv)
{
	rv = HandleWriteResult(rv);
/*
	if (next_state_ == STATE_WRITE_CC_COMPLETE) {
		DoLoop(rv);
	}
*/
}

int RdpClient::HandleWriteResult(int rv)
{
	if (rv < 0) {
		Close(rv);
		return rv;
	}

    HttpConnection::QueuedWriteIOBuffer* write_buf = write_buf_.get();
	{
		// threading::lock lock2(write_buf_mutex_);
		write_buf->DidConsume(rv);
	}
    if (write_buf->GetSizeToWrite() != 0) {
        // If there's still data written, write it out.
        rv = DoWriteLoop(TRAFFIC_ANNOTATION_FOR_TESTS);
    } else {
        rv = OK;
    }
	return rv;
}

void RdpClient::Close(int rv)
{
	VALIDATE_IN_RDPC_THREAD();

	if (rv != OK) {
		utils::string_map symbols;
		symbols["errcode"] = str_cast(rv);
		delegate_->send_startup_msg(SDL_GetTicks(), vgettext2("Close connect because error. errcode: $errcode", symbols), true, rdpstatus_connectionclosed);
	} else {
		delegate_->send_startup_msg(SDL_GetTicks(), _("Close connect"), false, rdpstatus_connectionclosed);
	}

	if (ctrl_socket_.get() == nullptr) {
		return;
	}
	ctrl_socket_.reset();
}

int RdpClient::did_read_layer(uint8_t* data, int bytes)
{
	int read = SDL_min(bytes, read_layer_size_ - read_layer_offset_);
	if (read > 0) {
		memcpy(data, read_layer_ptr_ + read_layer_offset_, read);
		read_layer_offset_ += read;
	}
	return read;
}

int RdpClient::did_write_layer(const uint8_t* data, int bytes)
{
	VALIDATE(bytes > 0, null_str);

	std::string data2((const char*)data, bytes);
	SDL_threadID id = SDL_ThreadID();
	if (tid_ != id) {
		// SDL_Log("RdpClient::did_write_layer, [main]tid: %lu, bytes: %i", id, bytes);
		// on android, as if in main-thread, thread_checker_.CalledOnValidThread() always return true!
		// I had to use specialized variable(in_main_thread) that current is in main thread.
		VALIDATE_IN_MAIN_THREAD();
		// Client Fast-Path Input Event PDU (TS_FP_INPUT_PDU)

		threading::lock lock2(main_thread_write_buf_mutex);
		main_thread_write_buf.push_back(data2);
		return bytes;

	} else {
		// SDL_Log("RdpClient::did_write_layer, [rdpthread]tid: %lu, bytes: %i", id, bytes);
		thread_checker_.CalledOnValidThread();
	}

	return SendData(data2);
}

void RdpClient::start_internal()
{
	write_buf_->set_max_buffer_size(max_write_buffer_size_);
	read_buf_->DisableAutoReduceBuffer();
	read_buf_->set_max_buffer_size(max_read_buffer_size_);
	read_buf_->SetCapacity(max_read_buffer_size_ / 2);
	next_state_ = STATE_TRANSPORT_CONNECT;
	DoLoop(net::OK);
}


RdpClientRose::RdpClientRose(base::Thread& thread, base::WaitableEvent& e, rtc::MessageHandler& message_handler, twebrtc_send_helper& send_helper)
	: thread_(thread)
	, delete_pend_tasks_e_(e)
	, message_handler_(message_handler)
	, send_helper_(send_helper)
	, weak_ptr_factory_(this)
	, check_slice_timeout_(300) // 300 ms
	, slice_running_(false)
	, rdp_context_(nullptr)
	, freerdp_(nullptr)
	, decoder_(nullptr)
	, desktop_(nullptr)
{
	// freerdp_server_ = rose_init_subsystem();
}

static void show_slice(int index)
{
	SDL_Log("%u ---show_slice#%i---", SDL_GetTicks(), index);
}

static void show_slice2(int index)
{
	SDL_Log("%u ---show_slice2#%i---", SDL_GetTicks(), index);
}

RdpClientRose::~RdpClientRose()
{
	VALIDATE_IN_RDPC_THREAD();

	SDL_Log("RdpClientRose::~RdpClientRose()---");
	slice_running_ = false;
	// don't call serve_.reset(), after server_->Release(), some require it's some variable keep valid.
	// server_->CloseAllConnection();
	// rose_release_subsystem(freerdp_server_);
	freerdp_disconnect(rdp_context_->instance);
	freerdp_client_context_free(rdp_context_);

	{
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 0));
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 2));
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 4));
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 1));
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 3));
		base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&show_slice, 5));
		const base::TimeDelta timeout = base::TimeDelta::FromMilliseconds(4000); // 4 second.
		base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE, base::Bind(&show_slice2, 0), timeout);
		base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE, base::Bind(&show_slice2, 2), timeout);
		base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE, base::Bind(&show_slice2, 1), timeout);
		base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE, base::Bind(&show_slice2, 3), timeout);
	}

	thread_.SetRequireDeletePendingTasks(delete_pend_tasks_e_);
	SDL_Log("---RdpClientRose::~RdpClientRose() X");
}

void RdpClientRose::RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints)
{
	pEntryPoints->Version = 1;
	pEntryPoints->Size = sizeof(RDP_CLIENT_ENTRY_POINTS_V1);
/*
	pEntryPoints->GlobalInit = wfreerdp_client_global_init;
	pEntryPoints->GlobalUninit = wfreerdp_client_global_uninit;
*/
	pEntryPoints->ContextSize = sizeof(wfContext);
/*
	pEntryPoints->ClientNew = wfreerdp_client_new;
	pEntryPoints->ClientFree = wfreerdp_client_free;
	pEntryPoints->ClientStart = wfreerdp_client_start;
	pEntryPoints->ClientStop = wfreerdp_client_stop;
*/
}

void RdpClientRose::prefix_connect()
{
	SDL_Log("RdpClientRose::prefix_connect---");
	VALIDATE(rdp_context_ == nullptr, null_str);

	memset(&clientEntryPoints_, 0, sizeof(clientEntryPoints_));
	RdpClientEntry(&clientEntryPoints_);

	rdp_context_ = freerdp_client_context_new(&clientEntryPoints_);
	wfContext* wfc = (wfContext*)rdp_context_;
	wfc->decoder = decoder_;
	wfc->desktop = desktop_;

	freerdp_ = rdp_context_->instance;
	freerdp* instance = freerdp_;
	rdpContext* context = rdp_context_;
	rdpSettings* settings = context->settings;

	SDL_Log("RdpClientRose::prefix_connect, settings->ClientHostname: %s", settings->ClientHostname);

	// /u:macbookpro15.2
	const char* username = "macbookpro15.2";
	settings->Username = _strdup(username);
	settings->Domain = _strdup("\0");
	// /p:000000 
	// /v:192.168.1.113
	settings->ServerHostname = _strdup(server_address_.ToStringWithoutPort().c_str());
	// /gfx:AVC444
	settings->SupportGraphicsPipeline = TRUE;
	settings->GfxH264 = TRUE;
	settings->GfxAVC444 = TRUE;

	// /clipboard
	// settings->RedirectClipboard = FALSE;

	if (settings->RemoteFxCodec || settings->NSCodec || settings->SupportGraphicsPipeline) {
		settings->FastPathOutput = TRUE;
		settings->LargePointerFlag =
		    0x0002; /* (LARGE_POINTER_FLAG_96x96 | LARGE_POINTER_FLAG_384x384); */
		settings->FrameMarkerCommandEnabled = TRUE;
		settings->ColorDepth = 32;
	}


	// BOOL rdp_client_connect(rdpRdp* rdp)
	rdpRdp* rdp = rdp_context_->rdp;
	rdpNego* nego = rdp->nego;
	bool bval = nego_set_cookie(rdp->nego, settings->Username);
	SDL_Log("RdpClientRose::prefix_connect, nego_set_cookie, bval: %s", bval? "true": "false");
	VALIDATE(bval, null_str);
	// nego->RequestedProtocols = PROTOCOL_HYBRID | PROTOCOL_SSL;

	// int rc = freerdp_client_start(context);
	// SDL_Log("RdpClientRose::prefix_connect, 6, rc: %i", rc);
	// VALIDATE(rc == 0, null_str);

	bval = freerdp_client_load_addins(context->channels, freerdp_->settings);
	SDL_Log("RdpClientRose::prefix_connect, freerdp_client_load_addins, bval: %s", bval? "true": "false");
	VALIDATE(bval, null_str);
#ifdef _WIN32
	freerdp_set_param_uint32(settings, FreeRDP_KeyboardLayout,
	                         (int)GetKeyboardLayout(0) & 0x0000FFFF);
#endif
	PubSub_SubscribeChannelConnected(instance->context->pubSub, wf_OnChannelConnectedEventHandler);
	PubSub_SubscribeChannelDisconnected(instance->context->pubSub,
	                                    wf_OnChannelDisconnectedEventHandler);

	freerdp_channels_pre_connect(instance->context->channels, instance);
	SDL_Log("---RdpClientRose::prefix_connect, X");
}

void RdpClientRose::SetUp(uint32_t ipv4, int port, void* decoder, void* desktop)
{
	IPAddress address((const uint8_t*)&ipv4, 4);
	server_address_ = IPEndPoint(address, port);
	decoder_ = decoder;
	desktop_ = desktop;

	prefix_connect();
	VALIDATE(rdp_context_ != nullptr, null_str);
	client_.reset(new RdpClient(server_address_, this, *rdp_context_));
	rose_register_extra(rdp_context_, did_rose_read_layer, did_rose_write_layer, this, client_.get());

	VALIDATE(!slice_running_, null_str);
	SDL_Log("will run rdpc_slice");
	slice_running_ = true;
	base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&RdpClientRose::rdpc_slice, weak_ptr_factory_.GetWeakPtr(), check_slice_timeout_));

	client_->start_internal();
}

void RdpClientRose::send_startup_msg(uint32_t ticks, const std::string& msg, bool fail, int rdpstatus)
{
	twebrtc_send_helper::tlock lock(send_helper_);
	if (lock.can_send()) {
		gui2::tdesktop::tmsg_startup_msg* pdata = new gui2::tdesktop::tmsg_startup_msg(ticks, msg, fail, rdpstatus);
		instance->sdl_thread().Send(RTC_FROM_HERE, &message_handler_, gui2::tdesktop::MSG_STARTUP_MSG, pdata);
	}
}

bool RdpClientRose::OnConnectionComplete()
{
	freerdp* instance = freerdp_;
	freerdp_channels_post_connect(instance->context->channels, instance);

	rdpRdp* rdp = rdp_context_->rdp;
	VALIDATE(rdp->last_recv_rtt_request_ticks == 0, null_str);
	rdp->last_recv_rtt_request_ticks = SDL_GetTicks();

	return true;
}

void RdpClientRose::hdrop_copied(const std::string& files)
{
	wfContext* client = (wfContext*)rdp_context_;
	// send pdu require RdpdThread running, don't block it when HdropCopied.
	if (client != nullptr && client->cliprdr->HdropCopied != nullptr) {
		client->cliprdr->HdropCopied(client->cliprdr, files.c_str(), files.size());
	}
}

bool RdpClientRose::hdrop_paste(gui2::tprogress_& progress, const std::string& path, int* err_code, char* err_msg, int max_bytes)
{
	wfContext* client = (wfContext*)rdp_context_;
	int ret = cliprdr_errcode_ok;
	if (client != nullptr && client->cliprdr->HdropPaste != nullptr) {
		ret = client->cliprdr->HdropPaste(client->cliprdr, path.c_str(), &progress, err_msg, max_bytes);
	}
	*err_code = ret;
	return *err_code == cliprdr_errcode_ok;
}

bool RdpClientRose::can_hdrop_paste() const
{
	bool ret = false;
	wfContext* client = (wfContext*)rdp_context_;
	if (client != nullptr && client->cliprdr->CanHdropPaste != nullptr) {
		ret = client->cliprdr->CanHdropPaste(client->cliprdr);
	}
	return ret;
}

bool RdpClientRose::push_explorer_update(uint32_t code, uint32_t data1, uint32_t data2, uint32_t data3)
{
	if (client_.get() == nullptr) {
		return false;
	}

	LEAGOR_EXPLORER_UPDATE update;
	memset(&update, 0, sizeof(update));

	update.code = code;
	update.data1 = data1;
	update.data2 = data2;
	update.data3 = data3;

	threading::lock lock2(rdpc_thread_explorer_update_mutex_);
	rdpc_thread_explorer_update_.push_back(update);
	return true;
}

void RdpClientRose::did_slice_quited(int timeout1)
{
	VALIDATE(client_.get() != nullptr, null_str);
	if (!slice_running_) {
		SDL_Log("will stop rdpc_slice");
		return;
	}

	// base::ThreadTaskRunnerHandle::Get()->PostTask(FROM_HERE, base::Bind(&RdpServerRose::rdpd_slice, weak_ptr_factory_.GetWeakPtr(), timeout1));

	const base::TimeDelta timeout = base::TimeDelta::FromMilliseconds(20); // 20 ms.
	base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(FROM_HERE, base::Bind(&RdpClientRose::rdpc_slice, weak_ptr_factory_.GetWeakPtr(), timeout1), timeout);
}

void RdpClientRose::rdpc_slice(int timeout)
{
	VALIDATE_IN_RDPC_THREAD();
	tauto_destruct_executor destruct_executor(std::bind(&RdpClientRose::did_slice_quited, this, timeout));

	if (!slice_running_) {
		return;
	}

	RdpClient* client_ptr = client_.get();

	const uint32_t now = SDL_GetTicks();

	if (client_ptr != nullptr && client_ptr->socket() != nullptr) {
		// whay 12 seconds? base on rtt_threshold is 5 second in launcher's rdp_server_rose.cc. 
		// so 12 at least tow rtt-request.
		const uint32_t recv_rtt_request_threshold = 13 * 1000; // 13 second
		rdpRdp* rdp = rdp_context_->rdp;
		if (rdp->last_recv_rtt_request_ticks != 0 && now - rdp->last_recv_rtt_request_ticks >= recv_rtt_request_threshold) {
			// Within capture_thread_threshold, handshaked connection must start the capture thread (which means entering activeed, etc.), 
			// otherwise an exception is considered, and force to disconnect.
			SDL_Log("%u rdpc_slice hasn't received rtt-message-request over %u seconds, think as disconnect", 
				now, (now - rdp->last_recv_rtt_request_ticks) / 1000);
			utils::string_map symbols;
			symbols["time"] = format_elapse_hms((now - rdp->last_recv_rtt_request_ticks) / 1000, false);
			send_startup_msg(now, vgettext2("Hasn't received RTT Measure Request over $time, think as disconnect", symbols), true, rdpstatus_connectionclosed);
			client_ptr->Close(net::OK);
			return;
		}

		{
			threading::lock lock2(client_ptr->main_thread_write_buf_mutex);
			std::vector<std::string>& write_buf = client_ptr->main_thread_write_buf;
			for (std::vector<std::string>::const_iterator it  = write_buf.begin(); it != write_buf.end(); ++ it) {
				const std::string& data = *it;
				client_ptr->SendData(data);
			}
			write_buf.clear();
		}

		if (!rdpc_thread_explorer_update_.empty()) {
			threading::lock lock2(rdpc_thread_explorer_update_mutex_);
			for (std::vector<LEAGOR_EXPLORER_UPDATE>::const_iterator it  = rdpc_thread_explorer_update_.begin(); it != rdpc_thread_explorer_update_.end(); ++ it) {
				const LEAGOR_EXPLORER_UPDATE& update = *it;
				wf_leagor_client_send_explorer_update(rdp_context_, &update);
			}
			rdpc_thread_explorer_update_.clear();
		}

		// std::vector<std::string> main_thread_write_buf;
		freerdp_channels_check_fds(rdp_context_->channels, rdp_context_->instance);
	}
}

trdp_manager::trdp_manager()
	: e_(base::WaitableEvent::ResetPolicy::AUTOMATIC, base::WaitableEvent::InitialState::NOT_SIGNALED)
{}

trdp_manager::~trdp_manager()
{
	stop();
}

void trdp_manager::did_set_event()
{
	e_.Signal();
}

void trdp_manager::start_internal(uint32_t ipaddr, int port, void* decoder, void* desktop, rtc::MessageHandler* message_handler_ptr, twebrtc_send_helper* send_helper_ptr)
{
	game_config::rdpc_tid = SDL_ThreadID();
	{
		tauto_destruct_executor destruct_executor(std::bind(&trdp_manager::did_set_event, this));
		delegate_.reset(new RdpClientRose(*thread_.get(), e_, *message_handler_ptr, *send_helper_ptr));
	}
	// Setup will send_startup_msg to main-thread, avoid deallock, main-thread must not wait Setup completed.
	delegate_->SetUp(ipaddr, port, decoder, desktop);
}

void trdp_manager::stop_internal()
{
	// thread_.SetRequireDeletePendingTasks in RdpClientRose::~RdpClientRose() signal e_, don't use tauto_destruct_executor.
	// tauto_destruct_executor destruct_executor(std::bind(&trdp_manager::did_set_event, this));
	VALIDATE(delegate_.get() != nullptr, null_str);
	delegate_.reset();
}

void trdp_manager::start(uint32_t ipaddr, int port, void* decoder, void* desktop, rtc::MessageHandler& message_handler, twebrtc_send_helper& send_helper)
{
	CHECK(thread_.get() == nullptr);
	thread_.reset(new base::Thread("RdpThread"));

	base::Thread::Options socket_thread_options;
	socket_thread_options.message_pump_type = base::MessagePumpType::IO;
	socket_thread_options.timer_slack = base::TIMER_SLACK_MAXIMUM;
	CHECK(thread_->StartWithOptions(socket_thread_options));

	thread_->task_runner()->PostTask(FROM_HERE, base::BindOnce(&trdp_manager::start_internal, base::Unretained(this), ipaddr, port, decoder, desktop, &message_handler, &send_helper));
	e_.Wait();
}

void trdp_manager::stop()
{
	if (thread_.get() == nullptr) {
		VALIDATE(delegate_.get() == nullptr, null_str);
		return;
	}

	CHECK(delegate_.get() != nullptr);
	// stop_internal will reset delegate_.

	thread_->task_runner()->PostTask(FROM_HERE, base::BindOnce(&trdp_manager::stop_internal, base::Unretained(this)));
	e_.Wait();
	
	SDL_Log("trdp_manager::stop(), pre thread_.reset()"); 

	thread_.reset();
	game_config::rdpc_tid = nposm;
}

bool trdp_manager::push_explorer_update(uint32_t code, uint32_t data1, uint32_t data2, uint32_t data3)
{
	if (delegate_.get() != nullptr) {
		return delegate_->push_explorer_update(code, data1, data2, data3);
	}
	return false;
}

}  // namespace net
