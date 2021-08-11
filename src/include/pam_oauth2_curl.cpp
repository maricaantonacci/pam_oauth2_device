//
// Created by jens on 23/07/2021.
//

#include "pam_oauth2_curl.hpp"
#include "pam_oauth2_curl_impl.hpp"
#include <curl/curl.h>
#include <utility>
#include <vector>
#include <algorithm>
#include "config.hpp"
#include "pam_oauth2_excpt.hpp"


// This is where make_unique could have been useful but it is not available till C++14
pam_oauth2_curl::pam_oauth2_curl(Config const &config): impl_(new pam_oauth2_curl_impl(config))
{
    if(!impl_)
        throw NetworkError();
    // shared options for all calls
}


pam_oauth2_curl::~pam_oauth2_curl()
{
    delete impl_;
}


pam_oauth2_curl::credential
pam_oauth2_curl::make_credential(Config const &config)
{
    // CHTC patch (revised again). Add secret (see RFC 6749 section 2.3.1) to parameters
    if(!config.http_basic_auth)
    {
	return std::move(pam_oauth2_curl::credential(config.client_id, config.client_secret, 0));
    }
    return std::move(credential(config.client_id, config.client_secret));
}



// TODO still too much code duplication between the calls

std::string
pam_oauth2_curl::call(Config const &config, std::string const &url)
{
    call_data readBuffer;
    impl_->reset(config);
    impl_->add_call_data(readBuffer);
    impl_->add_credential(readBuffer, make_credential(config));
    curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());

    CURLcode res = curl_easy_perform(impl_->curl);

    if(res != CURLE_OK)
	throw NetworkError();
    return readBuffer.callback_data;
}


std::string
pam_oauth2_curl::call(Config const &config, const std::string &url,
		      std::vector<std::pair<std::string,std::string>> const &postdata)
{
    call_data readBuffer;
    std::string params{pam_oauth2_curl_impl::make_post_data(postdata)};
    impl_->reset(config);
    impl_->add_call_data(readBuffer);
    impl_->add_credential(readBuffer, make_credential(config));
    curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(impl_->curl, CURLOPT_POSTFIELDS, params.c_str());
    // Automatically POSTs because we set postfields
    CURLcode res = curl_easy_perform(impl_->curl);
    if(res != CURLE_OK)
        throw NetworkError();
    return readBuffer.callback_data;
}


std::string
pam_oauth2_curl::call(Config const &config, const std::string &url, credential &&cred)
{
    call_data readBuffer;
    impl_->reset(config);
    impl_->add_call_data(readBuffer);
    impl_->add_credential(readBuffer, std::move(cred));
    curl_easy_setopt(impl_->curl, CURLOPT_URL, url.c_str());
    CURLcode res = curl_easy_perform(impl_->curl);
    if(res != CURLE_OK)
        throw NetworkError();
    return readBuffer.callback_data;
}



pam_oauth2_curl::params &
pam_oauth2_curl::add_params(pam_oauth2_curl::params &params, std::string const &key, std::string const &value)
{
    return impl_->add_params(params, key, value);
}


std::string
pam_oauth2_curl::encode(std::string const &in)
{
    return impl_->encode(in);
}


pam_oauth2_curl::credential::~credential()
{
    if(!pw_.empty())
        for( auto &c : pw_ )
            c = '*';
    if(!token_.empty())
        for( auto &c : token_ )
            c = '*';
}



// pam_oauth2_curl_impl implementation

pam_oauth2_curl_impl::pam_oauth2_curl_impl(Config const &config): curl{curl_easy_init()}, ret(CURLE_OK)
{
    if(!curl)
	throw NetworkError();
    reset(config);
}


pam_oauth2_curl_impl::~pam_oauth2_curl_impl()
{
    if(curl) {
	curl_easy_cleanup(curl);
	curl = nullptr;
    }
}




void
pam_oauth2_curl_impl::reset(const Config &config)
{
    // reset to shared options
    curl_easy_reset(curl);
    if(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L) != CURLE_OK)
        throw NetworkError();
    if(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L) != CURLE_OK)
        throw NetworkError();
    if(curl_easy_setopt(curl, CURLOPT_CAPATH, "/etc/grid-security/certificates") != CURLE_OK)
        throw NetworkError();
}


