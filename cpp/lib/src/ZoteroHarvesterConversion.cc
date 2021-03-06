/** \brief Classes related to the Zotero Harvester's JSON-to-MARC conversion API
 *  \author Madeeswaran Kannan
 *
 *  \copyright 2019-2020 Universitätsbibliothek Tübingen.  All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU Affero General Public License as
 *  published by the Free Software Foundation, either version 3 of the
 *  License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU Affero General Public License for more details.
 *
 *  You should have received a copy of the GNU Affero General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "BSZUtil.h"
#include "Downloader.h"
#include "FileUtil.h"
#include "HtmlUtil.h"
#include "LobidUtil.h"
#include "NGram.h"
#include "StlHelpers.h"
#include "StringUtil.h"
#include "TimeUtil.h"
#include "TranslationUtil.h"
#include "UBTools.h"
#include "UrlUtil.h"
#include "ZoteroHarvesterConversion.h"
#include "ZoteroHarvesterZederInterop.h"
#include "util.h"


namespace ZoteroHarvester {


namespace Conversion {


std::string MetadataRecord::toString() const {
    std::string out("MetadataRecord {\n");

    out += "\turl: " + url_ + ",\n";
    out += "\titem_type: " + item_type_ + ",\n";
    out += "\ttitle: " + title_ + ",\n";
    if (not short_title_.empty())
        out += "\tshort_title: " + short_title_ + ",\n";
    if (not abstract_note_.empty())
        out += "\tabstract_note: " + abstract_note_ + ",\n";
    if (not publication_title_.empty())
        out += "\tpublication_title: " + publication_title_ + ",\n";
    if (not volume_.empty())
        out += "\tvolume: " + volume_ + ",\n";
    if (not issue_.empty())
        out += "\tissue: " + issue_ + ",\n";
    if (not pages_.empty())
        out += "\tpages: " + pages_ + ",\n";
    if (not date_.empty())
        out += "\tdate: " + date_ + ",\n";
    if (not doi_.empty())
        out += "\tdoi: " + doi_ + ",\n";
    if (not language_.empty())
        out += "\tlanguage: " + language_ + ",\n";
    if (not issn_.empty())
        out += "\tissn: " + issn_ + ",\n";
    if (not superior_ppn_.empty())
        out += "\tsuperior_ppn: " + superior_ppn_ + ",\n";
    out += "\tsuperior_type: " + std::to_string(static_cast<int>(superior_type_)) + ",\n";
    out += "\tssg: " + std::to_string(static_cast<int>(ssg_)) + ",\n";

    if (not creators_.empty()) {
        std::string creators("creators: [\n");
        for (const auto &creator : creators_) {
            creators += "\t\t{\n";
            creators += "\t\t\tfirst_name: " + creator.first_name_ + ",\n";
            creators += "\t\t\tlast_name: " + creator.last_name_ + ",\n";
            creators += "\t\t\ttype: " + creator.type_ + ",\n";
            if (not creator.affix_.empty())
                creators += "\t\t\taffix: " + creator.affix_ + ",\n";
            if (not creator.title_.empty())
                creators += "\t\t\ttitle: " + creator.title_ + ",\n";
            if (not creator.ppn_.empty())
                creators += "\t\t\tppn: " + creator.ppn_ + ",\n";
            if (not creator.gnd_number_.empty())
                creators += "\t\t\tgnd_number: " + creator.gnd_number_ + ",\n";
            creators += "\t\t},\n";
        }
        creators += "\t]";
        out += "\t" + creators + ",\n";
    }

    if (not keywords_.empty()) {
        std::string keywords("keywords: [ ");
        for (const auto &keyword : keywords_)
            keywords += keyword + ", ";
        TextUtil::UnicodeTruncate(&keywords, keywords.size() - 2);
        keywords += " ]";
        out += "\t" + keywords + ",\n";
    }

    if (not custom_metadata_.empty()) {
        std::string custom_metadata("custom_metadata: [\n");
        for (const auto &metadata : custom_metadata_)
            custom_metadata += "\t\t{ " + metadata.first + ", " + metadata.second + " },\n";
        custom_metadata += "\t]";
        out += "\t" + custom_metadata + ",\n";
    }

    out += "}";
    return out;
}


MetadataRecord::SSGType MetadataRecord::GetSSGTypeFromString(const std::string &ssg_string) {
    const std::map<std::string, SSGType> ZEDER_STRINGS {
        { "FG_0",   SSGType::FG_0 },
        { "FG_1",   SSGType::FG_1 },
        { "FG_0/1", SSGType::FG_01 },
        { "FG_2,1", SSGType::FG_21 },
    };

    if (ZEDER_STRINGS.find(ssg_string) != ZEDER_STRINGS.end())
        return ZEDER_STRINGS.find(ssg_string)->second;

    return SSGType::INVALID;
}


void SuppressJsonMetadata(const std::string &node_name, const std::shared_ptr<JSON::JSONNode> &node,
                          const Util::HarvestableItem &download_item)
{
    const auto suppression_regex(download_item.journal_.zotero_metadata_params_.fields_to_suppress_.find(node_name));
    if (suppression_regex != download_item.journal_.zotero_metadata_params_.fields_to_suppress_.end()) {
        if (node->getType() != JSON::JSONNode::STRING_NODE)
            LOG_ERROR("metadata suppression filter has invalid node type '" + node_name + "'");

        const auto string_node(JSON::JSONNode::CastToStringNodeOrDie(node_name, node));
        if (suppression_regex->second->match(string_node->getValue())) {
            LOG_DEBUG("suppression regex '" + suppression_regex->second->getPattern() +
                      "' matched metadata field '" + node_name + "' value '" + string_node->getValue() + "'");
            string_node->setValue("");
        }
    }
}


void OverrideJsonMetadata(const std::string &node_name, const std::shared_ptr<JSON::JSONNode> &node,
                          const Util::HarvestableItem &download_item)
{
    const std::string ORIGINAL_VALUE_SPECIFIER("%org%");
    const auto override_pattern(download_item.journal_.zotero_metadata_params_.fields_to_override_.find(node_name));

    if (override_pattern != download_item.journal_.zotero_metadata_params_.fields_to_override_.end()) {
        if (node->getType() != JSON::JSONNode::STRING_NODE)
            LOG_ERROR("metadata override has invalid node type '" + node_name + "'");

        const auto string_node(JSON::JSONNode::CastToStringNodeOrDie(node_name, node));
        const auto string_value(string_node->getValue());
        const auto override_string(StringUtil::ReplaceString(ORIGINAL_VALUE_SPECIFIER, string_value,override_pattern->second));

        LOG_DEBUG("metadata field '" + node_name + "' value changed from '" + string_value + "' to '" + override_string + "'");
        string_node->setValue(override_string);
    }
}


void PostprocessTranslationServerResponse(const Util::HarvestableItem &download_item,
                                          std::shared_ptr<JSON::ArrayNode> * const response_json_array)
{
    // 'response_json_array' is a JSON array of metadata objects pertaining to individual URLs

    // firstly, we need to process item notes. they are encoded as separate objects
    // so, we'll need to iterate through the entires and append individual notes to their parents
    std::shared_ptr<JSON::ArrayNode> augmented_array(new JSON::ArrayNode());
    JSON::ObjectNode *last_entry(nullptr);

    for (auto entry : **response_json_array) {
        const auto json_object(JSON::JSONNode::CastToObjectNodeOrDie("entry", entry));
        const auto item_type(json_object->getStringValue("itemType"));

        if (item_type == "note") {
            if (last_entry == nullptr)
                LOG_ERROR("unexpected note object in translation server response!");

            const std::shared_ptr<JSON::ObjectNode> new_note(new JSON::ObjectNode());
            new_note->insert("note", std::shared_ptr<JSON::JSONNode>(new JSON::StringNode(json_object->getStringValue("note"))));
            last_entry->getArrayNode("notes")->push_back(new_note);
            continue;
        }

        // add the main entry to our array
        auto main_entry_copy(JSON::JSONNode::CastToObjectNodeOrDie("entry", json_object->clone()));
        main_entry_copy->insert("notes", std::shared_ptr<JSON::JSONNode>(new JSON::ArrayNode()));
        augmented_array->push_back(main_entry_copy);
        last_entry = main_entry_copy.get();
    }

    // swap the augmented array with the old one
    *response_json_array = augmented_array;

    // next, we modify the metadata objects to suppress and/or override individual fields
    for (auto entry : **response_json_array) {
        const auto json_object(JSON::JSONNode::CastToObjectNodeOrDie("entry", entry));
        JSON::VisitLeafNodes("root", json_object, SuppressJsonMetadata, std::ref(download_item));
        JSON::VisitLeafNodes("root", json_object, OverrideJsonMetadata, std::ref(download_item));
    }
}


bool ZoteroItemMatchesExclusionFilters(const Util::HarvestableItem &download_item,
                                       const std::shared_ptr<JSON::ObjectNode> &zotero_item)
{
    if (download_item.journal_.zotero_metadata_params_.exclusion_filters_.empty())
        return false;

    bool found_match(false);
    std::string exclusion_string;
    auto metadata_exclusion_predicate = [&found_match, &exclusion_string]
                                        (const std::string &node_name, const std::shared_ptr<JSON::JSONNode> &node,
                                         const Config::JournalParams &journal_params) -> void
    {
        const auto filter_regex(journal_params.zotero_metadata_params_.exclusion_filters_.find(node_name));
        if (filter_regex != journal_params.zotero_metadata_params_.exclusion_filters_.end()) {
            if (node->getType() != JSON::JSONNode::STRING_NODE)
                LOG_ERROR("metadata exclusion filter has invalid node type '" + node_name + "'");

            const auto string_node(JSON::JSONNode::CastToStringNodeOrDie(node_name, node));
            if (filter_regex->second->match(string_node->getValue())) {
                found_match = true;
                exclusion_string = node_name + "/" + filter_regex->second->getPattern() + "/";
            }
        }
    };

    JSON::VisitLeafNodes("root", zotero_item, metadata_exclusion_predicate, std::ref(download_item.journal_));
    if (found_match)
        LOG_INFO("zotero metadata for '" + download_item.url_.toString() + " matched exclusion filter (" + exclusion_string + ")");

    return found_match;
}


static inline std::string GetStrippedHTMLStringFromJSON(const std::shared_ptr<JSON::ObjectNode> &json_object,
                                                        const std::string &field_name)
{ return HtmlUtil::StripHtmlTags(json_object->getOptionalStringValue(field_name)); }


void ConvertZoteroItemToMetadataRecord(const std::shared_ptr<JSON::ObjectNode> &zotero_item,
                                       MetadataRecord * const metadata_record)
{
    metadata_record->item_type_ = GetStrippedHTMLStringFromJSON(zotero_item, "itemType");
    metadata_record->title_ = GetStrippedHTMLStringFromJSON(zotero_item, "title");
    metadata_record->short_title_ = GetStrippedHTMLStringFromJSON(zotero_item, "shortTitle");
    metadata_record->abstract_note_ = GetStrippedHTMLStringFromJSON(zotero_item, "abstractNote");
    metadata_record->publication_title_ = GetStrippedHTMLStringFromJSON(zotero_item, "publicationTitle");
    if (metadata_record->publication_title_.empty())
        metadata_record->publication_title_ = GetStrippedHTMLStringFromJSON(zotero_item, "websiteTitle");
    metadata_record->volume_ = GetStrippedHTMLStringFromJSON(zotero_item, "volume");
    metadata_record->issue_ = GetStrippedHTMLStringFromJSON(zotero_item, "issue");
    metadata_record->pages_ = GetStrippedHTMLStringFromJSON(zotero_item, "pages");
    metadata_record->date_ = GetStrippedHTMLStringFromJSON(zotero_item, "date");
    metadata_record->doi_ = GetStrippedHTMLStringFromJSON(zotero_item, "DOI");
    metadata_record->language_ = GetStrippedHTMLStringFromJSON(zotero_item, "language");
    metadata_record->url_ = GetStrippedHTMLStringFromJSON(zotero_item, "url");
    metadata_record->issn_ = GetStrippedHTMLStringFromJSON(zotero_item, "ISSN");

    const auto creators_array(zotero_item->getOptionalArrayNode("creators"));
    if (creators_array) {
        for (const auto &entry :*creators_array) {
            const auto creator_object(JSON::JSONNode::CastToObjectNodeOrDie("array_element", entry));
            metadata_record->creators_.emplace_back(GetStrippedHTMLStringFromJSON(creator_object, "firstName"),
                                                    GetStrippedHTMLStringFromJSON(creator_object, "lastName"),
                                                    GetStrippedHTMLStringFromJSON(creator_object, "creatorType"));
        }
    }

    const auto tags_array(zotero_item->getOptionalArrayNode("tags"));
    if (tags_array) {
        for (const auto &entry :*tags_array) {
            const auto tag_object(JSON::JSONNode::CastToObjectNodeOrDie("array_element", entry));
            const auto tag(GetStrippedHTMLStringFromJSON(tag_object, "tag"));
            if (not tag.empty())
                metadata_record->keywords_.emplace_back(tag);
        }
    }

    const auto notes_array(zotero_item->getOptionalArrayNode("notes"));
    if (notes_array) {
        for (const auto &entry :*notes_array) {
            const auto note_object(JSON::JSONNode::CastToObjectNodeOrDie("array_element", entry));
            const auto note(note_object->getOptionalStringValue("note"));
            if (not note.empty()) {
                const auto first_colon_pos(note.find(':'));
                if (unlikely(first_colon_pos == std::string::npos)) {
                    LOG_WARNING("additional metadata in \"notes\" is missing a colon! data: '" + note + "'");
                    continue;   // could be a valid note added by the translator
                }

                metadata_record->custom_metadata_[note.substr(0, first_colon_pos)] = note.substr(first_colon_pos + 1);
            }
        }
    }
}


void SplitIntoFirstAndLastAuthorNames(const std::string &author, std::string * const first_name, std::string * const last_name) {
    const auto normalised_author(TextUtil::CollapseAndTrimWhitespace(author));
    const auto name_separator(normalised_author.rfind(' '));
    if (name_separator != std::string::npos) {
        *first_name = normalised_author.substr(0, name_separator);
        *last_name = normalised_author.substr(name_separator + 1);
    } else {
        *first_name = normalised_author;
        last_name->clear();
    }
}


bool FilterEmptyAndCommentLines(std::string str) {
    StringUtil::TrimWhite(&str);
    return not str.empty() and str.front() != '#';
}


const std::string AUTHOR_NAME_BLACKLIST(UBTools::GetTuelibPath() + "zotero-enhancement-maps/author_name_blacklist.txt");


ThreadSafeRegexMatcher InitializeBlacklistedAuthorTokenMatcher() {
    std::unordered_set<std::string> blacklisted_tokens, filtered_blacklisted_tokens;
    auto string_data(FileUtil::ReadStringOrDie(AUTHOR_NAME_BLACKLIST));
    StringUtil::Split(string_data, '\n', &blacklisted_tokens, /* suppress_empty_components = */true);

    StlHelpers::Functional::Filter(blacklisted_tokens.begin(), blacklisted_tokens.end(), filtered_blacklisted_tokens,
                                   FilterEmptyAndCommentLines);

    std::string match_pattern("\\b(");
    bool is_first_token(true);
    for (const auto blacklisted_token : filtered_blacklisted_tokens) {
        if (not is_first_token)
            match_pattern += "|";
        else
            is_first_token = false;
        match_pattern += RegexMatcher::Escape(blacklisted_token);
    }
    match_pattern += ")\\b";

   return ThreadSafeRegexMatcher(match_pattern, ThreadSafeRegexMatcher::ENABLE_UTF8 | ThreadSafeRegexMatcher::ENABLE_UCP);
}


