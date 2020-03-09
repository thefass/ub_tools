package de.unituebingen.ub.ubtools.solrmarcMixin;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileReader;
import java.io.IOException;
import java.io.Reader.*;
import java.util.logging.Logger;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.*;
import org.marc4j.marc.DataField;
import org.marc4j.marc.Record;
import org.marc4j.marc.VariableField;
import org.marc4j.marc.*;
import org.solrmarc.index.SolrIndexer;
import org.solrmarc.index.SolrIndexerMixin;
import org.solrmarc.tools.DataUtil;
import org.solrmarc.tools.Utils;
import de.unituebingen.ub.ubtools.solrmarcMixin.*;

public class IxTheo extends SolrIndexerMixin {
    protected static Logger logger = Logger.getLogger(IxTheo.class.getName());


    static boolean parseIniFileLine(final String line, final StringBuilder key, final StringBuilder value)
    {
        final int firstEqualPos = line.indexOf('=');
        if (firstEqualPos == -1)
            return false;

        key.append(line.substring(0, firstEqualPos - 1).trim());

        final String possiblyQuotedValue = line.substring(firstEqualPos + 1).trim();
        if (possiblyQuotedValue.length() < 2 || possiblyQuotedValue.charAt(0) != '"'
            || possiblyQuotedValue.charAt(possiblyQuotedValue.length() - 1) != '"')
            return false;

        value.append(possiblyQuotedValue.substring(1, possiblyQuotedValue.length() - 1));

        return true;
    }

    static void processLanguageIniFile(final File iniFile, final HashMap<String, TreeSet<String>> ixtheoNotationsToDescriptionsMap,
                                       final String entryPrefix)
    {
        BufferedReader reader = null;
        try {
            reader = new BufferedReader(new FileReader(iniFile));
        } catch (final FileNotFoundException ex) {
            logger.severe("can't create a BufferedReader for \"" + iniFile.getName() + "\"!");
            System.exit(1);
        }

        try {
            final int ENTRY_PREFIX_LENGTH = entryPrefix.length();
            String line;
            while ((line = reader.readLine()) != null) {
                if (!line.startsWith(entryPrefix))
                    continue;

                final StringBuilder key = new StringBuilder();
                final StringBuilder value = new StringBuilder();
                if (!parseIniFileLine(line, key, value) || value.length() < key.length() - ENTRY_PREFIX_LENGTH + 2 /* 1 space and at least one character */)
                    continue;

                final String notationCode = key.toString().substring(ENTRY_PREFIX_LENGTH);
                final String notationDescription = value.toString().substring(key.length() - ENTRY_PREFIX_LENGTH).trim();

                if (!ixtheoNotationsToDescriptionsMap.containsKey(notationCode)) {
                    final TreeSet<String> newSet = new TreeSet<String>();
                    newSet.add(notationDescription);
                    ixtheoNotationsToDescriptionsMap.put(notationCode, newSet);
                } else {
                    final TreeSet<String> set = ixtheoNotationsToDescriptionsMap.get(notationCode);
                    set.add(notationDescription);
                }
            }
        } catch (final IOException ex) {
            logger.severe("We should *never* get here!");
            System.exit(1);
        }
    }

    final static String LANGUAGES_DIRECTORY = "/usr/local/vufind/local/tuefind/languages";

    static HashMap<String, TreeSet<String>> processLanguageIniFiles()
    {
        final HashMap<String, TreeSet<String>> ixtheoNotationsToDescriptionsMap = new HashMap<>();

        final File[] dir_entries = new File(LANGUAGES_DIRECTORY).listFiles();
        boolean foundAtLeastOne = false;
        for (final File dir_entry : dir_entries) {
            if (dir_entry.getName().length() != 6 || !dir_entry.getName().endsWith(".ini")) {
                logger.warning("Unexpected language file: " + dir_entry.getName());
                continue;
            }
            foundAtLeastOne = true;
            processLanguageIniFile(dir_entry, ixtheoNotationsToDescriptionsMap, "ixtheo-");
        }
        if (!foundAtLeastOne) {
            logger.severe("No language files found in \"" + LANGUAGES_DIRECTORY + "\"!");
            System.exit(1);
        }

        return ixtheoNotationsToDescriptionsMap;
    }

    private HashMap<String, TreeSet<String>> ixtheoNotationsToDescriptionsMap = processLanguageIniFiles();

    /**
     * Split the colon-separated ixTheo notation codes into individual codes and
     * return them.
     */
    public Set<String> getIxTheoNotations(final Record record) {
        final Set<String> ixTheoNotations = new TreeSet<>();
        final List fields = record.getVariableFields("652");
        if (fields.isEmpty())
            return ixTheoNotations;

        // We should only have one 652 field
        final DataField data_field = (DataField) fields.iterator().next();
        // There should always be exactly one $a subfield
        final String contents = data_field.getSubfield('a').getData();
        final String[] parts = contents.split(":");
        Collections.addAll(ixTheoNotations, parts);

        return ixTheoNotations;
    }

