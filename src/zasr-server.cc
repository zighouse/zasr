#include "zasr-server.h"
#include "zasr-logger.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace zasr {

ZAsrServer::ZAsrServer(const ZAsrConfig& config)
    : config_(config),
      io_conn_(),
      io_work_(),
      timeout_timer_(io_work_),
      work_guard_(asio::make_work_guard(io_work_)) {  // 保持 io_work_ 一直运行
  
  // 设置日志
  SetupLog();
  
  // 初始化WebSocket服务器
  SetupServer();
}

ZAsrServer::~ZAsrServer() {
  Stop();
}

void ZAsrServer::SetupLog() {
  if (!config_.log_file.empty()) {
    log_file_.open(config_.log_file, std::ios::app);
    if (log_file_.is_open()) {
      log_stream_ = &log_file_;
    } else {
      LOG_ERROR() << "Failed to open log file: " << config_.log_file
                << ", using stdout instead.";
    }
  }
  
  // 配置WebSocket服务器日志
  ws_server_.clear_access_channels(websocketpp::log::alevel::all);
  ws_server_.set_access_channels(websocketpp::log::alevel::connect);
  ws_server_.set_access_channels(websocketpp::log::alevel::disconnect);
  ws_server_.set_access_channels(websocketpp::log::alevel::fail);
  
  // 设置日志输出流
  ws_server_.get_alog().set_ostream(log_stream_);
  ws_server_.get_elog().set_ostream(log_stream_);
}

void ZAsrServer::SetupServer() {
  // 初始化ASIO
  ws_server_.init_asio(&io_conn_);
  
  // 设置重用地址
  ws_server_.set_reuse_addr(true);
  
  // 设置事件处理器
  ws_server_.set_open_handler([this](connection_hdl hdl) {
    OnOpen(hdl);
  });
  
  ws_server_.set_close_handler([this](connection_hdl hdl) {
    OnClose(hdl);
  });
  
  ws_server_.set_message_handler([this](connection_hdl hdl, server::message_ptr msg) {
    OnMessage(hdl, msg);
  });
  
  ws_server_.set_http_handler([this](connection_hdl hdl) {
    // 拒绝HTTP连接，只支持WebSocket
    auto con = ws_server_.get_con_from_hdl(hdl);
    con->set_status(websocketpp::http::status_code::bad_request);
    con->set_body("WebSocket connection required");
  });
  
  ws_server_.set_fail_handler([this](connection_hdl hdl) {
    auto con = ws_server_.get_con_from_hdl(hdl);
    LOG_FILE_WARN(*log_stream_) << "Connection failed: "
                 << con->get_remote_endpoint();
  });
}

bool ZAsrServer::Start() {
  if (is_running_) {
    LOG_FILE_WARN(*log_stream_) << "Server is already running.";
    return false;
  }

  // 验证配置
  if (!config_.Validate()) {
    LOG_FILE_ERROR(*log_stream_) << "Invalid configuration.";
    return false;
  }

  // 打印配置信息
  LOG_FILE_INFO(*log_stream_) << config_.ToString();
  
  try {
    // 绑定端口
    ws_server_.listen(asio::ip::tcp::v4(), config_.port);
    ws_server_.start_accept();

    LOG_FILE_INFO(*log_stream_) << "Server starting on " << config_.host << ":"
                 << config_.port;
    
    // 启动工作线程
    StartWorkerThreads();
    
    // 启动超时检查
    CheckTimeouts();
    
    is_running_ = true;
    
    // 运行连接IO上下文（主线程）
    ws_server_.run();
    
    return true;

  } catch (const std::exception& e) {
    LOG_FILE_ERROR(*log_stream_) << "Failed to start server: " << e.what();
    return false;
  }
}