const ThreadSafeRegexMatcher BLACKLISTED_AUTHOR_TOKEN_MATCHER(InitializeBlacklistedAuthorTokenMatcher());


void StripBlacklistedTokensFromAuthorName(std::string * const first_name, std::string * const last_name) {
    std::string first_name_buffer(BLACKLISTED_AUTHOR_TOKEN_MATCHER.replaceAll(*first_name, "")),
                last_name_buffer(BLACKLISTED_AUTHOR_TOKEN_MATCHER.replaceAll(*last_name, ""));

    StringUtil::TrimWhite(&first_name_buffer);
    StringUtil::TrimWhite(&last_name_buffer);

    *first_name = first_name_buffer;
    *last_name = last_name_buffer;
}


static const std::set<std::string> VALID_TITLES {
    "jr", "sr", "sj", "s.j", "s.j.", "fr", "hr", "dr", "prof", "em"
};


bool IsAuthorNameTokenTitle(std::string token) {
    bool final_period(token.back() == '.');
    if (final_period)
        token.erase(token.size() - 1);

    TextUtil::UTF8ToLower(&token);
    return VALID_TITLES.find(token) != VALID_TITLES.end();
}


static const std::set<std::string> VALID_AFFIXES {
    "i", "ii", "iii", "iv", "v"
};


