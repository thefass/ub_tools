/** \file    augment_bible_references.cc
 *  \brief   A tool for adding numeric bible references to MARC-21 datasets.
 *  \author  Dr. Johannes Ruscheinski
 */

/*
    Copyright (C) 2015, Library of the University of Tübingen

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

#include <algorithm>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_set>
#include <utility>
#include <cstdlib>
#include <cstring>
#include "BibleReferenceParser.h"
#include "DirectoryEntry.h"
#include "Leader.h"
#include "MapIO.h"
#include "MarcUtil.h"
#include "RegexMatcher.h"
#include "StringUtil.h"
#include "Subfields.h"
#include "TextUtil.h"
#include "util.h"


void Usage() {
    std::cerr << "Usage: " << progname
	      << " [--verbose] ix_theo_titles ix_theo_norm augmented_ix_theo_titles bible_order_map\n";
    std::exit(EXIT_FAILURE);
}


const std::unordered_set<std::string> books_of_the_bible { // Found in 130$a:100$t
    "matthäusevangelium", // -- start New Testament --
    "markusevangelium",
    "lukasevangelium",
    "johannesevangelium",
    "apostelgeschichte",
    "römerbrief",
    "korintherbrief", // 2 records "I." and "II." in $n
    "galaterbrief",
    "epheserbrief",
    "philipperbrief",
    "kolosserbrief",
    "thessalonicherbrief", // 2 records "I." and "II." in $n
    "timotheusbrief", // 2 records "I." and "II." in $n
    "titusbrief",
    "philemonbrief",
    "hebräerbrief",
    "jakobusbrief",
    "petrusbrief", // 2 records "I." and "II." in $n
    "johannesbrief", // 3 records "I.", "II." and "III." in $n
    "judasbrief",
    "johannes-apokalypse", // a.k.a. "Offenbarung des Johannes"
    "genesis", // -- start Old Testament --
    "exodus",
    "leviticus",
    "numeri",
    "deuteronomium",
    "josua", // $9g:Buch
    "richter", // $9g:Buch
    "rut", // $9g:Buch
    "samuel", // $9g:Buch, 2 records "I." and "II." in $n
    "könige", // $9g:Buch, 2 records "I." and "II." in $n
    "chronik", // 2 records "I." and "II." in $n
    "esra", // $9g:Buch
    "nehemia", // $9g:Buch
    "tobit", // $9g:Buch
    "judit", // $9g:Buch
    "ester", // $9g:Buch
    "makkabäer", // $9g:Buch, 4 records "I.", "II.", "III." and "IV." in $n
    "ijob", // $9g:Buch
    "psalmen",
    "sprichwörter", // $9g:Bibel
    "kohelet",
    "hoheslied",
    "weisheit", // $9g:Buch
    "sirach", // $9g:Buch
    "jesaja", // $9g:Buch
    "jeremia", // $9g:Buch
    "klagelieder jeremias", // a.k.a. "Klagelieder"
    "baruch", // $9g:Buch
    "jeremiabrief", // a.k.a. "Epistola Jeremiae"
    "ezechiel", // $9g:Buch
    "daniel", // $9g:Buch
    "hosea", // $9g:Buch
    "joel", // $9g:Buch
    "amos", // $9g:Buch
    "obadja", // $9g:Buch
    "jona", // $9g:Buch
    "micha", // $9g:Buch
    "nahum", // $9g:Buch
    "habakuk", // $9g:Buch
    "zefanja", // $9g:Buch
    "haggai", // $9g:Buch
    "sacharja", // $9g:Buch
    "maleachi", // $9g:Buch
};


// Books of the bible that are flagged as "g:Buch.*" in 130$9:
const std::unordered_set<std::string> explicit_books {
    "josua", "richter", "rut", "samuel", "könige", "esra", "nehemia", "tobit", "judit", "ester",
    "makkabäer", "ijob", "weisheit", "sirach", "jesaja", "jeremia", "baruch", "ezechiel", "daniel", "hosea", "joel",
    "amos", "obadja", "jona", "micha", "nahum", "habakuk", "zefanja", "haggai", "sacharja", "maleachi"
};


// Books of the bible that have ordinal Roman numerals in 130$n:
const std::unordered_set<std::string> books_with_ordinals {
    "korintherbrief", "thessalonicherbrief", "timotheusbrief", "petrusbrief", "johannesbrief", "samuel", "könige",
    "chronik", "makkabäer"
};


const std::string BIB_REF_RANGE_TAG("801");
const std::string BIB_BROWSE_TAG("802");


void LoadBibleOrderMap(const bool verbose, File * const input,
		       std::unordered_map<std::string, std::string> * const bible_order_map)
{
    if (verbose)
	std::cerr << "Started loading of the bible-order map.\n";

    unsigned line_no(0);
    while (not input->eof()) {
	const std::string line(input->getline());
	if (line.empty())
	    continue;
	++line_no;

	const size_t equal_pos(line.find('='));
	if (equal_pos == std::string::npos)
	    Error("malformed line #" + std::to_string(line_no) + " in the bible-order map file!");
	(*bible_order_map)[StringUtil::ToLower(line.substr(0, equal_pos))] = line.substr(equal_pos + 1);
    }

    if (verbose)
	std::cerr << "Loaded " << line_no << " entries from the bible-order map file.\n";
}


bool StartsWithSmallRomanOrdinal(const std::string &roman_ordinal_candidate) {
    return StringUtil::StartsWith(roman_ordinal_candidate, "I.")
           or StringUtil::StartsWith(roman_ordinal_candidate, "II.")
           or StringUtil::StartsWith(roman_ordinal_candidate, "III.")
           or StringUtil::StartsWith(roman_ordinal_candidate, "IV.")
           or StringUtil::StartsWith(roman_ordinal_candidate, "VI.");
}


// Extracts the Roman ordinals and converts them to cardinal numbers.
bool ExtractRomanOrdinals(const std::string &ordinals, std::set<unsigned> * extracted_set) {
    std::string scanned_text;
    for (auto ch : ordinals) {
        switch (ch) {
        case 'I':
        case 'V':
            scanned_text += ch;
            break;
        case '.':
            if (scanned_text == "I")
                extracted_set->insert(1);
            else if (scanned_text == "II")
                extracted_set->insert(2);
            else if (scanned_text == "III")
                extracted_set->insert(3);
            else if (scanned_text == "IV")
                extracted_set->insert(4);
            else if (scanned_text == "VI")
                extracted_set->insert(6);
            scanned_text.clear();
            break;
        default:
            scanned_text.clear();
        }
    }

    return not extracted_set->empty();
}


inline bool EndsWithLowercaseChar(const std::string &s) {
    return std::islower(s[s.length() - 1]);
}


/** \brief Tries to find a book of the bible in a subfield.
 *  \param fields_and_subfields  A colon separated list of fields plus subfield codes used to try to locate the name
 *                               of one of the books of the bible, e.g. "130a:100t".
 *  \param dir_entries           The directory entries of a MARC-21 record.
 *  \param field_data            The field data of a MARC-21 record.
 *  \param book_candidate        After a successful return this will contain the name of a book of the bible.
 *  \param field                 After a successful return this will contain the name of the field where the book of
 *                               the bible was found.
 *  \return True if a book of the bible was found in one of the subfields, else false.
 */
