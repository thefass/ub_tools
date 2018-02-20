/** \file    zts_client.cc
    \brief   Downloads bibliographic metadata using a Zotero Translation server.
    \author  Dr. Johannes Ruscheinski
    \author  Mario Trojan

    \copyright 2018 Universitätsbibliothek Tübingen

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU Affero General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Affero General Public License for more details.

    You should have received a copy of the GNU Affero General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#include <iostream>
#include <unordered_map>
#include <cinttypes>
#include "Compiler.h"
#include "Downloader.h"
#include "ExecUtil.h"
#include "File.h"
#include "FileUtil.h"
#include "HttpHeader.h"
#include "JSON.h"
#include "MARC.h"
#include "MiscUtil.h"
#include "RegexMatcher.h"
#include "SimpleCrawler.h"
#include "SocketUtil.h"
#include "StringUtil.h"
#include "TextUtil.h"
#include "TimeLimit.h"
#include "UrlUtil.h"
#include "util.h"
#include "WebUtil.h"
#include "Zotero.h"


namespace zts_client {


const std::string USER_AGENT("ub_tools (https://ixtheo.de/docs/user_agents)");
const std::string DEFAULT_SIMPLE_CRAWLER_CONFIG_PATH("/usr/local/var/lib/tuelib/zotero_crawler.conf");

// Default timeout values in milliseconds
const unsigned DEFAULT_TIMEOUT(10000);
const unsigned DEFAULT_MIN_URL_PROCESSING_TIME(200);
const unsigned DEFAULT_ZOTERO_CONVERSION_TIMEOUT(60000);


void Usage() {
    std::cerr << "Usage: " << ::progname << " [options] zts_server_url map_directory output_file\n"
              << "\t[ --ignore-robots-dot-txt)                                Nomen est omen.\n"
              << "\t[ --proxy=<proxy_host_and_port>)                          Proxy host and port, default none.\n"
              << "\t[ --simple-crawler-config-file=<path> ]                   Nomen est omen, default: " << DEFAULT_SIMPLE_CRAWLER_CONFIG_PATH << "\n"
              << "\t[ --progress-file=<path> ]                                Nomen est omen.\n"
              << "\t[ --output-format=<format> ]                              marcxml (default), marc21 or json.\n"
              << "\n"
              << "\tzts_server_url                                            URL for Zotero Translation Server.\n"
              << "\tmap_directory                                             path to a subdirectory containing all required\n"
              << "\t                                                          map files and the file containing hashes of\n"
              << "\t                                                          previously generated records.\n"
              << "\toutput_file                                               Nomen est omen.\n"
              << "\n";
    std::exit(EXIT_FAILURE);
}


struct Date {
    static const unsigned INVALID = 0;
    unsigned day_;
    unsigned month_;
    unsigned year_;
public:
    Date(): day_(INVALID), month_(INVALID), year_(INVALID) { }
};


Date StringToDate(const std::string &date_str) {
    Date date;

    time_t unix_time(WebUtil::ParseWebDateAndTime(date_str));
    if (unix_time != TimeUtil::BAD_TIME_T) {
        tm *tm(::gmtime(&unix_time));
        if (unlikely(tm == nullptr))
            ERROR("gmtime(3) failed to convert a time_t! (" + date_str + ")");
        date.day_   = tm->tm_mday;
        date.month_ = tm->tm_mon;
        date.year_  = tm->tm_year;
    } else
        WARNING("don't know how to convert \"" + date_str + "\" to a Date instance!");

    return date;
}


std::string GetNextControlNumber() {
    static unsigned last_control_number;
    ++last_control_number;
    static const std::string prefix("ZTS");
    return prefix + StringUtil::PadLeading(std::to_string(last_control_number), 7, '0');
}


// If "key" is in "map", then return the mapped value, o/w return "key".
inline std::string OptionalMap(const std::string &key, const std::unordered_map<std::string, std::string> &map) {
    const auto &key_and_value(map.find(key));
    return (key_and_value == map.cend()) ? key : key_and_value->second;
}


// "author" must be in the lastname,firstname format. Returns the empty string if no PPN was found.
std::string DownloadAuthorPPN(const std::string &author) {
    static const RegexMatcher * const matcher(RegexMatcher::RegexMatcherFactory(
         "<SMALL>PPN</SMALL>.*<div><SMALL>([0-9X]+)"));
    const std::string lookup_url("http://swb.bsz-bw.de/DB=2.104/SET=70/TTL=1/CMD?SGE=&ACT=SRCHM&MATCFILTER=Y"
                                 "&MATCSET=Y&NOSCAN=Y&PARSE_MNEMONICS=N&PARSE_OPWORDS=N&PARSE_OLDSETS=N&IMPLAND=Y"
                                 "&NOABS=Y&ACT0=SRCHA&SHRTST=50&IKT0=1&TRM0=" + UrlUtil::UrlEncode(author)
                                 +"&ACT1=*&IKT1=2057&TRM1=*&ACT2=*&IKT2=8977&TRM2=theolog*&ACT3=-&IKT3=8978-&TRM3=1"
                                 "[1%2C2%2C3%2C4%2C5%2C6%2C7%2C8][0%2C1%2C2%2C3%2C4%2C5%2C6%2C7%2C8%2C9]"
                                 "[0%2C1%2C2%2C3%2C4%2C5%2C6%2C7%2C8%2C9]?");
    Downloader downloader(lookup_url);
    if (downloader.anErrorOccurred())
        WARNING(downloader.getLastErrorMessage());
    else if (matcher->matched(downloader.getMessageBody()))
        return (*matcher)[1];
    return "";
}


struct ZtsClientMaps {
public:
    std::unordered_map<std::string, std::string> ISSN_to_SSG_map_;
    std::unordered_map<std::string, std::string> ISSN_to_keyword_field_map_;
    std::unordered_map<std::string, std::string> ISSN_to_language_code_map_;
    std::unordered_map<std::string, std::string> ISSN_to_licence_map_;
    std::unordered_map<std::string, std::string> ISSN_to_physical_form_map_;
    std::unordered_map<std::string, std::string> ISSN_to_superior_ppn_map_;
    std::unordered_map<std::string, std::string> ISSN_to_volume_map_;
    std::unordered_map<std::string, std::string> language_to_language_code_map_;
    std::unordered_set<std::string> previously_downloaded_;
};


void AugmentJsonCreators(JSON::ArrayNode * const creators_array,
                         std::vector<std::string> * const comments)
{
    for (size_t i(0); i < creators_array->size(); ++i) {
        JSON::ObjectNode * const creator_object(creators_array->getObjectNode(i));

        const JSON::JSONNode * const last_name_node(creator_object->getNode("lastName"));
        if (last_name_node == nullptr)
            ERROR("creator is missing a last name!");
        std::string name(creator_object->getStringValue("lastName"));

        const JSON::JSONNode * const first_name_node(creator_object->getNode("firstName"));
        if (first_name_node != nullptr)
            name += ", " + creator_object->getStringValue("firstName");

        const std::string PPN(DownloadAuthorPPN(name));
        if (not PPN.empty()) {
            comments->emplace_back("Added author PPN " + PPN + " for author " + name);
            creator_object->insert("ppn", new JSON::StringNode(PPN));
        }
    }
}


// Improve JSON result delivered by Zotero Translation Server
void AugmentJson(JSON::ObjectNode * const object_node, ZtsClientMaps &zts_client_maps) {
    INFO("Augmenting JSON...");
    std::map<std::string, std::string> custom_fields;
    std::vector<std::string> comments;
    std::string issn_raw, issn_normalized;
    JSON::StringNode *language_node(nullptr);
    for (auto &key_and_node : *object_node) {
        if (key_and_node.first == "language") {
            language_node = JSON::JSONNode::CastToStringNodeOrDie("language", key_and_node.second);
            const std::string language_json(language_node->getValue());
            const std::string language_mapped(OptionalMap(language_json,
                            zts_client_maps.language_to_language_code_map_));
            if (language_json != language_mapped) {
                language_node->setValue(language_mapped);
                comments.emplace_back("changed \"language\" from \"" + language_json + "\" to \"" + language_mapped + "\"");
            }
        }
        else if (key_and_node.first == "creators") {
            JSON::ArrayNode * creators_array(JSON::JSONNode::CastToArrayNodeOrDie("creators", key_and_node.second));
            AugmentJsonCreators(creators_array, &comments);
        }
        else if (key_and_node.first == "ISSN") {
            issn_raw = JSON::JSONNode::CastToStringNodeOrDie(key_and_node.first, key_and_node.second)->getValue();
            if (unlikely(not MiscUtil::NormaliseISSN(issn_raw, &issn_normalized)))
                ERROR("\"" + issn_raw + "\" is not a valid ISSN!");

            const auto ISSN_and_parent_ppn(zts_client_maps.ISSN_to_superior_ppn_map_.find(issn_normalized));
            if (ISSN_and_parent_ppn != zts_client_maps.ISSN_to_superior_ppn_map_.cend()) {
                issn_raw = ISSN_and_parent_ppn->second;
                issn_normalized = ISSN_and_parent_ppn->second;
            }

            custom_fields.emplace(std::pair<std::string, std::string>("issnRaw", issn_raw));
            custom_fields.emplace(std::pair<std::string, std::string>("issnNormalized", issn_normalized));
        }
    }

    // ISSN specific overrides
    if (not issn_normalized.empty()) {
        // physical form
        const auto ISSN_and_physical_form(zts_client_maps.ISSN_to_physical_form_map_.find(issn_normalized));
        if (ISSN_and_physical_form != zts_client_maps.ISSN_to_physical_form_map_.cend()) {
            if (ISSN_and_physical_form->second == "A")
                custom_fields.emplace(std::pair<std::string, std::string>("physicalForm", "A"));
            else if (ISSN_and_physical_form->second == "O")
                custom_fields.emplace(std::pair<std::string, std::string>("physicalForm", "O"));
            else
                ERROR("unhandled entry in physical form map: \""
                      + ISSN_and_physical_form->second + "\"!");
        }

        // language
        const auto ISSN_and_language(zts_client_maps.ISSN_to_language_code_map_.find(issn_normalized));
        if (ISSN_and_language != zts_client_maps.ISSN_to_language_code_map_.cend()) {
            if (language_node != nullptr) {
                const std::string language_old(language_node->getValue());
                language_node->setValue(ISSN_and_language->second);
                comments.emplace_back("changed \"language\" from \"" + language_old + "\" to \"" + ISSN_and_language->second + "\" due to ISSN map");
            } else {
                language_node = new JSON::StringNode(ISSN_and_language->second);
                object_node->insert("language", language_node);
                comments.emplace_back("added \"language\" \"" + ISSN_and_language->second + "\" due to ISSN map");
            }
        }

        // volume
        const std::string volume(object_node->getOptionalStringValue("volume"));
        if (volume.empty()) {
            const auto ISSN_and_volume(zts_client_maps.ISSN_to_volume_map_.find(issn_normalized));
            if (ISSN_and_volume != zts_client_maps.ISSN_to_volume_map_.cend()) {
                if (volume.empty()) {
                    JSON::JSONNode * const volume_node = object_node->getNode("volume");
                    reinterpret_cast<JSON::StringNode *>(volume_node)->setValue(ISSN_and_volume->second);
                } else {
                    JSON::StringNode *volume_node(new JSON::StringNode(ISSN_and_volume->second));
                    object_node->insert("volume", volume_node);
                }
            }
        }

        // license code
        const auto ISSN_and_license_code(zts_client_maps.ISSN_to_licence_map_.find(issn_normalized));
        if (ISSN_and_license_code != zts_client_maps.ISSN_to_licence_map_.end()) {
            if (ISSN_and_license_code->second != "l")
                WARNING("ISSN_to_licence.map contains an ISSN that has not been mapped to an \"l\" but \""
                        + ISSN_and_license_code->second
                        + "\" instead and we don't know what to do with it!");
            else
                custom_fields.emplace(std::pair<std::string, std::string>("licenseCode", ISSN_and_license_code->second));
        }

        // SSG numbers:
        const auto ISSN_and_SSGN_numbers(zts_client_maps.ISSN_to_SSG_map_.find(issn_normalized));
        if (ISSN_and_SSGN_numbers != zts_client_maps.ISSN_to_SSG_map_.end())
            custom_fields.emplace(std::pair<std::string, std::string>("ssgNumbers", ISSN_and_SSGN_numbers->second));
    }

    // Insert custom node with fields and comments
    if (comments.size() > 0 or custom_fields.size() > 0) {
        JSON::ObjectNode *custom_object(new JSON::ObjectNode);
        if (comments.size() > 0) {
            JSON::ArrayNode *comments_node(new JSON::ArrayNode);
            for (const auto &comment : comments) {
                JSON::StringNode *comment_node(new JSON::StringNode(comment));
                comments_node->push_back(comment_node);
            }
            custom_object->insert("comments", comments_node);
        }
        object_node->insert("ubtue", custom_object);
    }
}


const std::string DEFAULT_SUBFIELD_CODE("eng");

// forward declaration
struct ZtsClientParams;


class FormatHandler {
protected:
    std::string output_format_;
    std::string output_file_;
    ZtsClientMaps &zts_client_maps_;
    ZtsClientParams &zts_client_params_;
public:
    FormatHandler(const std::string &output_format, const std::string &output_file,
                  ZtsClientMaps * const zts_client_maps, ZtsClientParams * const zts_client_params) :
                  output_format_(output_format), output_file_(output_file),
                  zts_client_maps_(*zts_client_maps), zts_client_params_(*zts_client_params) {}
    virtual ~FormatHandler() {}

    virtual void prepareProcessing() = 0;
    virtual std::pair<unsigned, unsigned> processRecord(const JSON::ObjectNode * const object_node) = 0;
    virtual void finishProcessing() = 0;

    static std::unique_ptr<FormatHandler> Factory(const std::string &output_format,
                                                  const std::string &output_file,
                                                  ZtsClientMaps * const zts_client_maps,
                                                  ZtsClientParams * const zts_client_params);
};


class JsonFormatHandler : public FormatHandler {
    unsigned record_count_ = 0;
    File *output_file_object_;
public:
    using FormatHandler::FormatHandler;

    void prepareProcessing() {
        output_file_object_ = new File(output_file_, "w");
        output_file_object_->write("[");
    }

    std::pair<unsigned, unsigned> processRecord(const JSON::ObjectNode * const object_node) {
        if (record_count_ > 0)
            output_file_object_->write(",");
        output_file_object_->write(object_node->toString());
        ++record_count_;
        return std::make_pair(1, 0);
    }

    void finishProcessing() {
        output_file_object_->write("]");
        output_file_object_->close();
    }
};


class ZoteroFormatHandler : public FormatHandler {
    unsigned record_count_ = 0;
    std::string json_buffer_;
public:
    using FormatHandler::FormatHandler;

    void prepareProcessing() {
        json_buffer_ = "[";
    }

    std::pair<unsigned, unsigned> processRecord(const JSON::ObjectNode * const object_node) {
        if (record_count_ > 0)
            json_buffer_ += ",";
        json_buffer_ += object_node->toString();
        ++record_count_;
        return std::make_pair(1, 0);
    }

    // needs full definition for ZtsClientParams
    void finishProcessing();
};


class MarcFormatHandler : public FormatHandler {
    std::unique_ptr<MARC::Writer> marc_writer_;


    inline std::string CreateSubfieldFromStringNode(const std::string &key, const JSON::JSONNode * const node,
                                                const std::string &tag, const char subfield_code,
                                                MARC::Record * const marc_record, const char indicator1 = ' ',
                                                const char indicator2 = ' ')
    {
        const JSON::StringNode * const string_node(JSON::JSONNode::CastToStringNodeOrDie(key, node));
        const std::string value(string_node->getValue());
        marc_record->insertField(tag, { { subfield_code, value } }, indicator1, indicator2);
        return value;
    }


    inline std::string CreateSubfieldFromStringNode(const std::pair<std::string, JSON::JSONNode *> &key_and_node,
                                                    const std::string &tag, const char subfield_code,
                                                    MARC::Record * const marc_record, const char indicator1 = ' ',
                                                    const char indicator2 = ' ')
    {
        return CreateSubfieldFromStringNode(key_and_node.first, key_and_node.second, tag, subfield_code, marc_record,
                                            indicator1, indicator2);
    }


    void ExtractKeywords(const JSON::JSONNode * tags_node, const std::string &issn,
                     const std::unordered_map<std::string, std::string> &ISSN_to_keyword_field_map,
                     MARC::Record * const new_record)
    {
        const JSON::ArrayNode * const tags(JSON::JSONNode::CastToArrayNodeOrDie("tags", tags_node));

        // Where to stuff the data:
        std::string marc_field("653");
        char marc_subfield('a');
        if (not issn.empty()) {
            const auto issn_and_field_tag_and_subfield_code(ISSN_to_keyword_field_map.find(issn));
            if (issn_and_field_tag_and_subfield_code != ISSN_to_keyword_field_map.end()) {
                if (unlikely(issn_and_field_tag_and_subfield_code->second.length() != 3 + 1))
                    ERROR("\"" + issn_and_field_tag_and_subfield_code->second
                          + "\" is not a valid MARC tag + subfield code! (Error in \"ISSN_to_keyword_field.map\"!)");
                marc_field    = issn_and_field_tag_and_subfield_code->second.substr(0, 3);
                marc_subfield =  issn_and_field_tag_and_subfield_code->second[3];
            }
        }

        for (auto tag : *tags) {
            const JSON::ObjectNode * const tag_object(JSON::JSONNode::CastToObjectNodeOrDie("tag", tag));
            const JSON::JSONNode * const tag_node(tag_object->getNode("tag"));
            if (tag_node == nullptr)
                WARNING("unexpected: tag object does not contain a \"tag\" entry!");
            else if (tag_node->getType() != JSON::JSONNode::STRING_NODE)
                ERROR("unexpected: tag object's \"tag\" entry is not a string node!");
            else
                CreateSubfieldFromStringNode("tag", tag_node, marc_field, marc_subfield, new_record);
        }
    }


    void ExtractVolumeYearIssueAndPages(const JSON::ObjectNode &object_node, MARC::Record * const new_record) {
        std::vector<MARC::Subfield> subfields;

        const std::string date_str(object_node.getOptionalStringValue("date"));
        if (not date_str.empty()) {
            const Date date(StringToDate(date_str));
            if (date.year_ != Date::INVALID)
                subfields.emplace_back('j', std::to_string(date.year_));
        }

        const std::string issue(object_node.getOptionalStringValue("issue"));
        if (not issue.empty())
            subfields.emplace_back('e', issue);

        const std::string pages(object_node.getOptionalStringValue("pages"));
        if (not pages.empty())
            subfields.emplace_back('h', pages);

        const std::string volume(object_node.getOptionalStringValue("volume"));
        if (not volume.empty())
            subfields.emplace_back('d', volume);

        if (not subfields.empty())
            new_record->insertField("936", subfields);
    }


    void CreateCreatorFields(const JSON::JSONNode * const creators_node, MARC::Record * const marc_record) {
        const JSON::ArrayNode * const creators_array(JSON::JSONNode::CastToArrayNodeOrDie("creators", creators_node));
        for (auto creator_node : *creators_array) {
            const JSON::ObjectNode * const creator_object(JSON::JSONNode::CastToObjectNodeOrDie("creator", creator_node));

            const JSON::JSONNode * const last_name_node(creator_object->getNode("lastName"));
            if (last_name_node == nullptr)
                ERROR("creator is missing a last name!");
            const JSON::StringNode * const last_name(JSON::JSONNode::CastToStringNodeOrDie("lastName", last_name_node));
            std::string name(last_name->getValue());

            const JSON::JSONNode * const first_name_node(creator_object->getNode("firstName"));
            if (first_name_node != nullptr) {
                const JSON::StringNode * const first_name(JSON::JSONNode::CastToStringNodeOrDie("firstName", first_name_node));
                name += ", " + first_name->getValue();
            }

            std::string PPN;
            const JSON::JSONNode * const ppn_node(creator_object->getNode("ppn"));
            if (ppn_node != nullptr) {
                const JSON::StringNode * const ppn_string_node(JSON::JSONNode::CastToStringNodeOrDie("ppn", first_name_node));
                PPN = ppn_string_node->getValue();
                name = "!" + PPN + "!";
            }

            const JSON::JSONNode * const creator_type(creator_object->getNode("creatorType"));
            std::string creator_role;
            if (creator_type != nullptr) {
                const JSON::StringNode * const creator_role_node(JSON::JSONNode::CastToStringNodeOrDie("creatorType", creator_type));
                creator_role = creator_role_node->getValue();
            }

            if (creator_node == *(creators_array->begin())) {
                if (creator_role.empty())
                    marc_record->insertField("100", { { 'a', name } });
                else
                    marc_record->insertField("100", { { 'a', name }, { 'e', creator_role } });
            } else { // Not the first creator!
                if (creator_role.empty())
                    marc_record->insertField("700", { { 'a', name } });
                else
                    marc_record->insertField("700", { { 'a', name }, { 'e', creator_role } });
            }
        }
    }
public:
    using FormatHandler::FormatHandler;

    void prepareProcessing() {
        marc_writer_ = MARC::Writer::Factory(output_file_);
    }

    std::pair<unsigned, unsigned> processRecord(const JSON::ObjectNode * const object_node) {
        static RegexMatcher * const ignore_fields(RegexMatcher::RegexMatcherFactory(
            "^issue|pages|publicationTitle|volume|date|tags|libraryCatalog|itemVersion|accessDate$"));
        unsigned record_count(0), previously_downloaded_count(0);
        MARC::Record new_record(MARC::Record::TypeOfRecord::LANGUAGE_MATERIAL,
                                MARC::Record::BibliographicLevel::MONOGRAPH_OR_ITEM,
                                GetNextControlNumber());
        bool is_journal_article(false);
        std::string publication_title, parent_ppn, parent_issn, issn;
        for (const auto &key_and_node : *object_node) {
            if (ignore_fields->matched(key_and_node.first))
                continue;

            if (key_and_node.first == "language")
                new_record.insertField("041", { { 'a',
                    JSON::JSONNode::CastToStringNodeOrDie(key_and_node.first, key_and_node.second)->getValue() } });
            else if (key_and_node.first == "url")
                CreateSubfieldFromStringNode(key_and_node, "856", 'u', &new_record);
            else if (key_and_node.first == "title")
                CreateSubfieldFromStringNode(key_and_node, "245", 'a', &new_record);
            else if (key_and_node.first == "abstractNote")
                CreateSubfieldFromStringNode(key_and_node, "520", 'a', &new_record, /* indicator1 = */ '3');
            else if (key_and_node.first == "date")
                CreateSubfieldFromStringNode(key_and_node, "362", 'a', &new_record, /* indicator1 = */ '0');
            else if (key_and_node.first == "DOI") {
                if (unlikely(key_and_node.second->getType() != JSON::JSONNode::STRING_NODE))
                    ERROR("expected DOI node to be a string node!");
                new_record.insertField(
                    "856", { { 'u', "urn:doi:" + reinterpret_cast<JSON::StringNode *>(key_and_node.second)->getValue()} });
            } else if (key_and_node.first == "shortTitle")
                CreateSubfieldFromStringNode(key_and_node, "246", 'a', &new_record);
            else if (key_and_node.first == "creators")
                CreateCreatorFields(key_and_node.second, &new_record);
            else if (key_and_node.first == "itemType") {
                const std::string item_type(JSON::JSONNode::CastToStringNodeOrDie(key_and_node.first, key_and_node.second)->getValue());
                if (item_type == "journalArticle") {
                    is_journal_article = true;
                    publication_title = object_node->getOptionalStringValue("publicationTitle");
                    ExtractVolumeYearIssueAndPages(*object_node, &new_record);
                } else if (item_type == "magazineArticle")
                    ExtractVolumeYearIssueAndPages(*object_node, &new_record);
                else
                    WARNING("unknown item type: \"" + item_type + "\"!");
            } else if (key_and_node.first == "rights") {
                const std::string copyright(JSON::JSONNode::CastToStringNodeOrDie(key_and_node.first, key_and_node.second)->getValue());
                if (UrlUtil::IsValidWebUrl(copyright))
                    new_record.insertField("542", { { 'u', copyright } });
                else
                    new_record.insertField("542", { { 'f', copyright } });
            } else
                WARNING("unknown key \"" + key_and_node.first + "\" with node type "
                                + JSON::JSONNode::TypeToString(key_and_node.second->getType()) + "! ("
                                + key_and_node.second->toString() + ")");
        }

        const JSON::JSONNode * custom_node(object_node->getNode("ubtue"));
        if (custom_node != nullptr) {
            const JSON::ObjectNode * const custom_object(JSON::JSONNode::CastToObjectNodeOrDie("ubtue", custom_node));
            parent_issn = custom_object->getOptionalStringValue("issnRaw");
            issn = custom_object->getOptionalStringValue("issnNormalized");

            // physical form
            const std::string physical_form(custom_object->getOptionalStringValue("physicalForm"));
            if (not physical_form.empty()) {
                if (physical_form == "A")
                    new_record.insertField("007", "tu");
                else if (physical_form == "O")
                    new_record.insertField("007", "cr uuu---uuuuu");
                else
                    ERROR("unhandled value of physical form: \""
                          + physical_form + "\"!");
            }

            // volume
            const std::string volume(custom_object->getOptionalStringValue("volume"));
            if (not volume.empty()) {
                const auto field_it(new_record.findTag("936"));
                if (field_it == new_record.end())
                    new_record.insertField("936", { { 'v', volume } });
                else
                    field_it->getSubfields().addSubfield('v', volume);
            }

            // license code
            const std::string license(custom_object->getOptionalStringValue("licenseCode"));
            if (license == "l") {
                const auto field_it(new_record.findTag("936"));
                if (field_it != new_record.end())
                    field_it->getSubfields().addSubfield('z', "Kostenfrei");
            }

            // SSG numbers:
            const std::string ssg_numbers(custom_object->getOptionalStringValue("ssgNumbers"));
            if (not ssg_numbers.empty())
                new_record.addSubfield("084", 'a', ssg_numbers);
        }

        // keywords:
        const JSON::JSONNode * const tags_node(object_node->getNode("tags"));
        if (tags_node != nullptr)
            ExtractKeywords(tags_node, issn, zts_client_maps_.ISSN_to_keyword_field_map_, &new_record);

        // Populate 773:
        if (is_journal_article) {
            std::vector<MARC::Subfield> subfields;
            if (not publication_title.empty())
                subfields.emplace_back('a', publication_title);
            if (not parent_issn.empty())
                subfields.emplace_back('x', parent_issn);
            if (not parent_ppn.empty())
                subfields.emplace_back('w', "(DE-576))" + parent_ppn);
            if (not subfields.empty())
                new_record.insertField("773", subfields);
        }

        // language code fallback:
        if (not new_record.hasTag("041"))
            new_record.insertField("041", { { 'a', DEFAULT_SUBFIELD_CODE } });

        // previously downloaded?
        const std::string checksum(MARC::CalcChecksum(new_record, /* exclude_001 = */ true));
        if (zts_client_maps_.previously_downloaded_.find(checksum) == zts_client_maps_.previously_downloaded_.cend()) {
            zts_client_maps_.previously_downloaded_.emplace(checksum);
            marc_writer_->write(new_record);
        } else
            ++previously_downloaded_count;
        ++record_count;

        return std::make_pair(record_count, previously_downloaded_count);
    }

    void finishProcessing() {
        marc_writer_.reset();
    }
};