bool IsAuthorNameTokenAffix(std::string token) {
    TextUtil::UTF8ToLower(&token);
    return VALID_AFFIXES.find(token) != VALID_AFFIXES.end();
}


void PostProcessAuthorName(std::string * const first_name, std::string * const last_name, std::string * const title,
                           std::string * const affix)
{
    std::string first_name_buffer, title_buffer;
    std::vector<std::string> tokens;

    StringUtil::Split(*first_name, ' ', &tokens, /* suppress_empty_components = */true);
    for (const auto &token : tokens) {
        if (IsAuthorNameTokenTitle(token))
            title_buffer += token + " ";
        else
            first_name_buffer += token + " ";
    }

    std::string last_name_buffer, affix_buffer;
    StringUtil::Split(*last_name, ' ', &tokens, /* suppress_empty_components = */true);
    for (const auto &token : tokens) {
        if (IsAuthorNameTokenTitle(token))
            title_buffer += token + " ";
        else if (IsAuthorNameTokenAffix(token))
            affix_buffer += token + " ";
        else
            last_name_buffer += token + " ";
    }

    TextUtil::CollapseAndTrimWhitespace(&first_name_buffer);
    TextUtil::CollapseAndTrimWhitespace(&last_name_buffer);
    TextUtil::CollapseAndTrimWhitespace(&title_buffer);
    TextUtil::CollapseAndTrimWhitespace(&affix_buffer);

    StripBlacklistedTokensFromAuthorName(&first_name_buffer, &last_name_buffer);

    *title = title_buffer;
    *affix = affix_buffer;
    // try to reparse the name if either part of the name is empty
    if (first_name_buffer.empty())
        SplitIntoFirstAndLastAuthorNames(last_name_buffer, first_name, last_name);
    else if (last_name_buffer.empty())
        SplitIntoFirstAndLastAuthorNames(first_name_buffer, first_name, last_name);
    else if (not first_name_buffer.empty() and not last_name_buffer.empty()) {
        *first_name = first_name_buffer;
        *last_name = last_name_buffer;
    }

    LOG_DEBUG("post-processed author first name = '" + *first_name + "', last name = '" + *last_name +
              "', title = '" + *title + "', affix = '" + *affix + "'");
}