void ZAsrServer::Stop() {
  if (!is_running_) {
    return;
  }
  
  stop_requested_ = true;

  // 取消 io_work 上的定时检测任务
  timeout_timer_.cancel();
  
  // 移除 io_work 上的 work_guard
  work_guard_.reset();

  // 停止WebSocket服务器
  ws_server_.stop();
  
  // 清理所有连接
  std::vector<std::shared_ptr<ZAsrConnection>> connections_to_close;
  {
      std::lock_guard<std::recursive_mutex> lock(connections_mutex_);
      connections_to_close.reserve(connections_.size());
      for (auto& pair : connections_) {
          connections_to_close.push_back(pair.second);
      }
      connections_.clear();
  }

  // 在锁外关闭连接
  for (auto& connection : connections_to_close) {
      try {
          connection->Close();
      } catch (...) {
          // 忽略异常
      }
  }
    
  // 停止ASIO上下文
  io_conn_.stop();
  io_work_.stop();  // 现在会真正停止，因为work_guard已重置
  
  // 停止工作线程
  StopWorkerThreads();

  LOG_FILE_INFO(*log_stream_) << "Stopping the server ... case:" << __LINE__;
  // 等待所有工作线程结束（现在应该能正常join）
  for (auto& thread : worker_threads_) {
    if (thread.joinable()) {
      LOG_FILE_INFO(*log_stream_) << "Stopping the server ... case:" << __LINE__;
      thread.join();
      LOG_FILE_INFO(*log_stream_) << "Stopping the server ... case:" << __LINE__;
    }
  }

  // 清理线程容器
  LOG_FILE_INFO(*log_stream_) << "Stopping the server ... case:" << __LINE__;
  worker_threads_.clear();
  LOG_FILE_INFO(*log_stream_) << "Stopping the server ... case:" << __LINE__;

  is_running_ = false;

  LOG_FILE_INFO(*log_stream_) << "Server stopped.";
}

void ZAsrServer::StartWorkerThreads() {
  // 启动工作线程处理ASR计算
  // 每个线程运行一次 io_work.run()，共享同一个 io_context
  for (int i = 0; i < config_.worker_threads; ++i) {
    worker_threads_.emplace_back([this, i]() {
      LOG_FILE_INFO(*log_stream_) << "Worker thread " << i << " started.";

      try {
        io_work_.run();
      } catch (const std::exception& e) {
        LOG_FILE_ERROR(*log_stream_) << "Worker thread " << i << " error: "
                     << e.what();
      }

      LOG_FILE_INFO(*log_stream_) << "Worker thread " << i << " stopped.";
    });
  }
}

void ZAsrServer::StopWorkerThreads() {
  // 工作线程会在io_context停止后自动退出
}


void ZAsrServer::CheckTimeouts() {
  std::unique_lock<std::recursive_mutex> lock(connections_mutex_);
  
  auto now = std::chrono::steady_clock::now();
  std::vector<std::pair<connection_hdl, std::shared_ptr<ZAsrConnection>>> timed_out_connections;
  
  for (const auto& pair : connections_) {
    auto connection = pair.second;
    
    // 检查连接超时
    if (connection->IsTimeout(config_.connection_timeout_seconds)) {
      LOG_FILE_WARN(*log_stream_) << "Connection timeout: "
                   << connection->GetHandle().lock().get();
      timed_out_connections.push_back(pair);
    }
  }
  
  // 处理超时连接
  for (const auto& pair : timed_out_connections) {
    auto hdl = pair.first;
    auto connection = pair.second;
    
    // 先从映射中移除
    connections_.erase(hdl);
    
    // 解锁，避免在connection->Close()中死锁
    lock.unlock();
    
    // 清理连接资源（可能会发送消息）
    try {
      connection->Close();
    } catch (const std::exception& e) {
      LOG_FILE_ERROR(*log_stream_) << "Error closing connection in CheckTimeouts: " << e.what();
    }

    // 重新加锁
    lock.lock();

    // 关闭WebSocket连接
    try {
      Close(hdl, websocketpp::close::status::normal, "Connection timeout");
    } catch (const std::exception& e) {
      LOG_FILE_ERROR(*log_stream_) << "Error closing WebSocket in CheckTimeouts: " << e.what();
    }
  }

  if (!timed_out_connections.empty()) {
    LOG_FILE_INFO(*log_stream_) << "Removed " << timed_out_connections.size()
                 << " timeout connections. Active connections: "
                 << connections_.size();
  }
  
  // 重新调度下一次检查（只有在未请求停止时）
  if (!stop_requested_) {
    timeout_timer_.expires_after(std::chrono::seconds(1));
    timeout_timer_.async_wait([this](const asio::error_code& ec) {
      if (!ec && !stop_requested_) {
        CheckTimeouts();
      }
    });
  }
}

void ZAsrServer::OnOpen(connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(connections_mutex_);
  
  // 检查最大连接数
  if (connections_.size() >= static_cast<size_t>(config_.max_connections)) {
    Close(hdl, websocketpp::close::status::normal, "Too many connections");
    return;
  }
  
  // 创建新的连接对象
  auto connection = std::make_shared<ZAsrConnection>(this, hdl);
  connections_[hdl] = connection;

  auto con = ws_server_.get_con_from_hdl(hdl);
  LOG_FILE_INFO(*log_stream_) << "New connection from "
               << con->get_remote_endpoint()
               << ". Active connections: " << connections_.size();
}

