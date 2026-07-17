// Emulator HTTP transport for SyncCore — the libcurl counterpart of the
// device's HTTPClient implementation in src/service/Sync/Sync.cpp. Lets the
// real shared sync logic run against the live Apps Script endpoint on macOS.
#include "service/Sync/SyncCore.h"
#include "app/app.h"

#include <curl/curl.h>
#include <string>
#include <vector>

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    ((std::string *)userdata)->append(ptr, size * nmemb);
    return size * nmemb;
}

static SyncHttp do_request(const String &url, const std::string *postBody)
{
    SyncHttp r;
    r.code = -1;

    CURL *c = curl_easy_init();
    if (!c)
        return r;

    std::string resp;
    struct curl_slist *hdrs = nullptr;

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L); // Apps Script 302 hop
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);

    if (postBody)
    {
        hdrs = curl_slist_append(hdrs, "Content-Type: text/plain");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(c, CURLOPT_POST, 1L);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, postBody->data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)postBody->size());
    }

    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK)
    {
        long http = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
        r.code = (int)http;
        r.body = String(resp.c_str());
    }

    if (hdrs)
        curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);
    return r;
}

SyncHttp sync_http_get(const String &url)
{
    return do_request(url, nullptr);
}

// General request (GitHub backend seam): any method + custom headers + body.
// Keep-alive: a STATIC handle is reused across calls — curl_easy_reset clears
// per-request options but keeps the handle's connection cache, so the TLS
// connection to api.github.com is reused instead of re-handshaken every call
// (a history-preserving rename is 5-6 calls). Mirrors the device's setReuse.
SyncHttp sync_http(const String &method, const String &url,
                   const std::vector<String> &headers, const String &body)
{
    static CURL *c = nullptr;
    if (!c)
        c = curl_easy_init();

    SyncHttp r;
    r.code = -1;
    if (!c)
        return r;

    curl_easy_reset(c); // clears options, KEEPS the live connection pool

    std::string resp;
    struct curl_slist *hdrs = nullptr;
    for (const String &h : headers)
        hdrs = curl_slist_append(hdrs, h.c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &resp);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
    if (hdrs)
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    std::string b(body.c_str(), body.length());
    if (!b.empty())
    {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, b.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)b.size());
    }

    CURLcode rc = curl_easy_perform(c);
    if (rc == CURLE_OK)
    {
        long http = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &http);
        r.code = (int)http;
        r.body = String(resp.c_str());
    }

    if (hdrs)
        curl_slist_free_all(hdrs); // free the per-request header list (not the handle)
    return r;
}

SyncHttp sync_http_post_file(const String &url, const String &filePath)
{
    // Read the (small) base64 sidecar from the virtual SD into memory, then POST
    // — libcurl can't see gfs() paths directly.
    std::string body;
    File f = gfs()->open(filePath.c_str(), "r");
    if (f)
    {
        while (f.available())
        {
            uint8_t buf[1024];
            size_t n = f.read(buf, sizeof(buf));
            if (n == 0)
                break;
            body.append((char *)buf, n);
        }
        f.close();
    }
    return do_request(url, &body);
}