std::unique_ptr<FormatHandler> FormatHandler::Factory(const std::string &output_format,
                                                      const std::string &output_file,
                                                      ZtsClientMaps * const zts_client_maps,
                                                      ZtsClientParams * const zts_client_params)
{
    if (output_format == "marcxml" or output_format == "marc21")
        return std::unique_ptr<FormatHandler>(new MarcFormatHandler(output_format, output_file, zts_client_maps, zts_client_params));
    else if (output_format == "json")
        return std::unique_ptr<FormatHandler>(new JsonFormatHandler(output_format, output_file, zts_client_maps, zts_client_params));
    else if (std::find(Zotero::ExportFormats.begin(), Zotero::ExportFormats.end(), output_format) != Zotero::ExportFormats.end())
        return std::unique_ptr<FormatHandler>(new ZoteroFormatHandler(output_format, output_file, zts_client_maps, zts_client_params));
    else
        ERROR("invalid output-format: " + output_format);
}


struct ZtsClientParams {
public:
    Url zts_server_url_;
    TimeLimit min_url_processing_time_ = DEFAULT_MIN_URL_PROCESSING_TIME;
    unsigned harvested_url_count_ = 0;
    std::unique_ptr<FormatHandler> format_handler_;
};


void ZoteroFormatHandler::finishProcessing() {
    json_buffer_ += "]";

    Downloader::Params downloader_params;
    std::string response_body;
    std::string error_message;

    if (not Zotero::Export(zts_client_params_.zts_server_url_, DEFAULT_ZOTERO_CONVERSION_TIMEOUT, downloader_params,
                           output_format_, json_buffer_, &response_body, &error_message))
        ERROR("converting to target format failed: " + error_message);
    else
        FileUtil::WriteString(output_file_, response_body);
}


