/*
 * ZASR - Streaming ASR Server
 * Copyright (C) 2025-2026 zighouse <zighouse@users.noreply.github.com>
 *
 * Licensed under the MIT License
 */

#ifndef ZASR_SERVER_H
#define ZASR_SERVER_H

#include <memory>
#include <string>
#include <map>
#include <set>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>

#include <fstream>
#include "asio.hpp"
#include "websocketpp/config/asio_no_tls.hpp"
#include "websocketpp/server.hpp"

#include "zasr-config.h"
#include "zasr-connection.h"

namespace zasr {
  using server = websocketpp::server<websocketpp::config::asio>;
  using connection_hdl = websocketpp::connection_hdl;


class ZAsrServer {
 public:
  explicit ZAsrServer(const ZAsrConfig& config);
  ~ZAsrServer();
  
  // 禁止拷贝和赋值
  ZAsrServer(const ZAsrServer&) = delete;
  ZAsrServer& operator=(const ZAsrServer&) = delete;
  
  // 启动服务器
  bool Start();
  
  // 停止服务器
  void Stop();
  
  // 获取配置
  const ZAsrConfig& GetConfig() const { return config_; }
  
  // 发送消息到客户端
  void SendMessage(connection_hdl hdl, const std::string& message);
  
  // 检查连接是否存在
  bool Contains(connection_hdl hdl) const;
  
  // 获取连接上下文
  asio::io_context& GetConnectionContext() { return io_conn_; }
  asio::io_context& GetWorkContext() { return io_work_; }

  // 关闭连接
  void Close(connection_hdl hdl, websocketpp::close::status::value code,
             const std::string& reason);
  
 private:
  // 初始化WebSocket服务器
  void SetupServer();
  
  // 设置日志
  void SetupLog();
  
  // 启动工作线程
  void StartWorkerThreads();
  
  // 停止工作线程
  void StopWorkerThreads();
  
  // 连接超时检查
  void CheckTimeouts();
  
  // WebSocket事件处理
  void OnOpen(connection_hdl hdl);
  void OnClose(connection_hdl hdl);
  void OnMessage(connection_hdl hdl, server::message_ptr msg);
  
  // 从连接句柄获取连接对象
  std::shared_ptr<ZAsrConnection> GetConnection(connection_hdl hdl);
  
 private:
  ZAsrConfig config_;
  
  // ASIO上下文
  asio::io_context io_conn_;
  asio::io_context io_work_;
  
  // 保持 io_work_ 一直运行
  asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
  
  // WebSocket服务器
  server ws_server_;
  
  // 工作线程池
  std::vector<std::thread> worker_threads_;
  std::vector<std::thread> timeout_checker_threads_;
  
  // 连接管理
  mutable std::recursive_mutex connections_mutex_;
  std::map<connection_hdl, std::shared_ptr<ZAsrConnection>, 
           std::owner_less<connection_hdl>> connections_;
  
  // 服务器状态
  std::atomic<bool> is_running_{false};
  std::atomic<bool> stop_requested_{false};
  
  // 定时器
  asio::steady_timer timeout_timer_;
  
  // 日志
  std::ofstream log_file_;
  std::ostream* log_stream_ = &std::cout;
};

}  // namespace zasr

#endif  // ZASR_SERVER_H
