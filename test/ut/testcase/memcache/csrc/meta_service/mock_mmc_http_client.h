/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2026. All rights reserved.
 * MemCache_Hybrid is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *          http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#ifndef MOCK_MMC_HTTP_CLIENT_H
#define MOCK_MMC_HTTP_CLIENT_H

#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <memory>
#include <sstream>
#include <string>

struct MmcHttpResponse {
    int status = 0;
    std::string body;

    MmcHttpResponse() = default;
    MmcHttpResponse(int s, const std::string &b) : status(s), body(b) {}
};

class MockMmcHttpClient {
public:
    MockMmcHttpClient(const std::string &host, uint16_t port) : host_(host), port_(port) {}

    bool IsMock() const
    {
        return isMock_;
    }

    std::shared_ptr<MmcHttpResponse> Get(const std::string &path)
    {
        return DoRequest("GET", path, "", "");
    }

    std::shared_ptr<MmcHttpResponse> Put(const std::string &path, const std::string &body,
                                         const std::string &contentType)
    {
        return DoRequest("PUT", path, body, contentType);
    }

    std::shared_ptr<MmcHttpResponse> Delete(const std::string &path)
    {
        return DoRequest("DELETE", path, "", "");
    }

    std::shared_ptr<MmcHttpResponse> Post(const std::string &path, const std::string &body,
                                          const std::string &contentType)
    {
        return DoRequest("POST", path, body, contentType);
    }

private:
    int ConnectSocket() const
    {
        int sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock < 0) {
            return -1;
        }

        struct sockaddr_in serverAddr;
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port_);
        if (inet_pton(AF_INET, host_.c_str(), &serverAddr.sin_addr) <= 0) {
            close(sock);
            return -1;
        }

        if (connect(sock, reinterpret_cast<struct sockaddr *>(&serverAddr), sizeof(serverAddr)) < 0) {
            close(sock);
            return -1;
        }
        return sock;
    }

    std::string BuildRequest(const std::string &method, const std::string &path, const std::string &body,
                             const std::string &contentType) const
    {
        std::ostringstream request;
        request << method << " " << path << " HTTP/1.1\r\n" << "Host: " << host_ << ":" << port_ << "\r\n";
        if (!body.empty()) {
            request << "Content-Type: " << contentType << "\r\n" << "Content-Length: " << body.size() << "\r\n";
        }
        request << "\r\n";
        if (!body.empty()) {
            request << body;
        }
        return request.str();
    }

    bool ReadHeaderPart(int sock, std::string &headerPart)
    {
        char ch;
        while (headerPart.find("\r\n\r\n") == std::string::npos) {
            const ssize_t n = recv(sock, &ch, 1, 0);
            if (n <= 0) {
                return false;
            }
            headerPart += ch;
        }
        return true;
    }

    int ParseStatusCode(const std::string &headerPart) const
    {
        const auto space1 = headerPart.find(' ');
        const auto space2 = headerPart.find(' ', space1 + 1);
        if (space1 != std::string::npos && space2 != std::string::npos) {
            try {
                return std::stoi(headerPart.substr(space1 + 1, space2 - space1 - 1));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    }

    int ParseContentLength(const std::string &headerPart) const
    {
        const auto clPos = headerPart.find("Content-Length: ");
        if (clPos == std::string::npos) {
            return 0;
        }
        const auto clEnd = headerPart.find("\r\n", clPos);
        if (clEnd == std::string::npos) {
            return 0;
        }
        const std::string clStr = headerPart.substr(clPos + 16, clEnd - clPos - 16);
        try {
            return std::stoi(clStr);
        } catch (...) {
            return 0;
        }
    }

    std::string ReadBody(int sock, int contentLength)
    {
        if (contentLength <= 0) {
            return {};
        }
        std::string bodyPart;
        bodyPart.resize(static_cast<size_t>(contentLength));
        size_t totalRead = 0;
        while (totalRead < static_cast<size_t>(contentLength)) {
            const ssize_t n = recv(sock, &bodyPart[totalRead], static_cast<size_t>(contentLength) - totalRead, 0);
            if (n <= 0) {
                break;
            }
            totalRead += static_cast<size_t>(n);
        }
        bodyPart.resize(totalRead);
        return bodyPart;
    }

    std::shared_ptr<MmcHttpResponse> DoRequest(const std::string &method, const std::string &path,
                                               const std::string &body, const std::string &contentType)
    {
        const int sock = ConnectSocket();
        if (sock < 0) {
            return nullptr;
        }

        const std::string requestStr = BuildRequest(method, path, body, contentType);
        if (send(sock, requestStr.c_str(), requestStr.size(), 0) < 0) {
            close(sock);
            return nullptr;
        }

        std::string headerPart;
        if (!ReadHeaderPart(sock, headerPart)) {
            close(sock);
            return nullptr;
        }

        const int status = ParseStatusCode(headerPart);
        const int contentLength = ParseContentLength(headerPart);
        const std::string bodyPart = ReadBody(sock, contentLength);

        close(sock);
        return std::make_shared<MmcHttpResponse>(status, bodyPart);
    }

    bool isMock_{true};
    std::string host_;
    uint16_t port_;
};

#endif // MOCK_MMC_HTTP_CLIENT_H
