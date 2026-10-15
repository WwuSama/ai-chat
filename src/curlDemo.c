#include <stdio.h>
#include <curl/curl.h>

int main(int argv, char **argc)
{
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK)
        return 1;

    CURL *handle = curl_easy_init();
    if (!handle) {
        curl_global_cleanup();
        return 1;
    }

    rc = curl_easy_setopt(handle, CURLOPT_URL, "https://example.com");
    rc = curl_easy_setopt(handle, CURLOPT_VERBOSE, 1L);
    if(rc == CURLE_OK)
        rc = curl_easy_perform(handle);

    long status = 0;
    if(rc != CURLE_OK) {
        fprintf(stderr, "Transfer error: %s\n", curl_easy_strerror(rc));
    } else {
        rc = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
        if(rc == CURLE_OK)
            fprintf(stderr, "\nHTTP status: %ld\n", status);
    }

    curl_easy_cleanup(handle);
    curl_global_cleanup();
    return (rc == CURLE_OK && status >= 200 && status < 300) ? 0 : 1;
}