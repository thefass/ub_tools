package de.uni_tuebingen.ub.ixTheo.timeAspectRangeSearch;


import org.apache.solr.common.params.SolrParams;
import org.apache.solr.common.util.NamedList;
import org.apache.solr.request.SolrQueryRequest;
import org.apache.solr.search.QParser;
import org.apache.solr.search.QParserPlugin;


public class TimeAspectRangeParserPlugin extends QParserPlugin {

    @Override
    @SuppressWarnings("rawtypes")
    public void init(final NamedList args) {
    }

    @Override
    public QParser createParser(final String queryString, final SolrParams localParams, final SolrParams params,
                                final SolrQueryRequest req)
    {
        return new TimeAspectRangeParser(queryString, localParams, params, req);
    }
}
