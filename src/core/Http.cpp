#include "Http.h"
#include <curl/curl.h>
#include <cstring>
#include <mutex>
#include <sys/stat.h>

namespace amp {

namespace {

size_t WriteBody(char* ptr, size_t size, size_t nmemb, void* userdata)
{
    std::string* body = static_cast<std::string*>(userdata);
    body->append(ptr, size * nmemb);
    return size * nmemb;
}

const char* CaBundle()
{
    static const char* candidates[] = {
        "/boot/system/data/ssl/CARootCertificates.pem",
        "/system/data/ssl/CARootCertificates.pem",
        "/etc/ssl/certs/ca-certificates.crt",
        nullptr};
    for (int i = 0; candidates[i]; i++) {
        struct stat st;
        if (stat(candidates[i], &st) == 0)
            return candidates[i];
    }
    return nullptr;
}

HttpResponse Perform(const std::string& url, const std::vector<std::string>& headers, int timeoutSeconds)
{
    HttpResponse response;
    CURL* curl = curl_easy_init();
    if (!curl) {
        response.error = "curl init failed";
        return response;
    }
    struct curl_slist* list = nullptr;
    for (const std::string& h : headers)
        list = curl_slist_append(list, h.c_str());
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, (long)timeoutSeconds);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, Http::UserAgent().c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    if (const char* ca = CaBundle())
        curl_easy_setopt(curl, CURLOPT_CAINFO, ca);
    if (list)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        response.error = curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
        char* type = nullptr;
        if (curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &type) == CURLE_OK && type)
            response.contentType = type;
    }
    if (list)
        curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return response;
}

} // namespace

void Http::GlobalInit()
{
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

HttpResponse Http::Get(const std::string& url, const std::vector<std::string>& headers, int timeoutSeconds)
{
    GlobalInit();
    return Perform(url, headers, timeoutSeconds);
}

std::string Http::UrlEncode(const std::string& text)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back((char)c);
        else {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 15]);
        }
    }
    return out;
}

std::string Http::UserAgent()
{
    return "Amp/0.1.0 (Haiku; https://github.com/jmgasper/amp)";
}

} // namespace amp
