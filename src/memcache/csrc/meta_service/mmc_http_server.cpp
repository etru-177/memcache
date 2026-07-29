/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 * MemCache_Hybrid is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#include <ctime>

#include "nlohmann/json.hpp"

#include "mmc_logger.h"
#include "mf_tls_util.h"

#include "mmc_http_server.h"

constexpr uint32_t kHttpWorkerCount = 2;
constexpr char kContentTypeJsonUtf8[] = "application/json; charset=utf-8";
constexpr char kContentTypeTextUtf8[] = "text/plain; charset=utf-8";
constexpr char kContentTypePrometheus[] = "text/plain; version=0.0.4";
constexpr char kMetadataUpdatedText[] = "metadata updated";
constexpr char kMetadataDeletedText[] = "metadata deleted";
constexpr char K_KEY_DELETED_TEXT[] = "key deleted";
constexpr char K_ALL_KEYS_DELETED_TEXT[] = "all keys deleted";
constexpr char kErrorNotSupported[] = "Not supported";
constexpr char kErrorInternalServer[] = "Internal server error";
constexpr char kErrorKeyNotFound[] = "Key not found";
constexpr char kErrorSegmentNotFound[] = "Segment not found";
constexpr char kErrorMetadataNotFound[] = "Metadata key not found";

namespace {

uint64_t CurrentTimestamp()
{
    return static_cast<uint64_t>(std::time(nullptr));
}

std::string BuildMissingParameterMessage(const std::string &paramName)
{
    return "Missing '" + paramName + "' parameter";
}

std::string BodyToString(ock::acc::AccHttpRequestContext &ctx)
{
    if (ctx.BodyPtr() != nullptr && ctx.BodyLen() > 0) {
        return std::string(static_cast<const char *>(ctx.BodyPtr()), ctx.BodyLen());
    }
    return {};
}

int ReplyTextOk(ock::acc::AccHttpRequestContext &ctx, const std::string &body,
                const char *contentType = kContentTypeTextUtf8)
{
    return ctx.Reply(ock::acc::AccHttpStatusCode::OK, contentType, body);
}

int ReplyJsonOk(ock::acc::AccHttpRequestContext &ctx, const nlohmann::json &body)
{
    return ctx.Reply(ock::acc::AccHttpStatusCode::OK, kContentTypeJsonUtf8, body.dump());
}

int ReplyJsonError200(ock::acc::AccHttpRequestContext &ctx, const std::string &message)
{
    nlohmann::json errorBody;
    errorBody["success"] = false;
    errorBody["error_message"] = message;
    errorBody["timestamp"] = CurrentTimestamp();
    return ReplyJsonOk(ctx, errorBody);
}

bool GetRequiredParam(ock::acc::AccHttpRequestContext &ctx, const std::string &paramName, std::string &value)
{
    const auto params = ctx.Params();
    const auto paramIt = params.find(paramName);
    if (paramIt == params.end()) {
        ReplyJsonError200(ctx, BuildMissingParameterMessage(paramName));
        return false;
    }
    value = paramIt->second;
    return true;
}

void SplitCommaSeparated(const std::string &source, std::vector<std::string> &items)
{
    items.clear();
    size_t start = 0;
    while (start <= source.size()) {
        const size_t end = source.find(',', start);
        if (end == std::string::npos) {
            std::string item = source.substr(start);
            if (!item.empty()) {
                items.push_back(std::move(item));
            }
            return;
        }
        std::string item = source.substr(start, end - start);
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
        start = end + 1;
    }
}

} // namespace

namespace ock {
namespace mmc {

MmcHttpServer::~MmcHttpServer()
{
    Stop();
}

void MmcHttpServer::RegisterUrls()
{
    RegisterHealthCheckEndpoint();
    RegisterDataManagementEndpoints();
    RegisterSegmentManagementEndpoints();
    RegisterDrainJobEndpoints();
    RegisterMetricsEndpoint();
}

void MmcHttpServer::RegisterHealthCheckEndpoint()
{
    auto healthHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, restApiFacade_->BuildHealth(IsRunning()));
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/health", healthHandler);

    auto roleHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, restApiFacade_->GetRole());
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/role", roleHandler);

    auto haStatusHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, restApiFacade_->GetHaStatus());
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/ha_status", haStatusHandler);

    auto leaderHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, restApiFacade_->BuildLeader());
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/leader", leaderHandler);

    auto kvEventsHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, restApiFacade_->BuildKvEventsStatus());
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/kv_events/status", kvEventsHandler);
}

