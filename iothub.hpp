// -*- coding: utf-8-unix -*-
#ifndef AZURE_IOTHUB_H
#define AZURE_IOTHUB_H

#include <functional>
#include <future>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace aziot {

enum class loglevel { trace, info, debug, error };

class iothub {
 public:
  iothub(const std::string connection_string);
  iothub() = delete;
  ~iothub();
  void send(std::map< std::string, std::string > prop, std::shared_ptr< std::vector< uint8_t > > data);
  void send(std::map< std::string, std::string > prop, std::shared_ptr< std::string > str);

  static std::function< void(const aziot::loglevel level, const std::string& msg) > log;

  std::function<std::future< bool >(std::shared_ptr< std::vector< uint8_t > > data) > send_bytes_fallback = nullptr;
  std::function<std::future< bool >(std::shared_ptr< std::string > str) > send_string_fallback = nullptr;

 private:
  class impl;
  std::unique_ptr<impl> _impl;
};

}
#endif // AZURE_IOTHUB_H