bool ParseLine(const std::string &line, std::string * const key, std::string * const value) {
    key->clear(), value->clear();

    // Extract the key:
    auto ch(line.cbegin());
    while (ch != line.cend() and *ch != '=') {
        if (unlikely(*ch == '\\')) {
            ++ch;
            if (unlikely(ch == line.cend()))
                return false;
        }
        *key += *ch++;
    }
    if (unlikely(ch == line.cend()))
        return false;
    ++ch; // Skip over the equal-sign.

    // Extract value:
    while (ch != line.cend() and *ch != '#' /* Comment start. */) {
        if (unlikely(*ch == '\\')) {
            ++ch;
            if (unlikely(ch == line.cend()))
                return false;
        }
        *value += *ch++;
    }
    StringUtil::RightTrim(value);

    return not key->empty() and not value->empty();
}


void LoadMapFile(const std::string &filename, std::unordered_map<std::string, std::string> * const from_to_map) {
    std::unique_ptr<File> input(FileUtil::OpenInputFileOrDie(filename));

    unsigned line_no(0);
    while (not input->eof()) {
        std::string line(input->getline());
        ++line_no;

        StringUtil::Trim(&line);
        std::string key, value;
        if (not ParseLine(line, &key, &value))
            ERROR("invalid input on line \"" + std::to_string(line_no) + "\" in \""
                          + input->getPath() + "\"!");
        from_to_map->emplace(key, value);
    }
}


