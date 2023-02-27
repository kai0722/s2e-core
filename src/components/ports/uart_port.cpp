/**
 * @file uart_port.cpp
 * @brief Class to emulate UART communication port
 */

#include "uart_port.hpp"

SCIPort::SCIPort() : SCIPort(kDefaultBufferSize, kDefaultBufferSize) {}

SCIPort::SCIPort(int rx_buffer_size, int tx_buffer_size) {
  if (rx_buffer_size <= 0) rx_buffer_size = kDefaultBufferSize;
  if (tx_buffer_size <= 0) tx_buffer_size = kDefaultBufferSize;
  rx_buffer_ = new RingBuffer(rx_buffer_size);
  tx_buffer_ = new RingBuffer(tx_buffer_size);
}

SCIPort::~SCIPort() {
  delete rx_buffer_;
  delete tx_buffer_;
}

int SCIPort::WriteTx(unsigned char* buffer, int offset, int data_length) { return tx_buffer_->Write(buffer, offset, data_length); }

int SCIPort::WriteRx(unsigned char* buffer, int offset, int data_length) { return rx_buffer_->Write(buffer, offset, data_length); }

int SCIPort::ReadTx(unsigned char* buffer, int offset, int data_length) { return tx_buffer_->Read(buffer, offset, data_length); }

int SCIPort::ReadRx(unsigned char* buffer, int offset, int data_length) { return rx_buffer_->Read(buffer, offset, data_length); }