void
pam_oauth2_curl_impl::add_call_data(call_data &data)
{
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data.callback_data);
}


void
pam_oauth2_curl_impl::add_credential(call_data &data, pam_oauth2_curl::credential &&cred)
{
    // Even if the compiler defies us and copies cred, we MUST work with _our_ copy of cred
    // so the lifetime of the strings is no shorter than that of the call
    data.cred = std::move(cred);
    switch(data.cred.type_)
    {
	case pam_oauth2_curl::credential::type::NONE:
	    break;
        case pam_oauth2_curl::credential::type::BASIC:
            // curl will encode for us if necessary?
	    curl_easy_setopt(curl, CURLOPT_USERNAME, data.cred.un_.c_str());
	    curl_easy_setopt(curl, CURLOPT_PASSWORD, data.cred.pw_.c_str());
            break;
	case pam_oauth2_curl::credential::type::TOKEN:
	    data.auz_hdr = "Authorization: Bearer ";
	    // FIXME do we need to encode the token ourselves?
	    data.auz_hdr += encode(data.cred.token_);
	    data.headers = curl_slist_append(data.headers, data.auz_hdr.c_str());
	    if(!data.headers)
	        throw std::bad_alloc();
	    // FIXME if going through a proxy, the proxy server would also get the headers...
	    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, data.headers);
	    break;
	case pam_oauth2_curl::credential::type::SECRET:
	    // See RFC6749 section 2.3.1.
	    // TODO: check if the connection is secure?
	    add_params(data.post_data, "client_id", encode(cred.un_));
	    add_params(data.post_data, "client_secret", encode(cred.pw_));
	    break;
        case pam_oauth2_curl::credential::type::DENIED:
            throw NetworkError(); // FIXME make more sensible
        default:
            throw "Cannot happen XIQJA";
    }
}



// RFC 3986 reserved characters to be % encoded
std::string pam_oauth2_curl_impl::reserved = ":/?#[]@!$&'()*+,;=%";


// Annoyingly this can no longer be static because it needs a curl handle.
// Nor constexpr (if strings were constexpr)
std::string
pam_oauth2_curl_impl::encode(std::string const &in)
{
    char *encode = curl_easy_escape(curl, in.c_str(), in.size());
    if(!encode) throw std::bad_alloc();
    std::string result(encode);
    curl_free(encode);
    return result;
}


bool 
pam_oauth2_curl_impl::contains_reserved(const std::string &str)
{
    return str.find_first_of(reserved) != std::string::npos;
}



std::string
pam_oauth2_curl_impl::make_post_data(pam_oauth2_curl::params const &data)
{
    std::string tmp;
    for( auto const &p : data ) {
        if(!tmp.empty())
            tmp.append("&");
        tmp.append(p.first);
        tmp.append("=");
        tmp.append(p.second);
    }
    return tmp;
}


pam_oauth2_curl::params &
pam_oauth2_curl_impl::add_params(pam_oauth2_curl::params &params, std::string const &key, std::string const &value)
{
    if(pam_oauth2_curl_impl::contains_reserved(key))
        throw "Cannot happen QPAKD";
    // Check if it is already in there using some very lispy code
    auto q = params.end();
    pam_oauth2_curl::params::iterator p = std::find_if(params.begin(), q,
				      // The lambda can't use auto in C++11...
				      [&key](std::pair<std::string,std::string> const &pair) { return key == pair.first; });
    std::string key_copy(key);
    if(p == q)
    {
        params.push_back(std::make_pair<std::string,std::string>(std::move(key_copy), encode(value)));
    } else {
        // TODO warn if we are changing the value?
        p->second = value;
    }
    return params;
}



size_t
WriteCallback(char const *contents, size_t size, size_t nmemb, void *userp)
{
    ((call_data *)userp)->callback_data.append((char const *)contents, size * nmemb);
    return size * nmemb;
}