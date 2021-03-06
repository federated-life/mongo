/**
*    Copyright (C) 2008 10gen Inc.
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

/*
 * A C++ unit testing framework.
 *
 * For examples of basic usage, see mongo/unittest/unittest_test.cpp.
 */

#include <sstream>
#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/noncopyable.hpp>
#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/stringize.hpp>
#include <boost/scoped_ptr.hpp>
#include <boost/shared_ptr.hpp>

#include "mongo/util/assert_util.h"
#include "mongo/util/mongoutils/str.h"

/**
 * Fail unconditionally, reporting the given message.
 */
#define FAIL(MESSAGE) ::mongo::unittest::TestAssertion( __FILE__ , __LINE__ ).fail( (MESSAGE) )

/**
 * Fails unless "EXPRESSION" is true.
 */
#define ASSERT_TRUE(EXPRESSION) ::mongo::unittest::TestAssertion( __FILE__, __LINE__ ).failUnless( \
            (EXPRESSION), "Expected: " #EXPRESSION )
#define ASSERT(EXPRESSION) ASSERT_TRUE(EXPRESSION)

/**
 * Fails if "EXPRESSION" is true.
 */
#define ASSERT_FALSE(EXPRESSION) ::mongo::unittest::TestAssertion( __FILE__, __LINE__ ).failIf( \
            (EXPRESSION), "Expected: !(" #EXPRESSION ")" )

/*
 * Binary comparison assertions.
 */
#define ASSERT_EQUALS(a,b) _ASSERT_COMPARISON(Equal, a, b)
#define ASSERT_NOT_EQUALS(a,b) _ASSERT_COMPARISON(NotEqual, a, b)
#define ASSERT_LESS_THAN(a, b) _ASSERT_COMPARISON(LessThan, a, b)
#define ASSERT_NOT_LESS_THAN(a, b) _ASSERT_COMPARISON(NotLessThan, a, b)
#define ASSERT_GREATER_THAN(a, b) _ASSERT_COMPARISON(GreaterThan, a, b)
#define ASSERT_NOT_GREATER_THAN(a, b) _ASSERT_COMPARISON(NotGreaterThan, a, b)
#define ASSERT_LESS_THAN_OR_EQUALS(a, b) ASSERT_NOT_GREATER_THAN(a, b)
#define ASSERT_GREATER_THAN_OR_EQUALS(a, b) ASSERT_NOT_LESS_THAN(a, b)

/**
 * Binary comparison utility macro.  Do not use directly.
 */
#define _ASSERT_COMPARISON(COMPARISON, a, b) mongo::unittest::ComparisonAssertion( \
            #a, #b , __FILE__ , __LINE__ ).assert##COMPARISON( (a), (b) )

/**
 * Verify that the evaluation of "EXPRESSION" throws an exception of type EXCEPTION_TYPE.
 *
 * If "EXPRESSION" throws no exception, or one that is neither of type "EXCEPTION_TYPE" nor
 * of a subtype of "EXCEPTION_TYPE", the test is considered a failure and further evaluation
 * halts.
 */
