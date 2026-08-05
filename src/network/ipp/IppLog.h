#pragma once

// Logging shim so the IPP core compiles both in firmware and in the host test
// harness (test/ipp_host). Firmware routes to Logging.h; host goes to stderr.
#ifdef ARDUINO
#include <Logging.h>
#define IPP_LOG_DBG(fmt, ...) LOG_DBG("IPP", fmt, ##__VA_ARGS__)
#define IPP_LOG_ERR(fmt, ...) LOG_ERR("IPP", fmt, ##__VA_ARGS__)
#else
#include <cstdio>
#define IPP_LOG_DBG(fmt, ...) std::fprintf(stderr, "[IPP dbg] " fmt "\n", ##__VA_ARGS__)
#define IPP_LOG_ERR(fmt, ...) std::fprintf(stderr, "[IPP ERR] " fmt "\n", ##__VA_ARGS__)
#endif
