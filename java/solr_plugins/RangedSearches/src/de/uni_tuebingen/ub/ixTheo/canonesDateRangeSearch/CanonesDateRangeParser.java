package de.uni_tuebingen.ub.ixTheo.canonesDateRangeSearch;


import java.util.Set;
import java.util.TreeSet;
import org.apache.lucene.search.Query;
import org.apache.solr.common.params.SolrParams;
import org.apache.solr.request.SolrQueryRequest;
import org.apache.solr.search.QParser;
import org.apache.solr.search.SyntaxError;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;


public class CanonesDateRangeParser extends QParser {
    private static Logger logger = LoggerFactory.getLogger(CanonesDateRangeParser.class);

    /**
     * @param qstr        The part of the query string specific to this parser
     * @param localParams The set of parameters that are specific to this QParser.
     *                    See http://wiki.apache.org/solr/LocalParams
     * @param params      The rest of the {@link SolrParams}
     * @param req         The original {@link SolrQueryRequest}.
     */
    public CanonesDateRangeParser(final String qstr, final SolrParams localParams, final SolrParams params, final SolrQueryRequest req) {
        super(qstr, localParams, params, req);
    }

    static CanonesDateRange[] getRangesFromDatabaseField(final String db_field) {
        return CanonesDateRange.getRanges(db_field, ",");
    }

    private CanonesDateRange[] getRangesFromQuery() {
        return CanonesDateRange.getRanges(getFieldsFromQuery());
    }

    private String[] getFieldsFromQuery() {
        return qstr.split(" OR ");
    }

    @Override
    public Query parse() throws SyntaxError {
        final String queryString = "canon_law_date_ranges:" + getString();
        final QParser parser = getParser(queryString, "multiLanguageQueryParser", getReq());
        final CanonesDateRange[] ranges = getRangesFromQuery();
        return new CanonesDateRangeQuery(parser.parse(), ranges);
    }
}
