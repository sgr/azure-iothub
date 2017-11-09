// -*- coding: utf-8-unix -*-
#include "iothub.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <sstream>
#include <thread>

#include <azure_c_shared_utility/platform.h>
#include <azure_c_shared_utility/threadapi.h>
#include <azure_c_shared_utility/crt_abstractions.h>
#include <azure_c_shared_utility/shared_util_options.h>
#include <azure_c_shared_utility/xlogging.h>
#include <azureiot/iothub_client_options.h>
#include <azureiot/iothub_message.h>
#include <certs.h>

#ifdef AZURE_IOT_AMQP
#include <azureiot/iothubtransportamqp.h>
#include <azureiot/iothubtransport_amqp_common.h>
#include <azureiot/iothubtransport_amqp_device.h>
#include <azureiot/iothubtransportamqp_websockets.h>
#elif  AZURE_IOT_MQTT
#include <azureiot/iothubtransportmqtt.h>
#else
#include <azureiot/iothubtransporthttp.h>
#endif

namespace {

const uint64_t MESSAGE_TIMEOUT_MS = 30000;

// HTTP OPTION VALUES
const bool HTTP_BATCHING = true;
const unsigned int TIMEOUT_MS = 30000;
const unsigned int MINIMUM_POLLING_TIME = 9;

// AMQP OPTION VALUES
const uint32_t C2D_KEEP_ALIVE_FREQ_SECS = 120;
const size_t CBS_REQUEST_TIMEOUT = 30;
const size_t EVENT_SEND_TIMEOUT_SECS = 30;

// MQTT OPTION VALUE
const int KEEP_ALIVE = 240;

}

namespace aziot {

struct send_order {
  int64_t key;
  IOTHUB_MESSAGE_HANDLE msg_hdl;
  std::function< void() > fallback = nullptr;
  std::function< void() > finally = nullptr;
};

class iothub::impl {
 public:
  impl(const std::string connection_string);
  ~impl();
  void send(std::map< std::string, std::string > prop, std::shared_ptr< std::vector< uint8_t > > data, std::function< void() >&& fallback);
  void send(std::map< std::string, std::string > prop, std::shared_ptr< std::string > str, std::function< void() >&& fallback);

 private:
  const std::string _connection_string;
  IOTHUB_CLIENT_LL_HANDLE _iothub_client_handle;
  void send(IOTHUB_MESSAGE_HANDLE msg_hdl, std::function< void() >&& fallback);

  // 送信ループ用
  std::thread _th;
  std::atomic_bool _stop;
  std::mutex _mtx;
  std::condition_variable _cond;
  void run();

  std::map<int64_t, send_order*> _order;
};

}

extern "C" {
  #include <certs.c>

  // Azure IoT Hub SDK custom logging function
  void azure_log(LOG_CATEGORY log_category, const char* file, const char* func, int line, unsigned int options, const char* format, ...) {
    // ファイル名の処理
    std::string path(file);
    int path_i = path.find_last_of("/") + 1;
    std::string filename = path.substr(path_i, path.size());

    // メッセージの処理
    va_list args1;
    va_start(args1, format);
    va_list args2;
    va_copy(args2, args1);
    std::vector<char> buf(1 + std::vsnprintf(nullptr, 0, format, args1));
    va_end(args1);
    std::vsnprintf(buf.data(), buf.size(), format, args2);
    va_end(args2);

    std::ostringstream msg;
    msg << "[azure-iot-sdk-c] " << func << ": " << buf.data() << " (" << filename << "(" + std::to_string(line) << "))";

    switch(log_category) {
    case AZ_LOG_TRACE:
      aziot::iothub::log(aziot::loglevel::trace, msg.str());
      break;
    case AZ_LOG_INFO:
      aziot::iothub::log(aziot::loglevel::info, msg.str());
      break;
    case AZ_LOG_ERROR:
      aziot::iothub::log(aziot::loglevel::error, msg.str());
    }
  }

  void send_confirmation_callback(IOTHUB_CLIENT_CONFIRMATION_RESULT result, void* user_context) {
    aziot::send_order* o = (aziot::send_order*)user_context;
    IoTHubMessage_Destroy(o->msg_hdl);

    if (result != IOTHUB_CLIENT_CONFIRMATION_OK) {
      std::string msg("CALL FALLBACK CAUSED BY FAILED SENDING ");
      msg += std::to_string(o->key);
      aziot::iothub::log(aziot::loglevel::trace, msg);
      o->fallback();
    }

    o->finally();
  }
}