bool FindBibleBookInField(const std::string &fields_and_subfields, const std::vector<DirectoryEntry> &dir_entries,
                          const std::vector<std::string> &field_data, std::string * const book_candidate,
                          std::string * const field)
{
    std::vector<std::string> fields;
    StringUtil::Split(fields_and_subfields, ':', &fields);
    for (const auto &field_and_subfield : fields) {
        *field = field_and_subfield.substr(0, 3);
        const auto field_iter(DirectoryEntry::FindField(*field, dir_entries));
        if (field_iter == dir_entries.end())
            continue;
        const Subfields subfields(field_data[field_iter - dir_entries.begin()]);
        *book_candidate = subfields.getFirstSubfieldValue(field_and_subfield[3]);
        if (books_of_the_bible.find(*book_candidate) != books_of_the_bible.end())
            return true;
    }

    return false;
}


// Expects that 'components' are separated by semicolons.
std::string StripRomanNumerals(const std::string &field_contents) {
    std::vector<std::string> components;
    StringUtil::Split(field_contents, ';', &components);

    std::string filtered_field_contents;
    for (const auto component : components) {
        if (not StartsWithSmallRomanOrdinal(component)) {
            if (not filtered_field_contents.empty())
                filtered_field_contents += ';';
            filtered_field_contents += component;
        }
    }

    return filtered_field_contents;
}