void MmcHttpServer::RegisterDataManagementEndpoints()
{
    server_->RegisterHttpHandler(
        acc::AccHttpMethod::GET, "/metadata", [this](acc::AccHttpRequestContext &ctx) -> int32_t {
            if (restApiFacade_ == nullptr) {
                return ReplyJsonError200(ctx, kErrorInternalServer);
            }

            std::string key;
            if (!GetRequiredParam(ctx, "key", key)) {
                return acc::ACC_OK;
            }

            std::string value;
            const Result ret = restApiFacade_->GetMetadata(key, value);
            if (ret != MMC_OK) {
                const std::string message =
                    ret == ock::smem::StoreErrorCode::NOT_EXIST ? kErrorMetadataNotFound : kErrorInternalServer;
                return ReplyJsonError200(ctx, message);
            }
            return ReplyTextOk(ctx, value);
        });

    auto putMetadataHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string key;
        if (!GetRequiredParam(ctx, "key", key)) {
            return acc::ACC_OK;
        }

        const std::string body = BodyToString(ctx);
        const Result ret = restApiFacade_->PutMetadata(key, body);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, kMetadataUpdatedText);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::PUT, "/metadata", putMetadataHandler);

    server_->RegisterHttpHandler(
        acc::AccHttpMethod::DELETE, "/metadata", [this](acc::AccHttpRequestContext &ctx) -> int32_t {
            if (restApiFacade_ == nullptr) {
                return ReplyJsonError200(ctx, kErrorInternalServer);
            }

            std::string key;
            if (!GetRequiredParam(ctx, "key", key)) {
                return acc::ACC_OK;
            }

            const Result ret = restApiFacade_->DeleteMetadata(key);
            if (ret != MMC_OK) {
                const std::string message =
                    ret == ock::smem::StoreErrorCode::NOT_EXIST ? kErrorMetadataNotFound : kErrorInternalServer;
                return ReplyJsonError200(ctx, message);
            }
            return ReplyTextOk(ctx, kMetadataDeletedText);
        });

    server_->RegisterHttpHandler(
        acc::AccHttpMethod::DELETE, "/key", [this](acc::AccHttpRequestContext &ctx) -> int32_t {
            if (restApiFacade_ == nullptr) {
                return ReplyJsonError200(ctx, kErrorInternalServer);
            }

            std::string key;
            if (!GetRequiredParam(ctx, "key", key)) {
                return acc::ACC_OK;
            }

            const Result ret = restApiFacade_->RemoveKey(key);
            if (ret != MMC_OK) {
                const std::string message = ret == MMC_UNMATCHED_KEY ? kErrorKeyNotFound : kErrorInternalServer;
                return ReplyJsonError200(ctx, message);
            }
            nlohmann::json body;
            body["success"] = true;
            body["message"] = K_KEY_DELETED_TEXT;
            return ReplyJsonOk(ctx, body);
        });

    auto removeAllKeysHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        const Result ret = restApiFacade_->RemoveAllKeys();
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        nlohmann::json body;
        body["success"] = true;
        body["message"] = K_ALL_KEYS_DELETED_TEXT;
        return ReplyJsonOk(ctx, body);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::DELETE, "/all_keys", removeAllKeysHandler);

    auto getAllKeysHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->GetAllKeysText(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/get_all_keys", getAllKeysHandler);

    server_->RegisterHttpHandler(
        acc::AccHttpMethod::GET, "/query_key", [this](acc::AccHttpRequestContext &ctx) -> int32_t {
            if (restApiFacade_ == nullptr) {
                return ReplyJsonError200(ctx, kErrorInternalServer);
            }

            std::string key;
            if (!GetRequiredParam(ctx, "key", key)) {
                return acc::ACC_OK;
            }

            nlohmann::json result;
            const Result ret = restApiFacade_->QueryKey(key, result);
            if (ret != MMC_OK) {
                const std::string message = ret == MMC_UNMATCHED_KEY ? kErrorKeyNotFound : kErrorInternalServer;
                return ReplyJsonError200(ctx, message);
            }
            return ReplyJsonOk(ctx, result);
        });

    auto batchQueryKeysHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string keyString;
        if (!GetRequiredParam(ctx, "keys", keyString)) {
            return acc::ACC_OK;
        }

        std::vector<std::string> keys;
        SplitCommaSeparated(keyString, keys);
        nlohmann::json result;
        const Result ret = restApiFacade_->BatchQueryKeys(keys, result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/batch_query_keys", batchQueryKeysHandler);
}

void MmcHttpServer::RegisterSegmentManagementEndpoints()
{
    auto getAllSegmentsHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->GetAllSegmentsText(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/get_all_segments", getAllSegmentsHandler);

    auto querySegmentHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string segmentId;
        if (!GetRequiredParam(ctx, "segment", segmentId)) {
            return acc::ACC_OK;
        }

        RestSegmentSnapshot segment;
        const Result ret = restApiFacade_->QuerySegment(segmentId, segment);
        if (ret != MMC_OK) {
            const std::string message = ret == MMC_UNMATCHED_KEY ? kErrorSegmentNotFound : kErrorInternalServer;
            return ReplyJsonError200(ctx, message);
        }
        return ReplyJsonOk(ctx, restApiFacade_->BuildSegment(segment));
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/query_segment", querySegmentHandler);

    auto segmentStatusHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string segmentId;
        if (!GetRequiredParam(ctx, "segment", segmentId)) {
            return acc::ACC_OK;
        }

        nlohmann::json result;
        const Result ret = restApiFacade_->BuildSegmentStatus(segmentId, result);
        if (ret != MMC_OK) {
            const std::string message = ret == MMC_UNMATCHED_KEY ? kErrorSegmentNotFound : kErrorInternalServer;
            return ReplyJsonError200(ctx, message);
        }
        return ReplyJsonOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/api/v1/segments/status", segmentStatusHandler);

    auto capacityUsageHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        nlohmann::json result;
        const Result ret = restApiFacade_->BuildCapacityUsage(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/api/v1/capacity/usage", capacityUsageHandler);

    auto segmentRemainingHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        nlohmann::json result;
        const Result ret = restApiFacade_->BuildSegmentRemaining(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyJsonOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/api/v1/capacity/segment_remaining",
                                 segmentRemainingHandler);
}

void MmcHttpServer::RegisterDrainJobEndpoints()
{
    auto notSupportedHandler = [](acc::AccHttpRequestContext &ctx) -> int32_t {
        return ReplyJsonError200(ctx, kErrorNotSupported);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::POST, "/api/v1/drain_jobs", notSupportedHandler);
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/api/v1/drain_jobs/query", notSupportedHandler);
    server_->RegisterHttpHandler(acc::AccHttpMethod::POST, "/api/v1/drain_jobs/cancel", notSupportedHandler);
}

void MmcHttpServer::RegisterMetricsEndpoint()
{
    auto metricsHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->BuildPrometheusMetrics(IsRunning(), result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result, kContentTypePrometheus);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/metrics", metricsHandler);

    auto metricsSummaryHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->BuildMetricsSummary(IsRunning(), result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/metrics/summary", metricsSummaryHandler);

    auto ptracerHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->GetPtracerText(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/metrics/ptracer", ptracerHandler);

    auto allocFreeLatencyHandler = [this](acc::AccHttpRequestContext &ctx) -> int32_t {
        if (restApiFacade_ == nullptr) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }

        std::string result;
        const Result ret = restApiFacade_->GetAllocFreeLatencyText(result);
        if (ret != MMC_OK) {
            return ReplyJsonError200(ctx, kErrorInternalServer);
        }
        return ReplyTextOk(ctx, result);
    };
    server_->RegisterHttpHandler(acc::AccHttpMethod::GET, "/api/v1/analysis/alloc_free_latency",
                                 allocFreeLatencyHandler);
}

bool MmcHttpServer::Start()
{
    if (running_.load()) {
        return true;
    }

    if (server_ == nullptr) {
        MMC_LOG_ERROR("HTTP server is null");
        return false;
    }

    server_->RegisterLinkBrokenHandler([](const acc::AccTcpLinkComplexPtr &) -> int32_t { return acc::ACC_OK; });

    if (!SetupTls()) {
        return false;
    }

    acc::AccHttpServerOptions opts;
    opts.enableListener = true;
    opts.listenIp = host_;
    opts.listenPort = port_;
    opts.workerCount = kHttpWorkerCount;

    const auto ret = server_->Start(opts, tlsOption_);
    if (ret != acc::ACC_OK) {
        MMC_LOG_ERROR("Failed to start HTTP server on " << host_ << ":" << port_);
        return false;
    }

    running_ = true;
    MMC_LOG_INFO("HTTP server started on " << host_ << ":" << port_);
    return true;
}

bool MmcHttpServer::SetupTls()
{
    if (!tlsOption_.enableTls) {
        return true;
    }
    if (sslLibPath_.empty()) {
        MMC_LOG_ERROR("sslLibPath is empty when TLS is enabled");
        return false;
    }
    const auto loadRet = server_->LoadDynamicLib(sslLibPath_);
    if (loadRet != acc::ACC_OK) {
        MMC_LOG_ERROR("Failed to load openssl dynamic library from " << sslLibPath_);
        return false;
    }
    if (!tlsOption_.tlsPkPwd.empty()) {
        if (decrypterLibPath_.empty()) {
            MMC_LOG_WARN("No decrypter provided, using default decrypter handler");
            server_->RegisterDecryptHandler(mf::MfTlsUtil::DefaultDecrypter);
        } else {
            const auto decrypter = mf::MfTlsUtil::LoadDecryptFunction(decrypterLibPath_.c_str());
            if (decrypter == nullptr) {
                MMC_LOG_ERROR("Failed to load customized decrypt function from " << decrypterLibPath_);
                return false;
            }
            server_->RegisterDecryptHandler(decrypter);
        }
    }
    return true;
}

void MmcHttpServer::Stop()
{
    if (!running_.load()) {
        return;
    }

    server_->Stop();
    running_ = false;
    MMC_LOG_INFO("HTTP server stopped");
}

} // namespace mmc
} // namespace ock
