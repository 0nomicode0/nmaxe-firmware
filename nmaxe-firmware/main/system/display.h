/**
 * @file display.h
 * @brief 웹 대시보드 (HTTP REST API)
 *
 * 엔드포인트:
 *   GET  /api/status   → 실시간 상태 JSON
 *   GET  /api/config   → 현재 설정 JSON
 *   POST /api/config   → 설정 변경
 *   POST /api/restart  → 재시작
 */
#pragma once
#include "esp_err.h"

esp_err_t display_init(void);
void      display_update(void);