/** \brief True if a GND code was found in 035$a else false. */
bool GetGNDCode(const std::vector<DirectoryEntry> &dir_entries, const std::vector<std::string> &field_data,
                std::string * const gnd_code)
{
    gnd_code->clear();

    const auto _035_iter(DirectoryEntry::FindField("035", dir_entries));
    if (_035_iter == dir_entries.end())
        return false;
    const Subfields _035_subfields(field_data[_035_iter - dir_entries.begin()]);
    const std::string _035a_field(_035_subfields.getFirstSubfieldValue('a'));
    if (not StringUtil::StartsWith(_035a_field, "(DE-588)"))
        return false;
    *gnd_code = _035a_field.substr(8);
    return not gnd_code->empty();
}


/** Returns true if subfield "n" of "field" is empty or contains a valid chapter/verse reference, else false. */
bool GetChapterAndVerse(const std::string &field, std::string * const chapters_and_verses) {
    chapters_and_verses->clear();

    const Subfields subfields(field);
    *chapters_and_verses = subfields.getFirstSubfieldValue('n');
    if (chapters_and_verses->empty())
        return true;

    return CanParseBibleReference(*chapters_and_verses);
}


// Splits numeric references from $n and $9 subfields into an optional roman numeral part and an optional
// chapter/verse part.
void SplitNumericReferences(const Subfields &subfields, std::vector<std::string> * const roman_refs,
                            std::vector<std::string> * const rest)
{
    roman_refs->clear(), rest->clear();
    
    std::pair<Subfields::ConstIterator, Subfields::ConstIterator> begin_end(subfields.getIterators('n'));
    for (auto code_and_value(begin_end.first); code_and_value != begin_end.second; ++code_and_value) {
        if (StartsWithSmallRomanOrdinal(code_and_value->second))
            roman_refs->push_back(code_and_value->second);
        else if (not code_and_value->second.empty())
            rest->push_back(code_and_value->second);
    }

    begin_end = subfields.getIterators('9');
    for (auto code_and_value(begin_end.first); code_and_value != begin_end.second; ++code_and_value) {
        std::string candidate;
        if (StringUtil::StartsWith(code_and_value->second, "g:Buch, "))
            candidate = code_and_value->second.substr(8);
        else if (StringUtil::StartsWith(code_and_value->second, "g:Buch "))
            candidate = code_and_value->second.substr(7);
        else if (StringUtil::StartsWith(code_and_value->second, "g:"))
            candidate = code_and_value->second.substr(2);
        if (candidate.empty())
            continue;

        if (StartsWithSmallRomanOrdinal(candidate)) {
            std::string roman_numeral, remainder;
            bool in_roman_numeral(true), in_remainder(false);
            for (const auto ch : candidate) {
                if (in_roman_numeral) {
                    if (ch == 'I' or ch == 'V')
                        roman_numeral += ch;
                    else if (ch == '.') {
                        roman_numeral += ch;
                        in_roman_numeral = false;
                    }
                } else if (in_remainder) {
                    if (ch != ' ')
                        remainder += ch;
                } else {
                    if (ch != ',' and ch != ' ') {
                        remainder += ch;
                        in_remainder = true;
                    }
                }
            }
            roman_refs->push_back(roman_numeral);
            if (not remainder.empty())
                rest->push_back(remainder);
        } else if (isdigit(candidate[0]))
            rest->push_back(StringUtil::RightTrim(&candidate));
    }
}