void IdentifyMissingLanguage(MetadataRecord * const metadata_record, const Config::JournalParams &journal_params) {
    const unsigned minimum_token_count(5);

    if (journal_params.language_params_.expected_languages_.empty())
        return;

    if (journal_params.language_params_.expected_languages_.size() == 1) {
        metadata_record->language_ = *journal_params.language_params_.expected_languages_.begin();
        LOG_DEBUG("language set to default language '" + metadata_record->language_ + "'");
        return;
    }

    // attempt to automatically detect the language
    std::vector<std::string> top_languages;
    std::string record_text;

    if (journal_params.language_params_.source_text_fields_.empty()
        or journal_params.language_params_.source_text_fields_ == "title")
    {
        record_text = metadata_record->title_;
        // use naive tokenization to count tokens in the title
        // additionally use abstract if we have too few tokens in the title
        if (StringUtil::CharCount(record_text, ' ') < minimum_token_count) {
            record_text += " " + metadata_record->abstract_note_;
            LOG_DEBUG("too few tokens in title. applying heuristic on the abstract as well");
        }
    } else if (journal_params.language_params_.source_text_fields_ == "abstract")
        record_text = metadata_record->abstract_note_;
    else if (journal_params.language_params_.source_text_fields_ == "title+abstract")
        record_text = metadata_record->title_ + " " + metadata_record->abstract_note_;
    else
        LOG_ERROR("unknown text field '" + journal_params.language_params_.source_text_fields_ + "' for language detection");

    NGram::ClassifyLanguage(record_text, &top_languages, journal_params.language_params_.expected_languages_,
                            NGram::DEFAULT_NGRAM_NUMBER_THRESHOLD);
    metadata_record->language_ = top_languages.front();
    LOG_INFO("automatically detected language to be '" + metadata_record->language_ + "'");
}


const ThreadSafeRegexMatcher PAGE_RANGE_MATCHER("^(.+)-(.+)$");
const ThreadSafeRegexMatcher PAGE_RANGE_DIGIT_MATCHER("^(\\d+)-(\\d+)$");
const ThreadSafeRegexMatcher PAGE_ROMAN_NUMERAL_MATCHER("^M{0,4}(CM|CD|D?C{0,3})(XC|XL|L?X{0,3})(IX|IV|V?I{0,3})$");


void AugmentMetadataRecord(MetadataRecord * const metadata_record, const Config::JournalParams &journal_params,
                           const Config::GroupParams &group_params)
{
    // normalise date
    if (not metadata_record->date_.empty()) {
        struct tm tm(TimeUtil::StringToStructTm(metadata_record->date_, journal_params.strptime_format_string_));
        const std::string date_normalized(std::to_string(tm.tm_year + 1900) + "-"
                                          + StringUtil::ToString(tm.tm_mon + 1, 10, 2, '0') + "-"
                                          + StringUtil::ToString(tm.tm_mday, 10, 2, '0'));
        metadata_record->date_ = date_normalized;
    }

    // normalise issue/volume
    StringUtil::LeftTrim(&metadata_record->issue_, '0');
    StringUtil::LeftTrim(&metadata_record->volume_, '0');

    // normalise pages
    const auto pages(metadata_record->pages_);
    // force uppercase for roman numeral detection
    auto page_match(PAGE_RANGE_MATCHER.match(StringUtil::ToUpper(pages)));
    if (page_match) {
        std::string converted_pages;
        if (PAGE_ROMAN_NUMERAL_MATCHER.match(page_match[1]))
            converted_pages += std::to_string(StringUtil::RomanNumeralToDecimal(page_match[1]));
        else
            converted_pages += page_match[1];

        converted_pages += "-";

        if (PAGE_ROMAN_NUMERAL_MATCHER.match(page_match[2]))
            converted_pages += std::to_string(StringUtil::RomanNumeralToDecimal(page_match[2]));
        else
            converted_pages += page_match[2];

        if (converted_pages != pages) {
            LOG_DEBUG("converted roman numeral page range '" + pages + "' to decimal page range '"
                      + converted_pages + "'");
            metadata_record->pages_ = converted_pages;
        }
    }

    page_match = PAGE_RANGE_DIGIT_MATCHER.match(metadata_record->pages_);
    if (page_match and page_match[1] == page_match[2])
        metadata_record->pages_ = page_match[1];

    // override publication title
    metadata_record->publication_title_ = journal_params.name_;

    // override ISSN (online > print > zotero) and select superior PPN (online > print)
    const auto &issn(journal_params.issn_);
    const auto &ppn(journal_params.ppn_);
    if (not issn.online_.empty()) {
        if (ppn.online_.empty())
            throw std::runtime_error("cannot use online ISSN \"" + issn.online_ + "\" because no online PPN is given!");
        metadata_record->issn_ = issn.online_;
        metadata_record->superior_ppn_ = ppn.online_;
        metadata_record->superior_type_ = MetadataRecord::SuperiorType::ONLINE;

        LOG_DEBUG("use online ISSN \"" + issn.online_ + "\" with online PPN \"" + ppn.online_ + "\"");
    } else if (not issn.print_.empty()) {
        if (ppn.print_.empty())
            throw std::runtime_error("cannot use print ISSN \"" + issn.print_ + "\" because no print PPN is given!");
        metadata_record->issn_ = issn.print_;
        metadata_record->superior_ppn_ = ppn.print_;
        metadata_record->superior_type_ = MetadataRecord::SuperiorType::PRINT;

        LOG_DEBUG("use print ISSN \"" + issn.print_ + "\" with print PPN \"" + ppn.print_ + "\"");
    } else {
        throw std::runtime_error("ISSN and PPN could not be chosen! ISSN online: \"" + issn.online_ + "\""
                                 + ", ISSN print: \"" + issn.print_ + "\", ISSN zotero: \"" + metadata_record->issn_ + "\""
                                 + ", PPN online: \"" + ppn.online_ + "\", PPN print: \"" + ppn.print_ + "\"");
    }

    // fetch creator GNDs and postprocess names
    for (auto &creator : metadata_record->creators_) {
        PostProcessAuthorName(&creator.first_name_, &creator.last_name_, &creator.title_, &creator.affix_);

        if (not creator.last_name_.empty()) {
            std::string combined_name(creator.last_name_);
            if (not creator.first_name_.empty())
                combined_name += ", " + creator.first_name_;

            creator.gnd_number_ = HtmlUtil::StripHtmlTags(BSZUtil::GetAuthorGNDNumber(combined_name, group_params.author_swb_lookup_url_));
            if (not creator.gnd_number_.empty())
                LOG_DEBUG("added GND number " + creator.gnd_number_ + " for author " + combined_name + " (SWB lookup)");
            else {
                creator.gnd_number_ = HtmlUtil::StripHtmlTags(LobidUtil::GetAuthorGNDNumber(
                                                              combined_name, group_params.author_lobid_lookup_query_params_));
                if (not creator.gnd_number_.empty())
                    LOG_DEBUG("added GND number " + creator.gnd_number_ + " for author " + combined_name + "(Lobid lookup)");
            }
        }
    }

    // autodetect or map language
    bool autodetect_language(false);
    const std::string autodetect_message("forcing automatic language detection, reason: ");
    if (journal_params.language_params_.force_automatic_language_detection_) {
        LOG_DEBUG(autodetect_message + "conf setting");
        autodetect_language = true;
    } else if (metadata_record->language_.empty()) {
        LOG_DEBUG(autodetect_message + "empty language");
        autodetect_language = true;
    } else if (not TranslationUtil::IsValidInternational2LetterCode(metadata_record->language_)
               and not TranslationUtil::IsValidGerman3Or4LetterCode(metadata_record->language_))
    {
        LOG_DEBUG(autodetect_message + "invalid language \"" + metadata_record->language_ + "\"");
        autodetect_language = true;
    }

    if (autodetect_language)
        IdentifyMissingLanguage(metadata_record, journal_params);
    else {
        if (TranslationUtil::IsValidInternational2LetterCode(metadata_record->language_))
            metadata_record->language_ = TranslationUtil::MapInternational2LetterCodeToGerman3Or4LetterCode(metadata_record->language_);
        if (TranslationUtil::IsValidGerman3Or4LetterCode(metadata_record->language_))
            metadata_record->language_ = TranslationUtil::MapGermanLanguageCodesToFake3LetterEnglishLanguagesCodes(metadata_record->language_);
    }

    // fill-in license and SSG values
    if (journal_params.license_ == "LF")
        metadata_record->license_ = journal_params.license_;
    metadata_record->ssg_ = MetadataRecord::GetSSGTypeFromString(journal_params.ssgn_);

    // tag reviews
    const auto &review_matcher(journal_params.review_regex_);
    if (review_matcher != nullptr) {
        if (review_matcher->match(metadata_record->title_)) {
            LOG_DEBUG("title matched review pattern");
            metadata_record->item_type_ = "review";
        } else if (review_matcher->match(metadata_record->short_title_)) {
            LOG_DEBUG("short title matched review pattern");
            metadata_record->item_type_ = "review";
        } else {
            for (const auto &keyword : metadata_record->keywords_) {
                if (review_matcher->match(keyword)) {
                    LOG_DEBUG("keyword matched review pattern");
                    metadata_record->item_type_ = "review";
                }
            }
        }
    }
}


