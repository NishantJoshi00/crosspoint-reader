#pragma once
enum sntp_sync_status_t { SNTP_SYNC_STATUS_RESET, SNTP_SYNC_STATUS_COMPLETED };
bool esp_sntp_enabled();
void esp_sntp_stop();
void sntp_set_sync_status(sntp_sync_status_t status);
sntp_sync_status_t sntp_get_sync_status();