bool ConvertArabicNumeralsToRomanNumerals(const std::string &arabic_numerals_candidate,
					  std::vector<std::string> * const roman_refs)
{
    static const RegexMatcher * const matcher(RegexMatcher::RegexMatcherFactory("[123]\\."));
    static std::map<char, std::string> arabic_to_roman_map{{'1', "I"}, {'2', "II"}, {'3', "III"}};
    std::string replacement_string(arabic_numerals_candidate);

    bool replaced_one_or_more(false);
    size_t match_start_pos;
    while (matcher->matched(replacement_string, /* err_msg = */nullptr, &match_start_pos)) {
	replacement_string = replacement_string.substr(0, match_start_pos)
                             + arabic_to_roman_map[replacement_string[match_start_pos]]
                             + replacement_string.substr(match_start_pos + 1);
	replaced_one_or_more = true;
    }

    if (replaced_one_or_more)
	roman_refs->push_back(replacement_string);
    return replaced_one_or_more;
}


bool ExtractBibleReference(const bool verbose, const std::string &control_number, const std::string &field,
                           const char subfield_code,
			   const std::unordered_map<std::string, std::string> &bible_book_to_code_map,
			   std::string * const book_name,
                           std::set<std::pair<std::string, std::string>> * const ranges)
{
    const Subfields subfields(field);

    *book_name = StringUtil::ToLower(subfields.getFirstSubfieldValue(subfield_code));

    const size_t last_space_pos(book_name->find_last_of(' '));
    std::string chapters_and_verses;
    if (last_space_pos != std::string::npos and last_space_pos > 2
        and CanParseBibleReference(book_name->substr(last_space_pos + 1))) // Old format.
    {
        chapters_and_verses = book_name->substr(last_space_pos + 1);
        *book_name = StringUtil::ToLower(StringUtil::RightTrim(book_name->substr(0, last_space_pos)));
    }

    if (book_name->empty() or books_of_the_bible.find(*book_name) == books_of_the_bible.end())
        return false;

    // Filter records that look like bible books but would have to have a $9 subfield starting
    // with "g:Buch" in order to qualify:
    if (explicit_books.find(*book_name) != explicit_books.end()) {
        if (not StringUtil::StartsWith(subfields.getFirstSubfieldValue('9'), "g:Buch"))
            return false;
    }

    std::vector<std::string> roman_refs, other_refs;
    SplitNumericReferences(subfields, &roman_refs, &other_refs);
    if (other_refs.empty() and not chapters_and_verses.empty())
        other_refs.push_back(chapters_and_verses);

    // Filter records that looks like bible books but would have to have a $n or $9 subfield
    // containing a roman ordinal number in order to qualify:
    std::set<unsigned> book_ordinals;
    if (books_with_ordinals.find(*book_name) != books_with_ordinals.end()) {
	if (roman_refs.empty() and other_refs.size() == 1) {
	    if (ConvertArabicNumeralsToRomanNumerals(other_refs[0], &roman_refs))
		other_refs.clear();
	}
        if (roman_refs.empty()) {
            if (verbose)
                std::cerr << "Warning: roman numerals missing for PPN " << control_number << ".\n";
            return false;
        } else if (roman_refs.size() > 1) {
            if (verbose)
                std::cerr << "Warning: multiple roman numerals for PPN " << control_number << ".\n";
        }

        if (not ExtractRomanOrdinals(roman_refs.front(), &book_ordinals)) {
            if (verbose)
                std::cerr << "Warning: failed to extract roman numerals from \"" << roman_refs.front()
                          << "\", PPN is " << control_number << ".\n";
            return false;
        }
    }

    // Deal with chapters and verses:
    if (other_refs.size() > 1)
        return false;
    else if (other_refs.size() == 1) {
        if (not CanParseBibleReference(other_refs.front()))
            return false;
    }

    // Generate the mapping from books of the bible to numeric codes:
    std::vector<std::string> current_book_codes;
    if (book_ordinals.empty()) {
        const auto book_and_code(bible_book_to_code_map.find(*book_name));
        if (likely(book_and_code != bible_book_to_code_map.end()))
	    current_book_codes.emplace_back(book_and_code->second);
	else  {
            Warning("norm data record with PPN " + control_number + " contains book name \"" + *book_name
		    + "\" for which we habe no code!");
	    return false;
	}
    } else {
        for (const auto ordinal : book_ordinals) {
            std::string augmented_book_name(std::to_string(ordinal) + *book_name);
            const auto book_and_code(bible_book_to_code_map.find(augmented_book_name));
            if (book_and_code != bible_book_to_code_map.end())
                current_book_codes.emplace_back(book_and_code->second);
            else {
                Warning("norm data record with PPN " + control_number + " contains book name \"" + *book_name
			+ "\" for which we habe no code!");
		return false;
            }
        }
    }

    // Generate numeric codes:
    if (other_refs.empty()) {
	for (const auto &current_book_code : current_book_codes)
	    ranges->insert(std::make_pair(current_book_code + "00000", current_book_code + "99999"));
    } else {
	if (current_book_codes.size() != 1) {
	    Warning("norm data record with PPN " + control_number + " contains 0 or 2 or more bible book references "
		    " as well as additional, typical chapter/verse, information which we don't know how to process!");
	    return false;
	}
	if (not ParseBibleReference(other_refs.front(), current_book_codes[0], ranges)) {
	    std::cerr << "Bad ranges: " << control_number << ": " << other_refs.front() << '\n';
	    return false;
	}
    }

    return true;
}