const ThreadSafeRegexMatcher CUSTOM_MARC_FIELD_PLACEHOLDER_MATCHER("%(.+)%");


void InsertCustomMarcFields(const MetadataRecord &metadata_record, const Config::JournalParams &journal_params,
                            MARC::Record * const marc_record)
{
    for (auto custom_field : journal_params.marc_metadata_params_.fields_to_add_) {
        const auto placeholder_match(CUSTOM_MARC_FIELD_PLACEHOLDER_MATCHER.match(custom_field));
        const auto custom_field_copy(custom_field);

        if (placeholder_match) {
            std::string first_missing_placeholder;
            for (unsigned i(1); i < placeholder_match.size(); ++i) {
                const auto placeholder(placeholder_match[i]);
                const auto substitution(metadata_record.custom_metadata_.find(placeholder));
                if (substitution == metadata_record.custom_metadata_.end()) {
                    first_missing_placeholder = placeholder;
                    break;
                }

                custom_field = StringUtil::ReplaceString(placeholder_match[0], substitution->second, custom_field);
            }

            if (not first_missing_placeholder.empty()) {
                LOG_DEBUG("custom field '" + custom_field_copy + "' has missing placeholder(s) '"
                          + first_missing_placeholder + "'");
                continue;
            }
        }

        if (unlikely(custom_field.length() < MARC::Record::TAG_LENGTH))
            LOG_ERROR("custom field '" + custom_field_copy + "' is too short");

        const size_t MIN_CONTROl_FIELD_LENGTH(1);
        const size_t MIN_DATA_FIELD_LENGTH(2 /*indicators*/ + 1 /*subfield separator*/ + 1 /*subfield code*/ + 1 /*subfield value*/);

        const MARC::Tag tag(custom_field.substr(0, MARC::Record::TAG_LENGTH));
        if ((tag.isTagOfControlField() and custom_field.length() < MARC::Record::TAG_LENGTH + MIN_CONTROl_FIELD_LENGTH)
            or (not tag.isTagOfControlField() and custom_field.length() < MARC::Record::TAG_LENGTH + MIN_DATA_FIELD_LENGTH))
        {
            LOG_ERROR("custom field '" + custom_field_copy + "' is too short");
        }

        marc_record->insertField(tag, custom_field.substr(MARC::Record::TAG_LENGTH));
        LOG_DEBUG("inserted custom field '" + custom_field + "'");
    }
}


bool GetMatchedMARCFields(MARC::Record * marc_record, const std::string &field_or_field_and_subfield_code,
                          const ThreadSafeRegexMatcher &matcher, std::vector<MARC::Record::iterator> * const matched_fields)
{
    if (unlikely(field_or_field_and_subfield_code.length() < MARC::Record::TAG_LENGTH
                 or field_or_field_and_subfield_code.length() > MARC::Record::TAG_LENGTH + 1))
    {
        LOG_ERROR("\"field_or_field_and_subfield_code\" must be a tag or a tag plus a subfield code!");
    }

    const char subfield_code((field_or_field_and_subfield_code.length() == MARC::Record::TAG_LENGTH + 1) ?
                             field_or_field_and_subfield_code[MARC::Record::TAG_LENGTH] : '\0');

    matched_fields->clear();
    const MARC::Record::Range field_range(marc_record->getTagRange(field_or_field_and_subfield_code.substr(0,
                                          MARC::Record::TAG_LENGTH)));

    for (auto field_itr(field_range.begin()); field_itr != field_range.end(); ++field_itr) {
        const auto &field(*field_itr);
        if (subfield_code != '\0' and field.hasSubfield(subfield_code)) {
            if (matcher.match(field.getFirstSubfieldWithCode(subfield_code)))
                matched_fields->emplace_back(field_itr);
        } else if (matcher.match(field.getContents()))
            matched_fields->emplace_back(field_itr);
    }

    return not matched_fields->empty();
}


// Zotero values see https://raw.githubusercontent.com/zotero/zotero/master/test/tests/data/allTypesAndFields.js
// MARC21 values see https://www.loc.gov/marc/relators/relaterm.html
const std::map<std::string, std::string> CREATOR_TYPES_TO_MARC21_MAP {
    { "artist",             "art" },
    { "attorneyAgent",      "csl" },
    { "author",             "aut" },
    { "bookAuthor",         "edc" },
    { "cartographer",       "ctg" },
    { "castMember",         "act" },
    { "commenter",          "cwt" },
    { "composer",           "cmp" },
    { "contributor",        "ctb" },
    { "cosponsor",          "spn" },
    { "director",           "drt" },
    { "editor",             "edt" },
    { "guest",              "pan" },
    { "interviewee",        "ive" },
    { "inventor",           "inv" },
    { "performer",          "prf" },
    { "podcaster",          "brd" },
    { "presenter",          "pre" },
    { "producer",           "pro" },
    { "programmer",         "prg" },
    { "recipient",          "rcp" },
    { "reviewedAuthor",     "aut" },
    { "scriptwriter",       "aus" },
    { "seriesEditor",       "edt" },
    { "sponsor",            "spn" },
    { "translator",         "trl" },
    { "wordsBy",            "wam" },
};


