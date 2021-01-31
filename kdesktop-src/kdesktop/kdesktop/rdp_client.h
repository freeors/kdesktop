// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RDP_CLIENT_H_
#define RDP_CLIENT_H_

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <string>
#include <unordered_map>

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "net/http/http_status_code.h"
#include "net/traffic_annotation/network_traffic_annotation.h"
#include "net/base/io_buffer.h"
#include "net/base/ip_endpoint.h"
#include <rtc_base/thread_checker.h>
#include <net/base/address_list.h>
#include "net/log/net_log_with_source.h"
#include "net/server/http_connection.h" // will use met::HttpConnection::QueuedWriteIOBuffer

#include <net/cert/mock_cert_verifier.h>
#include <net/http/transport_security_state.h>
#include <net/cert/do_nothing_ct_verifier.h>
#include <net/cert/ct_policy_enforcer.h>
#include <net/socket/ssl_client_socket.h>

// webrtc
#include "serialization/string_utils.hpp"
// #include "rtc_base/event.h"
#include "base/threading/thread.h"

#include "gui/dialogs/dialog.hpp"
#include "thread.hpp"
#include "wml_exception.hpp"

#include "wf_client.hpp"

enum {rdpstatus_connectionfinished, rdpstatus_connectionclosed};

namespace net {

class RdpConnection;
class IPEndPoint;
class ServerSocket;
class StreamSocket;
class ClientSocketFactory;

class RdpServerRequestInfo
{
public:
	RdpServerRequestInfo() {}
	~RdpServerRequestInfo() {}

	// Request peer address.
	IPEndPoint peer;
};

class RdpClient
{
public:
	// Delegate to handle http/websocket events. Beware that it is not safe to
	// destroy the RdpServer in any of these callbacks.
	class Delegate {
	public:
		virtual ~Delegate() {}

		virtual void send_startup_msg(uint32_t ticks, const std::string& msg, bool fail, int rdpstatus) = 0;
		virtual bool OnConnectionComplete() = 0;
		virtual void OnConnect() = 0;
		// virtual void OnRdpRequest(RdpConnection& connection, const uint8_t* buf, int buf_len) = 0;
		virtual void OnClose() = 0;
	};

	// Instantiates a http server with |server_socket| which already started
	// listening, but not accepting.  This constructor schedules accepting
	// connections asynchronously in case when |delegate| is not ready to get
	// callbacks yet.
	RdpClient(const IPEndPoint& server_address, RdpClient::Delegate* delegate, rdpContext& rdp_context);
	~RdpClient();

	void start_internal();
	int SendData(const std::string& data);
	StreamSocket* socket() const { return ctrl_socket_.get(); }
	void Close(int rv);

	struct tdid_read_lock {
		tdid_read_lock(RdpClient& _owner, const uint8_t* data, int size)
			: owner(_owner)
		{
			VALIDATE(owner.read_layer_ptr_ == nullptr && owner.read_layer_size_ == 0 && owner.read_layer_offset_ == 0, null_str);
			owner.read_layer_ptr_ = data;
			owner.read_layer_size_ = size;
		}
		int consumed() const
		{
			VALIDATE(owner.read_layer_ptr_ != nullptr && owner.read_layer_size_ >= owner.read_layer_offset_, null_str);
			return owner.read_layer_offset_;
		}
		~tdid_read_lock()
		{
			VALIDATE(owner.read_layer_ptr_ != nullptr && owner.read_layer_size_ >= owner.read_layer_offset_, null_str);
			owner.read_layer_ptr_ = nullptr;
			owner.read_layer_size_ = 0;
			owner.read_layer_offset_ = 0;
		}
		RdpClient& owner;
	};
	int did_read_layer(uint8_t* data, int bytes);
	int did_write_layer(const uint8_t* data, int bytes);

private:
	friend class RdpClientRose;

	enum State {
		STATE_TRANSPORT_CONNECT,
		STATE_TRANSPORT_CONNECT_COMPLETE,
		STATE_WRITE_CR,
		STATE_WRITE_CR_COMPLETE,
		STATE_READ_CC,
		STATE_READ_CC_COMPLETE,
		STATE_HANDSHAKE,
		STATE_HANDSHAKE_COMPLETE,
		STATE_WRITE_MCS_REQUEST,
		STATE_READ_MCS_RESPONSE,
		STATE_RDP_CONNECTION_COMPLETE,
		STATE_READ_PDU,
		STATE_READ_PDU_COMPLETE,
		STATE_NONE,
	};

	void OnHandshakeComplete(int result);
	void OnIOComplete(int result);
	int DoLoop(int result);

	int DoTransportConnect();
	int DoTransportConnectComplete(int result);
	int DoWriteCR();
	int DoWriteCRComplete(int result);
	int DoReadCC();
	int DoReadCCComplete(int result);
	int DoHandshake();
	int DoHandshakeComplete(int result);
	int HandleHandshakeResult(int result);
	int DoMcsBegin();
	int DoWriteMcsRequestComplete(int result);
	int DoRdpConnectionComplete(int result);
	int DoReadPDU();
	int DoReadPDUComplete(int result);

	net::StreamSocket* getNetSock() { return ctrl_socket_.get(); }
	int sendData_chromium(net::StreamSocket* netSock, const uint8_t* data, int dataSize);

	int DoReadLoop();
	void OnReadCompleted(int rv);
	int HandleReadResult(int rv);

	int DoWriteLoop(NetworkTrafficAnnotationTag traffic_annotation);
	void OnWriteCompleted(NetworkTrafficAnnotationTag traffic_annotation,
						int rv);
	int HandleWriteResult(int rv);

public:
	rdpContext& rdp_context;
	threading::mutex main_thread_write_buf_mutex;
	std::vector<std::string> main_thread_write_buf;

private:
	rtc::ThreadChecker thread_checker_;
	SDL_threadID tid_;
	const IPEndPoint& server_address_;
	RdpClient::Delegate* const delegate_;

