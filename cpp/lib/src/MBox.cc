/** \file   MBox.cc
 *  \brief  mbox processing support
 *  \author Dr. Johannes Ruscheinski (johannes.ruscheinski@uni-tuebingen.de)
 *
 *  \copyright 2020 Universitätsbibliothek Tübingen.  All rights reserved.
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
#include "MBox.h"
#include "FileUtil.h"
#include "StringUtil.h"
#include "TimeUtil.h"
#include "util.h"


MBox::Message &MBox::Message::swap(Message &other_message) {
    other_message.original_host_.swap(original_host_);
    other_message.sender_.swap(sender_);
    other_message.subject_.swap(subject_);
    other_message.message_body_.swap(message_body_);
    return *this;
}


void MBox::const_iterator::operator++() {
    if (message_.empty())
        LOG_ERROR("attempted to read beyond the end of \"" + mbox_->getPath() + "\"!");

    message_ = mbox_->getNextMessage();
}


MBox::MBox(const std::string &filename): last_reception_time_(TimeUtil::BAD_TIME_T) {
    input_ = FileUtil::OpenInputFileOrDie(filename).release();
}


// Attempts to extract an email sender's email address from the "From " line of an mbox email.
// From lines start with "From " followed by an email address followed by a datetime as
// generated by asctime(3).  The documentation here http://qmail.org./man/man5/mbox.html may
// be useful.
static bool ParseFrom(const std::string &from_line_candidate, time_t * const reception_time) {
    const auto first_space_pos(from_line_candidate.find(' '));
    if (first_space_pos == std::string::npos or first_space_pos != 4)
        return false;

    if (from_line_candidate.substr(0, 4) != "From")
        return false;

    const auto second_space_pos(from_line_candidate.find(' ', 4 + 1));
    if (second_space_pos == std::string::npos)
        return false;

    const auto sender(from_line_candidate.substr(4 + 1, second_space_pos - 4 - 1));
    if (sender != "MAILER-DAEMON" and sender != "nobody" and sender.find('@') == std::string::npos)
        return false;

    size_t asctime_start(second_space_pos + 1);
    while (asctime_start < from_line_candidate.length() - 1 and from_line_candidate[asctime_start] == ' ')
        ++asctime_start;

    struct tm tm;
    if (not TimeUtil::AscTimeToStructTm(from_line_candidate.substr(asctime_start), &tm)) {
        LOG_WARNING("bad asctime \"" + from_line_candidate.substr(asctime_start));
        return false;
    }

    *reception_time = TimeUtil::TimeGm(tm);
    return *reception_time != TimeUtil::BAD_TIME_T;
}


static bool ParseRFC822Header(const std::string &line, std::string * const field_name,
                              std::string * const field_body)
{
    const auto first_colon_pos(line.find(':'));
    if (first_colon_pos == std::string::npos or first_colon_pos == 0)
        return false;

    *field_name = line.substr(0, first_colon_pos);
    for (const char ch : *field_name) {
        if (ch == ' ' or not StringUtil::IsPrintableASCIICharacter(ch))
            return false;
    }
    StringUtil::ASCIIToLower(field_name); // According to the RFC, case does not matter in field names.

    *field_body = StringUtil::TrimWhite(line.substr(first_colon_pos + 1));
    return true;
}


// See section 6 of RFC 822 in order to understand the following.
bool ParseFromBody(const std::string &field_body, std::string * const sender) {
    std::vector<std::string> parts;
    if (StringUtil::SplitThenTrimWhite(field_body, ' ', &parts) == 0)
        return false;

    if (parts[0].find('@') != std::string::npos) {
        sender->swap(parts[0]);
        return true;
    }

    for (auto part(parts.begin() + 1); part != parts.end(); ++part) {
        if (part->front() == '<' and part->back() == '>') {
            *sender = part->substr(1, part->length() - 2);
            return true;
        }
    }

    return false;
}


bool ParseReceivedBody(const std::string &field_body, std::string * const host) {
    std::vector<std::string> parts;
    if (StringUtil::SplitThenTrimWhite(field_body, ' ', &parts) < 2)
        return false;

    if (parts[0] == "from") {
        host->swap(parts[1]);
        return true;
    }

    return false;
}


MBox::Message MBox::getNextMessage() const {
    if (input_->eof())
        return Message();

    std::string line;

    time_t reception_time;
    if (input_->tell() != 0)
        reception_time = last_reception_time_;
    else {
        input_->getline(&line);
        if (not ParseFrom(line, &reception_time))
            LOG_ERROR("invalid From line \"" + line + "\" in \"" + input_->getPath() + "\"!");
    }

    std::string sender, original_host, subject;
    for (;;) {
        if (unlikely(input_->eof()))
            LOG_ERROR("unexpected EOF while looking for the end of the message headers in \""
                      + input_->getPath() + "\"!");

        line = getNextLogicalHeaderLine();
        if (line.empty())
            break;

        std::string field_name, field_body;
        if (unlikely(not ParseRFC822Header(line, &field_name, &field_body)))
            LOG_ERROR("cannot parse RFC822 header line \"" + line + "\" in \"" + input_->getPath() + "\"!");

        if (field_name == "from" and not ParseFromBody(field_body, &sender))
            LOG_ERROR("failed to extract email address from \"" + line + "\" in \"" + input_->getPath() + "\"!");
        else if (field_name == "subject")
            subject = field_body;
        else if (field_name == "received") {
            std::string new_host;
            if (ParseReceivedBody(field_body, &new_host))
                original_host = new_host;
        }
    }

    std::string message_body;
    for (;;) {
        if (unlikely(input_->eof()))
            break;

        input_->getline(&line);
        if (ParseFrom(line, &reception_time)) {
            last_reception_time_ = reception_time;
            if (message_body.size() >= 2 and message_body.back() == '\n')
                message_body.resize(message_body.size() - 1); // Strip off the blank line at the end.
            break;
        }

        if (StringUtil::StartsWith(line, ">From")) // Escaped From-line.
            message_body += line.substr(1);
        else
            message_body += line;
        message_body += '\n';
    }

    return Message(reception_time, original_host, sender, subject, message_body);
}


std::string MBox::getNextLogicalHeaderLine() const {
    if (unlikely(input_->eof()))
        LOG_ERROR("unexpected EOF in \"" + input_->getPath() + "\" while trying to read a message header!");

    std::string logical_line;
    if (input_->getline(&logical_line) == 0)
        return logical_line;

    // Process continuation lines:
    for (;;) {
        const int lookahead_char(input_->peek());
        if (lookahead_char != ' ' and lookahead_char != '\t')
            break;

        std::string continuation_line;
        input_->getline(&continuation_line);
        logical_line += continuation_line;
    }

    // Normalise multiple tabs and spaces to a single space each:
    bool space_seen(true); // => remove leading spaces.
    std::string whitespace_normalised_line;
    for (const auto ch : logical_line) {
        if (ch == ' ' or ch == '\t') {
            if (not space_seen) {
                whitespace_normalised_line += ' ';
                space_seen = true;
            }
        } else {
            whitespace_normalised_line += ch;
            space_seen = false;
        }
    }

    return whitespace_normalised_line;
}
