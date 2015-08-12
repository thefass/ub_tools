package de.uni_tuebingen.ub.ixTheo.keywordChainSearch;


import java.util.ArrayList;


public class KeywordChainMetric {

    static int calculateCommonPrefixLength(final String s1, final String s2) {
	final int minLength = Math.min(s1.length(), s2.length());
	int commonPrefixLength = 0;

	for (int i = 0; i < minLength; ++i) {
	    if (s1.charAt(i) != s2.charAt(i))
		return commonPrefixLength;
	    ++commonPrefixLength;
	}

	return commonPrefixLength;
    }


    public static double calculateSimilarityScore(final ArrayList<String> referenceChain,
						  final ArrayList<String> comparisonChain)
    {
	double similarityScore = 0.0;
	for (String referenceComponentMixedCase : referenceChain) {
	    final String referenceComponent = referenceComponentMixedCase.toLowerCase();
	    double maxComponentSimilarity = 0.0;
	    for (String comparisonComponentMixedCase : comparisonChain) {
		final String comparisonComponent = comparisonComponentMixedCase.toLowerCase();
		final int commonPrefixLength = calculateCommonPrefixLength(referenceComponent, comparisonComponent);
		final int maxLength = Math.max(referenceComponent.length(), comparisonComponent.length());
		final double score = (double)(commonPrefixLength) / maxLength;
		if (score > maxComponentSimilarity)
		    maxComponentSimilarity = score;
	    }
	    similarityScore += maxComponentSimilarity;
	}

	/** Take differences in chain lengths into account.  The greater the difference in chain lengths,
	    the lower the similarity score. */
	similarityScore -= Math.abs(referenceChain.size() - comparisonChain.size()) * 1e-10;

	return similarityScore;
    }


    /** \brief Splits a colon-separated String into individual components. */
    static void parseArg(final String arg, final ArrayList<String> components) {
	String[] parts = arg.split(":");
	for (String part : parts)
	    components.add(part);
    }


    static void usage() {
	System.err.println("usage: de.uni_tuebingen.ub.ixTheo.keywordChainSearch.KeywordChainMetric "
			   + "referenceChain comparisonChain1 [comparisonChain2 .. comparisonChainN]");
	System.exit(-1);
    }

    public static void main(String[] args) {
	if (args.length < 2)
	    usage();

	final ArrayList<String> referenceChain = new ArrayList<String>();
	parseArg(args[0], referenceChain);

	for (int arg_no = 1; arg_no < args.length; ++arg_no) {
	    final ArrayList<String> comparisonChain = new ArrayList<String>();
	    parseArg(args[arg_no], comparisonChain);
	    System.out.println("CalculateSimilarityScore(" + args[0] + ", " + args[arg_no] + ") = "
			       + calculateSimilarityScore(referenceChain, comparisonChain));
	}
    }
}