std::function< void(const aziot::loglevel level, const std::string& msg) > aziot::iothub::log = [](const aziot::loglevel level, const std::string& msg) {
  std::string slevel("[trace] ");
  switch (level) {
    case aziot::loglevel::info:
      slevel = "[info] ";
      break;
    case aziot::loglevel::debug:
      slevel = "[debug] ";
      break;
    case aziot::loglevel::error:
      slevel = "[error] ";
  }

  std::cerr << slevel << msg << std::endl;
};

aziot::iothub::impl::impl(const std::string connection_string) : _connection_string(connection_string), _stop(false) {
  xlogging_set_log_function(azure_log);
  if (platform_init()) {
    throw std::runtime_error("FAILED TO INITIALIZE THE PLATFORM");
  }

#ifdef AZURE_IOT_AMQP
  IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol = AMQP_Protocol_over_WebSocketsTls; //AMQP_Protocol;
#elif  AZURE_IOT_MQTT
  IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol = MQTT_Protocol;
#else
  IOTHUB_CLIENT_TRANSPORT_PROVIDER protocol = HTTP_Protocol;
#endif

  if ((_iothub_client_handle = IoTHubClient_LL_CreateFromConnectionString(_connection_string.c_str(), protocol)) == NULL) {
    throw std::runtime_error("FAILED TO CREATE THE IOTHUB CLIENT");
  }

#ifdef AZURE_IOT_AMQP
  // Set keep alive is optional. If it is not set the default (240 secs) will be used.
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_C2D_KEEP_ALIVE_FREQ_SECS, &C2D_KEEP_ALIVE_FREQ_SECS) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"c2d_keep_alive_freq_secs\"");
  }
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_CBS_REQUEST_TIMEOUT, &CBS_REQUEST_TIMEOUT) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"cbs_request_timeout\"");
  }
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_EVENT_SEND_TIMEOUT_SECS, &EVENT_SEND_TIMEOUT_SECS) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"event_send_timeout_secs\"");
  }
#elif  AZURE_IOT_MQTT
  // if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_KEEP_ALIVE, &KEEP_ALIVE) != IOTHUB_CLIENT_OK) {
  //   throw std::runtime_error("FAILED TO SET OPTION \"keepalive\"");
  // }
#else // HTTP
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, "timeout", &TIMEOUT_MS) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"timeout\"");
  }
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_BATCHING, &HTTP_BATCHING) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"Batching\"");
  }
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_MIN_POLLING_TIME, &MINIMUM_POLLING_TIME) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"MinimumPollingTime\"");
  }
#endif
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_TRUSTED_CERT, certificates) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"TrustedCerts\"");
  }
  if (IoTHubClient_LL_SetOption(_iothub_client_handle, OPTION_MESSAGE_TIMEOUT, &TIMEOUT_MS) != IOTHUB_CLIENT_OK) {
    throw std::runtime_error("FAILED TO SET OPTION \"messageTimeout\"");
  }

  /* Setting Message call back, so we can receive Commands. */
  auto mc = [&] (IOTHUB_MESSAGE_HANDLE message, void* userContextCallback) {
    return IOTHUBMESSAGE_ACCEPTED;
  };
  if (IoTHubClient_LL_SetMessageCallback(_iothub_client_handle, mc, NULL) == IOTHUB_CLIENT_OK) {
    aziot::iothub::log(aziot::loglevel::trace, "SUCCESS: IoTHubClient_LL_SetMessageCallback");
  } else {
    throw std::runtime_error("FAILED IoTHubClient_LL_SetMessageCallback");
  }

  auto csc = [&] (IOTHUB_CLIENT_CONNECTION_STATUS result, IOTHUB_CLIENT_CONNECTION_STATUS_REASON reason, void* userContextCallback) {
    std::string msg("CONNECTION STATUS: ");
    msg += std::to_string(result) + "(" + std::to_string(reason) + ")";
    aziot::iothub::log(aziot::loglevel::debug, msg);
  };
  if (IoTHubClient_LL_SetConnectionStatusCallback(_iothub_client_handle, csc, NULL) == IOTHUB_CLIENT_OK) {
    aziot::iothub::log(aziot::loglevel::trace, "SUCCESS: IoTHubClient_LL_SetConnectionStatusCallback");
  } else {
    throw std::runtime_error("FAILED IoTHubClient_LL_SetConnectionStatusCallback");
  }

  _th = std::thread(&aziot::iothub::impl::run, this);
}

aziot::iothub::impl::~impl() {
  _stop.store(true);
  _th.join();
  IoTHubClient_LL_Destroy(_iothub_client_handle);
  platform_deinit();
}