void GenerateMarcRecordFromMetadataRecord(const Util::HarvestableItem &download_item, const MetadataRecord &metadata_record,
                                          const Config::GroupParams &group_params, MARC::Record * const marc_record,
                                          std::string * const marc_record_hash)
{
    *marc_record = MARC::Record(MARC::Record::TypeOfRecord::LANGUAGE_MATERIAL,
                                MARC::Record::BibliographicLevel::SERIAL_COMPONENT_PART);

    // Control fields (001 depends on the hash of the record, so it's generated towards the end)
    marc_record->insertField("003", group_params.isil_);

    if (metadata_record.superior_type_ == MetadataRecord::SuperiorType::ONLINE)
        marc_record->insertField("007", "cr|||||");
    else
        marc_record->insertField("007", "tu");

    // Authors/Creators
    // Use reverse iterator to keep order, because "insertField" inserts at first possible position
    // The first creator is always saved in the "100" field, all following creators go into the 700 field
    unsigned num_creators_left(metadata_record.creators_.size());
    for (auto creator(metadata_record.creators_.rbegin()); creator != metadata_record.creators_.rend(); ++creator) {
        MARC::Subfields subfields;
        if (not creator->ppn_.empty())
            subfields.appendSubfield('0', "(DE-627)" + creator->ppn_);
        if (not creator->gnd_number_.empty())
            subfields.appendSubfield('0', "(DE-588)" + creator->gnd_number_);
        if (not creator->type_.empty()) {
            const auto creator_type_marc21(CREATOR_TYPES_TO_MARC21_MAP.find(creator->type_));
            if (creator_type_marc21 == CREATOR_TYPES_TO_MARC21_MAP.end())
                LOG_ERROR("zotero creator type '" + creator->type_ + "' could not be mapped to MARC21");

            subfields.appendSubfield('4', creator_type_marc21->second);
        }

        subfields.appendSubfield('a', StringUtil::Join(std::vector<std::string>({ creator->last_name_, creator->first_name_ }),
                                 ", "));

        if (not creator->affix_.empty())
            subfields.appendSubfield('b', creator->affix_ + ".");
        if (not creator->title_.empty())
            subfields.appendSubfield('c', creator->title_);
        subfields.appendSubfield('e', "VerfasserIn");

        if (num_creators_left == 1)
            marc_record->insertField("100", subfields, /* indicator 1 = */'1');
        else
            marc_record->insertField("700", subfields, /* indicator 1 = */'1');

        if (not creator->ppn_.empty() or not creator->gnd_number_.empty()) {
            const std::string _887_data("Autor in der Zoterovorlage [" + creator->last_name_ + ", "
                                        + creator->first_name_ + "] maschinell zugeordnet");
            marc_record->insertField("887", { { 'a', _887_data }, { '2', "ixzom" } });
        }

        --num_creators_left;
    }

    // RDA
    marc_record->insertField("040", { { 'a', "DE-627" }, { 'b', "ger" }, { 'c', "DE-627" }, { 'e', "rda" }, });

    // Title
    if (metadata_record.title_.empty())
        throw std::runtime_error("no title provided for download item from URL " + download_item.url_.toString());
    else
        marc_record->insertField("245", { { 'a', metadata_record.title_ } }, /* indicator 1 = */'0', /* indicator 2 = */'0');

    // Language
    if (not metadata_record.language_.empty())
        marc_record->insertField("041", { { 'a', metadata_record.language_ } });

    // Abstract Note
    if (not metadata_record.abstract_note_.empty())
        marc_record->insertField("520", { { 'a', metadata_record.abstract_note_ } });

    // Date & Year
    const auto &date(metadata_record.date_);
    const auto &item_type(metadata_record.item_type_);
    if (not date.empty() and item_type != "journalArticle" and item_type != "review")
        marc_record->insertField("362", { { 'a', date } });

    unsigned year_num(0);
    std::string year;
    if (TimeUtil::StringToYear(date, &year_num))
        year = std::to_string(year_num);
    else
        year = TimeUtil::GetCurrentYear();

    marc_record->insertField("264", { { 'c', year } });

    // URL
    if (not metadata_record.url_.empty()) {
        MARC::Subfields subfields({ { 'u', metadata_record.url_ } });
        if (not metadata_record.license_.empty())
            subfields.appendSubfield('z', metadata_record.license_);
        marc_record->insertField("856", subfields, /* indicator1 = */'4', /* indicator2 = */'0');
    }

    // DOI
    const auto &doi(metadata_record.doi_);
    if (not doi.empty()) {
        marc_record->insertField("024", { { 'a', doi }, { '2', "doi" } }, '7');
        const std::string doi_url("https://doi.org/" + doi);
        if (doi_url != metadata_record.url_) {
            MARC::Subfields subfields({ { 'u', doi_url } });
            if (not metadata_record.license_.empty())
                subfields.appendSubfield('z', metadata_record.license_);
            marc_record->insertField("856", subfields, /* indicator1 = */'4', /* indicator2 = */'0');
        }
    }

    // Review-specific modifications
    if (item_type == "review") {
        marc_record->insertField("655", { { 'a', "Rezension" }, { '0', "(DE-588)4049712-4" },
                                 { '0', "(DE-627)106186019" }, { '2', "gnd-content" } },
                                 /* indicator1 = */' ', /* indicator2 = */'7');
    }

    // Differentiating information about source (see BSZ Konkordanz MARC 936)
    MARC::Subfields _936_subfields;
    const auto &volume(metadata_record.volume_);
    const auto &issue(metadata_record.issue_);
    if (not volume.empty()) {
        _936_subfields.appendSubfield('d', volume);
        if (not issue.empty())
            _936_subfields.appendSubfield('e', issue);
    } else if (not issue.empty())
        _936_subfields.appendSubfield('d', issue);

    const std::string pages(metadata_record.pages_);
    if (not pages.empty())
        _936_subfields.appendSubfield('h', pages);

    _936_subfields.appendSubfield('j', year);
    if (not _936_subfields.empty())
        marc_record->insertField("936", _936_subfields, 'u', 'w');

    // Information about superior work (See BSZ Konkordanz MARC 773)
    MARC::Subfields _773_subfields;
    const std::string publication_title(metadata_record.publication_title_);
    if (not publication_title.empty()) {
        _773_subfields.appendSubfield('i', "In: ");
        _773_subfields.appendSubfield('t', publication_title);
    }
    if (not metadata_record.issn_.empty())
        _773_subfields.appendSubfield('x', metadata_record.issn_);
    if (not metadata_record.superior_ppn_.empty())
        _773_subfields.appendSubfield('w', "(DE-627)" + metadata_record.superior_ppn_);

    // 773g, example: "52 (2018), 1, Seite 1-40" => <volume>(<year>), <issue>, S. <pages>
    const bool _773_subfields_iaxw_present(not _773_subfields.empty());
    bool _773_subfield_g_present(false);
    std::string g_content;
    if (not volume.empty()) {
        g_content += volume + " (" + year + ")";
        if (not issue.empty())
            g_content += ", " + issue;

        if (not pages.empty())
            g_content += ", Seite " + pages;

        _773_subfields.appendSubfield('g', g_content);
        _773_subfield_g_present = true;
    }

    if (_773_subfields_iaxw_present and _773_subfield_g_present)
        marc_record->insertField("773", _773_subfields, '0', '8');
    else
        marc_record->insertField("773", _773_subfields);

    // Keywords
    for (const auto &keyword : metadata_record.keywords_)
        marc_record->insertField(MARC::GetIndexField(TextUtil::CollapseAndTrimWhitespace(keyword)));

    // SSG numbers
    if (metadata_record.ssg_ != MetadataRecord::SSGType::INVALID) {
        MARC::Subfields _084_subfields;
        switch(metadata_record.ssg_) {
        case MetadataRecord::SSGType::FG_0:
            _084_subfields.appendSubfield('a', "0");
            break;
        case MetadataRecord::SSGType::FG_1:
            _084_subfields.appendSubfield('a', "1");
            break;
        case MetadataRecord::SSGType::FG_01:
            _084_subfields.appendSubfield('a', "0");
            _084_subfields.appendSubfield('a', "1");
            break;
        case MetadataRecord::SSGType::FG_21:
            _084_subfields.appendSubfield('a', "2,1");
            break;
        default:
            break;
        }
        _084_subfields.appendSubfield('2', "ssgn");
        marc_record->insertField("084", _084_subfields);
    }

    // Zotero sigil
    // Similar to the 100/700 fields, we need to insert 935 fields in reverse
    // order to preserve the intended ordering
    marc_record->insertField("935", { { 'a', "zota" }, { '2', "LOK" } });

    // Abrufzeichen und ISIL
    const auto zeder_instance(ZederInterop::GetZederInstanceForGroup(group_params));
    switch (zeder_instance) {
    case Zeder::Flavour::IXTHEO:
        marc_record->insertField("935", { { 'a', "ixzs" }, { '2', "LOK" } });
        marc_record->insertField("935", { { 'a', "mteo" } });
        break;
    case Zeder::Flavour::KRIMDOK:
        marc_record->insertField("935", { { 'a', "mkri" } });
        break;
    }
    marc_record->insertField("852", { { 'a', group_params.isil_ } });

    // Book-keeping fields
    marc_record->insertField("URL", { { 'a', download_item.url_.toString() } });
    marc_record->insertField("ZID", { { 'a', std::to_string(download_item.journal_.zeder_id_) },
                                      { 'b', StringUtil::ASCIIToLower(Zeder::FLAVOUR_TO_STRING_MAP.at(zeder_instance)) } });
    marc_record->insertField("JOU", { { 'a', download_item.journal_.name_ } });

    // Add custom fields
    InsertCustomMarcFields(metadata_record, download_item.journal_, marc_record);

    // Remove fields
    std::vector<MARC::Record::iterator> matched_fields;
    for (const auto &filter : download_item.journal_.marc_metadata_params_.fields_to_remove_) {
        const auto &tag_and_subfield_code(filter.first);
        GetMatchedMARCFields(marc_record, filter.first, *filter.second.get(), &matched_fields);

        for (const auto &matched_field : matched_fields) {
            marc_record->erase(matched_field);
            LOG_DEBUG("erased field '" + tag_and_subfield_code + "' due to removal filter '" + filter.second->getPattern() + "'");
        }
    }

    // Has to be generated in the very end as it contains the hash of the record
    *marc_record_hash = CalculateMarcRecordHash(*marc_record);
    marc_record->insertField("001", group_params.name_ + "#" + TimeUtil::GetCurrentDateAndTime("%Y-%m-%d")
                             + "#" + *marc_record_hash);
}