#define ASSERT_THROWS(EXPRESSION, EXCEPTION_TYPE)                       \
    do {                                                                \
        bool threw = false;                                             \
        ::mongo::unittest::TestAssertion _testAssertion( __FILE__, __LINE__ ); \
        try {                                                            \
            EXPRESSION;                                               \
        } catch ( const EXCEPTION_TYPE& ) { threw = true; }            \
        if (!threw)                                                     \
            _testAssertion.fail("Expected expression " #EXPRESSION      \
                                " to throw " #EXCEPTION_TYPE " but it threw nothing."); \
    } while( false )


/**
 * Construct a single test, named "TEST_NAME" within the test case "CASE_NAME".
 *
 * Usage:
 *
 * TEST(MyModuleTests, TestThatFooFailsOnErrors) {
 *     ASSERT_EQUALS(error_success, foo(invalidValue));
 * }
 */
#define TEST(CASE_NAME, TEST_NAME) \
    class _TEST_TYPE_NAME(CASE_NAME, TEST_NAME) : public ::mongo::unittest::Test { \
    private:                                                            \
        virtual void _doTest();                                         \
                                                                        \
        static const RegistrationAgent<_TEST_TYPE_NAME(CASE_NAME, TEST_NAME) > _agent; \
    };                                                                  \
    const ::mongo::unittest::Test::RegistrationAgent<_TEST_TYPE_NAME(CASE_NAME, TEST_NAME) > \
            _TEST_TYPE_NAME(CASE_NAME, TEST_NAME)::_agent(#CASE_NAME, #TEST_NAME); \
    void _TEST_TYPE_NAME(CASE_NAME, TEST_NAME)::_doTest()

/** Define a templated test. The template<...> portion goes above.
 *
 *  Usage:
 *
 *  template<int a, int b>
 *  TEMPLATE_TEST(Silly, Example) {
 *      ASSERT_EQUALS(a, b);
 *  }
 *
 *  TEMPLATE_TEST_INSTANCE(Silly, Example, 1, 1); // passes
 *  TEMPLATE_TEST_INSTANCE(Silly, Example, 1, 2); // fails
 *
 *  Note this must be used with TEMPLATE_TEST_INSTANCE
 */
#define TEMPLATE_TEST(CASE_NAME, TEST_NAME) \
    void _TEST_TYPE_NAME(CASE_NAME, TEST_NAME)()

/** Instantiate and register a template test with specified template arguments.
 *
 *  Usage: see TEMPLATE_TEST
 */
#define TEMPLATE_TEST_INSTANCE(CASE_NAME, TEST_NAME, ...) \
    static bool BOOST_PP_CAT(runCode_, __LINE__) = \
        (::mongo::unittest::Suite::getSuite(#CASE_NAME) \
            ->add((#TEST_NAME  BOOST_PP_STRINGIZE((__VA_ARGS__))), \
                ::mongo::unittest::TestFunction( \
                    _TEST_TYPE_NAME(CASE_NAME, TEST_NAME)<__VA_ARGS__>)) \
        , &BOOST_PP_CAT(runCode_, __LINE__))  /* makes run_code not be considered unused */


/** Put this at the top of a templated test suite class
 *
 *  Usage:
 *
 *  template <int a, int b>
 *  class Example {
 *      TEMPLATE_SUITE_BOILERPLATE;
 *
 *      TEMPLATE_SUITE_TEST(Example, Equals) {
 *          ASSERT_EQUALS(a, b);
 *      }
 *
 *      TEMPLATE_SUITE_TEST(Example, NotEquals) {
 *          ASSERT_NOT_EQUALS(a, b);
 *      }
 *  };
 *
 *  TEMPLATE_SUITE_INSTANCE(Example, 1, 1); // pass then fail
 *  TEMPLATE_SUITE_INSTANCE(Example, 1, 2); // fail then pass
 */
#define TEMPLATE_SUITE_BOILERPLATE \
    static const char* suiteName()

/** Define a test inside of a templated test suite.
 *
 *  Note: You must pass the exact class name in CASE_NAME
 *
 *  Usage: See TEMPLATE_SUITE_BOILERPLATE
 */
#define TEMPLATE_SUITE_TEST(CASE_NAME, TEST_NAME) \
    class _TEST_TYPE_NAME(CASE_NAME, TEST_NAME) { \
    public: \
        class runner : public ::mongo::unittest::Test { \
            virtual void _doTest() { CASE_NAME::TEST_NAME(); } \
        }; \
        _TEST_TYPE_NAME(CASE_NAME, TEST_NAME) () { \
            ::mongo::unittest::Test::RegistrationAgent<runner> agent(suite(), test()); \
        } \
        static const char* suite() { return CASE_NAME::suiteName(); }\
        static const char* test() { return #TEST_NAME; } \
    } TEST_NAME##_metadata; \
    static void TEST_NAME()

/** Instantiate and register a templated test suite with specified template arguments.
 *
 *  Note: automatically registers all tests in the suite for these arguments.
 *
 *  Usage: See TEMPLATE_SUITE_BOILERPLATE
 */
#define TEMPLATE_SUITE_INSTANCE(CASE_NAME, ...) \
    template <> \
    const char* CASE_NAME<__VA_ARGS__>::suiteName() { \
        return #CASE_NAME BOOST_PP_STRINGIZE((__VA_ARGS__)); \
    } \
    CASE_NAME<__VA_ARGS__> BOOST_PP_CAT(CASE_NAME##_instance_, __LINE__)

/**
 * Construct a single test named TEST_NAME that has access to a common class (a "fixture")
 * named "FIXTURE_NAME".
 *
 * Usage:
 *
 * class FixtureClass : public mongo::unittest::Test {
 * protected:
 *   int myVar;
 *   void setUp() { myVar = 10; }
 * };
 *
 * TEST(FixtureClass, TestThatUsesFixture) {
 *     ASSERT_EQUALS(10, myVar);
 * }
 */
#define TEST_F(FIXTURE_NAME, TEST_NAME) \
    class _TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME) : public FIXTURE_NAME { \
    private:                                                            \
        virtual void _doTest();                                         \
                                                                        \
        static const RegistrationAgent<_TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME) > _agent; \
    };                                                                  \
    const ::mongo::unittest::Test::RegistrationAgent<_TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME) > \
            _TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME)::_agent(#FIXTURE_NAME, #TEST_NAME); \
    void _TEST_TYPE_NAME(FIXTURE_NAME, TEST_NAME)::_doTest()

/**
 * Macro to construct a type name for a test, from its "CASE_NAME" and "TEST_NAME".
 * Do not use directly in test code.
 */
#define _TEST_TYPE_NAME(CASE_NAME, TEST_NAME)   \
    UnitTest__##CASE_NAME##__##TEST_NAME

namespace mongo {

    namespace unittest {

        class Result;

        /**
         * Type representing the function composing a test.
         */
        typedef boost::function<void (void)> TestFunction;

        /**
         * Container holding a test function and its name.  Suites
         * contain lists of these.
         */
        class TestHolder : private boost::noncopyable {
        public:
            TestHolder(const std::string& name, const TestFunction& fn)
                : _name(name), _fn(fn) {}

            ~TestHolder() {}
            void run() const { _fn(); }
            std::string getName() const { return _name; }

        private:
            std::string _name;
            TestFunction _fn;
        };

        /// Registration agent for adding tests to suites, used by TEMPLATE_SUITE_TEST macro.
        template <typename Test>
        class TemplateSuiteRegistrationAgent : private boost::noncopyable {
        public:
            TemplateSuiteRegistrationAgent();
        };

        /**
         * Base type for unit test fixtures.  Also, the default fixture type used
         * by the TEST() macro.
         */
        class Test : private boost::noncopyable {
        public:
            Test();
            virtual ~Test();

            void run();

            /**
             * Registration agent for adding tests to suites, used by TEST macro.
             */
            template <typename T>
            class RegistrationAgent : private boost::noncopyable {
            public:
                RegistrationAgent(const std::string& suiteName, const std::string& testName);
            };


        protected:
            /**
             * This exception class is used to exercise the testing framework itself. If a test
             * case throws it, the framework would not consider it an error.
             */
            class FixtureExceptionForTesting : public std::exception {
            };

        private:
            /**
             * Called on the test object before running the test.
             */
            virtual void setUp();

            /**
             * Called on the test object after running the test.
             */
            virtual void tearDown();

            /**
             * The test itself.
             */
            virtual void _doTest() = 0;
        };

        /**
         * Representation of a collection of tests.
         *
         * One suite is constructed for each "CASE_NAME" when using the TEST macro.
         * Additionally, tests that are part of dbtests are manually assigned to suites
         * by the programmer by overriding setupTests() in a subclass of Suite.  This
         * approach is deprecated.
         */
        class Suite : private boost::noncopyable {
        public:
            Suite( const string& name );
            virtual ~Suite();

            template<class T>
            void add() { add<T>(demangleName(typeid(T))); }

            template<class T , typename A >
            void add( const A& a ) {
                add(demangleName(typeid(T)), boost::bind(&Suite::runTestObjectWithArg<T, A>, a));
            }

            template<class T>
            void add(const std::string& name) {
                add(name, &Suite::runTestObject<T>);
            }

            void add(const std::string& name, const TestFunction& testFn);

            Result * run( const std::string& filter , int runsPerTest );

            static int run( const std::vector<std::string>& suites , const std::string& filter , int runsPerTest );

            /**
             * Get a suite with the given name, creating it if necessary.
             *
             * The implementation of this function must be safe to call during the global static
             * initialization block before main() executes.
             */
            static Suite *getSuite(const string& name);

        protected:
            virtual void setupTests();

        private:
            typedef std::vector<TestHolder *> TestHolderList;

            template <typename T>
            static void runTestObject() {
                T testObj;
                testObj.run();
            }

            template <typename T, typename A>
            static void runTestObjectWithArg(const A& a) {
                T testObj(a);
                testObj.run();
            }

            std::string _name;
            TestHolderList _tests;
            bool _ran;

            void registerSuite( const std::string& name , Suite* s );
        };

        /**
         * Collection of information about failed tests.  Used in reporting
         * failures.
         */
        class TestAssertionFailureDetails : private boost::noncopyable {
        public:
            TestAssertionFailureDetails( const std::string& theFile,
                                         unsigned theLine,
                                         const std::string& theMessage );

            const std::string file;
            const unsigned line;
            const std::string message;
        };

        /**
         * Exception thrown when a test assertion fails.
         *
         * Typically thrown by helpers in the TestAssertion class and its ilk, below.
         *
         * NOTE(schwerin): This intentionally does _not_ extend std::exception, so that code under
         * test that (foolishly?) catches std::exception won't swallow test failures.  Doesn't
         * protect you from code that foolishly catches ..., but you do what you can.
         */
        class TestAssertionFailureException {
        public:
            TestAssertionFailureException( const std::string& theFile,
                                           unsigned theLine,
                                           const std::string& theMessage );

            const std::string& getFile() const { return _details->file; }
            unsigned getLine() const { return _details->line; }
            const std::string& getMessage() const { return _details->message; }

            std::string toString() const;

        private:
            boost::shared_ptr<TestAssertionFailureDetails> _details;
        };

        /**
         * Object representing an assertion about some condition.
         */
        class TestAssertion : private boost::noncopyable {

        public:
            TestAssertion( const std::string& file, unsigned line );
            ~TestAssertion();

            void fail( const std::string& message) const;
            void failIf( bool flag, const std::string &message ) const {
                if ( flag ) fail( message );
            }
            void failUnless( bool flag, const std::string& message ) const {
                failIf( !flag, message );
            }

        private:
            const std::string _file;
            const unsigned _line;
        };

        /**
         * Specialization of TestAssertion for binary comparisons.
         */
        class ComparisonAssertion : private TestAssertion {
        public:
            ComparisonAssertion( const std::string& aexp , const std::string& bexp ,
                                 const std::string& file , unsigned line );

            template<typename A,typename B>
            void assertEqual( const A& a , const B& b ) {
                failUnless(a == b, getComparisonFailureMessage("==", a, b));
            }

            template<typename A,typename B>
            void assertNotEqual( const A& a , const B& b ) {
                failUnless(a != b, getComparisonFailureMessage("!=", a, b));
            }

            template<typename A,typename B>
            void assertLessThan( const A& a , const B& b ) {
                failUnless(a < b, getComparisonFailureMessage("<", a, b));
            }

            template<typename A,typename B>
            void assertNotLessThan( const A& a , const B& b ) {
                failUnless(a >= b, getComparisonFailureMessage(">=", a, b));
            }

            template<typename A,typename B>
            void assertGreaterThan( const A& a , const B& b ) {
                failUnless(a > b, getComparisonFailureMessage(">", a, b));
            }

            template<typename A,typename B>
            void assertNotGreaterThan( const A& a , const B& b ) {
                failUnless(a <= b, getComparisonFailureMessage("<=", a, b));
            }

        private:
            template< typename A, typename B>
            std::string getComparisonFailureMessage(const std::string &theOperator,
                                                    const A& a, const B& b);

            std::string _aexp;
            std::string _bexp;
        };

        /**
         * Hack to support the runaway test observer in dbtests.  This is a hook that
         * unit test running harnesses (unittest_main and dbtests) must implement.
         */
        void onCurrentTestNameChange( const std::string& testName );

        /**
         * Return a list of suite names.
         */
        std::vector<std::string> getAllSuiteNames();

    }  // namespace unittest
}  // namespace mongo

#include "mongo/unittest/unittest-inl.h"
