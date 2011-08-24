/**
 * Copyright (c) 2011 10gen Inc.
 *
 * This program is free software: you can redistribute it and/or  modify
 * it under the terms of the GNU Affero General Public License, version 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "pch.h"

#include <boost/unordered_map.hpp>
#include "util/intrusive_counter.h"
#include "client/parallel.h"
#include "db/jsobj.h"
#include "db/pipeline/document.h"
#include "db/pipeline/expression.h"
#include "db/pipeline/value.h"

namespace mongo {
    class Accumulator;
    class Cursor;
    class Document;
    class Expression;
    class ExpressionContext;
    class ExpressionFieldPath;
    class ExpressionObject;
    class Matcher;

    class DocumentSource :
        public IntrusiveCounter {
    public:
	virtual ~DocumentSource();

        /*
	  Is the source at EOF?

	  @returns true if the source has no more Documents to return.
        */
        virtual bool eof() = 0;

        /*
	  Advance the state of the DocumentSource so that it will return the
	  next Document.

	  @returns whether there is another document to fetch, i.e., whether or
	    not getCurrent() will succeed.
        */
        virtual bool advance() = 0;

        /*
          Advance the source, and return the next Expression.

	  @returns the current Document
          TODO throws an exception if there are no more expressions to return.
        */
        virtual intrusive_ptr<Document> getCurrent() = 0;

	/*
	  Set the underlying source this source should use to get Documents
	  from.

	  It is an error to set the source more than once.  This is to
	  prevent changing sources once the original source has been started;
	  this could break the state maintained by the DocumentSource.

	  @param pSource the underlying source to use
	 */
	virtual void setSource(const intrusive_ptr<DocumentSource> &pSource);

	/*
	  Attempt to coalesce this DocumentSource with its successor in the
	  document processing pipeline.  If successful, the successor
	  DocumentSource should be removed from the pipeline and discarded.

	  If successful, this operation can be applied repeatedly, in an
	  attempt to coalesce several sources together.

	  The default implementation is to do nothing, and return false.

	  @param pNextSource the next source in the document processing chain.
	  @returns whether or not the attempt to coalesce was successful or not;
	    if the attempt was not successful, nothing has been changed
	 */
	virtual bool coalesce(const intrusive_ptr<DocumentSource> &pNextSource);

	/*
	  Optimize the pipeline operation, if possible.  This is a local
	  optimization that only looks within this DocumentSource.  For best
	  results, first coalesce compatible sources using coalesce().

	  This is intended for any operations that include expressions, and
	  provides a hook for those to optimize those operations.

	  The default implementation is to do nothing.
	 */
	virtual void optimize();

        /*
	  Add the DocumentSource to the array builder.

	  The default implementation calls sourceToBson() in order to
	  convert the inner part of the object which will be added to the
	  array being built here.

	  @params pBuilder the array builder to add the operation to.
         */
	virtual void addToBsonArray(BSONArrayBuilder *pBuilder) const;

    protected:
	/*
	  Create an object that represents the document source.  The object
	  will have a single field whose name is the source's name.  This
	  will be used by the default implementation of addToBsonArray()
	  to add this object to a pipeline being represented in BSON.

	  @params pBuilder a blank object builder to write to
	 */
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const = 0;

	/*
	  Most DocumentSources have an underlying source they get their data
	  from.  This is a convenience for them.

	  The default implementation of setSource() sets this; if you don't
	  need a source, override that to assert().  The default is to
	  assert() if this has already been set.
	*/
	intrusive_ptr<DocumentSource> pSource;
    };


    class DocumentSourceBsonArray :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceBsonArray();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();
	virtual void setSource(const intrusive_ptr<DocumentSource> &pSource);

	/*
	  Create a document source based on a BSON array.

	  This is usually put at the beginning of a chain of document sources
	  in order to fetch data from the database.

	  CAUTION:  the BSON is not read until the source is used.  Any
	  elements that appear after these documents must not be read until
	  this source is exhausted.

	  @param pBsonElement the BSON array to treat as a document source
	  @returns the newly created document source
	*/
	static intrusive_ptr<DocumentSourceBsonArray> create(
	    BSONElement *pBsonElement);

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceBsonArray(BSONElement *pBsonElement);

	BSONObj embeddedObject;
	BSONObjIterator arrayIterator;
	BSONElement currentElement;
	bool haveCurrent;
    };

    
    class DocumentSourceCommandFutures :
	public DocumentSource {
    public:
	// virtuals from DocumentSource
	virtual ~DocumentSourceCommandFutures();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();
	virtual void setSource(const intrusive_ptr<DocumentSource> &pSource);

	/* convenient shorthand for a commonly used type */
	typedef list<shared_ptr<Future::CommandResult> > FuturesList;

	/*
	  Create a DocumentSource that wraps a list of Command::Futures.

	  @param errmsg place to write error messages to; must exist for the
	    lifetime of the created DocumentSourceCommandFutures
	  @param pList the list of futures
	 */
	static intrusive_ptr<DocumentSourceCommandFutures> create(
	    string &errmsg, FuturesList *pList);

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
	DocumentSourceCommandFutures(string &errmsg, FuturesList *pList);

	/*
	  Advance to the next document, setting pCurrent appropriately.

	  Adjusts pCurrent, pBsonSource, and iterator, as needed.  On exit,
	  pCurrent is the Document to return, or NULL.  If NULL, this
	  indicates there is nothing more to return.
	 */
	void getNextDocument();

	bool newSource; // set to true for the first item of a new source
	intrusive_ptr<DocumentSourceBsonArray> pBsonSource;
	intrusive_ptr<Document> pCurrent;
	FuturesList::iterator iterator;
	FuturesList::iterator listEnd;
	string &errmsg;
    };


    class DocumentSourceCursor :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceCursor();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();
	virtual void setSource(const intrusive_ptr<DocumentSource> &pSource);

	/*
	  Create a document source based on a cursor.

	  This is usually put at the beginning of a chain of document sources
	  in order to fetch data from the database.

	  @param pCursor the cursor to use to fetch data
	*/
	static intrusive_ptr<DocumentSourceCursor> create(
	    const shared_ptr<Cursor> &pCursor);

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceCursor(const shared_ptr<Cursor> &pTheCursor);

	void findNext();
        shared_ptr<Cursor> pCursor;
	intrusive_ptr<Document> pCurrent;
    };


    /*
      This contains all the basic mechanics for filtering a stream of
      Documents, except for the actual predicate evaluation itself.  This was
      factored out so we could create DocumentSources that use both Matcher
      style predicates as well as full Expressions.
     */
    class DocumentSourceFilterBase :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceFilterBase();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();

	/*
	  Create a BSONObj suitable for Matcher construction.

	  This is used after filter analysis has moved as many filters to
	  as early a point as possible in the document processing pipeline.
	  See db/Matcher.h and the associated wiki documentation for the
	  format.  This conversion is used to move back to the low-level
	  find() Cursor mechanism.

	  @params pBuilder the builder to write to
	 */
	virtual void toMatcherBson(BSONObjBuilder *pBuilder) const = 0;

    protected:
        DocumentSourceFilterBase();

	/*
	  Test the given document against the predicate and report if it
	  should be accepted or not.

	  @param pDocument the document to test
	  @returns true if the document matches the filter, false otherwise
	 */
	virtual bool accept(const intrusive_ptr<Document> &pDocument) const = 0;

    private:

        void findNext();

        bool unstarted;
        bool hasNext;
        intrusive_ptr<Document> pCurrent;
    };


    class DocumentSourceFilter :
        public DocumentSourceFilterBase {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceFilter();
	virtual bool coalesce(const intrusive_ptr<DocumentSource> &pNextSource);
	virtual void optimize();

	/*
	  Create a filter.

          @param pBsonElement the raw BSON specification for the filter
          @returns the filter
	 */
	static intrusive_ptr<DocumentSource> createFromBson(
	    BSONElement *pBsonElement,
	    const intrusive_ptr<ExpressionContext> &pCtx);

        /*
          Create a filter.

          @param pFilter the expression to use to filter
          @returns the filter
         */
        static intrusive_ptr<DocumentSourceFilter> create(
            const intrusive_ptr<Expression> &pFilter);

	/*
	  Create a BSONObj suitable for Matcher construction.

	  This is used after filter analysis has moved as many filters to
	  as early a point as possible in the document processing pipeline.
	  See db/Matcher.h and the associated wiki documentation for the
	  format.  This conversion is used to move back to the low-level
	  find() Cursor mechanism.

	  @params pBuilder the builder to write to
	 */
	void toMatcherBson(BSONObjBuilder *pBuilder) const;

	static const char filterName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

	// virtuals from DocumentSourceFilterBase
	virtual bool accept(const intrusive_ptr<Document> &pDocument) const;

    private:
        DocumentSourceFilter(const intrusive_ptr<Expression> &pFilter);

        intrusive_ptr<Expression> pFilter;
    };


    class DocumentSourceGroup :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceGroup();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();

        /*
          Create a new grouping DocumentSource.
	  
	  @param pCtx the expression context
	  @returns the DocumentSource
         */
        static intrusive_ptr<DocumentSourceGroup> create(
	    const intrusive_ptr<ExpressionContext> &pCtx);

        /*
          Set the Id Expression.

          Documents that pass through the grouping Document are grouped
          according to this key.  This will generate the id_ field in the
          result documents.

          @param pExpression the group key
         */
        void setIdExpression(const intrusive_ptr<Expression> &pExpression);

        /*
          Add an accumulator.

          Accumulators become fields in the Documents that result from
          grouping.  Each unique group document must have it's own
          accumulator; the accumulator factory is used to create that.

          @param fieldName the name the accumulator result will have in the
                result documents
          @param pAccumulatorFactory used to create the accumulator for the
                group field
         */
        void addAccumulator(string fieldName,
			    intrusive_ptr<Accumulator> (*pAccumulatorFactory)(
			    const intrusive_ptr<ExpressionContext> &),
                            const intrusive_ptr<Expression> &pExpression);

	/*
	  Create a grouping DocumentSource from BSON.

	  This is a convenience method that uses the above, and operates on
	  a BSONElement that has been deteremined to be an Object with an
	  element named $group.

	  @param pBsonElement the BSONELement that defines the group
	  @param pCtx the expression context
	  @returns the grouping DocumentSource
	 */
        static intrusive_ptr<DocumentSource> createFromBson(
	    BSONElement *pBsonElement,
	    const intrusive_ptr<ExpressionContext> &pCtx);


	/*
	  Create a unifying group that can be used to combine group results
	  from shards.

	  @returns the grouping DocumentSource
	*/
	intrusive_ptr<DocumentSource> createMerger();

	static const char groupName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceGroup(const intrusive_ptr<ExpressionContext> &pCtx);

	/*
	  Before returning anything, this source must fetch everything from
	  the underlying source and group it.  populate() is used to do that
	  on the first call to any method on this source.  The populated
	  boolean indicates that this has been done.
	 */
        void populate();
        bool populated;

        intrusive_ptr<Expression> pIdExpression;

	typedef boost::unordered_map<intrusive_ptr<const Value>,
	    vector<intrusive_ptr<Accumulator> >, Value::Hash> GroupsType;
        GroupsType groups;

        /*
          The field names for the result documents and the accumulator
          factories for the result documents.  The Expressions are the
          common expressions used by each instance of each accumulator
          in order to find the right-hand side of what gets added to the
          accumulator.  Note that each of those is the same for each group,
          so we can share them across all groups by adding them to the
          accumulators after we use the factories to make a new set of
          accumulators for each new group.

          These three vectors parallel each other.
        */
        vector<string> vFieldName;
        vector<intrusive_ptr<Accumulator> (*)(
	    const intrusive_ptr<ExpressionContext> &)> vpAccumulatorFactory;
        vector<intrusive_ptr<Expression> > vpExpression;


        intrusive_ptr<Document> makeDocument(
	    const GroupsType::iterator &rIter);

        GroupsType::iterator groupsIterator;
        intrusive_ptr<Document> pCurrent;

	intrusive_ptr<ExpressionContext> pCtx;
    };


    class DocumentSourceMatch :
        public DocumentSourceFilterBase {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceMatch();

	/*
	  Create a filter.

          @param pBsonElement the raw BSON specification for the filter
          @returns the filter
	 */
	static intrusive_ptr<DocumentSource> createFromBson(
	    BSONElement *pBsonElement,
	    const intrusive_ptr<ExpressionContext> &pCtx);

	/*
	  Create a BSONObj suitable for Matcher construction.

	  This is used after filter analysis has moved as many filters to
	  as early a point as possible in the document processing pipeline.
	  See db/Matcher.h and the associated wiki documentation for the
	  format.  This conversion is used to move back to the low-level
	  find() Cursor mechanism.

	  @params pBuilder the builder to write to
	 */
	void toMatcherBson(BSONObjBuilder *pBuilder) const;

	static const char matchName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

	// virtuals from DocumentSourceFilterBase
	virtual bool accept(const intrusive_ptr<Document> &pDocument) const;

    private:
        DocumentSourceMatch(const BSONObj &query);

	Matcher matcher;
    };


    class DocumentSourceOut :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceOut();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();

	/*
	  Create a document source for output and pass-through.

	  This can be put anywhere in a pipeline and will store content as
	  well as pass it on.

	  @returns the newly created document source
	*/
	static intrusive_ptr<DocumentSourceOut> createFromBson(
	    BSONElement *pBsonElement);

	static const char outName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceOut(BSONElement *pBsonElement);
    };

    
    class DocumentSourceProject :
        public DocumentSource,
        public boost::enable_shared_from_this<DocumentSourceProject> {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceProject();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();
	virtual void optimize();

        /*
          Create a new DocumentSource that can implement projection.

	  @returns the projection DocumentSource
        */
        static intrusive_ptr<DocumentSourceProject> create();

	/*
	  Include a field path in a projection.

	  @param fieldPath the path of the field to include
	*/
	void includePath(const string &fieldPath);

	/*
	  Exclude a field path from the projection.

	  @param fieldPath the path of the field to exclude
	 */
	void excludePath(const string &fieldPath);

        /*
          Add an output Expression in the projection.

          BSON document fields are ordered, so the new field will be
          appended to the existing set.

          @param fieldName the name of the field as it will appear
          @param pExpression the expression used to compute the field
          @param unwindArray if the result of the expression is an array value,
              the projection will create one Document per value in the array;
              a sequence of documents is generated, with each one containing one
              value from the array.  Note there can only be one unwound field
	      per projection.
        */
        void addField(const string &fieldName,
		      const intrusive_ptr<Expression> &pExpression,
		      bool unwindArray);

	/*
	  Create a new projection DocumentSource from BSON.

	  This is a convenience for directly handling BSON, and relies on the
	  above methods.

	  @params pBsonElement the BSONElement with an object named $project
	  @returns the created projection
	 */
        static intrusive_ptr<DocumentSource> createFromBson(
            BSONElement *pBsonElement,
	    const intrusive_ptr<ExpressionContext> &pCtx);

	static const char projectName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceProject();

        // configuration state
	bool excludeId;
	intrusive_ptr<ExpressionObject> pEO;
	string unwindName; // name of the field to unwind, if any
        size_t unwindWhich; // if unwinding, current Document's index

        // iteration state
        intrusive_ptr<Document> pNoUnwindDocument;
                                              // document to return, pre-unwind
        intrusive_ptr<const Value> pUnwindArray; // field being unwound
        intrusive_ptr<ValueIterator> pUnwind; // iterator used for unwinding
        intrusive_ptr<const Value> pUnwindValue; // current value
    };


    class DocumentSourceSort :
        public DocumentSource {
    public:
        // virtuals from DocumentSource
        virtual ~DocumentSourceSort();
        virtual bool eof();
        virtual bool advance();
        virtual intrusive_ptr<Document> getCurrent();

        /*
          Create a new sorting DocumentSource.
	  
	  @param pCtx the expression context
	  @returns the DocumentSource
         */
        static intrusive_ptr<DocumentSourceSort> create(
	    const intrusive_ptr<ExpressionContext> &pCtx);

	/*
	  Add sort key field.

	  Adds a sort key field to the key being built up.  A concatenated
	  key is built up by calling this repeatedly.

	  @params fieldPath the field path to the key component
	  @params ascending if true, use the key for an ascending sort,
	    otherwise, use it for descending
	*/
	void addKey(const string &fieldPath, bool ascending);

	/*
	  Create a sorting DocumentSource from BSON.

	  This is a convenience method that uses the above, and operates on
	  a BSONElement that has been deteremined to be an Object with an
	  element named $group.

	  @param pBsonElement the BSONELement that defines the group
	  @param pCtx the expression context
	  @returns the grouping DocumentSource
	 */
        static intrusive_ptr<DocumentSource> createFromBson(
	    BSONElement *pBsonElement,
	    const intrusive_ptr<ExpressionContext> &pCtx);


	static const char sortName[];

    protected:
	// virtuals from DocumentSource
	virtual void sourceToBson(BSONObjBuilder *pBuilder) const;

    private:
        DocumentSourceSort(const intrusive_ptr<ExpressionContext> &pCtx);

	/*
	  Before returning anything, this source must fetch everything from
	  the underlying source and group it.  populate() is used to do that
	  on the first call to any method on this source.  The populated
	  boolean indicates that this has been done.
	 */
        void populate();
        bool populated;
        long long limit;
        long long skip;
        long long count;

	/* these two parallel each other */
	vector<intrusive_ptr<ExpressionFieldPath> > vSortKey;
	vector<bool> vAscending;

	class Carrier {
	public:
	    /*
	      We need access to the key for compares, so we have to carry
	      this around.
	    */
	    DocumentSourceSort *pSort;

	    intrusive_ptr<Document> pDocument;

	    Carrier(DocumentSourceSort *pSort,
		    const intrusive_ptr<Document> &pDocument);

	    static bool lessThan(const Carrier &rL, const Carrier &rR);
	};

	/*
	  Compare two documents according to the specified sort key.

	  @param rL reference to the left document
	  @param rR reference to the right document
	  @returns a number less than, equal to, or greater than zero,
	    indicating pL < pR, pL == pR, or pL > pR, respectively
	 */
	int compare(const intrusive_ptr<Document> &pL,
		    const intrusive_ptr<Document> &pR);

	typedef list<Carrier> ListType;
	ListType documents;

        ListType::iterator listIterator;
        intrusive_ptr<Document> pCurrent;

	intrusive_ptr<ExpressionContext> pCtx;
    };

}


/* ======================= INLINED IMPLEMENTATIONS ========================== */

namespace mongo {

    inline void DocumentSourceGroup::setIdExpression(
        const intrusive_ptr<Expression> &pExpression) {
        pIdExpression = pExpression;
    }

    inline DocumentSourceSort::Carrier::Carrier(
	DocumentSourceSort *pTheSort,
	const intrusive_ptr<Document> &pTheDocument):
	pSort(pTheSort),
	pDocument(pTheDocument) {
    }
}