void aziot::iothub::impl::run() {
  IOTHUB_CLIENT_STATUS status;

  while (_stop.load() == false) {
    if ((IoTHubClient_LL_GetSendStatus(this->_iothub_client_handle, &status) == IOTHUB_CLIENT_OK) &&
        (status == IOTHUB_CLIENT_SEND_STATUS_BUSY)) {
      aziot::iothub::log(aziot::loglevel::trace, "IoTHubClient_LL_DoWork");
      IoTHubClient_LL_DoWork(this->_iothub_client_handle);
    }
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  // バックオーダーを処理させる
  for (int i = 0; i < _order.size() * 2; i++) {
    IoTHubClient_LL_DoWork(this->_iothub_client_handle);
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  {
    std::unique_lock<std::mutex> lock(_mtx);
    _cond.wait(lock, [&]() { return _order.size() == 0; });
  }
}

void aziot::iothub::impl::send(IOTHUB_MESSAGE_HANDLE msg_hdl, std::function< void() >&& fallback) {
  int64_t now = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now().time_since_epoch()).count();
  send_order* o = new send_order;
  o->key = now;
  o->msg_hdl = msg_hdl;
  o->fallback = fallback;
  o->finally = [this, now]() {
    {
      std::lock_guard< std::mutex > lock(this->_mtx);
      this->_order.erase(now);
    }
    this->_cond.notify_one();
  };

  {
    std::lock_guard< std::mutex > lock(_mtx);
    IOTHUB_CLIENT_RESULT result = IoTHubClient_LL_SendEventAsync(_iothub_client_handle, msg_hdl, send_confirmation_callback, (void*)o);
    if (result == IOTHUB_CLIENT_OK) {
      _order[now] = o;
      std::string lmsg("IoTHubClient_LL_SendEventAsync (");
      lmsg += std::to_string(now) + ") => " + std::to_string(_order.size());
      aziot::iothub::log(aziot::loglevel::trace, lmsg);
    } else {
      fallback();
      throw std::runtime_error("FAILED: IoTHubClient_SendEventAsync");
    }
  }
}

void aziot::iothub::impl::send(std::map< std::string, std::string > prop, std::shared_ptr< std::vector< uint8_t > > data, std::function< void() >&& fallback) {
  IOTHUB_MESSAGE_HANDLE msg_hdl = IoTHubMessage_CreateFromByteArray(data->data(), data->size());
  if (msg_hdl != NULL) {
    MAP_HANDLE prop_map = IoTHubMessage_Properties(msg_hdl);
    for (auto it = prop.cbegin(); it != prop.cend() ; ++it) {
      Map_AddOrUpdate(prop_map, it->first.c_str(), it->second.c_str());
    }
    return send(msg_hdl, std::move(fallback));
  } else {
    throw std::runtime_error("FAILED IoTHubMessage_CreateFromByteArray");
  }
}

void aziot::iothub::impl::send(std::map< std::string, std::string > prop, std::shared_ptr< std::string > str, std::function< void() >&& fallback) {
  IOTHUB_MESSAGE_HANDLE msg_hdl = IoTHubMessage_CreateFromString(str->c_str());
  if (msg_hdl != NULL) {
    MAP_HANDLE prop_map = IoTHubMessage_Properties(msg_hdl);
    for (auto it = prop.cbegin(); it != prop.cend() ; ++it) {
      Map_AddOrUpdate(prop_map, it->first.c_str(), it->second.c_str());
    }
    return send(msg_hdl, std::move(fallback));
  } else {
    throw std::runtime_error("FAILED IoTHubMessage_CreateFromString");
  }
}

aziot::iothub::iothub(const std::string connection_string) :
    _impl(new aziot::iothub::impl(connection_string)) {
}

aziot::iothub::~iothub() {
}

void aziot::iothub::send(std::map< std::string, std::string > prop, std::shared_ptr< std::vector< uint8_t > > data) {
  return _impl->send(prop, data, [this, data]() {
      if (this->send_bytes_fallback) {
        aziot::iothub::log(aziot::loglevel::error, "CALL send_fallback");
        this->send_bytes_fallback(data).get();
      }
    });
}

void aziot::iothub::send(std::map< std::string, std::string > prop, std::shared_ptr< std::string > str) {
  return _impl->send(prop, str, [this, str]() {
      if (this->send_string_fallback) {
        aziot::iothub::log(aziot::loglevel::error, "CALL send_fallback");
        this->send_string_fallback(str).get();
      }
    });
}