void ZAsrServer::OnClose(connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(connections_mutex_);

  // 获取连接对象并清理
  auto it = connections_.find(hdl);
  if (it != connections_.end()) {
    auto connection = it->second;
    try {
      connection->Close();  // 通知连接进行清理
    } catch (const std::exception& e) {
      LOG_FILE_ERROR(*log_stream_) << "Error closing connection in OnClose: " << e.what();
    }
  }

  connections_.erase(hdl);

  LOG_FILE_INFO(*log_stream_) << "Connection closed. Active connections: "
               << connections_.size();
}

void ZAsrServer::OnMessage(connection_hdl hdl, server::message_ptr msg) {
  auto connection = GetConnection(hdl);
  if (!connection) {
    LOG_FILE_WARN(*log_stream_) << "Received message from unknown connection.";
    return;
  }

  try {
    if (msg->get_opcode() == websocketpp::frame::opcode::text) {
      // 文本消息（认证等）
      connection->HandleTextMessage(msg->get_payload());
    } else if (msg->get_opcode() == websocketpp::frame::opcode::binary) {
      // 二进制消息（音频数据）
      // 在工作线程中处理音频数据，添加异常处理防止worker线程崩溃
      asio::post(io_work_, [connection, payload = msg->get_payload()]() {
        try {
          connection->HandleBinaryMessage(payload.data(), payload.size());
        } catch (const std::exception& e) {
          std::cerr << "Exception in binary message handler: " << e.what() << std::endl;
          try {
            connection->SendError(41040009, "Error processing audio data: " + std::string(e.what()));
          } catch (...) {
            // Ignore send errors
          }
        } catch (...) {
          std::cerr << "Unknown exception in binary message handler" << std::endl;
          try {
            connection->SendError(41040009, "Unknown error processing audio data");
          } catch (...) {
            // Ignore send errors
          }
        }
      });
    } else {
      LOG_FILE_WARN(*log_stream_) << "Unsupported message opcode: "
                   << msg->get_opcode();
    }
  } catch (const std::exception& e) {
    LOG_FILE_ERROR(*log_stream_) << "Error processing message: " << e.what();
    connection->SendError(41040009, "Internal server error: " + std::string(e.what()));
  }
}

void ZAsrServer::SendMessage(connection_hdl hdl, const std::string& message) {
  // 使用 asio::post 确保在 io_conn_ 线程中发送消息（websocketpp 不是线程安全的）
  asio::post(io_conn_, [this, hdl, message]() {
    try {
      if (!Contains(hdl)) {
        return;
      }

      websocketpp::lib::error_code ec;
      ws_server_.send(hdl, message, websocketpp::frame::opcode::text, ec);

      if (ec) {
        LOG_FILE_ERROR(*log_stream_) << "Failed to send message: " << ec.message();

        // 如果发送失败，关闭连接
        if (ec.value() == websocketpp::error::invalid_state ||
            ec.value() == websocketpp::error::bad_connection) {
          Close(hdl, websocketpp::close::status::normal, "Send failed");
        }
      }
    } catch (const std::exception& e) {
      LOG_FILE_ERROR(*log_stream_) << "Exception in SendMessage: " << e.what();
    }
  });
}

bool ZAsrServer::Contains(connection_hdl hdl) const {
  std::lock_guard<std::recursive_mutex> lock(connections_mutex_);
  return connections_.find(hdl) != connections_.end();
}

void ZAsrServer::Close(connection_hdl hdl, websocketpp::close::status::value code,
                        const std::string& reason) {
  try {
    websocketpp::lib::error_code ec;
    ws_server_.close(hdl, code, reason, ec);

    if (ec) {
      LOG_FILE_ERROR(*log_stream_) << "Failed to close connection: " << ec.message();
    }
  } catch (const std::exception& e) {
    LOG_FILE_ERROR(*log_stream_) << "Exception in Close: " << e.what();
  }
}

std::shared_ptr<ZAsrConnection> ZAsrServer::GetConnection(connection_hdl hdl) {
  std::lock_guard<std::recursive_mutex> lock(connections_mutex_);
  auto it = connections_.find(hdl);
  if (it != connections_.end()) {
    return it->second;
  }
  return nullptr;
}

}  // namespace zasr