RegexMatcher *LoadSupportedURLsRegex(const std::string &map_directory_path) {
    std::unique_ptr<File> input(FileUtil::OpenInputFileOrDie(map_directory_path + "targets.regex"));

    std::string combined_regex;
    while (not input->eof()) {
        std::string line(input->getline());
        StringUtil::Trim(&line);
        if (likely(not line.empty())) {
            if (likely(not combined_regex.empty()))
                combined_regex += '|';
            combined_regex += "(?:" + line + ")";
        }
    }

    std::string err_msg;
    RegexMatcher * const supported_urls_regex(RegexMatcher::RegexMatcherFactory(combined_regex, &err_msg));
    if (supported_urls_regex == nullptr)
        ERROR("compilation of the combined regex failed: " + err_msg);

    return supported_urls_regex;
}


void LoadPreviouslyDownloadedHashes(File * const input,
                                    std::unordered_set<std::string> * const previously_downloaded)
{
    while (not input->eof()) {
        std::string line(input->getline());
        StringUtil::Trim(&line);
        if (likely(not line.empty()))
            previously_downloaded->emplace(TextUtil::Base64Decode(line));
    }

    logger->info("Loaded " + StringUtil::ToString(previously_downloaded->size()) + " hashes of previously generated records.");
}


std::pair<unsigned, unsigned> Harvest(const std::string &harvest_url,
                                      const std::string &harvested_html,
                                      ZtsClientParams &zts_client_params,
                                      ZtsClientMaps &zts_client_maps,
                                      bool log = true)
{
    std::pair<unsigned, unsigned> record_count_and_previously_downloaded_count;
    static std::unordered_set<std::string> already_harvested_urls;
    if (already_harvested_urls.find(harvest_url) != already_harvested_urls.end()) {
        logger->info("Skipping URL (already harvested): " + harvest_url);
        return record_count_and_previously_downloaded_count;
    }
    already_harvested_urls.emplace(harvest_url);

    logger->info("Harvesting URL: " + harvest_url);

    std::string response_body, error_message;
    unsigned response_code;
    zts_client_params.min_url_processing_time_.sleepUntilExpired();
    Downloader::Params downloader_params;
    const bool download_result(Zotero::Web(zts_client_params.zts_server_url_, /* time_limit = */ DEFAULT_TIMEOUT, downloader_params,
                               Url(harvest_url), harvested_html, &response_body, &response_code, &error_message));

    zts_client_params.min_url_processing_time_.restart();
    if (not download_result) {
        logger->info("Zotero conversion failed: " + error_message);
        return std::make_pair(0, 0);
    }

    // 500 => internal server error (e.g. error in translator))
    if (response_code == 500) {
        logger->info("Error: " + response_body);
        return std::make_pair(0, 0);
    }

    // 501 => not implemented (e.g. no translator available)
    if (response_code == 501) {
        logger->debug("Skipped (" + response_body + ")");
        return std::make_pair(0, 0);
    }

    JSON::JSONNode *tree_root(nullptr);
    try {
        JSON::Parser json_parser(response_body);
        if (not (json_parser.parse(&tree_root)))
            ERROR("failed to parse returned JSON: " + json_parser.getErrorMessage() + "\n" + response_body);

        // 300 => multiple matches found, try to harvest children
        if (response_code == 300) {
            logger->info("multiple articles found => trying to harvest children");
            if (tree_root->getType() == JSON::ArrayNode::OBJECT_NODE) {
                const JSON::ObjectNode * const object_node(JSON::JSONNode::CastToObjectNodeOrDie("tree_root", tree_root));
                for (const auto &key_and_node : *object_node) {
                    std::pair<unsigned, unsigned> record_count_and_previously_downloaded_count2 =
                        Harvest(key_and_node.first, "", zts_client_params, zts_client_maps, false /* log */);

                    record_count_and_previously_downloaded_count.first += record_count_and_previously_downloaded_count2.first;
                    record_count_and_previously_downloaded_count.second += record_count_and_previously_downloaded_count2.second;
                }
            }
        } else {
            const JSON::ArrayNode * const json_array(JSON::JSONNode::CastToArrayNodeOrDie("tree_root", tree_root));
            for (const auto entry : *json_array) {
                JSON::ObjectNode * const json_object(JSON::JSONNode::CastToObjectNodeOrDie("entry", entry));
                AugmentJson(json_object, zts_client_maps);
                record_count_and_previously_downloaded_count = zts_client_params.format_handler_->processRecord(json_object);
            }
        }
        ++zts_client_params.harvested_url_count_;
        delete tree_root;
    } catch (...) {
        delete tree_root;
        throw;
    }

    if (log) {
        logger->info("Harvested " + StringUtil::ToString(record_count_and_previously_downloaded_count.first) + " record(s) from "
                  + harvest_url + '\n' + "of which "
                  + StringUtil::ToString(record_count_and_previously_downloaded_count.first
                      - record_count_and_previously_downloaded_count.second)
                  + " records were new records.");
    }
    return record_count_and_previously_downloaded_count;
}