bool MarcRecordMatchesExclusionFilters(const Util::HarvestableItem &download_item, MARC::Record * const marc_record) {
    bool found_match(false);
    std::string exclusion_string;

    std::vector<MARC::Record::iterator> matched_fields;
    for (const auto &filter : download_item.journal_.marc_metadata_params_.exclusion_filters_) {
        if (GetMatchedMARCFields(marc_record, filter.first, *filter.second.get(), &matched_fields)) {
            exclusion_string = filter.first + "/" + filter.second->getPattern() + "/";
            found_match = true;
            break;
        }
    }

    if (found_match)
        LOG_INFO("MARC field for '" + download_item.url_.toString() + " matched exclusion filter (" + exclusion_string + ")");
    return found_match;
}


const std::set<MARC::Tag> EXCLUDED_FIELDS_DURING_CHECKSUM_CALC {
    "001", "URL", "ZID", "JOU",
};


std::string CalculateMarcRecordHash(const MARC::Record &marc_record) {
    return StringUtil::ToHexString(MARC::CalcChecksum(marc_record, EXCLUDED_FIELDS_DURING_CHECKSUM_CALC));
}


const std::vector<std::string> VALID_ITEM_TYPES_FOR_ONLINE_FIRST {
    "journalArticle", "magazineArticle", "review"
};


bool ExcludeOnlineFirstRecord(const MetadataRecord &metadata_record, const ConversionParams &parameters) {
    if (std::find(VALID_ITEM_TYPES_FOR_ONLINE_FIRST.begin(),
                  VALID_ITEM_TYPES_FOR_ONLINE_FIRST.end(),
                  metadata_record.item_type_) == VALID_ITEM_TYPES_FOR_ONLINE_FIRST.end())
    {
        return false;
    }

    if (metadata_record.issue_.empty() and metadata_record.volume_.empty()) {
        if (parameters.skip_online_first_articles_unconditonally_) {
            LOG_DEBUG("Skipping: online-first article unconditionally");
            return true;
        } else if (metadata_record.doi_.empty()) {
            LOG_DEBUG("Skipping: online-first article without a DOI");
            return true;
        }
    }

    return false;
}


bool ExcludeEarlyViewRecord(const MetadataRecord &metadata_record, const ConversionParams &/*unused*/) {
    if (std::find(VALID_ITEM_TYPES_FOR_ONLINE_FIRST.begin(),
                  VALID_ITEM_TYPES_FOR_ONLINE_FIRST.end(),
                  metadata_record.item_type_) == VALID_ITEM_TYPES_FOR_ONLINE_FIRST.end())
    {
        return false;
    }

    if (metadata_record.issue_ == "n/a" or metadata_record.volume_ == "n/a") {
        LOG_DEBUG("Skipping: early-view article");
        return true;
    }

    return false;
}