void FindPericopes(const std::string &pericope_field, const std::string &book_name,
                   const std::vector<DirectoryEntry> &dir_entries, const std::vector<std::string> &field_data,
                   const std::set<std::pair<std::string, std::string>> &ranges,
                   std::unordered_multimap<std::string, std::string> * const pericopes_to_ranges_map)
{
    std::vector<std::string> pericopes;
    auto field_iter(DirectoryEntry::FindField(pericope_field, dir_entries));
    while (field_iter != dir_entries.end() and field_iter->getTag() == pericope_field) {
        const Subfields subfields(field_data[field_iter - dir_entries.begin()]);
        std::string a_subfield(subfields.getFirstSubfieldValue('a'));
        StringUtil::ToLower(&a_subfield);
        if (a_subfield != book_name)
            pericopes.push_back(a_subfield);
        ++field_iter;
    }

    if (not pericopes.empty()) {
        for (const auto &pericope : pericopes) {
            for (const auto &range : ranges)
                pericopes_to_ranges_map->emplace(pericope, range.first + ":" + range.second);
        }
    }
}


void LoadNormData(const bool verbose, const std::unordered_map<std::string, std::string> &bible_order_map,
		  File * const norm_input,
                  std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>> * const
                      gnd_codes_to_bible_ref_codes_map)
{
    gnd_codes_to_bible_ref_codes_map->clear();
    if (verbose)
        std::cerr << "Starting loading of norm data.\n";

    const std::string bible_book_map_filename("books_of_the_bible_to_code.map");
    std::ofstream bible_book_map(bible_book_map_filename, std::ofstream::out | std::ofstream::trunc);
    if (bible_book_map.fail())
        Error("Failed to open \"" + bible_book_map_filename + "\" for writing!");

    unsigned count(0), bible_ref_count(0), _130a_count(0), _100t_count(0), _430a_count(0);
    std::unordered_multimap<std::string, std::string> pericopes_to_ranges_map;
    while (const MarcUtil::Record record = MarcUtil::Record::XmlFactory(norm_input)) {
        ++count;

	const std::vector<DirectoryEntry> &dir_entries(record.getDirEntries());
        const auto _001_iter(DirectoryEntry::FindField("001", dir_entries));
        if (_001_iter == dir_entries.end())
            continue;
	const std::vector<std::string> &fields(record.getFields());
        const std::string &control_number(fields[_001_iter - dir_entries.begin()]);

        const auto _065_begin_end(DirectoryEntry::FindFields("065", dir_entries));
        bool found_a_bible_indicator(false);
        for (auto _065_iter(_065_begin_end.first); _065_iter != _065_begin_end.second; ++_065_iter) {
            const Subfields _065_subfields(fields[_065_iter - dir_entries.begin()]);
            const std::string _065_a(_065_subfields.getFirstSubfieldValue('a'));
            if (StringUtil::StartsWith(_065_a, "3.2aa") or StringUtil::StartsWith(_065_a, "3.2ba")) {
                found_a_bible_indicator = true;
                break;
            }
        }
        if (not found_a_bible_indicator)
            continue;

        const auto _079_iter(DirectoryEntry::FindField("079", dir_entries));
        if (_079_iter == dir_entries.end())
            continue;
        const Subfields _079_subfields(fields[_079_iter - dir_entries.begin()]);
        if (_079_subfields.getFirstSubfieldValue('v') != "wit")
            continue;

        std::string gnd_code;
        if (not GetGNDCode(dir_entries, fields, &gnd_code))
            continue;

        // Look for bible book references in 130$a, 100$t, and 430$a:
        bool found_ref(false);
        std::string book_name;
        std::set<std::pair<std::string, std::string>> ranges;
        const auto _130_iter(DirectoryEntry::FindField("130", dir_entries));
        if (_130_iter != dir_entries.end()
            and ExtractBibleReference(verbose, control_number, fields[_130_iter - dir_entries.begin()], 'a',
                                      bible_order_map, &book_name, &ranges))
        {
            if (gnd_codes_to_bible_ref_codes_map->find(gnd_code) == gnd_codes_to_bible_ref_codes_map->end())
                (*gnd_codes_to_bible_ref_codes_map)[gnd_code] = ranges;
            else
                (*gnd_codes_to_bible_ref_codes_map)[gnd_code].insert(ranges.begin(), ranges.end());
            found_ref = true;
            FindPericopes("430", book_name, dir_entries, fields, ranges, &pericopes_to_ranges_map);
            ++_130a_count;
        }
        if (not found_ref) {
            const auto _100_iter(DirectoryEntry::FindField("100", dir_entries));
            if (_100_iter != dir_entries.end()
                and ExtractBibleReference(verbose, control_number, fields[_100_iter - dir_entries.begin()],
                                          't', bible_order_map, &book_name, &ranges))
            {
                if (gnd_codes_to_bible_ref_codes_map->find(gnd_code) == gnd_codes_to_bible_ref_codes_map->end())
                    (*gnd_codes_to_bible_ref_codes_map)[gnd_code] = ranges;
                else
                    (*gnd_codes_to_bible_ref_codes_map)[gnd_code].insert(ranges.begin(), ranges.end());
                found_ref = true;
                ++_100t_count;
            }
        }
        if (not found_ref) {
            std::vector<std::string> pericopes;
            for (auto _430_iter(DirectoryEntry::FindField("430", dir_entries));
                 _430_iter != dir_entries.end() and _430_iter->getTag() == "430"; ++_430_iter)
            {
                if (ExtractBibleReference(verbose, control_number, fields[_430_iter - dir_entries.begin()], 'a',
                                          bible_order_map, &book_name, &ranges))
                {
                    if (gnd_codes_to_bible_ref_codes_map->find(gnd_code) == gnd_codes_to_bible_ref_codes_map->end())
                        (*gnd_codes_to_bible_ref_codes_map)[gnd_code] = ranges;
                    else
                        (*gnd_codes_to_bible_ref_codes_map)[gnd_code].insert(ranges.begin(), ranges.end());
                    found_ref = true;
                } else { // Possible pericope.
                    const Subfields subfields(fields[_430_iter - dir_entries.begin()]);
                    const std::string subfield_a(subfields.getFirstSubfieldValue('a'));
                    if (not subfield_a.empty())
                        pericopes.push_back(StringUtil::ToLower(subfield_a));
                }
            }
            if (found_ref) {
                ++_430a_count;
                FindPericopes("130", book_name, dir_entries, fields, ranges, &pericopes_to_ranges_map);
                for (const auto &pericope : pericopes) {
                    for (const auto &range : ranges)
                        pericopes_to_ranges_map.emplace(pericope, range.first + ":" + range.second);
                }
            }
        }

        if (not found_ref)
            continue;

        ++bible_ref_count;
    }

    if (verbose)
	std::cerr << "About to write \"pericopes_to_codes.map\".\n";
    MapIO::SerialiseMap("pericopes_to_codes.map", pericopes_to_ranges_map);

    if (verbose) {
        std::cerr << "Read " << count << " norm data records.\n";
        std::cerr << "Found " << bible_ref_count << " reference records.\n";
        std::cerr << "Found " << _130a_count << " 130$a reference records.\n";
        std::cerr << "Found " << _100t_count << " 100$t reference records.\n";
        std::cerr << "Found " << _430a_count << " 430$a reference records.\n";
    }
}