void StorePreviouslyDownloadedHashes(File * const output,
                                     const std::unordered_set<std::string> &previously_downloaded)
{
    for (const auto &hash : previously_downloaded)
        output->write(TextUtil::Base64Encode(hash) + '\n');

    logger->info("Stored " + StringUtil::ToString(previously_downloaded.size()) + " hashes of previously generated records.");
}


void StartHarvesting(const bool ignore_robots_dot_txt, const std::string &proxy_host_and_port, const std::string &simple_crawler_config_path,
                     ZtsClientParams &zts_client_params, ZtsClientMaps &zts_client_maps, std::unique_ptr<File> &progress_file,
                     unsigned * const total_record_count, unsigned * const total_previously_downloaded_count)
{
    SimpleCrawler::Params crawler_params;
    crawler_params.ignore_robots_dot_txt_ = ignore_robots_dot_txt;
    crawler_params.timeout_ = DEFAULT_TIMEOUT;
    crawler_params.min_url_processing_time_ = DEFAULT_MIN_URL_PROCESSING_TIME;
    crawler_params.proxy_host_and_port_ = proxy_host_and_port;

    std::vector<SimpleCrawler::SiteDesc> site_descs;
    SimpleCrawler::ParseConfigFile(simple_crawler_config_path, &site_descs);

    unsigned processed_url_count(0);
    for (const auto &site_desc : site_descs) {
        logger->info("Start crawling for base URL: " +  site_desc.start_url_);
        SimpleCrawler crawler(site_desc, crawler_params);
        SimpleCrawler::PageDetails page_details;
        while (crawler.getNextPage(&page_details)) {
            ++processed_url_count;
            if (page_details.error_message_.empty()) {
                const auto record_count_and_previously_downloaded_count(
                    Harvest(page_details.url_, page_details.body_, zts_client_params, zts_client_maps)
                );
                *total_record_count                += record_count_and_previously_downloaded_count.first;
                *total_previously_downloaded_count += record_count_and_previously_downloaded_count.second;
                if (progress_file != nullptr) {
                    progress_file->rewind();
                    if (unlikely(not progress_file->write(
                            std::to_string(processed_url_count) + ";" + std::to_string(crawler.getRemainingCallDepth()) + ";" + page_details.url_)))
                        ERROR("failed to write progress to \"" + progress_file->getPath());
                }
            }
        }
    }

    logger->info("Processed " + std::to_string(processed_url_count) + " URL's.");
}


