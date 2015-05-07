#include "SmartDownloader.h"
#include <iostream>
#include "Downloader.h"
#include "StringUtil.h"
#include "util.h"


SmartDownloader::SmartDownloader(const std::string &regex) {
    std::string err_msg;
    matcher_.reset(RegexMatcher::RegexMatcherFactory(regex, &err_msg));
    if (not matcher_) {
	std::cerr << progname << ": in SmartDownloader::SmartDownloader: pattern failed to compile \""
		  << regex << "\"!\n";
	std::exit(EXIT_FAILURE);
    }
}


bool SmartDownloader::canHandleThis(const std::string &url) const {
    std::string err_msg;
    if (matcher_->matched(url, &err_msg)) {
	if (not err_msg.empty()) {
	    std::cerr << progname
		      << ": in SmartDownloader::canHandleThis: an error occurred while trying to match \""
		      << url << "\" with \"" << matcher_->getPattern() << "\"! (" << err_msg << ")\n";
	    std::exit(EXIT_FAILURE);
	}
	return true;
    }

    return false;
}


bool SmartDownloader::downloadDoc(const std::string &url, const unsigned timeout, std::string * const document) {
    if (downloadDocImpl(url, timeout, document)) {
	++success_count_;
	return true;
    } else
	return false;
}


bool SimpleSuffixDownloader::canHandleThis(const std::string &url) const {
    for (const auto &suffix : suffixes_) {
	if (StringUtil::IsProperSuffixOfIgnoreCase(suffix, url))
	    return true;
    }

    return false;
}


bool SimpleSuffixDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					     std::string * const document)
{
    return Download(url, timeout, document) == 0;
}


bool SimplePrefixDownloader::canHandleThis(const std::string &url) const {
    for (const auto &prefix : prefixes_) {
	if (StringUtil::StartsWith(url, prefix, /* ignore_case = */ true))
	    return true;
    }

    return false;
}


bool SimplePrefixDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					     std::string * const document)
{
    return Download(url, timeout, document) == 0;
}


bool DigiToolSmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					      std::string * const document)
{
    return Download(url, timeout, document) == 0;
}


bool IdbSmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					 std::string * const document)
{
    const size_t last_slash_pos(url.find_last_of('/'));
    const std::string doc_url("http://idb.ub.uni-tuebingen.de/cgi-bin/digi-downloadPdf.fcgi?projectname="
			      + url.substr(last_slash_pos + 1));
    return Download(doc_url, timeout, document) == 0;
}


bool BszSmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					 std::string * const document)
{
    const std::string doc_url(url.substr(0, url.size() - 3) + "pdf");
    return Download(doc_url, timeout, document) == 0;
}


bool BvbrSmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					  std::string * const document)
{
    std::string html;
    if (Download(url, timeout, &html) != 0)
	return false;
    const std::string start_string("<body onload=window.location=\"");
    size_t start_pos(html.find(start_string));
    if (start_pos == std::string::npos)
	return false;
    start_pos += start_string.size();
    const size_t end_pos(html.find('"', start_pos + 1));
    if (end_pos == std::string::npos)
	return false;
    const std::string doc_url("http://bvbr.bib-bvb.de:8991" + html.substr(start_pos, end_pos - start_pos));
    return Download(doc_url, timeout, document) == 0;
}


bool Bsz21SmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					   std::string * const document)
{
    std::string html;
    const int retcode = Download(url, timeout, &html);
    if (retcode != 0)
	return false;
    const std::string start_string("<meta content=\"https://publikationen.uni-tuebingen.de/xmlui/bitstream/");
    size_t start_pos(html.find(start_string));
    if (start_pos == std::string::npos)
	return false;
    start_pos += start_string.size() - 55;
    const size_t end_pos(html.find('"', start_pos + 1));
    if (end_pos == std::string::npos)
	return false;
    const std::string doc_url(html.substr(start_pos, end_pos - start_pos));
    return Download(doc_url, timeout, document) == 0;
}


bool LocGovSmartDownloader::downloadDocImpl(const std::string &url, const unsigned timeout,
					    std::string * const document)
{
    if (url.length() < 11)
	return false;
    const std::string doc_url("http://catdir" + url.substr(10));
    std::string html;
    const int retcode = Download(doc_url, timeout, &html);

    if (retcode != 0)
	return false;
    size_t toc_start_pos(StringUtil::FindCaseInsensitive(html, "<TITLE>Table of contents"));
    if (toc_start_pos == std::string::npos)
	return false;
    const size_t pre_start_pos(StringUtil::FindCaseInsensitive(html, "<pre>"));
    if (pre_start_pos == std::string::npos)
	return false;
    const size_t pre_end_pos(StringUtil::FindCaseInsensitive(html, "</pre>"));
    if (pre_end_pos == std::string::npos)
	return false;
    *document = html.substr(pre_start_pos + 5, pre_end_pos - pre_start_pos - 5);
    return true;
}