bool FindGndCodes(const std::string &tags, const MarcUtil::Record &record,
                  const std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>>
                  &gnd_codes_to_bible_ref_codes_map, std::set<std::string> * const ranges)
{
    ranges->clear();

    std::vector<std::string> individual_tags;
    StringUtil::Split(tags, ':', &individual_tags);

    bool found_at_least_one(false);
    for (const auto &tag : individual_tags) {
        const ssize_t first_index(record.getFieldIndex(tag));
        if (first_index == -1)
            continue;

	const std::vector<DirectoryEntry> &dir_entries(record.getDirEntries());
        for (size_t index(first_index); index < dir_entries.size() and dir_entries[index].getTag() == tag; ++index) {
	    const std::vector<std::string> &fields(record.getFields());
            const Subfields subfields(fields[index]);
            const std::string subfield2(subfields.getFirstSubfieldValue('2'));
            if (subfield2.empty() or subfield2 != "gnd")
                continue;

            const auto begin_end(subfields.getIterators('0'));
            for (auto subfield0(begin_end.first); subfield0 != begin_end.second; ++subfield0) {
                if (not StringUtil::StartsWith(subfield0->second, "(DE-588)"))
                    continue;

                const std::string gnd_code(subfield0->second.substr(8));
                const auto gnd_code_and_ranges(gnd_codes_to_bible_ref_codes_map.find(gnd_code));
                if (gnd_code_and_ranges != gnd_codes_to_bible_ref_codes_map.end()) {
                    found_at_least_one = true;
                    for (const auto &range : gnd_code_and_ranges->second)
                        ranges->insert(range.first + ":" + range.second);
                }
            }
        }
    }

    return found_at_least_one;
}


