package de.uni_tuebingen.ub.ixTheo.bibleDateRangeSearch;


import org.apache.lucene.search.IndexSearcher;
import org.apache.lucene.search.Query;
import org.apache.lucene.search.Weight;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import java.io.IOException;
import java.util.Arrays;
import de.uni_tuebingen.ub.ixTheo.rangeSearch.Range;
import de.uni_tuebingen.ub.ixTheo.rangeSearch.RangeQuery;


class BibleDateRangeQuery extends RangeQuery {
    BibleDateRangeQuery(final Query query, final BibleDateRange[] ranges) {
        super(query, Arrays.copyOf(ranges, ranges.length, Range[].class));
    }

    @Override
    public Weight createWeight(final IndexSearcher searcher, final boolean needsScores, final float boost) throws IOException {
        return new BibleDateRangeWeight(this, Arrays.copyOf(ranges, ranges.length, BibleDateRange[].class),
                                        super.createWeight(searcher, needsScores, boost));
    }

    @Override
    public boolean equals(final Object obj) {
        if (!(obj instanceof BibleDateRangeQuery))
            return false;

        return super.equals(obj);
    }
}
