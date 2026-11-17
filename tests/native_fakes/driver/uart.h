#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>
typedef int uart_port_t;
typedef uint32_t TickType_t;
enum { UART_NUM_0 = 0 };
struct HostUart {
    std::vector<uint8_t> rx;
    std::vector<uint8_t> tx;
    size_t rx_at = 0;
};
inline HostUart host_uart;
inline int uart_read_bytes(uart_port_t, void *buf, uint32_t length, TickType_t)
{
    auto *dst = static_cast<uint8_t *>(buf);
    const size_t avail = host_uart.rx.size() - host_uart.rx_at;
    const size_t n = length < avail ? length : avail;
    if (!n) return 0;
    memcpy(dst, host_uart.rx.data() + host_uart.rx_at, n);
    host_uart.rx_at += n;
    return static_cast<int>(n);
}
inline int uart_write_bytes(uart_port_t, const void *src, size_t size)
{
    auto *bytes = static_cast<const uint8_t *>(src);
    host_uart.tx.insert(host_uart.tx.end(), bytes, bytes + size);
    return static_cast<int>(size);
}
inline int uart_wait_tx_done(uart_port_t, TickType_t) { return 0; }
inline int uart_set_baudrate(uart_port_t, uint32_t) { return 0; }