	base::WeakPtrFactory<RdpClient> weak_ptr_factory_;

	uint32_t create_ticks_;
	uint32_t handshaked_ticks_;

	// AddressList addresses_;
	NetLogWithSource net_log2_;
/*
	scoped_refptr<net::IOBufferWithSize> write_buf_;
*/
	State next_state_;
/*
	int resolve_result_;
*/
	ClientSocketFactory* const socket_factory_;
/*
	bool use_chromium_;
	responseHandler* describeResponseHandler_;
	Authenticator* describeAuthenticator_;
	int socket_op_result_;
*/
	std::unique_ptr<StreamSocket> ctrl_socket_;
/*
	net::StreamSocket* fOutputSocket_;
	bool read_by_rtpinterface_;
*/
	const int max_read_buffer_size_;
	const int max_write_buffer_size_;
	const scoped_refptr<HttpConnection::ReadIOBuffer> read_buf_;
	const scoped_refptr<HttpConnection::QueuedWriteIOBuffer> write_buf_;

	std::unique_ptr<SSLConfigService> ssl_config_service_;
	std::unique_ptr<MockCertVerifier> cert_verifier_;
	std::unique_ptr<TransportSecurityState> transport_security_state_;
	std::unique_ptr<DoNothingCTVerifier> ct_verifier_;
	std::unique_ptr<DefaultCTPolicyEnforcer> ct_policy_enforcer_;
	std::unique_ptr<SSLClientSessionCache> ssl_client_session_cache_;
	// SSLClientSocketContext context_;
	std::unique_ptr<SSLClientContext> context_;
	std::unique_ptr<SSLClientSocket> sock_;

	const uint8_t* read_layer_ptr_;
	int read_layer_size_;
	int read_layer_offset_;

	DISALLOW_COPY_AND_ASSIGN(RdpClient);
};

class RdpClientRose: public RdpClient::Delegate
{
	friend class trdp_manager;
public:
	RdpClientRose(base::Thread& thread, base::WaitableEvent& e, rtc::MessageHandler& desktop, twebrtc_send_helper& send_helper);
	~RdpClientRose();

	void SetUp(uint32_t ipaddr, int port, void* decoder, void* desktop);

	bool OnConnectionComplete() override;
	void OnConnect() override {}

	// void OnRdpRequest(RdpConnection& connection, const uint8_t* buf, int buf_len) override;

	void OnClose() override {}

	RdpClient* client() { return client_.get(); }
/*
	const IPEndPoint& server_address() const { return server_address_; }
	const std::string& server_url() const { return server_url_; }

	void clipboard_updated(const std::string& text);
*/
	void hdrop_copied(const std::string& files);
	bool hdrop_paste(gui2::tprogress_& progress, const std::string& path, int* err_code, char* err_msg, int max_bytes);
	bool can_hdrop_paste() const;
	bool push_explorer_update(uint32_t code, uint32_t data1, uint32_t data2, uint32_t data3);

private:
	// void did_connect_bh();
	void rdpc_slice(int timeout);
	void did_slice_quited(int timeout);
	// void handle_pause_record_screen(RdpConnection& connection, bool desire_pause);
	void prefix_connect();
	void RdpClientEntry(RDP_CLIENT_ENTRY_POINTS* pEntryPoints);

	void send_startup_msg(uint32_t ticks, const std::string& msg, bool fail, int rdpstatus) override;

protected:
	base::Thread& thread_;
	base::WaitableEvent& delete_pend_tasks_e_;
	rtc::MessageHandler& message_handler_;
	twebrtc_send_helper& send_helper_;
	std::unique_ptr<RdpClient> client_;
	IPEndPoint server_address_;
	std::string server_url_;
	void* decoder_;
	void* desktop_;

private:
	base::WeakPtrFactory<RdpClientRose> weak_ptr_factory_;
	const int check_slice_timeout_;
	bool slice_running_;

	// freerdp section
	RDP_CLIENT_ENTRY_POINTS clientEntryPoints_;
	rdpContext* rdp_context_;
	freerdp* freerdp_;

	threading::mutex rdpc_thread_explorer_update_mutex_;
	std::vector<LEAGOR_EXPLORER_UPDATE> rdpc_thread_explorer_update_;
};

class trdp_manager
{
public:
	trdp_manager();
	~trdp_manager();

	const std::string& url() const
	{ 
		// return delegate_.get() != nullptr? delegate_->server_url(): null_str; 
		return null_str;
	}

	RdpClient& rdp_client() 
	{ 
		return *delegate_.get()->client_.get();
	}

	RdpClientRose& rdp_delegate() 
	{ 
		return *delegate_.get();
	}

	void start(uint32_t ipaddr, int port, void* decoder, void* desktop, rtc::MessageHandler& message_handler, twebrtc_send_helper& send_helper);
	void stop();
	bool push_explorer_update(uint32_t code, uint32_t data1, uint32_t data2, uint32_t data3);

private:
	void did_set_event();
	void start_internal(uint32_t ipaddr, int port, void* decoder, void* desktop, rtc::MessageHandler* desktop_ptr, twebrtc_send_helper* send_helper_ptr);
	void stop_internal();

private:
	std::unique_ptr<base::Thread> thread_;
	std::unique_ptr<RdpClientRose> delegate_;
	// rtc::Event e_;
	base::WaitableEvent e_;
};

}  // namespace net

#endif // RDP_CLIENT_H_
