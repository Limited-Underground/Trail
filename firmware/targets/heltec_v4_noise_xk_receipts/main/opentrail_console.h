#pragma once

bool ot_console_install() noexcept;
bool ot_console_healthy() noexcept;
bool ot_console_begin_session() noexcept;
bool ot_console_session_started() noexcept;
void ot_receipt_log(char level, const char* tag, const char* format, ...) noexcept;
#define OT_RECEIPT_I(tag, format, ...) ot_receipt_log('I', tag, format, ##__VA_ARGS__)
#define OT_RECEIPT_W(tag, format, ...) ot_receipt_log('W', tag, format, ##__VA_ARGS__)
