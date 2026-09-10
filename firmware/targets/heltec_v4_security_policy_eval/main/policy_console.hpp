#pragma once
#include <cstddef>
bool ot_console_install() noexcept;
bool ot_console_healthy() noexcept;
bool ot_console_begin_session() noexcept;
bool ot_policy_read(char&) noexcept;
bool ot_policy_send(const char*,std::size_t) noexcept;