void ConversionTasklet::run(const ConversionParams &parameters, ConversionResult * const result) {
    LOG_INFO("Converting item " + parameters.download_item_.toString());

    std::shared_ptr<JSON::JSONNode> tree_root;
    JSON::Parser json_parser(parameters.json_metadata_);

    if (not json_parser.parse(&tree_root)) {
        LOG_WARNING("failed to parse JSON: " + json_parser.getErrorMessage());
        return;
    }

    const auto &download_item(parameters.download_item_);
    auto array_node(JSON::JSONNode::CastToArrayNodeOrDie("tree_root", tree_root));
    PostprocessTranslationServerResponse(parameters.download_item_, &array_node);

    if (array_node->size() == 0) {
        LOG_WARNING("no items found in translation server response");
        LOG_WARNING("JSON response:\n" + parameters.json_metadata_);
        return;
    }

    for (const auto &entry : *array_node) {
        const auto json_object(JSON::JSONNode::CastToObjectNodeOrDie("entry", entry));

        try {
            if (ZoteroItemMatchesExclusionFilters(download_item, json_object)) {
                ++result->num_skipped_since_exclusion_filters_;
                continue;
            }

            MetadataRecord new_metadata_record;
            ConvertZoteroItemToMetadataRecord(json_object, &new_metadata_record);
            AugmentMetadataRecord(&new_metadata_record, download_item.journal_, parameters.group_params_);

            LOG_DEBUG("Augmented metadata record: " + new_metadata_record.toString());
            if (new_metadata_record.url_.empty())
                throw std::runtime_error("no URL set");

            if (ExcludeOnlineFirstRecord(new_metadata_record, parameters)) {
                ++result->num_skipped_since_online_first_;
                continue;
            } else if (ExcludeEarlyViewRecord(new_metadata_record, parameters)) {
                ++result->num_skipped_since_early_view_;
                continue;
            }

            // a dummy record that will be replaced subsequently
            std::unique_ptr<MARC::Record> new_marc_record(new MARC::Record(std::string(MARC::Record::LEADER_LENGTH, ' ')));
            std::string new_marc_record_hash;
            GenerateMarcRecordFromMetadataRecord(download_item, new_metadata_record, parameters.group_params_,
                                                 new_marc_record.get(), &new_marc_record_hash);

            if (MarcRecordMatchesExclusionFilters(download_item, new_marc_record.get())) {
                ++result->num_skipped_since_exclusion_filters_;
                continue;
            }

            result->marc_records_.emplace_back(new_marc_record.release());
            LOG_INFO("Generated record with hash '" + new_marc_record_hash + "'\n");
        } catch (const std::exception &x) {
            LOG_WARNING("couldn't convert record: " + std::string(x.what()));
        }
    }

    LOG_INFO("Conversion complete");
}


ConversionTasklet::ConversionTasklet(ThreadUtil::ThreadSafeCounter<unsigned> * const instance_counter,
                                     std::unique_ptr<ConversionParams> parameters)
 : Util::Tasklet<ConversionParams, ConversionResult>(instance_counter, parameters->download_item_,
                                                     "Conversion: " + parameters->download_item_.url_.toString(),
                                                     std::bind(&ConversionTasklet::run, this, std::placeholders::_1, std::placeholders::_2),
                                                     std::unique_ptr<ConversionResult>(new ConversionResult()),
                                                     std::move(parameters), ResultPolicy::YIELD) {}



void *ConversionManager::BackgroundThreadRoutine(void * parameter) {
    static const unsigned BACKGROUND_THREAD_SLEEP_TIME(16 * 1000);   // ms -> us

    ConversionManager * const conversion_manager(reinterpret_cast<ConversionManager *>(parameter));

    while (not conversion_manager->stop_background_thread_.load()) {
        conversion_manager->processQueue();
        conversion_manager->cleanupCompletedTasklets();

        ::usleep(BACKGROUND_THREAD_SLEEP_TIME);
    }

    pthread_exit(nullptr);
}


void ConversionManager::processQueue() {
    if (conversion_tasklet_execution_counter_ == MAX_CONVERSION_TASKLETS)
        return;

    std::lock_guard<std::mutex> conversion_queue_lock(conversion_queue_mutex_);
    while (not conversion_queue_.empty()
           and conversion_tasklet_execution_counter_ < MAX_CONVERSION_TASKLETS)
    {
        std::shared_ptr<ConversionTasklet> tasklet(conversion_queue_.front());
        active_conversions_.emplace_back(tasklet);
        conversion_queue_.pop_front();
        tasklet->start();
    }
}


void ConversionManager::cleanupCompletedTasklets() {
    for (auto iter(active_conversions_.begin()); iter != active_conversions_.end();) {
        if ((*iter)->isComplete()) {
            iter = active_conversions_.erase(iter);
            continue;
        }
        ++iter;
    }
}


ConversionManager::ConversionManager(const GlobalParams &global_params)
 : global_params_(global_params), stop_background_thread_(false)
{
    if (::pthread_create(&background_thread_, nullptr, BackgroundThreadRoutine, this) != 0)
        LOG_ERROR("background conversion manager thread creation failed!");
}


ConversionManager::~ConversionManager() {
    stop_background_thread_.store(true);
    const auto retcode(::pthread_join(background_thread_, nullptr));
    if (retcode != 0)
        LOG_WARNING("couldn't join with the conversion manager background thread! result = " + std::to_string(retcode));

    active_conversions_.clear();
    conversion_queue_.clear();
}


std::unique_ptr<Util::Future<ConversionParams, ConversionResult>> ConversionManager::convert(const Util::HarvestableItem &source,
                                                                                             const std::string &json_metadata,
                                                                                             const Config::GroupParams &group_params)
{
    std::unique_ptr<ConversionParams> parameters(new ConversionParams(source, json_metadata,
                                                 global_params_.skip_online_first_articles_unconditonally_, group_params));
    std::shared_ptr<ConversionTasklet> new_tasklet(new ConversionTasklet(&conversion_tasklet_execution_counter_,
                                                   std::move(parameters)));

    {
        std::lock_guard<std::mutex> conversion_queue_lock(conversion_queue_mutex_);
        conversion_queue_.emplace_back(new_tasklet);
    }

    std::unique_ptr<Util::Future<ConversionParams, ConversionResult>>
        conversion_result(new Util::Future<ConversionParams, ConversionResult>(new_tasklet));
    return conversion_result;
}


} // end namespace Conversion


} // end namespace ZoteroHarvester
