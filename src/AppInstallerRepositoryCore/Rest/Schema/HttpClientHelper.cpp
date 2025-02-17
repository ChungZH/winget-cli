// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.
#include "pch.h"
#include "HttpClientHelper.h"

namespace AppInstaller::Repository::Rest::Schema
{
    HttpClientHelper::HttpClientHelper(std::optional<std::shared_ptr<web::http::http_pipeline_stage>> stage) : m_defaultRequestHandlerStage(stage) {}

    pplx::task<web::http::http_response> HttpClientHelper::Post(
        const utility::string_t& uri, const web::json::value& body, const std::unordered_map<utility::string_t, utility::string_t>& headers) const
    {
        AICLI_LOG(Repo, Info, << "Sending http POST request to: " << utility::conversions::to_utf8string(uri));
        web::http::client::http_client client = GetClient(uri);
        web::http::http_request request{ web::http::methods::POST };
        request.headers().set_content_type(web::http::details::mime_types::application_json);
        request.set_body(body.serialize());

        // Add headers
        for (auto& pair : headers)
        {
            request.headers().add(pair.first, pair.second);
        }

        AICLI_LOG(Repo, Verbose, << "Http POST request details:\n" << utility::conversions::to_utf8string(request.to_string()));

        return client.request(request);
    }

    std::optional<web::json::value> HttpClientHelper::HandlePost(
        const utility::string_t& uri, const web::json::value& body, const std::unordered_map<utility::string_t, utility::string_t>& headers) const
    {
        web::http::http_response httpResponse;
        HttpClientHelper::Post(uri, body, headers).then([&httpResponse](const web::http::http_response& response)
            {
                httpResponse = response;
            }).wait();

        return ValidateAndExtractResponse(httpResponse);
    }

    pplx::task<web::http::http_response> HttpClientHelper::Get(
        const utility::string_t& uri, const std::unordered_map<utility::string_t, utility::string_t>& headers) const
    {
        AICLI_LOG(Repo, Info, << "Sending http GET request to: " << utility::conversions::to_utf8string(uri));
        web::http::client::http_client client = GetClient(uri);
        web::http::http_request request{ web::http::methods::GET };
        request.headers().set_content_type(web::http::details::mime_types::application_json);

        // Add headers
        for (auto& pair : headers)
        {
            request.headers().add(pair.first, pair.second);
        }

        AICLI_LOG(Repo, Verbose, << "Http GET request details:\n" << utility::conversions::to_utf8string(request.to_string()));

        return client.request(request);
    }

    std::optional<web::json::value> HttpClientHelper::HandleGet(
        const utility::string_t& uri, const std::unordered_map<utility::string_t, utility::string_t>& headers) const
    {
        web::http::http_response httpResponse;
        Get(uri, headers).then([&httpResponse](const web::http::http_response& response)
            {
                httpResponse = response;
            }).wait();

        return ValidateAndExtractResponse(httpResponse);
    }

    web::http::client::http_client HttpClientHelper::GetClient(const utility::string_t& uri) const
    {
        web::http::client::http_client client{ uri };

        // Add default custom handlers if any.
        if (m_defaultRequestHandlerStage)
        {
            client.add_handler(m_defaultRequestHandlerStage.value());
        }

        return client;
    }

    std::optional<web::json::value> HttpClientHelper::ValidateAndExtractResponse(const web::http::http_response& response) const
    {
        AICLI_LOG(Repo, Info, << "Response status: " << response.status_code());
        AICLI_LOG(Repo, Verbose, << "Response details: " << utility::conversions::to_utf8string(response.to_string()));

        std::optional<web::json::value> result;
        switch (response.status_code())
        {
        case web::http::status_codes::OK:
            result = ExtractJsonResponse(response);
            break;

        case web::http::status_codes::NotFound:
            THROW_HR(APPINSTALLER_CLI_ERROR_RESTSOURCE_ENDPOINT_NOT_FOUND);
            break;

        case web::http::status_codes::NoContent:
            result = {};
            break;

        case web::http::status_codes::BadRequest:
            THROW_HR(APPINSTALLER_CLI_ERROR_RESTSOURCE_INTERNAL_ERROR);
            break;

        default:
            THROW_HR(MAKE_HRESULT(SEVERITY_ERROR, FACILITY_HTTP, response.status_code()));
            break;
        }

        return result;
    }

    std::optional<web::json::value> HttpClientHelper::ExtractJsonResponse(const web::http::http_response& response) const
    {
        utility::string_t contentType = response.headers().content_type();

        THROW_HR_IF(APPINSTALLER_CLI_ERROR_RESTSOURCE_UNSUPPORTED_MIME_TYPE,
            !contentType._Starts_with(web::http::details::mime_types::application_json));

        return response.extract_json().get();
    }
}