void AugmentBibleRefs(const bool verbose, File * const input, File * const output,
                      const std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>>
                          &gnd_codes_to_bible_ref_codes_map)
{
    if (verbose)
        std::cerr << "Starting augmentation of title records.\n";

    XmlWriter xml_writer(output);
    unsigned total_count(0), augment_count(0);
    xml_writer.openTag("marc:collection",
                       { std::make_pair("xmlns:marc", "http://www.loc.gov/MARC21/slim"),
                         std::make_pair("xmlns:xsi", "http://www.w3.org/2001/XMLSchema-instance"),
                         std::make_pair("xsi:schemaLocation", "http://www.loc.gov/standards/marcxml/schema/MARC21slim.xsd")});
    while (MarcUtil::Record record = MarcUtil::Record::XmlFactory(input)) {
	record.setRecordWillBeWrittenAsXml(true);
        ++total_count;

        // Make sure that we don't use a bible reference tag that is already in use for another
        // purpose:
	const std::vector<DirectoryEntry> &dir_entries(record.getDirEntries());
        const auto bib_ref_begin_end(DirectoryEntry::FindFields(BIB_REF_RANGE_TAG, dir_entries));
        if (bib_ref_begin_end.first != bib_ref_begin_end.second)
            Error("We need another bible reference tag than \"" + BIB_REF_RANGE_TAG + "\"!");

        std::set<std::string> ranges;
        if (FindGndCodes("600:610:611:630:648:651:655:689", record, gnd_codes_to_bible_ref_codes_map, &ranges)) {
            ++augment_count;
            std::string range_string;
            for (auto &range : ranges) {
                if (not range_string.empty())
                    range_string += ',';
                range_string += StringUtil::Map(range, ':', '_');
            }

            // Put the data into the $a subfield:
            range_string = "  ""\x1F""a" + range_string;
            record.insertField(BIB_REF_RANGE_TAG, range_string);
        }

	record.write(&xml_writer);
    }
    xml_writer.closeTag();

    if (verbose)
        std::cerr << "Augmented the " << BIB_REF_RANGE_TAG << "$a field of " << augment_count
                  << " records of a total of " << total_count << " records.\n";
}