void Main(int argc, char *argv[]) {
    ::progname = argv[0];
    if (argc < 4 or argc > 9)
        Usage();

    bool ignore_robots_dot_txt(false);
    if (std::strcmp(argv[1], "--ignore-robots-dot-txt") == 0) {
        ignore_robots_dot_txt = true;
        --argc, ++argv;
    }

    std::string proxy_host_and_port;
    const std::string PROXY_FLAG_PREFIX("--proxy=");
    if (StringUtil::StartsWith(argv[1], PROXY_FLAG_PREFIX)) {
        proxy_host_and_port = argv[1] + PROXY_FLAG_PREFIX.length();
        --argc, ++argv;
    }

    std::string simple_crawler_config_path;
    const std::string CONFIG_FLAG_PREFIX("--simple-crawler-config-file=");
    if (StringUtil::StartsWith(argv[1], CONFIG_FLAG_PREFIX)) {
        simple_crawler_config_path = argv[1] + CONFIG_FLAG_PREFIX.length();
        --argc, ++argv;
    } else
        simple_crawler_config_path = DEFAULT_SIMPLE_CRAWLER_CONFIG_PATH;

    std::string progress_filename;
    const std::string PROGRESS_FILE_FLAG_PREFIX("--progress-file=");
    if (StringUtil::StartsWith(argv[1], PROGRESS_FILE_FLAG_PREFIX)) {
        progress_filename = argv[1] + PROGRESS_FILE_FLAG_PREFIX.length();
        --argc, ++argv;
    }

    ZtsClientParams zts_client_params;
    const std::string OUTPUT_FORMAT_FLAG_PREFIX("--output-format=");
    std::string output_format("marcxml");
    if (StringUtil::StartsWith(argv[1], OUTPUT_FORMAT_FLAG_PREFIX)) {
        output_format = argv[1] + OUTPUT_FORMAT_FLAG_PREFIX.length();
        --argc, ++argv;
    }

    if (argc != 4)
        Usage();

    std::string map_directory_path(argv[2]);
    if (not StringUtil::EndsWith(map_directory_path, '/'))
        map_directory_path += '/';

    try {
        zts_client_params.zts_server_url_ = Url(argv[1]);
        ZtsClientMaps zts_client_maps;
        LoadMapFile(map_directory_path + "language_to_language_code.map", &zts_client_maps.language_to_language_code_map_);
        LoadMapFile(map_directory_path + "ISSN_to_language_code.map", &zts_client_maps.ISSN_to_language_code_map_);
        LoadMapFile(map_directory_path + "ISSN_to_licence.map", &zts_client_maps.ISSN_to_licence_map_);
        LoadMapFile(map_directory_path + "ISSN_to_keyword_field.map", &zts_client_maps.ISSN_to_keyword_field_map_);
        LoadMapFile(map_directory_path + "ISSN_to_physical_form.map", &zts_client_maps.ISSN_to_physical_form_map_);
        LoadMapFile(map_directory_path + "ISSN_to_superior_ppn.map", &zts_client_maps.ISSN_to_superior_ppn_map_);
        LoadMapFile(map_directory_path + "ISSN_to_volume.map", &zts_client_maps.ISSN_to_volume_map_);
        LoadMapFile(map_directory_path + "ISSN_to_SSG.map", &zts_client_maps.ISSN_to_SSG_map_);

        const RegexMatcher * const supported_urls_regex(LoadSupportedURLsRegex(map_directory_path));
        (void)supported_urls_regex;

        const std::string PREVIOUSLY_DOWNLOADED_HASHES_PATH(map_directory_path + "previously_downloaded.hashes");
        if (FileUtil::Exists(PREVIOUSLY_DOWNLOADED_HASHES_PATH)) {
            std::unique_ptr<File> previously_downloaded_input(
                FileUtil::OpenInputFileOrDie(PREVIOUSLY_DOWNLOADED_HASHES_PATH));
            LoadPreviouslyDownloadedHashes(previously_downloaded_input.get(), &zts_client_maps.previously_downloaded_);
        }

        const std::string output_file(argv[3]);
        zts_client_params.format_handler_ = FormatHandler::Factory(output_format, output_file, &zts_client_maps, &zts_client_params);
        unsigned total_record_count(0), total_previously_downloaded_count(0);

        std::unique_ptr<File> progress_file;
        if (not progress_filename.empty())
            progress_file = FileUtil::OpenOutputFileOrDie(progress_filename);

        zts_client_params.format_handler_->prepareProcessing();
        StartHarvesting(ignore_robots_dot_txt, proxy_host_and_port, simple_crawler_config_path,
                        zts_client_params, zts_client_maps, progress_file,
                        &total_record_count, &total_previously_downloaded_count);
        zts_client_params.format_handler_->finishProcessing();

        INFO("Harvested a total of " + StringUtil::ToString(total_record_count) + " records of which "
             + StringUtil::ToString(total_previously_downloaded_count) + " were already previously downloaded.");

        std::unique_ptr<File> previously_downloaded_output(
            FileUtil::OpenOutputFileOrDie(map_directory_path + "previously_downloaded.hashes"));
        StorePreviouslyDownloadedHashes(previously_downloaded_output.get(), zts_client_maps.previously_downloaded_);
    } catch (const std::exception &x) {
        ERROR("caught exception: " + std::string(x.what()));
    }
}


} // namespace zts_client


int main(int argc, char *argv[]) {
    zts_client::Main(argc, argv);
}