    /**
     * Split the colon-separated ixTheo notation codes into individual codes and
     * return the expanded and translated versions.
     */
    public Set<String> getExpandedIxTheoNotations(final Record record) {
        final Set<String> notationCodes = getIxTheoNotations(record);

        final HashSet<String> expandedIxTheoNotations = new HashSet<>();
        for (final String notationCode : notationCodes) {
            final Set<String> notationDescriptions = ixtheoNotationsToDescriptionsMap.get(notationCode);
            if (notationDescriptions != null) {
                for (final String notationDescription : notationDescriptions)
                    expandedIxTheoNotations.add(notationDescription);
            }
        }

        return expandedIxTheoNotations;
    }

    public Set<String> getIxTheoNotationFacets(final Record record) {
        final Set<String> ixTheoNotations = getIxTheoNotations(record);
        if (ixTheoNotations.isEmpty()) {
            return TuelibMixin.UNASSIGNED_SET;
        }
        return ixTheoNotations;
    }

    /**
     * Translate set of terms to given language if a translation is found
     */
    public Set<String> translateTopics(Set<String> topics, String langShortcut) {
        if (langShortcut.equals("de"))
            return topics;
        Set<String> translated_topics = new HashSet<String>();
        Map<String, String> translation_map = TuelibMixin.getTranslationMap(langShortcut);

        for (String topic : topics) {
            // Some ordinary topics contain words with an escaped slash as a
            // separator
            // See whether we can translate the single parts
            if (topic.contains("\\/")) {
                String[] subtopics = topic.split("\\/");
                int i = 0;
                for (String subtopic : subtopics) {
                    subtopics[i] = (translation_map.get(subtopic) != null) ? translation_map.get(subtopic) : subtopic;
                    ++i;
                }
                translated_topics.add(Utils.join(new HashSet<String>(Arrays.asList(subtopics)), "\\/"));

            } else {
                topic = (translation_map.get(topic) != null) ? translation_map.get(topic) : topic;
                translated_topics.add(topic);
            }
        }

        return translated_topics;
    }

    public String getIsCanonLaw(final Record record) {
        return record.getVariableFields("CAN").isEmpty() ? "false" : "true";
    }

    /** \class RegexAndReplacement
     *  \brief Combines a compiled regular expression w/ a replacement string w/ backreferences
     */
    public static class RegexAndReplacement {
        Pattern pattern;
        char[] replacement;
        public RegexAndReplacement(final String pattern, final String replacement) {
            this.pattern = Pattern.compile(pattern);
            this.replacement = replacement.toCharArray();
        }

        /** \return If the regex matched all matches will be replaced by the replacemnt pattern o/w the original
            "subject" will be returned. */
        public String replaceAll(final String subject) {
            final Matcher matcher = this.pattern.matcher(subject);
            final StringBuilder stringBuilder = new StringBuilder();
            boolean foundAtLeastOneMatch = false;
            int lastEnd = 0;
            while (matcher.find()) {
                foundAtLeastOneMatch = true;
                stringBuilder.append(subject.substring(lastEnd, matcher.start()));
                boolean backslash_seen = false;
                for (final char ch : this.replacement) {
                    if (backslash_seen) {
                        if (ch == '\\')
                            stringBuilder.append('\\');
                        else {
                            final int groupIndex = Character.digit(ch, 10);
                            if (groupIndex == -1) {
                                System.err.println("bad backreference group index character in " + this.replacement);
                                System.exit(-1);
                            }
                            stringBuilder.append(matcher.group(groupIndex));
                        }
                        backslash_seen = false;
                    } else if (ch == '\\')
                          backslash_seen = true;
                    else
                        stringBuilder.append(ch);

                }
                lastEnd = matcher.end();
            }

            return foundAtLeastOneMatch ? stringBuilder.toString() : subject;
        }
    }

    // Non-standard BCE year references and their standardized replacements. Backreferences for matched groups look like \\N
    // where N is a single-digit ASCII character referecing the N-th matched group.
    private static List<RegexAndReplacement> bce_replacement_map;
    static {
        final ArrayList<RegexAndReplacement> tempList = new ArrayList<RegexAndReplacement>();
        tempList.add(new RegexAndReplacement("v(\\d+) ?- ?v(\\d+)", "\\1 v.Chr.-\\2 v.Chr"));
        tempList.add(new RegexAndReplacement("v(\\d+) ?- ?(\\d+)", "\\1 v.Chr.-\\2"));
        tempList.add(new RegexAndReplacement("v(\\d+)", "\\1 v. Chr."));
        bce_replacement_map = Collections.unmodifiableList(tempList);
    }

    // Replaces all occurences of the first match found in bce_replacement_map, or returns the original string if no matches were found.
    static String replaceBCEPatterns(final String s) {
        for (final RegexAndReplacement regex_and_replacement : bce_replacement_map) {
            final String patchedString = regex_and_replacement.replaceAll(s);
            if (!patchedString.equals(s))
                return patchedString;
        }

        return s;
    }

    // For subfields sepcified by "fieldSpecs" matches found in "bce_replacement_map" will be substituted w/ their replacements.
    // Non-matching subfields contents will be returned unaltered.
    public Set<String> getBCENormalizedContents(final Record record, final String fieldSpecs) {
        Set<String> normalizedValues = new HashSet<String>();

        final Set<String> values = SolrIndexer.instance().getFieldList(record, fieldSpecs);
        for (final String value : values)
            normalizedValues.add(replaceBCEPatterns(value));

        return normalizedValues;
    }
}