int main(int argc, char **argv) {
    progname = argv[0];

    if (argc != 5 and argc != 6)
        Usage();

    const bool verbose(std::strcmp(argv[1], "--verbose") == 0);
    if (verbose ? (argc != 6) : (argc != 5))
        Usage();

    const std::string title_input_filename(argv[verbose ? 2 : 1]);
    File title_input(title_input_filename, "r");
    if (not title_input)
        Error("can't open \"" + title_input_filename + "\" for reading!");

    const std::string norm_input_filename(argv[verbose ? 3 : 2]);
    File norm_input(norm_input_filename, "r");
    if (not norm_input)
        Error("can't open \"" + norm_input_filename + "\" for reading!");

    const std::string title_output_filename(argv[verbose ? 4 : 3]);
    File title_output(title_output_filename, "w");
    if (not title_output)
        Error("can't open \"" + title_output_filename + "\" for writing!");

    if (unlikely(title_input_filename == title_output_filename))
        Error("Title input file name equals title output file name!");

    if (unlikely(norm_input_filename == title_output_filename))
        Error("Norm data input file name equals title output file name!");

    const std::string bible_order_map_filename(argv[verbose ? 5 : 4]);
    File bible_order_map_file(bible_order_map_filename, "r");
    if (not bible_order_map_file)
        Error("can't open \"" + bible_order_map_filename + "\" for reading!");

    try {
	std::unordered_map<std::string, std::string> bible_order_map;
	LoadBibleOrderMap(verbose, &bible_order_map_file, &bible_order_map);

	std::unordered_map<std::string, std::set<std::pair<std::string, std::string>>> gnd_codes_to_bible_ref_codes_map;
	LoadNormData(verbose, bible_order_map, &norm_input, &gnd_codes_to_bible_ref_codes_map);
	AugmentBibleRefs(verbose, &title_input, &title_output, gnd_codes_to_bible_ref_codes_map);
    } catch (const std::exception &x) {
	Error("caught exception: " + std::string(x.what()));
    }
}
